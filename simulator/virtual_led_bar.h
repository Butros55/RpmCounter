#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "simulator_types.h"

struct VirtualLedBarFrame
{
    std::vector<uint32_t> leds;
    int litCount = 0;
    bool blinkActive = false;
    float rpmRatio = 0.0f;
};

VirtualLedBarFrame build_virtual_led_bar_frame(const UiRuntimeState &state,
                                               const SimulatorLedBarConfig &config,
                                               uint32_t nowMs);

std::string virtual_led_color_hex(uint32_t rgb);
