#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "simulator_app.h"
#include "virtual_led_bar.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
    constexpr intptr_t kInvalidSocketHandle = static_cast<intptr_t>(INVALID_SOCKET);
#else
    constexpr intptr_t kInvalidSocketHandle = -1;
#endif

    void expect_true(bool condition, const char *message)
    {
        if (!condition)
        {
            std::cerr << "Test failed: " << message << '\n';
            std::exit(1);
        }
    }

    void close_socket_handle(intptr_t handle)
    {
#ifdef _WIN32
        if (handle != static_cast<intptr_t>(INVALID_SOCKET))
        {
            closesocket(static_cast<SOCKET>(handle));
        }
#else
        if (handle >= 0)
        {
            close(static_cast<int>(handle));
        }
#endif
    }

    void set_env_value(const char *name, const std::string &value)
    {
#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        setenv(name, value.c_str(), 1);
#endif
    }

    bool send_udp_packet(uint16_t port, const char *payload)
    {
#ifdef _WIN32
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            return false;
        }
#endif

        bool sent = false;
        const intptr_t socketHandle = static_cast<intptr_t>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (socketHandle != kInvalidSocketHandle)
        {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            const int payloadLength = static_cast<int>(std::strlen(payload));
#ifdef _WIN32
            sent = ::sendto(static_cast<SOCKET>(socketHandle),
                            payload,
                            payloadLength,
                            0,
                            reinterpret_cast<const sockaddr *>(&address),
                            sizeof(address)) == payloadLength;
#else
            sent = ::sendto(static_cast<int>(socketHandle),
                            payload,
                            static_cast<size_t>(payloadLength),
                            0,
                            reinterpret_cast<const sockaddr *>(&address),
                            sizeof(address)) == payloadLength;
#endif

            close_socket_handle(socketHandle);
        }

#ifdef _WIN32
        WSACleanup();
