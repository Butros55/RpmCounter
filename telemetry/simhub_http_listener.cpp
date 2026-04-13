#include "simhub_http_listener.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>

#include "json_extract.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
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
}

SimHubHttpListener::SimHubHttpListener() = default;

SimHubHttpListener::~SimHubHttpListener()
{
    stop();
}

bool SimHubHttpListener::start(uint16_t port, uint32_t pollIntervalMs, bool debugLogging)
{
    stop();
    port_ = port;
    pollIntervalMs_ = std::max<uint32_t>(pollIntervalMs, 15U);
    debugLogging_ = debugLogging;

    stopRequested_ = false;
    running_ = true;
    worker_ = std::thread(&SimHubHttpListener::run, this);
    std::cout << "[telemetry] Polling SimHub API on http://127.0.0.1:" << port_ << '\n';
    return true;
}

void SimHubHttpListener::stop()
{
    stopRequested_ = true;
    if (worker_.joinable())
    {
        worker_.join();
    }
    running_ = false;

    std::lock_guard<std::mutex> lock(mutex_);
    latestFrame_ = NormalizedTelemetryFrame{};
    latestSequence_ = 0;
    lastPolledSequence_ = 0;
    sourceReachable_ = false;
    waitingForData_ = true;
}

bool SimHubHttpListener::isRunning() const
{
    return running_;
}

bool SimHubHttpListener::poll(NormalizedTelemetryFrame &frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (latestSequence_ == 0 || latestSequence_ == lastPolledSequence_)
    {
        return false;
    }

    frame = latestFrame_;
    lastPolledSequence_ = latestSequence_;
    return true;
}

bool SimHubHttpListener::sourceReachable() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sourceReachable_;
}

bool SimHubHttpListener::waitingForData() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return waitingForData_;
}

void SimHubHttpListener::run()
{
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "[telemetry] Failed to initialize WinSock for SimHub API polling\n";
        running_ = false;
        return;
    }
#endif

    while (!stopRequested_)
    {
        bool gameRunning = false;
        bool hasNewData = false;
        const bool stateAvailable = fetch_game_state(gameRunning, hasNewData);
        set_status(stateAvailable, !(stateAvailable && gameRunning && hasNewData));

        if (stateAvailable && gameRunning && hasNewData)
        {
            NormalizedTelemetryFrame frame{};
            if (fetch_frame(frame))
            {
                std::lock_guard<std::mutex> lock(mutex_);
                latestFrame_ = frame;
                ++latestSequence_;
                if (debugLogging_)
                {
                    logFrame(frame);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs_));
    }

#ifdef _WIN32
    WSACleanup();
#endif
    running_ = false;
}

bool SimHubHttpListener::fetch_game_state(bool &gameRunning, bool &hasNewData)
{
    std::string payload;
    if (!http_get("/Api/GetGameData", payload))
    {
        gameRunning = false;
        hasNewData = false;
        return false;
    }

    bool runningFlag = false;
    const bool hasRunningFlag = telemetry_json::extract_json_bool(payload, {"GameRunning", "RunningGameProcessDetected"}, runningFlag);
    gameRunning = hasRunningFlag && runningFlag;
    hasNewData = payload.find("\"NewData\":null") == std::string::npos &&
                 payload.find("\"NewData\": null") == std::string::npos;
    return true;
}

bool SimHubHttpListener::fetch_frame(NormalizedTelemetryFrame &frame)
{
    std::string simplePayload;
    if (!http_get("/Api/GetGameDataSimple", simplePayload))
    {
        return false;
    }

    double rpm = 0.0;
    double speed = 0.0;
    double throttle = 0.0;
    int gear = 0;

    const bool hasRpm = telemetry_json::extract_json_number(simplePayload, {"rpms", "rpm", "Rpm", "RPM"}, rpm);
    const bool hasSpeed = telemetry_json::extract_json_number(simplePayload, {"speed", "speedKmh", "SpeedKmh"}, speed);
    const bool hasGear = telemetry_json::extract_json_gear(simplePayload, {"gear", "Gear"}, gear);
    if (!hasRpm && !hasSpeed && !hasGear)
    {
        return false;
    }

    std::string throttlePayload;
    if (http_get("/Api/GetProperty/DataCorePlugin.GameData.NewData.Throttle", throttlePayload))
    {
        telemetry_json::extract_json_number(throttlePayload, {"Throttle", "throttle"}, throttle);
    }

    frame.rpm = std::max(0, static_cast<int>(std::lround(rpm)));
    frame.speedKmh = std::max(0, static_cast<int>(std::lround(speed)));
    frame.gear = std::max(0, gear);
    frame.throttle = std::clamp(static_cast<float>(throttle), 0.0f, 1.0f);
    frame.stale = false;
    frame.usingFallback = false;
    frame.live = true;
    return true;
}

bool SimHubHttpListener::http_get(const char *path, std::string &responseBody) const
{
    responseBody.clear();

    const intptr_t socketHandle = static_cast<intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (socketHandle == kInvalidSocketHandle)
    {
        return false;
    }

    const int timeoutMs = 300;
#ifdef _WIN32
    setsockopt(static_cast<SOCKET>(socketHandle), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(static_cast<SOCKET>(socketHandle), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000);
    setsockopt(static_cast<int>(socketHandle), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(static_cast<int>(socketHandle), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int connectResult = 0;
#ifdef _WIN32
    connectResult = ::connect(static_cast<SOCKET>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#else
    connectResult = ::connect(static_cast<int>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#endif
    if (connectResult != 0)
    {
        close_socket_handle(socketHandle);
        return false;
    }

    const std::string request = std::string("GET ") + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";

    size_t sentBytes = 0;
    while (sentBytes < request.size())
    {
#ifdef _WIN32
        const int chunk = ::send(static_cast<SOCKET>(socketHandle), request.data() + sentBytes, static_cast<int>(request.size() - sentBytes), 0);
#else
        const int chunk = static_cast<int>(::send(static_cast<int>(socketHandle), request.data() + sentBytes, request.size() - sentBytes, 0));
#endif
        if (chunk <= 0)
        {
            close_socket_handle(socketHandle);
            return false;
        }
        sentBytes += static_cast<size_t>(chunk);
    }

    std::string response;
    std::array<char, 2048> buffer{};
    while (true)
    {
#ifdef _WIN32
        const int bytesRead = ::recv(static_cast<SOCKET>(socketHandle), buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const int bytesRead = static_cast<int>(::recv(static_cast<int>(socketHandle), buffer.data(), buffer.size(), 0));
#endif
        if (bytesRead <= 0)
        {
            break;
        }
        response.append(buffer.data(), static_cast<size_t>(bytesRead));
    }

    close_socket_handle(socketHandle);

    const size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        return false;
    }

    if (response.rfind("HTTP/1.1 200", 0) != 0 && response.rfind("HTTP/1.0 200", 0) != 0)
    {
        return false;
    }

    responseBody = response.substr(headerEnd + 4);
    return true;
}

void SimHubHttpListener::set_status(bool sourceReachable, bool waitingForData)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sourceReachable_ = sourceReachable;
    waitingForData_ = waitingForData;
}

void SimHubHttpListener::logFrame(const NormalizedTelemetryFrame &frame) const
{
    std::cout << "[telemetry] SimHub API sample received\n";
    std::cout << "[telemetry] Parsed rpm=" << frame.rpm
              << " speed=" << frame.speedKmh
              << " gear=" << frame.gear
              << " throttle=" << frame.throttle
              << '\n';
}
