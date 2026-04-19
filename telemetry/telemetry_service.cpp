#include "telemetry_service.h"

#include <iostream>

TelemetryService::TelemetryService()
{
    reset();
}

TelemetryService::~TelemetryService()
{
    udpListener_.stop();
    httpListener_.stop();
}

void TelemetryService::configure(const TelemetryServiceConfig &config)
{
    config_ = config;
    reset();
}

void TelemetryService::reset()
{
    udpListener_.stop();
    httpListener_.stop();
    simulator_.reset();
    frame_ = NormalizedTelemetryFrame{};
    lastLiveFrame_ = NormalizedTelemetryFrame{};
    lastLiveFrameMs_ = 0;
    hasLiveFrame_ = false;
    lastStaleState_ = false;
    lastFallbackState_ = false;
}

void TelemetryService::tick(uint32_t nowMs)
{
    if (config_.mode == TelemetryInputMode::SimHub)
    {
        updateSimHub(nowMs);
    }
    else
    {
        updateSimulator(nowMs);
    }
}

void TelemetryService::increaseSimulatorRpm()
{
    simulator_.increaseRpm();
}

void TelemetryService::decreaseSimulatorRpm()
{
    simulator_.decreaseRpm();
}

void TelemetryService::toggleSimulatorAnimation()
{
    simulator_.toggleAnimation();
}

bool TelemetryService::simulatorAnimationEnabled() const
{
    return simulator_.animationEnabled();
}

const TelemetryServiceConfig &TelemetryService::config() const
{
    return config_;
}

const NormalizedTelemetryFrame &TelemetryService::frame() const
{
    return frame_;
}

void TelemetryService::updateSimulator(uint32_t nowMs)
{
    const NormalizedTelemetryFrame previousFrame = frame_;
    const uint32_t previousFrameMs = frame_.timestampMs;
    simulator_.tick(nowMs);
    frame_ = simulator_.frame();
    frame_.timestampMs = nowMs;
    frame_.stale = false;
    frame_.usingFallback = false;
    frame_.live = true;
    enhanceTractionFrame(frame_,
                         nowMs,
                         previousFrameMs > 0 ? &previousFrame : nullptr,
                         previousFrameMs);
}

void TelemetryService::updateWaitingFrame()
{
    frame_ = NormalizedTelemetryFrame{};
    frame_.timestampMs = 0;
    frame_.stale = true;
    frame_.usingFallback = false;
    frame_.live = false;
}

void TelemetryService::updateSimHub(uint32_t nowMs)
{
    if (config_.simHubTransport == SimHubTransport::HttpApi)
    {
        if (!httpListener_.isRunning())
        {
            httpListener_.start(config_.httpPort, config_.pollIntervalMs, config_.debugLogging);
        }
    }
    else if (!udpListener_.isRunning())
    {
        udpListener_.start(config_.udpPort, config_.debugLogging);
    }

    NormalizedTelemetryFrame sample{};
    bool hasFreshSample = false;
    if (config_.simHubTransport == SimHubTransport::HttpApi)
    {
        hasFreshSample = httpListener_.poll(sample);
    }
    else
    {
        hasFreshSample = udpListener_.poll(nowMs, sample);
    }

    if (hasFreshSample)
    {
        sample.timestampMs = nowMs;
        enhanceTractionFrame(sample,
                             nowMs,
                             hasLiveFrame_ ? &lastLiveFrame_ : nullptr,
                             lastLiveFrameMs_);
        frame_ = sample;
        lastLiveFrame_ = sample;
        lastLiveFrameMs_ = nowMs;
        hasLiveFrame_ = true;
    }
    else if (hasLiveFrame_)
    {
        frame_ = lastLiveFrame_;
        const uint32_t ageMs = nowMs - lastLiveFrameMs_;
        frame_.stale = ageMs > config_.staleTimeoutMs;
        frame_.usingFallback = false;
        frame_.live = !frame_.stale;
        frame_.timestampMs = lastLiveFrameMs_;
    }
    else if (config_.allowSimulatorFallback)
    {
        simulator_.tick(nowMs);
        frame_ = simulator_.frame();
        frame_.timestampMs = nowMs;
        frame_.stale = true;
        frame_.usingFallback = true;
        frame_.live = false;
    }
    else
    {
        updateWaitingFrame();
    }

    logStaleTransition(frame_.stale, frame_.usingFallback);
}

void TelemetryService::enhanceTractionFrame(NormalizedTelemetryFrame &frame,
                                            uint32_t nowMs,
                                            const NormalizedTelemetryFrame *previousFrame,
                                            uint32_t previousFrameMs)
{
    if (previousFrame == nullptr || previousFrameMs == 0 || nowMs <= previousFrameMs)
    {
        side_led_enhance_traction_state(frame.sideLeds.traction,
                                        frame.speedKmh,
                                        frame.speedKmh,
                                        frame.rpm,
                                        frame.rpm,
                                        0);
        return;
    }

    side_led_enhance_traction_state(frame.sideLeds.traction,
                                    frame.speedKmh,
                                    previousFrame->speedKmh,
                                    frame.rpm,
                                    previousFrame->rpm,
                                    nowMs - previousFrameMs);
}

void TelemetryService::logStaleTransition(bool stale, bool usingFallback)
{
    if (!config_.debugLogging)
    {
        return;
    }

    if (lastStaleState_ == stale && lastFallbackState_ == usingFallback)
    {
        return;
    }

    if (!stale)
    {
        std::cout << "[telemetry] SimHub telemetry live\n";
    }
    else if (usingFallback)
    {
        std::cout << "[telemetry] SimHub telemetry stale, using simulator fallback\n";
    }
    else if (!hasLiveFrame_)
    {
        std::cout << "[telemetry] Waiting for SimHub telemetry\n";
    }
    else
    {
        std::cout << "[telemetry] SimHub telemetry stale, keeping last known value\n";
    }

    lastStaleState_ = stale;
    lastFallbackState_ = usingFallback;
}
