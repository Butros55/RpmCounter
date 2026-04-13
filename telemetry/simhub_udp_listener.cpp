#include "simhub_udp_listener.h"

#include <cmath>
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

    bool is_would_block_error()
    {
#ifdef _WIN32
        const int errorCode = WSAGetLastError();
        return errorCode == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
    }
}

SimHubUdpListener::SimHubUdpListener() = default;

SimHubUdpListener::~SimHubUdpListener()
{
    stop();
}

bool SimHubUdpListener::start(uint16_t port, bool debugLogging)
{
    stop();
    debugLogging_ = debugLogging;

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "[telemetry] Failed to initialize WinSock\n";
        return false;
    }
    winsockReady_ = true;
#endif

    const intptr_t socketHandle = static_cast<intptr_t>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (socketHandle == kInvalidSocketHandle)
    {
        std::cerr << "[telemetry] Failed to create SimHub UDP socket\n";
        stop();
        return false;
    }

#ifdef _WIN32
    u_long nonBlocking = 1;
    if (ioctlsocket(static_cast<SOCKET>(socketHandle), FIONBIO, &nonBlocking) != 0)
    {
        std::cerr << "[telemetry] Failed to configure non-blocking UDP socket\n";
        close_socket_handle(socketHandle);
        stop();
        return false;
    }
#else
    const int flags = fcntl(static_cast<int>(socketHandle), F_GETFL, 0);
    if (flags < 0 || fcntl(static_cast<int>(socketHandle), F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "[telemetry] Failed to configure non-blocking UDP socket\n";
        close_socket_handle(socketHandle);
        stop();
        return false;
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    int bindResult = 0;
#ifdef _WIN32
    bindResult = ::bind(static_cast<SOCKET>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#else
    bindResult = ::bind(static_cast<int>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#endif

    if (bindResult != 0)
    {
        std::cerr << "[telemetry] Failed to bind SimHub UDP socket to port " << port << '\n';
        close_socket_handle(socketHandle);
        stop();
        return false;
    }

    socketHandle_ = socketHandle;
    running_ = true;
    std::cout << "[telemetry] Listening for SimHub UDP on 127.0.0.1:" << port << '\n';
    return true;
}

void SimHubUdpListener::stop()
{
    if (socketHandle_ != kInvalidSocketHandle)
    {
        close_socket_handle(socketHandle_);
        socketHandle_ = kInvalidSocketHandle;
    }
    running_ = false;

#ifdef _WIN32
    if (winsockReady_)
    {
        WSACleanup();
        winsockReady_ = false;
    }
#endif
}

bool SimHubUdpListener::isRunning() const
{
    return running_;
}

bool SimHubUdpListener::poll(uint32_t nowMs, NormalizedTelemetryFrame &frame)
{
    if (!running_ || socketHandle_ == kInvalidSocketHandle)
    {
        return false;
    }

    bool receivedPacket = false;
    while (true)
    {
        char buffer[4096]{};
        sockaddr_in remoteAddress{};
#ifdef _WIN32
        int remoteLength = sizeof(remoteAddress);
        const int bytesRead = recvfrom(static_cast<SOCKET>(socketHandle_),
                                       buffer,
                                       static_cast<int>(sizeof(buffer) - 1),
                                       0,
                                       reinterpret_cast<sockaddr *>(&remoteAddress),
                                       &remoteLength);
#else
        socklen_t remoteLength = sizeof(remoteAddress);
        const int bytesRead = static_cast<int>(recvfrom(static_cast<int>(socketHandle_),
                                                        buffer,
                                                        sizeof(buffer) - 1,
                                                        0,
                                                        reinterpret_cast<sockaddr *>(&remoteAddress),
                                                        &remoteLength));
#endif

        if (bytesRead < 0)
        {
            if (is_would_block_error())
            {
                break;
            }

            std::cerr << "[telemetry] Error while receiving SimHub UDP packet\n";
            break;
        }

        buffer[bytesRead] = '\0';
        const std::string payload(buffer, static_cast<size_t>(bytesRead));
        NormalizedTelemetryFrame parsedFrame{};
        if (parsePacket(payload, nowMs, parsedFrame))
        {
            frame = parsedFrame;
            receivedPacket = true;
            if (debugLogging_)
            {
                logPacket(payload, parsedFrame);
            }
        }
    }

    return receivedPacket;
}

bool SimHubUdpListener::parsePacket(const std::string &payload, uint32_t nowMs, NormalizedTelemetryFrame &frame) const
{
    double rpm = 0.0;
    double speed = 0.0;
    double throttle = 0.0;
    int gear = 0;

    const bool hasRpm = telemetry_json::extract_json_number(payload, {"rpm", "Rpm", "RPM", "engineRpm", "EngineRpm", "rpms"}, rpm);
    const bool hasSpeed = telemetry_json::extract_json_number(payload, {"speedKmh", "SpeedKmh", "speed", "Speed", "vehicleSpeed"}, speed);
    const bool hasGear = telemetry_json::extract_json_gear(payload, {"gear", "Gear", "currentGear", "CurrentGear"}, gear);
    const bool hasThrottle = telemetry_json::extract_json_number(payload, {"throttle", "Throttle", "gas", "Gas"}, throttle);

    if (!hasRpm && !hasSpeed && !hasGear)
    {
        return false;
    }

    frame.rpm = std::max(0, static_cast<int>(std::lround(rpm)));
    frame.speedKmh = std::max(0, static_cast<int>(std::lround(speed)));
    frame.gear = std::max(0, gear);
    frame.throttle = hasThrottle ? std::clamp(static_cast<float>(throttle), 0.0f, 1.0f) : 0.0f;
    frame.timestampMs = nowMs;
    frame.stale = false;
    frame.usingFallback = false;
    frame.live = true;
    return true;
}

void SimHubUdpListener::logPacket(const std::string &payload, const NormalizedTelemetryFrame &frame) const
{
    std::cout << "[telemetry] SimHub packet received (" << payload.size() << " bytes)\n";
    std::cout << "[telemetry] Parsed rpm=" << frame.rpm
              << " speed=" << frame.speedKmh
              << " gear=" << frame.gear
              << " throttle=" << frame.throttle
              << '\n';
}