#endif
        return sent;
    }

    class TestHttpServer
    {
    public:
        ~TestHttpServer()
        {
            stop();
        }

        bool start(uint16_t port)
        {
#ifdef _WIN32
            WSADATA wsaData{};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            {
                return false;
            }
            winsockReady_ = true;
#endif

            const intptr_t socketHandle = static_cast<intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
            if (socketHandle == kInvalidSocketHandle)
            {
                stop();
                return false;
            }

            int reuse = 1;
#ifdef _WIN32
            setsockopt(static_cast<SOCKET>(socketHandle), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#else
            setsockopt(static_cast<int>(socketHandle), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            int bindResult = 0;
#ifdef _WIN32
            bindResult = ::bind(static_cast<SOCKET>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#else
            bindResult = ::bind(static_cast<int>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#endif
            if (bindResult != 0)
            {
                close_socket_handle(socketHandle);
                stop();
                return false;
            }

#ifdef _WIN32
            u_long nonBlocking = 1;
            ioctlsocket(static_cast<SOCKET>(socketHandle), FIONBIO, &nonBlocking);
#else
            const int flags = fcntl(static_cast<int>(socketHandle), F_GETFL, 0);
            fcntl(static_cast<int>(socketHandle), F_SETFL, flags | O_NONBLOCK);
#endif

#ifdef _WIN32
            if (::listen(static_cast<SOCKET>(socketHandle), SOMAXCONN) != 0)
#else
            if (::listen(static_cast<int>(socketHandle), SOMAXCONN) != 0)
#endif
            {
                close_socket_handle(socketHandle);
                stop();
                return false;
            }

            listenSocket_ = socketHandle;
            stopRequested_ = false;
            worker_ = std::thread(&TestHttpServer::run, this);
            return true;
        }

        void stop()
        {
            stopRequested_ = true;
            if (worker_.joinable())
            {
                worker_.join();
            }
            close_socket_handle(listenSocket_);
            listenSocket_ = kInvalidSocketHandle;
#ifdef _WIN32
            if (winsockReady_)
            {
                WSACleanup();
                winsockReady_ = false;
            }
#endif
        }

        void set_waiting()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            gameRunning_ = true;
            hasNewData_ = false;
            simpleJson_ = R"({"speed":0,"gear":"N","rpms":0,"maxRpm":0})";
            gameDataExtras_.clear();
            throttle_ = "0.0";
            brake_ = "0.0";
        }

        void set_live(const std::string &simpleJson,
                      const std::string &throttle,
                      const std::string &brake,
                      const std::string &gameDataExtras = std::string())
        {
            std::lock_guard<std::mutex> lock(mutex_);
            gameRunning_ = true;
            hasNewData_ = true;
            simpleJson_ = simpleJson;
            gameDataExtras_ = gameDataExtras;
            throttle_ = throttle;
            brake_ = brake;
        }

        void set_offline()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            gameRunning_ = false;
            hasNewData_ = false;
        }

    private:
        void run()
        {
            while (!stopRequested_)
            {
                sockaddr_in clientAddress{};
#ifdef _WIN32
                int clientLength = sizeof(clientAddress);
                const intptr_t clientSocket = static_cast<intptr_t>(::accept(static_cast<SOCKET>(listenSocket_), reinterpret_cast<sockaddr *>(&clientAddress), &clientLength));
                const bool noClient = clientSocket == static_cast<intptr_t>(INVALID_SOCKET) && WSAGetLastError() == WSAEWOULDBLOCK;
#else
                socklen_t clientLength = sizeof(clientAddress);
                const intptr_t clientSocket = static_cast<intptr_t>(::accept(static_cast<int>(listenSocket_), reinterpret_cast<sockaddr *>(&clientAddress), &clientLength));
                const bool noClient = clientSocket < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
#endif
                if (noClient)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                if (clientSocket == kInvalidSocketHandle)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                std::string request;
                char buffer[1024]{};
                while (true)
                {
#ifdef _WIN32
                    const int bytesRead = ::recv(static_cast<SOCKET>(clientSocket), buffer, static_cast<int>(sizeof(buffer)), 0);
#else
                    const int bytesRead = static_cast<int>(::recv(static_cast<int>(clientSocket), buffer, sizeof(buffer), 0));
#endif
                    if (bytesRead <= 0)
                    {
                        break;
                    }
                    request.append(buffer, static_cast<size_t>(bytesRead));
                    if (request.find("\r\n\r\n") != std::string::npos)
                    {
                        break;
                    }
                }

                const std::string response = build_response(request);
#ifdef _WIN32
                ::send(static_cast<SOCKET>(clientSocket), response.data(), static_cast<int>(response.size()), 0);
#else
                ::send(static_cast<int>(clientSocket), response.data(), response.size(), 0);
#endif
                close_socket_handle(clientSocket);
            }
        }

        std::string build_response(const std::string &request)
        {
            std::string statusPayload;
            std::string simplePayload;
            std::string throttlePayload;
            std::string brakePayload;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                statusPayload = std::string("{\"GameRunning\":") + (gameRunning_ ? "true" : "false") +
                                ",\"NewData\":" + (hasNewData_ ? "{\"Rpms\":1}" : "null") +
                                gameDataExtras_ + "}";
                simplePayload = simpleJson_;
                throttlePayload = throttle_;
                brakePayload = brake_;
            }

            std::string body = "{}";
            if (request.find("GET /Api/GetGameData ") != std::string::npos)
            {
                body = statusPayload;
            }
            else if (request.find("GET /Api/GetGameDataSimple ") != std::string::npos)
            {
                body = simplePayload;
            }
            else if (request.find("GET /Api/GetProperty/DataCorePlugin.GameData.NewData.Throttle ") != std::string::npos)
            {
                body = throttlePayload;
            }
            else if (request.find("GET /Api/GetProperty/DataCorePlugin.GameData.NewData.Brake ") != std::string::npos)
            {
                body = brakePayload;
            }

            return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                   std::to_string(body.size()) +
                   "\r\nConnection: close\r\n\r\n" + body;
        }

        std::atomic<bool> stopRequested_{false};
        std::thread worker_{};
        intptr_t listenSocket_ = kInvalidSocketHandle;
        std::mutex mutex_{};
        bool gameRunning_ = true;
        bool hasNewData_ = false;
        std::string simpleJson_ = R"({"speed":0,"gear":"N","rpms":0,"maxRpm":0})";
        std::string gameDataExtras_;
        std::string throttle_ = "0.0";
        std::string brake_ = "0.0";
#ifdef _WIN32
        bool winsockReady_ = false;
#endif
    };
}

