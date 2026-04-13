#pragma once

#include <cstdint>

#include "telemetry/telemetry_types.h"

class RpmSimulator
{
public:
    RpmSimulator();

    void reset();
    void tick(uint32_t nowMs);

    void increaseRpm();
    void decreaseRpm();
    void toggleAnimation();

    bool animationEnabled() const;
    const NormalizedTelemetryFrame &frame() const;

private:
    NormalizedTelemetryFrame frame_{};
    uint32_t startMs_ = 0;
    int manualRpmOffset_ = 0;
    bool animateRpm_ = true;
};
