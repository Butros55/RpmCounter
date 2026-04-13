#pragma once

#include <cstdint>
#include <string>

#include "telemetry_types.h"

class SimHubUdpListener
{
public:
    SimHubUdpListener();
    ~SimHubUdpListener();

    bool start(uint16_t port, bool debugLogging);
    void stop();

    bool isRunning() const;
    bool poll(uint32_t nowMs, NormalizedTelemetryFrame &frame);

private:
    bool parsePacket(const std::string &payload, uint32_t nowMs, NormalizedTelemetryFrame &frame) const;
    void logPacket(const std::string &payload, const NormalizedTelemetryFrame &frame) const;

    intptr_t socketHandle_ = -1;
    bool running_ = false;
    bool debugLogging_ = false;
#ifdef _WIN32
    bool winsockReady_ = false;
#endif
};
