#pragma once

#include <cstdint>

#include "telemetry/telemetry_service.h"
#include "ui/ui_runtime.h"

enum class SimulatorCommand : uint8_t
{
    ToggleBleState = 0,
    CycleWifiState,
    IncreaseRpm,
    DecreaseRpm,
    ToggleShift,
    ToggleAnimation,
    ResetState
};

class SimulatorApp
{
public:
    SimulatorApp();

    void configureTelemetry(const TelemetryServiceConfig &config);
    void reset();
    void tick(uint32_t nowMs);
    void execute(SimulatorCommand command);

    void setBrightness(uint8_t value);
    void saveSettings(const UiSettings &settings);

    const UiRuntimeState &state() const;
    const TelemetryServiceConfig &telemetryConfig() const;

private:
    enum class BleMode : uint8_t
    {
        Disconnected = 0,
        Connecting,
        Connected
    };

    enum class WifiModePreset : uint8_t
    {
        Offline = 0,
        ApIdle,
        Connecting,
        Connected
    };

    void applyBleMode();
    void applyWifiMode();
    void applyTelemetryFrame();
    void populateStaticLists();

    UiRuntimeState state_{};
    TelemetryService telemetryService_{};
    TelemetryServiceConfig telemetryConfig_{};
    bool shiftOverrideEnabled_ = false;
    bool shiftOverrideValue_ = false;
    BleMode bleMode_ = BleMode::Connected;
    WifiModePreset wifiMode_ = WifiModePreset::Connected;
};
