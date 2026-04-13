#pragma once

#include <cstdint>

enum class TelemetryInputMode : uint8_t
{
    Simulator = 0,
    SimHub
};

enum class SimHubTransport : uint8_t
{
    HttpApi = 0,
    JsonUdp
};

struct NormalizedTelemetryFrame
{
    int rpm = 0;
    int speedKmh = 0;
    int gear = 0;
    float throttle = 0.0f;
    uint32_t timestampMs = 0;
    bool stale = false;
    bool usingFallback = false;
    bool live = false;
};

struct TelemetryServiceConfig
{
    TelemetryInputMode mode = TelemetryInputMode::Simulator;
    SimHubTransport simHubTransport = SimHubTransport::HttpApi;
    uint16_t udpPort = 20888;
    uint16_t httpPort = 8888;
    uint32_t pollIntervalMs = 25;
    uint32_t staleTimeoutMs = 2000;
    bool debugLogging = false;
    bool allowSimulatorFallback = false;
};