int main()
{
    const auto uniqueSuffix =
        std::to_string(static_cast<long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const std::filesystem::path persistedStatePath =
        std::filesystem::temp_directory_path() / ("rpmcounter_simulator_test_state_" + uniqueSuffix + ".cfg");
    std::error_code removeError;
    std::filesystem::remove(persistedStatePath, removeError);
    set_env_value("SIM_SETTINGS_PATH", persistedStatePath.string());

    SimulatorApp app;
    TelemetryServiceConfig simulatorConfig{};
    simulatorConfig.mode = TelemetryInputMode::Simulator;
    app.configureTelemetry(simulatorConfig);

    app.tick(0);
    const UiRuntimeState initial = app.state();
    expect_true(initial.bleConnected, "initial BLE state should be connected");
    expect_true(initial.staConnected, "initial WiFi state should be connected");
    expect_true(initial.rpm > 0, "initial RPM should be populated");

    app.execute(SimulatorCommand::ToggleBleState);
    app.tick(100);
    expect_true(app.state().bleConnecting, "BLE toggle should enter connecting state");

    app.execute(SimulatorCommand::ToggleBleState);
    app.tick(200);
    expect_true(!app.state().bleConnected && !app.state().bleConnecting, "BLE toggle should enter disconnected state");

    app.execute(SimulatorCommand::ToggleBleState);
    app.tick(250);
    expect_true(app.state().bleConnected, "BLE toggle should cycle back to connected");

    app.execute(SimulatorCommand::CycleWifiState);
    app.tick(300);
    expect_true(app.state().apActive, "WiFi cycle should enable AP mode");

    app.execute(SimulatorCommand::ToggleAnimation);
    app.execute(SimulatorCommand::IncreaseRpm);
    app.tick(400);
    expect_true(app.state().rpm >= initial.rpm, "RPM increase should raise telemetry");

    UiSettings settings = app.state().settings;
    settings.displayBrightness = 150;
    settings.tutorialSeen = true;
    app.saveSettings(settings);
    expect_true(app.state().settings.displayBrightness == 150, "settings save should update brightness");
    expect_true(app.state().settings.tutorialSeen, "settings save should update tutorial flag");

    SimulatorLedBarConfig persistedLedConfig = app.ledBarConfigSnapshot();
    persistedLedConfig.brightness = 123;
    persistedLedConfig.activeLedCount = 26;
    persistedLedConfig.maxRpmPerGearEnabled = true;
    persistedLedConfig.fixedMaxRpmByGear[0] = 7100;
    persistedLedConfig.fixedMaxRpmByGear[4] = 8650;
    app.applyLedBarConfig(persistedLedConfig);

    SimulatorDeviceConfig persistedDeviceConfig = app.deviceConfigSnapshot();
    persistedDeviceConfig.staSsid = "Track WiFi";
    persistedDeviceConfig.apPassword = "persist123";
    persistedDeviceConfig.useMph = true;
    app.applyDeviceConfig(persistedDeviceConfig);

    TelemetryServiceConfig persistedTelemetryConfig = app.telemetryConfigSnapshot();
    persistedTelemetryConfig.mode = TelemetryInputMode::SimHub;
    persistedTelemetryConfig.simHubTransport = SimHubTransport::HttpApi;
    persistedTelemetryConfig.httpPort = 18989;
    persistedTelemetryConfig.allowSimulatorFallback = true;
    app.configureTelemetry(persistedTelemetryConfig);
    SideLedConfig persistedSideConfig = app.sideLedConfigSnapshot();
    persistedSideConfig.enabled = true;
    persistedSideConfig.allowSpotter = false;
    persistedSideConfig.allowTraction = true;
    persistedSideConfig.ledCountPerSide = 11;
    persistedSideConfig.testMode = true;
    persistedSideConfig.blinkSpeedFastMs = 90;
    app.applySideLedConfig(persistedSideConfig);
    app.connectBleDevice("Persisted Dongle", "12:34:56:78:9A:BC");

    SimulatorPersistedState loadedState{};
    expect_true(load_simulator_persisted_state(loadedState), "persisted state file should exist");
    expect_true(loadedState.telemetry.simHubTransport == SimHubTransport::HttpApi, "persisted state file should store telemetry transport");
    expect_true(loadedState.telemetry.httpPort == 18989, "persisted state file should store telemetry port");
    expect_true(loadedState.bleTargetName == "Persisted Dongle", "persisted state file should store BLE target name");
    expect_true(loadedState.bleTargetAddress == "12:34:56:78:9A:BC", "persisted state file should store BLE target address");
    expect_true(!loadedState.ledBar.learnedMaxRpmByGear[0], "manual per-gear mode should not persist learned gear state");
    expect_true(loadedState.sideLeds.testMode, "persisted state file should store side LED test mode");
    expect_true(!loadedState.sideLeds.allowSpotter, "persisted state file should store side LED spotter toggle");
    expect_true(loadedState.sideLeds.ledCountPerSide == 11, "persisted state file should store side LED count");
    expect_true(loadedState.sideLeds.blinkSpeedFastMs == 90, "persisted state file should store side LED blink speed");

    SimulatorApp persistedApp;
    expect_true(persistedApp.stateSnapshot().settings.displayBrightness == 150, "persisted app should restore UI brightness");
    expect_true(persistedApp.ledBarConfigSnapshot().brightness == 123, "persisted app should restore LED brightness");
    expect_true(persistedApp.ledBarConfigSnapshot().activeLedCount == 26, "persisted app should restore LED count");
    expect_true(persistedApp.ledBarConfigSnapshot().maxRpmPerGearEnabled, "persisted app should restore per-gear RPM toggle");
    expect_true(persistedApp.ledBarConfigSnapshot().fixedMaxRpmByGear[0] == 7100, "persisted app should restore gear 1 RPM");
    expect_true(persistedApp.ledBarConfigSnapshot().fixedMaxRpmByGear[4] == 8650, "persisted app should restore gear 5 RPM");
    expect_true(persistedApp.deviceConfigSnapshot().staSsid == "Track WiFi", "persisted app should restore STA SSID");
    expect_true(persistedApp.deviceConfigSnapshot().apPassword == "persist123", "persisted app should restore AP password");
    expect_true(persistedApp.deviceConfigSnapshot().useMph, "persisted app should restore mph preference");
    expect_true(persistedApp.telemetryConfigSnapshot().simHubTransport == SimHubTransport::HttpApi, "persisted app should restore telemetry transport");
    expect_true(persistedApp.telemetryConfigSnapshot().httpPort == 18989, "persisted app should restore telemetry port");
    expect_true(persistedApp.statusSnapshot().bleTargetName == "Persisted Dongle", "persisted app should restore BLE target name");
    expect_true(persistedApp.statusSnapshot().bleTargetAddress == "12:34:56:78:9A:BC", "persisted app should restore BLE target address");
    expect_true(persistedApp.sideLedConfigSnapshot().testMode, "persisted app should restore side LED test mode");
    expect_true(!persistedApp.sideLedConfigSnapshot().allowSpotter, "persisted app should restore side LED spotter toggle");
    expect_true(persistedApp.sideLedConfigSnapshot().ledCountPerSide == 11, "persisted app should restore side LED count");
    expect_true(persistedApp.sideLedConfigSnapshot().blinkSpeedFastMs == 90, "persisted app should restore side LED blink speed");

    TestHttpServer httpServer;
    expect_true(httpServer.start(18888), "HTTP test server should start");
    httpServer.set_waiting();

    SimulatorApp waitingApp;
    TelemetryServiceConfig waitingConfig{};
    waitingConfig.mode = TelemetryInputMode::SimHub;
    waitingConfig.simHubTransport = SimHubTransport::HttpApi;
    waitingConfig.httpPort = 18888;
    waitingConfig.pollIntervalMs = 25;
    waitingConfig.allowSimulatorFallback = false;
    waitingApp.configureTelemetry(waitingConfig);
    SideLedConfig waitingSideConfig = waitingApp.sideLedConfigSnapshot();
    waitingSideConfig.allowTraction = true;
    waitingSideConfig.allowSpotter = false;
    waitingSideConfig.allowFlags = false;
    waitingSideConfig.allowWarnings = false;
    waitingSideConfig.testMode = false;
    waitingSideConfig.ledCountPerSide = 8;
    waitingApp.applySideLedConfig(waitingSideConfig);
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    waitingApp.tick(100);
    expect_true(waitingApp.state().telemetrySource == UiTelemetrySource::SimHubNetwork, "HTTP mode should mark SimHub as source");
    expect_true(waitingApp.state().telemetryStale, "HTTP mode without data should be stale");
    expect_true(!waitingApp.state().telemetryUsingFallback, "HTTP mode should not use simulator fallback by default");
    expect_true(waitingApp.state().rpm == 0, "HTTP waiting state should not inject demo RPM");
    expect_true(!waitingApp.state().session.hasAnyData, "HTTP waiting state should not invent session data");

    httpServer.set_live(
        R"({"speed":155,"gear":"5","rpms":6123,"maxRpm":7200})",
        "0.82",
        "0.18",
        R"(,"BestLapTime":"01:23.924","LastLapTime":"01:24.445","LiveDeltaToSessionBest":0.521,"PredictedLapTime":"01:24.445","SessionTimeLeft":"23:14","Position":4,"TotalCars":21,"CurrentLap":2,"TotalLaps":12,"Fuel":6.4,"FuelAvgPerLap":2.85,"FuelLapsRemaining":2.2,"OilTemp":78.8,"OilPressure":3.1,"OilLevel":5.0,"FuelPressure":2.3,"WaterTemp":78.3,"BatteryVoltage":14.1,"TcLevel":3,"TcInAction":true,"AbsLevel":4,"BrakeBias":52.8,"EngineMap":1,"Flag_Yellow":true,"Flag_Name":"Yellow","SpotterCarLeft":true,"SpotterCarLeftDistance":0.12,"FeedbackData.RearLeftWheelSlip":0.28,"FeedbackData.RearRightWheelSlip":0.04)");
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    waitingApp.tick(200);
    expect_true(waitingApp.state().rpm == 6123, "HTTP live mode should map RPM");
    expect_true(waitingApp.state().speedKmh == 155, "HTTP live mode should map speed");
    expect_true(waitingApp.state().gear == 5, "HTTP live mode should map gear");
    expect_true(!waitingApp.state().telemetryStale, "HTTP live data should not be stale");
    expect_true(waitingApp.state().session.hasAnyData, "HTTP live mode should map session data");
    expect_true(waitingApp.state().session.hasPredictedLap && waitingApp.state().session.predictedLapMs == 84445, "HTTP live mode should map predicted lap");
    expect_true(waitingApp.state().session.hasBestLap && waitingApp.state().session.bestLapMs == 83924, "HTTP live mode should map best lap");
    expect_true(waitingApp.state().session.hasLastLap && waitingApp.state().session.lastLapMs == 84445, "HTTP live mode should map last lap");
    expect_true(waitingApp.state().session.hasPosition && waitingApp.state().session.position == 4, "HTTP live mode should map position");
    expect_true(waitingApp.state().session.hasTotalPositions && waitingApp.state().session.totalPositions == 21, "HTTP live mode should map total positions");
    expect_true(waitingApp.state().session.hasLap && waitingApp.state().session.lap == 2, "HTTP live mode should map lap number");
    expect_true(waitingApp.state().session.hasTotalLaps && waitingApp.state().session.totalLaps == 12, "HTTP live mode should map total laps");
    expect_true(waitingApp.state().session.hasFuelLiters && waitingApp.state().session.fuelLiters > 6.3f, "HTTP live mode should map fuel liters");
    expect_true(waitingApp.state().session.hasBrakeBias && waitingApp.state().session.brakeBias > 52.7f, "HTTP live mode should map brake bias");
    expect_true(waitingApp.state().sideTelemetry.flags.yellow, "HTTP live mode should map yellow flag telemetry");
    expect_true(waitingApp.state().sideTelemetry.spotter.leftClose, "HTTP live mode should map left close spotter telemetry");
    expect_true(waitingApp.state().sideTelemetry.traction.throttle > 0.10f,
                "HTTP live mode should derive a non-zero grip load for traction bars");
    expect_true(waitingApp.state().sideTelemetry.traction.brake < waitingApp.state().sideTelemetry.traction.throttle,
                "HTTP live mode should keep brake load below drive load in an accelerating sample");
    expect_true(waitingApp.state().sideTelemetry.traction.direction == SideLedTractionDirection::Accelerating,
                "HTTP live mode should mark traction bars as accelerating when throttle dominates");
    expect_true(waitingApp.state().sideTelemetry.traction.leftLevel == 4, "HTTP live mode should map strong left traction slip");
    expect_true(waitingApp.state().sideTelemetry.traction.rightLevel <= 1, "HTTP live mode should keep very light right-side slip below the hard warning threshold");
    expect_true(waitingApp.state().sideLedFrame.source == SideLedSource::Traction,
                "HTTP live mode should render side LEDs from the traction pipeline");
    expect_true(waitingApp.state().sideLedFrame.event == SideLedEvent::TractionAccelerating,
                "HTTP live mode should render an accelerating traction frame");
    expect_true(waitingApp.state().sideLedFrame.direction == SideLedTractionDirection::Accelerating,
                "HTTP live mode should carry the accelerating direction into the rendered frame");
    expect_true(waitingApp.state().sideLedFrame.ledCountPerSide == 8,
                "HTTP live mode should honor the configured side LED count");
    expect_true(waitingApp.state().sideLedFrame.leftLevel >= 6,
                "HTTP live mode should strongly extend the left bar near the traction limit");
    expect_true(waitingApp.state().sideLedFrame.rightLevel >= 1 && waitingApp.state().sideLedFrame.rightLevel <= 4,
                "HTTP live mode should keep the lighter right side as a small warning only");
    expect_true(waitingApp.state().sideLedFrame.leftLevel > waitingApp.state().sideLedFrame.rightLevel,
                "HTTP live mode should emphasize the side with the stronger slip");
    expect_true(waitingApp.state().sideLedFrame.right.front() == 0 && waitingApp.state().sideLedFrame.right[7] != 0,
                "accelerating traction should fill the side bars from bottom to top");
    expect_true(!waitingApp.state().sideLedFrame.blinkFast && !waitingApp.state().sideLedFrame.blinkSlow,
                "traction bars should stay solid without the old blink behavior");

    httpServer.set_waiting();
    waitingApp.tick(800);
    expect_true(!waitingApp.state().telemetryStale, "recent HTTP data should stay live within timeout");
    waitingApp.tick(3201);
    expect_true(waitingApp.state().telemetryStale, "HTTP data should become stale after timeout");
    expect_true(waitingApp.state().rpm == 6123, "HTTP stale data should keep last live RPM");

    SimulatorApp fallbackApp;
    TelemetryServiceConfig fallbackConfig{};
    fallbackConfig.mode = TelemetryInputMode::SimHub;
    fallbackConfig.simHubTransport = SimHubTransport::HttpApi;
    fallbackConfig.httpPort = 18888;
    fallbackConfig.pollIntervalMs = 25;
    fallbackConfig.allowSimulatorFallback = true;
    httpServer.set_waiting();
    fallbackApp.configureTelemetry(fallbackConfig);
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    fallbackApp.tick(100);
    expect_true(fallbackApp.state().telemetryUsingFallback, "HTTP mode should use fallback when explicitly enabled");
    expect_true(fallbackApp.state().telemetrySource == UiTelemetrySource::Simulator, "fallback should report simulator as active source");
    expect_true(fallbackApp.state().rpm > 0, "fallback simulator should still provide telemetry");
    httpServer.set_live(R"({"speed":144,"gear":"4","rpms":5988,"maxRpm":7200})", "0.61", "0.00");
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    fallbackApp.tick(200);
    expect_true(!fallbackApp.state().telemetryUsingFallback, "live SimHub data should disable fallback automatically");
    expect_true(fallbackApp.state().telemetrySource == UiTelemetrySource::SimHubNetwork, "live SimHub data should become the active source automatically");
    expect_true(fallbackApp.state().rpm == 5988, "live SimHub data should replace fallback RPM");

    SimulatorApp tractionOnlyApp;
    tractionOnlyApp.configureTelemetry(waitingConfig);
    SideLedConfig tractionOnlyConfig = tractionOnlyApp.sideLedConfigSnapshot();
    tractionOnlyConfig.allowFlags = false;
    tractionOnlyConfig.allowSpotter = false;
    tractionOnlyConfig.allowWarnings = false;
    tractionOnlyConfig.allowTraction = true;
    tractionOnlyConfig.testMode = false;
    tractionOnlyConfig.ledCountPerSide = 12;
    tractionOnlyApp.applySideLedConfig(tractionOnlyConfig);
    httpServer.set_live(
        R"({"speed":132,"gear":"4","rpms":5800,"maxRpm":7200})",
        "0.67",
        "0.00",
        R"(,"FeedbackData.RearLeftWheelSlip":0.27,"FeedbackData.RearRightWheelSlip":0.11)");
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    tractionOnlyApp.tick(250);
    expect_true(tractionOnlyApp.state().sideLedFrame.source == SideLedSource::Traction,
                "HTTP traction-only config should let tyre slip drive the side LEDs");
    expect_true(tractionOnlyApp.state().sideLedFrame.event == SideLedEvent::TractionAccelerating,
                "HTTP traction-only config should use the accelerating frame when throttle dominates");
    expect_true(tractionOnlyApp.state().sideLedFrame.ledCountPerSide == 12,
                "HTTP traction-only config should support more than four LEDs per side");
    expect_true(tractionOnlyApp.state().sideLedFrame.leftLevel >= 9,
                "HTTP traction-only config should drive a tall left warning bar for strong slip");
    expect_true(tractionOnlyApp.state().sideLedFrame.rightLevel >= 3 && tractionOnlyApp.state().sideLedFrame.rightLevel <= 7,
                "HTTP traction-only config should keep the lighter right side in a moderate warning range");
    expect_true(tractionOnlyApp.state().sideLedFrame.leftLevel > tractionOnlyApp.state().sideLedFrame.rightLevel,
                "HTTP traction-only config should make the stronger slipping side dominant");

    SimulatorApp derivedLoadApp;
    derivedLoadApp.configureTelemetry(waitingConfig);
    SideLedConfig derivedSideConfig = derivedLoadApp.sideLedConfigSnapshot();
    derivedSideConfig.allowTraction = true;
    derivedSideConfig.allowSpotter = false;
    derivedSideConfig.allowFlags = false;
    derivedSideConfig.allowWarnings = false;
    derivedSideConfig.testMode = false;
    derivedSideConfig.ledCountPerSide = 10;
    derivedLoadApp.applySideLedConfig(derivedSideConfig);

    httpServer.set_live(
        R"({"speed":92,"gear":"3","rpms":4020,"maxRpm":7200})",
        "0.00",
        "0.00",
        R"(,"FeedbackData.RearLeftWheelSlip":0.03,"FeedbackData.RearRightWheelSlip":0.03)");
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    derivedLoadApp.tick(100);

    httpServer.set_live(
        R"({"speed":108,"gear":"3","rpms":5120,"maxRpm":7200})",
        "0.00",
        "0.00",
        R"(,"FeedbackData.RearLeftWheelSlip":0.04,"FeedbackData.RearRightWheelSlip":0.05)");
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    derivedLoadApp.tick(200);
    expect_true(derivedLoadApp.state().sideLedFrame.source == SideLedSource::Off,
                "derived traction should stay off during clean acceleration without meaningful slip");
    expect_true(derivedLoadApp.state().sideLedFrame.leftLevel == 0 && derivedLoadApp.state().sideLedFrame.rightLevel == 0,
                "derived traction should not raise a warning bar from speed gain alone");

    httpServer.set_live(
        R"({"speed":84,"gear":"3","rpms":3620,"maxRpm":7200})",
        "0.00",
        "0.00",
        R"(,"FeedbackData.FrontLeftWheelSlip":0.05,"FeedbackData.FrontRightWheelSlip":0.05)");
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    derivedLoadApp.tick(300);
    expect_true(derivedLoadApp.state().sideLedFrame.event == SideLedEvent::TractionBraking,
                "derived traction should detect braking without direct pedal telemetry");
    expect_true(derivedLoadApp.state().sideLedFrame.leftLevel >= 1 && derivedLoadApp.state().sideLedFrame.rightLevel >= 1,
                "derived braking should show a visible braking pre-warning");
    expect_true(derivedLoadApp.state().sideLedFrame.leftLevel <= 4 && derivedLoadApp.state().sideLedFrame.rightLevel <= 4,
                "derived braking without heavy lockup should stay below a full warning sweep");

    SimulatorApp perGearApp;
    TelemetryServiceConfig perGearConfig{};
    perGearConfig.mode = TelemetryInputMode::Simulator;
    perGearApp.configureTelemetry(perGearConfig);
    SimulatorLedBarConfig perGearLed = perGearApp.ledBarConfigSnapshot();
    perGearLed.autoScaleMaxRpm = false;
    perGearLed.maxRpmPerGearEnabled = true;
    perGearLed.fixedMaxRpmByGear[0] = 7000;
    perGearLed.fixedMaxRpmByGear[1] = 7600;
    perGearLed.fixedMaxRpmByGear[2] = 8100;
    perGearApp.applyLedBarConfig(perGearLed);
    perGearApp.tick(0);
    const int fixedInitialGear = perGearApp.state().gear;
    expect_true(fixedInitialGear >= 1 && fixedInitialGear <= 8, "per-gear fixed max test should start in a valid drive gear");
    expect_true(perGearApp.ledBarConfigSnapshot().effectiveMaxRpm == perGearLed.fixedMaxRpmByGear[static_cast<size_t>(fixedInitialGear - 1)],
                "per-gear fixed max RPM should match the current gear immediately");
    for (int i = 0; i < 60 && perGearApp.state().gear == fixedInitialGear; ++i)
    {
        perGearApp.execute(SimulatorCommand::IncreaseRpm);
        perGearApp.tick(100 + i);
    }
    const int fixedShiftedGear = perGearApp.state().gear;
    expect_true(fixedShiftedGear > fixedInitialGear && fixedShiftedGear <= 8, "simulator RPM ramp should advance into a higher gear for per-gear max test");
    expect_true(perGearApp.ledBarConfigSnapshot().effectiveMaxRpm == perGearLed.fixedMaxRpmByGear[static_cast<size_t>(fixedShiftedGear - 1)],
                "per-gear fixed max RPM should follow the active gear");

    SimulatorApp learnedPerGearApp;
    learnedPerGearApp.configureTelemetry(perGearConfig);
    SimulatorLedBarConfig learnedPerGearLed = learnedPerGearApp.ledBarConfigSnapshot();
    learnedPerGearLed.autoScaleMaxRpm = true;
    learnedPerGearLed.maxRpmPerGearEnabled = true;
    learnedPerGearLed.fixedMaxRpmByGear[0] = 6500;
    learnedPerGearLed.fixedMaxRpmByGear[1] = 7200;
    learnedPerGearApp.applyLedBarConfig(learnedPerGearLed);
    learnedPerGearApp.tick(0);
    const int learnedInitialGear = learnedPerGearApp.state().gear;
    expect_true(learnedInitialGear >= 1 && learnedInitialGear <= 8, "per-gear auto max test should start in a valid drive gear");
    expect_true(learnedPerGearApp.ledBarConfigSnapshot().effectiveMaxRpm == learnedPerGearLed.fixedMaxRpmByGear[static_cast<size_t>(learnedInitialGear - 1)],
                "per-gear auto max should start from the current gear baseline");
    for (int i = 0; i < 60 && learnedPerGearApp.state().gear == learnedInitialGear; ++i)
    {
        learnedPerGearApp.execute(SimulatorCommand::IncreaseRpm);
        learnedPerGearApp.tick(200 + i);
    }
    const int learnedShiftedGear = learnedPerGearApp.state().gear;
    expect_true(learnedShiftedGear > learnedInitialGear && learnedShiftedGear <= 8, "simulator RPM ramp should advance into a higher gear for per-gear auto max test");
    expect_true(learnedPerGearApp.ledBarConfigSnapshot().effectiveMaxRpm >= learnedPerGearLed.fixedMaxRpmByGear[static_cast<size_t>(learnedShiftedGear - 1)],
                "per-gear auto max should use the active gear baseline");
    expect_true(learnedPerGearApp.ledBarConfigSnapshot().effectiveMaxRpmByGear[static_cast<size_t>(learnedInitialGear - 1)] ==
                    learnedPerGearLed.fixedMaxRpmByGear[static_cast<size_t>(learnedInitialGear - 1)],
                "the previously active gear should keep its own learned max isolated");
    expect_true(learnedPerGearApp.ledBarConfigSnapshot().learnedMaxRpmByGear[static_cast<size_t>(learnedInitialGear - 1)],
                "per-gear auto max should mark the first driven gear as learned once it reaches the configured shift range");
    expect_true(!learnedPerGearApp.ledBarConfigSnapshot().learnedMaxRpmByGear[7],
                "per-gear auto max should ignore gears that have not been driven yet");

    learnedPerGearApp.execute(SimulatorCommand::IncreaseRpm);
    learnedPerGearApp.tick(500);
    const SimulatorPersistedState learnedPersistedState = []() {
        SimulatorPersistedState state{};
        expect_true(load_simulator_persisted_state(state), "learned per-gear app should persist simulator state");
        return state;
    }();
    expect_true(learnedPersistedState.ledBar.learnedMaxRpmByGear[static_cast<size_t>(learnedInitialGear - 1)],
                "persisted auto mode should store learned gear state");

    SimulatorApp learnedPersistedApp;
    expect_true(learnedPersistedApp.ledBarConfigSnapshot().learnedMaxRpmByGear[static_cast<size_t>(learnedInitialGear - 1)],
                "reloaded simulator app should restore learned per-gear state");

    SimulatorApp udpLiveApp;
    TelemetryServiceConfig udpLiveConfig{};
    udpLiveConfig.mode = TelemetryInputMode::SimHub;
    udpLiveConfig.simHubTransport = SimHubTransport::JsonUdp;
    udpLiveConfig.udpPort = 20779;
    udpLiveConfig.allowSimulatorFallback = false;
    udpLiveApp.configureTelemetry(udpLiveConfig);
    SideLedConfig udpSideConfig = udpLiveApp.sideLedConfigSnapshot();
    udpSideConfig.allowTraction = true;
    udpSideConfig.allowSpotter = false;
    udpSideConfig.allowFlags = false;
    udpSideConfig.allowWarnings = false;
    udpSideConfig.testMode = false;
    udpSideConfig.ledCountPerSide = 8;
    udpLiveApp.applySideLedConfig(udpSideConfig);
    udpLiveApp.tick(0);
    expect_true(send_udp_packet(udpLiveConfig.udpPort, "{\"rpm\":6123,\"speed\":155,\"gear\":5,\"throttle\":0.18,\"brake\":0.84,\"BestLapTime\":\"01:23.924\",\"LastLapTime\":\"01:24.445\",\"PredictedLapTime\":\"01:24.950\",\"Position\":3,\"TotalCars\":19,\"CurrentLap\":7,\"TotalLaps\":18,\"Fuel\":18.2,\"Flag_Blue\":true,\"Flag_Name\":\"Blue\",\"SpotterCarRight\":true,\"SpotterCarRightDistance\":0.13,\"PitLimiterOn\":true,\"FeedbackData.RearRightWheelSlip\":0.22}"),
                "UDP live test should send packet");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    udpLiveApp.tick(100);
    expect_true(udpLiveApp.state().rpm == 6123, "UDP live mode should map RPM");
    expect_true(udpLiveApp.state().speedKmh == 155, "UDP live mode should map speed");
    expect_true(udpLiveApp.state().gear == 5, "UDP live mode should map gear");
    expect_true(!udpLiveApp.state().telemetryStale, "fresh UDP data should not be stale");
    expect_true(!udpLiveApp.state().telemetryUsingFallback, "fresh UDP data should not use fallback");
    expect_true(udpLiveApp.state().session.hasBestLap && udpLiveApp.state().session.bestLapMs == 83924, "UDP live mode should map best lap");
    expect_true(udpLiveApp.state().session.hasLap && udpLiveApp.state().session.lap == 7, "UDP live mode should map lap number");
    expect_true(udpLiveApp.state().session.hasFuelLiters && udpLiveApp.state().session.fuelLiters > 18.1f, "UDP live mode should map fuel liters");
    expect_true(udpLiveApp.state().sideTelemetry.flags.blue, "UDP live mode should map blue flag telemetry");
    expect_true(udpLiveApp.state().sideTelemetry.spotter.rightClose, "UDP live mode should map right close spotter telemetry");
    expect_true(udpLiveApp.state().sideTelemetry.warnings.pitLimiter, "UDP live mode should map pit limiter telemetry");
    expect_true(udpLiveApp.state().sideTelemetry.traction.brake > 0.18f,
                "UDP live mode should derive a visible brake load for traction bars");
    expect_true(udpLiveApp.state().sideTelemetry.traction.direction == SideLedTractionDirection::Braking,
                "UDP live mode should mark braking when brake dominates");
    expect_true(udpLiveApp.state().sideTelemetry.traction.rightLevel >= 3, "UDP live mode should map right traction slip");
    expect_true(udpLiveApp.state().sideLedFrame.source == SideLedSource::Traction,
                "UDP live mode should render the new traction bars instead of the old GT3 awareness state");
    expect_true(udpLiveApp.state().sideLedFrame.event == SideLedEvent::TractionBraking,
                "UDP live mode should render a braking traction frame");
    expect_true(udpLiveApp.state().sideLedFrame.direction == SideLedTractionDirection::Braking,
                "UDP live mode should carry the braking direction into the rendered frame");
    expect_true(!udpLiveApp.state().sideLedFrame.blinkFast && !udpLiveApp.state().sideLedFrame.blinkSlow,
                "UDP traction bars should also stay solid without blinking");
    UiRuntimeState ledState{};
    ledState.rpm = 6900;

    SimulatorLedBarConfig casualConfig{};
    casualConfig.mode = SimulatorLedMode::Casual;
    const VirtualLedBarFrame casualFrame = build_virtual_led_bar_frame(ledState, casualConfig, 0);
    expect_true(!casualFrame.blinkActive, "casual LED mode should stay solid at shift RPM");

    SimulatorLedBarConfig gt3Config{};
    gt3Config.mode = SimulatorLedMode::Gt3;
    ledState.rpm = 2800;
    const VirtualLedBarFrame gt3Frame = build_virtual_led_bar_frame(ledState, gt3Config, 0);
    expect_true(gt3Frame.leds.front() != 0 && gt3Frame.leds.back() != 0, "GT3 LED mode should light both outer edges first");
    expect_true(gt3Frame.leds.front() != gt3Frame.leds[gt3Frame.leds.size() / 2], "GT3 LED mode should keep the center visually darker at low fill");

    SimulatorLedBarConfig aggressiveConfig{};
    aggressiveConfig.mode = SimulatorLedMode::Aggressive;
    ledState.rpm = 7000;
    const VirtualLedBarFrame aggressiveFrame = build_virtual_led_bar_frame(ledState, aggressiveConfig, 0);
    expect_true(aggressiveFrame.blinkActive, "aggressive LED mode should enter blink mode near max RPM");
    expect_true(aggressiveFrame.litCount == aggressiveConfig.activeLedCount, "aggressive LED mode should drive the full bar during the shift blink");

    httpServer.stop();
    std::filesystem::remove(persistedStatePath, removeError);
    std::cout << "simulator_app_tests passed\n";
    return 0;
}
