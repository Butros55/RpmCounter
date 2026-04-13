#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "telemetry_types.h"

class SimHubHttpListener
{
public:
    SimHubHttpListener();
    ~SimHubHttpListener();

    bool start(uint16_t port, uint32_t pollIntervalMs, bool debugLogging);
    void stop();

    bool isRunning() const;
    bool poll(NormalizedTelemetryFrame &frame);
    bool sourceReachable() const;
    bool waitingForData() const;

private:
    void run();
    bool fetch_game_state(bool &gameRunning, bool &hasNewData);
    bool fetch_frame(NormalizedTelemetryFrame &frame);
    bool http_get(const char *path, std::string &responseBody) const;
    void set_status(bool sourceReachable, bool waitingForData);
    void logFrame(const NormalizedTelemetryFrame &frame) const;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::thread worker_{};
    uint16_t port_ = 8888;
    uint32_t pollIntervalMs_ = 25;
    bool debugLogging_ = false;

    mutable std::mutex mutex_{};
    NormalizedTelemetryFrame latestFrame_{};
    uint64_t latestSequence_ = 0;
    uint64_t lastPolledSequence_ = 0;
    bool sourceReachable_ = false;
    bool waitingForData_ = true;
};
