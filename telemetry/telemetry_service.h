#pragma once

#include <cstdint>

#include "simulator/rpm_simulator.h"
#include "simhub_http_listener.h"
#include "simhub_udp_listener.h"
#include "telemetry_types.h"

class TelemetryService
{
public:
    TelemetryService();
    ~TelemetryService();

    void configure(const TelemetryServiceConfig &config);
    void reset();
    void tick(uint32_t nowMs);

    void increaseSimulatorRpm();
    void decreaseSimulatorRpm();
    void toggleSimulatorAnimation();
    bool simulatorAnimationEnabled() const;

    const TelemetryServiceConfig &config() const;
    const NormalizedTelemetryFrame &frame() const;

private:
    void updateSimulator(uint32_t nowMs);
    void updateSimHub(uint32_t nowMs);
    void updateWaitingFrame();
    void logStaleTransition(bool stale, bool usingFallback);

    TelemetryServiceConfig config_{};
    RpmSimulator simulator_{};
    SimHubUdpListener udpListener_{};
    SimHubHttpListener httpListener_{};
    NormalizedTelemetryFrame frame_{};
    NormalizedTelemetryFrame lastLiveFrame_{};
    uint32_t lastLiveFrameMs_ = 0;
    bool hasLiveFrame_ = false;
    bool lastStaleState_ = false;
    bool lastFallbackState_ = false;
};
