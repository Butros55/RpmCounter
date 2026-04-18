#pragma once

#include <cstdint>
#include <string>

#include "src/ui/ui_runtime.h"
#include "telemetry/telemetry_types.h"

enum class SimulatorLedMode : uint8_t
{
    Casual = 0,
    F1 = 1,
    Aggressive = 2,
    Gt3 = 3
};

inline SimulatorLedMode simulator_led_mode_from_int(int value)
{
    switch (value)
    {
    case 0:
        return SimulatorLedMode::Casual;
    case 2:
        return SimulatorLedMode::Aggressive;
    case 3:
        return SimulatorLedMode::Gt3;
    case 1:
    default:
        return SimulatorLedMode::F1;
    }
}

inline const char *simulator_led_mode_name(SimulatorLedMode mode)
{
    switch (mode)
    {
    case SimulatorLedMode::Casual:
        return "casual";
    case SimulatorLedMode::Aggressive:
        return "aggressive";
    case SimulatorLedMode::Gt3:
        return "gt3";
    case SimulatorLedMode::F1:
    default:
        return "f1";
    }
}

inline const char *simulator_led_mode_label(SimulatorLedMode mode)
{
    switch (mode)
    {
    case SimulatorLedMode::Casual:
        return "Casual";
    case SimulatorLedMode::Aggressive:
        return "Aggressiv";
    case SimulatorLedMode::Gt3:
        return "GT3 / Endurance";
    case SimulatorLedMode::F1:
    default:
        return "F1-Style";
    }
}

struct SimulatorLedBarConfig
{
    SimulatorLedMode mode = SimulatorLedMode::F1;
    int activeLedCount = 30;
    int brightness = 80;
    int startRpm = 1000;
    int maxRpm = 7000;
    int greenEndPct = 60;
    int yellowEndPct = 85;
    int redEndPct = 90;
    int blinkStartPct = 90;
    int blinkSpeedPct = 80;
};

struct SimulatorStatusSnapshot
{
    UiRuntimeState runtime{};
    UiDebugSnapshot ui{};
    TelemetryServiceConfig telemetry{};
    SimulatorLedBarConfig ledBar{};
    uint16_t webPort = 8765;
    std::string webBaseUrl = "http://127.0.0.1:8765";
};
