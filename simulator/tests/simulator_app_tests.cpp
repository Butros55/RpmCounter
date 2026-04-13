#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "simulator_app.h"

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
            throttle_ = "0.0";
        }

        void set_live(const std::string &simpleJson, const std::string &throttle)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            gameRunning_ = true;
            hasNewData_ = true;
            simpleJson_ = simpleJson;
            throttle_ = throttle;
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

            {
                std::lock_guard<std::mutex> lock(mutex_);
                statusPayload = std::string("{\"GameRunning\":") + (gameRunning_ ? "true" : "false") +
                                ",\"NewData\":" + (hasNewData_ ? "{\"Rpms\":1}" : "null") + "}";
                simplePayload = simpleJson_;
                throttlePayload = throttle_;
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
        std::string throttle_ = "0.0";
#ifdef _WIN32
        bool winsockReady_ = false;
#endif
    };
}

int main()
{
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
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    waitingApp.tick(100);
    expect_true(waitingApp.state().telemetrySource == UiTelemetrySource::SimHubNetwork, "HTTP mode should mark SimHub as source");
    expect_true(waitingApp.state().telemetryStale, "HTTP mode without data should be stale");
    expect_true(!waitingApp.state().telemetryUsingFallback, "HTTP mode should not use simulator fallback by default");
    expect_true(waitingApp.state().rpm == 0, "HTTP waiting state should not inject demo RPM");

    httpServer.set_live(R"({"speed":155,"gear":"5","rpms":6123,"maxRpm":7200})", "0.82");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    waitingApp.tick(200);
    expect_true(waitingApp.state().rpm == 6123, "HTTP live mode should map RPM");
    expect_true(waitingApp.state().speedKmh == 155, "HTTP live mode should map speed");
    expect_true(waitingApp.state().gear == 5, "HTTP live mode should map gear");
    expect_true(!waitingApp.state().telemetryStale, "HTTP live data should not be stale");

    httpServer.set_waiting();
    waitingApp.tick(800);
    expect_true(!waitingApp.state().telemetryStale, "recent HTTP data should stay live within timeout");
    waitingApp.tick(2301);
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
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    fallbackApp.tick(100);
    expect_true(fallbackApp.state().telemetryUsingFallback, "HTTP mode should use fallback when explicitly enabled");
    expect_true(fallbackApp.state().rpm > 0, "fallback simulator should still provide telemetry");

    SimulatorApp udpLiveApp;
    TelemetryServiceConfig udpLiveConfig{};
    udpLiveConfig.mode = TelemetryInputMode::SimHub;
    udpLiveConfig.simHubTransport = SimHubTransport::JsonUdp;
    udpLiveConfig.udpPort = 20779;
    udpLiveConfig.allowSimulatorFallback = false;
    udpLiveApp.configureTelemetry(udpLiveConfig);
    udpLiveApp.tick(0);
    expect_true(send_udp_packet(udpLiveConfig.udpPort, "{\"rpm\":6123,\"speed\":155,\"gear\":5,\"throttle\":0.82}"),
                "UDP live test should send packet");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    udpLiveApp.tick(100);
    expect_true(udpLiveApp.state().rpm == 6123, "UDP live mode should map RPM");
    expect_true(udpLiveApp.state().speedKmh == 155, "UDP live mode should map speed");
    expect_true(udpLiveApp.state().gear == 5, "UDP live mode should map gear");
    expect_true(!udpLiveApp.state().telemetryStale, "fresh UDP data should not be stale");
    expect_true(!udpLiveApp.state().telemetryUsingFallback, "fresh UDP data should not use fallback");

    httpServer.stop();
    std::cout << "simulator_app_tests passed\n";
    return 0;
}
