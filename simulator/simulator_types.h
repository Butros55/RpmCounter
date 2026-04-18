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
    bool autoScaleMaxRpm = true;
    int fixedMaxRpm = 7000;
    int effectiveMaxRpm = 7000;
    int activeLedCount = 30;
    int brightness = 80;
    int startRpm = 1000;
    int greenEndPct = 60;
    int yellowEndPct = 85;
    int redEndPct = 90;
    int blinkStartPct = 90;
    int blinkSpeedPct = 80;
    uint32_t greenColor = 0x00FF00u;
    uint32_t yellowColor = 0xFFB400u;
    uint32_t redColor = 0xFF0000u;
    std::string greenLabel = "Green";
    std::string yellowLabel = "Yellow";
    std::string redLabel = "Red";
};

struct SimulatorDeviceConfig
{
    bool autoBrightnessEnabled = false;
    int ambientLightSdaPin = 47;
    int ambientLightSclPin = 48;
    int autoBrightnessStrengthPct = 100;
    int autoBrightnessMin = 18;
    int autoBrightnessResponsePct = 35;
    int autoBrightnessLuxMin = 2;
    int autoBrightnessLuxMax = 5000;
    bool logoOnIgnitionOn = true;
    bool logoOnEngineStart = true;
    bool logoOnIgnitionOff = true;
    bool simSessionLedEffectsEnabled = false;
    bool gestureControlEnabled = true;
    bool useMph = false;
    bool autoReconnect = true;
    UiWifiMode wifiModePreference = UiWifiMode::StaWithApFallback;
    std::string staSsid = "Simulator LAN";
    std::string staPassword;
    std::string apSsid = "ShiftLight";
    std::string apPassword = "shift1234";
};

struct SimulatorStatusSnapshot
{
    UiRuntimeState runtime{};
    UiDebugSnapshot ui{};
    TelemetryServiceConfig telemetry{};
    SimulatorLedBarConfig ledBar{};
    SimulatorDeviceConfig device{};
    uint16_t webPort = 8765;
    std::string webBaseUrl = "http://127.0.0.1:8765";
};
