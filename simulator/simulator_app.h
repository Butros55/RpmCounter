#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "simulator_settings_store.h"
#include "telemetry/telemetry_service.h"
#include "simulator_types.h"
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
    void setWebServerPort(uint16_t port);
    void applyLedBarConfig(const SimulatorLedBarConfig &config);
    void applySideLedConfig(const SideLedConfig &config);
    void applyDeviceConfig(const SimulatorDeviceConfig &config);
    void connectWifi(const std::string &ssid, const std::string &password);
    void disconnectWifi();
    void connectBleDevice(const std::string &name, const std::string &address);
    void disconnectBle();
    void updateUiDebugSnapshot(const UiDebugSnapshot &snapshot);
    void queueUiAction(UiDebugAction action);
    std::vector<UiDebugAction> takePendingUiActions();

    const UiRuntimeState &state() const;
    const TelemetryServiceConfig &telemetryConfig() const;
    UiRuntimeState stateSnapshot() const;
    TelemetryServiceConfig telemetryConfigSnapshot() const;
    SimulatorLedBarConfig ledBarConfigSnapshot() const;
    SideLedConfig sideLedConfigSnapshot() const;
    SimulatorDeviceConfig deviceConfigSnapshot() const;
    SimulatorStatusSnapshot statusSnapshot() const;
    void triggerSideLedTest(SideLedTestPattern pattern, uint32_t nowMs);
    void clearSideLedTest();

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

    void applyBleModeLocked();
    void applyWifiModeLocked();
    void applyTelemetryFrameLocked();
    void refreshWebStateLocked();
    void populateStaticLists();
    void normalizeLedBarConfigLocked();
    void normalizeSideLedConfigLocked();
    void syncObservedMaxRpmLocked();
    void syncLearnedMaxRpmLocked();
    void updateEffectiveLedMaxRpmLocked();
    void loadPersistedState();
    void persistStateLocked();

    UiRuntimeState state_{};
    TelemetryService telemetryService_{};
    TelemetryServiceConfig telemetryConfig_{};    
    SimulatorLedBarConfig ledBarConfig_{};
    SideLedConfig sideLedConfig_ = side_led_config_for_preset(SideLedPreset::Gt3);
    SimulatorDeviceConfig deviceConfig_{};
    UiDebugSnapshot uiDebugSnapshot_{};
    bool shiftOverrideEnabled_ = false;
    bool shiftOverrideValue_ = false;
    BleMode bleMode_ = BleMode::Connected;
    WifiModePreset wifiMode_ = WifiModePreset::Connected;
    uint16_t webServerPort_ = 8765;
    int maxObservedRpm_ = 0;
    std::array<int, kSimulatorGearCount> maxObservedRpmByGear_{};
    SideLedController sideLedController_{};
    SideLedTestState sideLedTestState_{};
    std::string bleTargetName_ = "OBD-II Dongle";
    std::string bleTargetAddress_ = "66:1E:32:9D:2E:5D";
    std::vector<UiDebugAction> pendingUiActions_{};
    mutable std::mutex mutex_{};
};
