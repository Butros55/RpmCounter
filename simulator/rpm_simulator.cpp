#include "rpm_simulator.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kMinRpm = 900;
    constexpr int kMaxRpm = 7200;
    constexpr int kShiftThreshold = 5600;

    int clamp_rpm(int value)
    {
        return std::max(kMinRpm, std::min(kMaxRpm, value));
    }
}

RpmSimulator::RpmSimulator()
{
    reset();
}

void RpmSimulator::reset()
{
    frame_ = NormalizedTelemetryFrame{};
    frame_.rpm = kMinRpm;
    frame_.speedKmh = 18;
    frame_.gear = 1;
    frame_.throttle = 0.15f;
    frame_.live = true;

    startMs_ = 0;
    manualRpmOffset_ = 0;
    animateRpm_ = true;
}

void RpmSimulator::tick(uint32_t nowMs)
{
    if (startMs_ == 0)
    {
        startMs_ = nowMs;
    }

    int rpm = frame_.rpm;
    if (animateRpm_)
    {
        const double elapsed = static_cast<double>(nowMs - startMs_);
        const double wave = (std::sin(elapsed / 1200.0) + 1.0) * 0.5;
        rpm = static_cast<int>(2800 + wave * 3200);
    }

    frame_.rpm = clamp_rpm(rpm + manualRpmOffset_);
    frame_.timestampMs = nowMs;
    frame_.stale = false;
    frame_.usingFallback = false;
    frame_.live = true;

    if (frame_.rpm < 1600)
    {
        frame_.gear = 1;
        frame_.speedKmh = 18;
    }
    else if (frame_.rpm < 2600)
    {
        frame_.gear = 2;
        frame_.speedKmh = 34;
    }
    else if (frame_.rpm < 3800)
    {
        frame_.gear = 3;
        frame_.speedKmh = 58;
    }
    else if (frame_.rpm < 5000)
    {
        frame_.gear = 4;
        frame_.speedKmh = 82;
    }
    else if (frame_.rpm < 6200)
    {
        frame_.gear = 5;
        frame_.speedKmh = 116;
    }
    else
    {
        frame_.gear = 6;
        frame_.speedKmh = 144;
    }

    const float normalizedRpm = static_cast<float>(frame_.rpm - kMinRpm) / static_cast<float>(kMaxRpm - kMinRpm);
    frame_.throttle = std::clamp(0.15f + normalizedRpm * 0.8f, 0.0f, 1.0f);
}

void RpmSimulator::increaseRpm()
{
    manualRpmOffset_ = std::min(manualRpmOffset_ + 250, 2200);
}

void RpmSimulator::decreaseRpm()
{
    manualRpmOffset_ = std::max(manualRpmOffset_ - 250, -1800);
}

void RpmSimulator::toggleAnimation()
{
    animateRpm_ = !animateRpm_;
}

bool RpmSimulator::animationEnabled() const
{
    return animateRpm_;
}

const NormalizedTelemetryFrame &RpmSimulator::frame() const
{
    return frame_;
}
