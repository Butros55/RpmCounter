#include "simulator_app.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

#include "virtual_led_bar.h"

namespace
{
    constexpr int kMinLedMaxRpm = 1000;
    constexpr int kMaxLedMaxRpm = 14000;
    constexpr int kMaxLedStartRpm = 12000;

    UiTelemetrySource to_ui_telemetry_source(TelemetryInputMode mode, bool usingFallback)
    {
        if (usingFallback)
        {
            return UiTelemetrySource::Simulator;
        }
        return mode == TelemetryInputMode::SimHub ? UiTelemetrySource::SimHubNetwork : UiTelemetrySource::Simulator;
    }

    std::string local_web_host(uint16_t port)
    {
        return "127.0.0.1:" + std::to_string(port);
    }

    UiTelemetryPreference default_telemetry_preference(TelemetryInputMode mode)
    {
        return mode == TelemetryInputMode::SimHub ? UiTelemetryPreference::SimHub : UiTelemetryPreference::Auto;
    }

    SimulatorLedBarConfig default_led_bar_config()
    {
        return SimulatorLedBarConfig{};
    }

    SimulatorDeviceConfig default_device_config()
    {
        return SimulatorDeviceConfig{};
    }

    bool is_supported_drive_gear(int gear)
    {
        return gear >= 1 && gear <= static_cast<int>(kSimulatorGearCount);
    }

    size_t gear_index_for(int gear)
    {
        return static_cast<size_t>(std::clamp(gear, 1, static_cast<int>(kSimulatorGearCount)) - 1);
    }

    int clamp_led_max_rpm(int value, int startRpm)
    {
        return std::clamp(value, startRpm + 1, kMaxLedMaxRpm);
    }
}

SimulatorApp::SimulatorApp()
{
    telemetryConfig_.mode = TelemetryInputMode::SimHub;
    telemetryConfig_.simHubTransport = SimHubTransport::HttpApi;
    telemetryConfig_.udpPort = 20888;
    telemetryConfig_.httpPort = 8888;
    telemetryConfig_.pollIntervalMs = 25;
    telemetryConfig_.staleTimeoutMs = 2000;
    telemetryConfig_.debugLogging = false;
    telemetryConfig_.allowSimulatorFallback = false;
    ledBarConfig_ = default_led_bar_config();
    deviceConfig_ = default_device_config();
    state_.settings = UiSettings{};
    loadPersistedState();
    reset();
}

void SimulatorApp::configureTelemetry(const TelemetryServiceConfig &config)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        telemetryConfig_ = config;
        telemetryService_.configure(telemetryConfig_);
    }
    reset();
    std::lock_guard<std::mutex> lock(mutex_);
    persistStateLocked();
}

void SimulatorApp::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);

    UiSettings preservedSettings = state_.settings;
    if (preservedSettings.displayBrightness <= 0)
    {
        preservedSettings.displayBrightness = 220;
        preservedSettings.tutorialSeen = false;
        preservedSettings.lastMenuIndex = 0;
        preservedSettings.nightMode = true;
        preservedSettings.telemetryPreference = default_telemetry_preference(telemetryConfig_.mode);
        preservedSettings.displayFocus = UiDisplayFocusMetric::Rpm;
    }

    state_ = UiRuntimeState{};
    state_.settings = preservedSettings;
    state_.settings.telemetryPreference = default_telemetry_preference(telemetryConfig_.mode);
    state_.settings.nightMode = preservedSettings.nightMode;
    state_.settings.displayFocus = preservedSettings.displayFocus;
    state_.wifiMode = UiWifiMode::StaWithApFallback;
    state_.simTransportMode = telemetryConfig_.mode == TelemetryInputMode::SimHub ? UiSimTransportMode::NetworkOnly : UiSimTransportMode::Auto;
    state_.usbState = UiUsbState::Disabled;
    normalizeLedBarConfigLocked();
    normalizeSideLedConfigLocked();
    syncLearnedMaxRpmLocked();
    syncObservedMaxRpmLocked();
    sideLedController_.reset();
    sideLedTestState_ = SideLedTestState{};

    shiftOverrideEnabled_ = false;
    shiftOverrideValue_ = false;
    bleMode_ = BleMode::Connected;
    wifiMode_ = WifiModePreset::Connected;
    pendingUiActions_.clear();

    populateStaticLists();
    applyBleModeLocked();
    applyWifiModeLocked();

    telemetryService_.configure(telemetryConfig_);
    telemetryService_.tick(0);
    applyTelemetryFrameLocked();
    updateEffectiveLedMaxRpmLocked();
}

void SimulatorApp::populateStaticLists()
{
    state_.wifiScanResults = {
        {"Simulator LAN", -32},
        {"Garage 5G", -52},
        {"DynoNet", -63},
        {"Pitlane Guest", -71},
    };

    state_.bleScanResults = {
        {"OBD-II Dongle", "66:1E:32:9D:2E:5D"},
        {"Track Logger", "AA:BB:CC:10:20:30"},
        {"Phone Bridge", "11:22:33:44:55:66"},
    };
}

void SimulatorApp::applyBleModeLocked()
{
    state_.bleConnected = (bleMode_ == BleMode::Connected);
    state_.bleConnecting = (bleMode_ == BleMode::Connecting);
    state_.bleSuppressed = telemetryConfig_.mode == TelemetryInputMode::SimHub;
}

void SimulatorApp::refreshWebStateLocked()
{
    const std::string host = local_web_host(webServerPort_);
    state_.ip = host;
    if (state_.staConnected || state_.staConnecting)
    {
        state_.staIp = host;
    }
    if (state_.apActive)
    {
        state_.apIp = host;
    }
    state_.usbHost = "127.0.0.1";
}

void SimulatorApp::applyWifiModeLocked()
{
    state_.staLastError.clear();
    const std::string staSsid = deviceConfig_.staSsid.empty() ? "Simulator LAN" : deviceConfig_.staSsid;
    const std::string apSsid = deviceConfig_.apSsid.empty() ? "ShiftLight" : deviceConfig_.apSsid;

    switch (wifiMode_)
    {
    case WifiModePreset::Offline:
        state_.wifiMode = UiWifiMode::StaWithApFallback;
        state_.apActive = false;
        state_.apClients = 0;
        state_.apIp.clear();
        state_.staConnected = false;
        state_.staConnecting = false;
        state_.currentSsid.clear();
        state_.staIp.clear();
        state_.staLastError = "offline";
        break;
    case WifiModePreset::ApIdle:
        state_.wifiMode = UiWifiMode::ApOnly;
        state_.apActive = true;
        state_.apClients = 0;
        state_.apIp.clear();
        state_.staConnected = false;
        state_.staConnecting = false;
        state_.currentSsid = apSsid;
        state_.staIp.clear();
        break;
    case WifiModePreset::Connecting:
        state_.wifiMode = UiWifiMode::StaWithApFallback;
        state_.apActive = true;
        state_.apClients = 1;
        state_.apIp.clear();
        state_.staConnected = false;
        state_.staConnecting = true;
        state_.currentSsid = staSsid;
        state_.staIp.clear();
        break;
    case WifiModePreset::Connected:
    default:
        state_.wifiMode = UiWifiMode::StaWithApFallback;
        state_.apActive = true;
        state_.apClients = 1;
        state_.apIp.clear();
        state_.staConnected = true;
        state_.staConnecting = false;
        state_.currentSsid = staSsid;
        state_.staIp.clear();
        break;
    }

    refreshWebStateLocked();
}

void SimulatorApp::applyTelemetryFrameLocked()
{
    const NormalizedTelemetryFrame &frame = telemetryService_.frame();
    const int previousEffectiveMaxRpm = ledBarConfig_.effectiveMaxRpm;
    const SimulatorGearRpmArray previousEffectiveMaxRpmByGear = ledBarConfig_.effectiveMaxRpmByGear;
    const SimulatorGearLearnedArray previousLearnedMaxRpmByGear = ledBarConfig_.learnedMaxRpmByGear;
    state_.telemetrySource = to_ui_telemetry_source(telemetryConfig_.mode, frame.usingFallback);
    state_.telemetryStale = frame.stale;
    state_.telemetryUsingFallback = frame.usingFallback;
    state_.telemetryTimestampMs = frame.timestampMs;
    state_.throttle = frame.throttle;
    state_.rpm = frame.rpm;
    state_.speedKmh = frame.speedKmh;
    state_.gear = frame.gear;
    state_.session = frame.session;
    state_.sideLedConfig = sideLedConfig_;
    state_.sideTelemetry = frame.sideLeds;
    state_.simHubConfigured = telemetryConfig_.mode == TelemetryInputMode::SimHub;
    state_.simHubReachable = !frame.stale;
    state_.simHubEndpoint = telemetryConfig_.mode == TelemetryInputMode::SimHub
                                ? local_web_host(telemetryConfig_.simHubTransport == SimHubTransport::HttpApi ? telemetryConfig_.httpPort : telemetryConfig_.udpPort)
                                : std::string();
    state_.usbConnected = false;
    state_.usbBridgeConnected = false;
    state_.wifiSuppressed = false;
    state_.usbState = UiUsbState::Disabled;

    updateEffectiveLedMaxRpmLocked();
    state_.ledStartRpm = ledBarConfig_.startRpm;
    state_.ledMaxRpm = ledBarConfig_.effectiveMaxRpm;
    const float ledRatio =
        std::clamp((static_cast<float>(state_.rpm) - static_cast<float>(state_.ledStartRpm)) /
                       static_cast<float>(std::max(1, state_.ledMaxRpm - state_.ledStartRpm)),
                   0.0f,
                   1.0f);
    const VirtualLedBarFrame ledFrame = build_virtual_led_bar_frame(state_, ledBarConfig_, state_.telemetryTimestampMs);
    state_.sideLedFrame = sideLedController_.update(frame.sideLeds, sideLedConfig_, state_.telemetryTimestampMs, &sideLedTestState_);
    state_.sideLedPriority = sideLedController_.lastPriorityResult();
    state_.shiftWindowActive = ledRatio >= (static_cast<float>(ledBarConfig_.blinkStartPct) / 100.0f);
    state_.shift = ledFrame.blinkActive;
    if (shiftOverrideEnabled_)
    {
        state_.shift = shiftOverrideValue_;
    }

    if (telemetryConfig_.mode == TelemetryInputMode::SimHub)
    {
        state_.simTransportMode = UiSimTransportMode::NetworkOnly;
        state_.simHubState = frame.stale ? UiSimHubState::WaitingForData : UiSimHubState::Live;
        state_.bleConnected = !frame.stale;
        state_.bleConnecting = false;
        state_.bleSuppressed = true;
    }
    else
    {
        state_.simTransportMode = UiSimTransportMode::Auto;
        state_.simHubState = UiSimHubState::Disabled;
        applyBleModeLocked();
    }

    refreshWebStateLocked();

    const bool ledLearningChanged =
        previousEffectiveMaxRpm != ledBarConfig_.effectiveMaxRpm ||
        previousEffectiveMaxRpmByGear != ledBarConfig_.effectiveMaxRpmByGear ||
        previousLearnedMaxRpmByGear != ledBarConfig_.learnedMaxRpmByGear;
    if (ledLearningChanged && ledBarConfig_.maxRpmPerGearEnabled && ledBarConfig_.autoScaleMaxRpm)
    {
        persistStateLocked();
    }
}

void SimulatorApp::tick(uint32_t nowMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    telemetryService_.tick(nowMs);
    applyTelemetryFrameLocked();
}

void SimulatorApp::execute(SimulatorCommand command)
{
    bool needsFullReset = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        switch (command)
        {
        case SimulatorCommand::ToggleBleState:
            if (telemetryConfig_.mode == TelemetryInputMode::SimHub)
            {
                return;
            }

            switch (bleMode_)
            {
            case BleMode::Connected:
                bleMode_ = BleMode::Connecting;
                break;
            case BleMode::Connecting:
                bleMode_ = BleMode::Disconnected;
                break;
            case BleMode::Disconnected:
            default:
                bleMode_ = BleMode::Connected;
                break;
            }
            applyBleModeLocked();
            break;
        case SimulatorCommand::CycleWifiState:
            switch (wifiMode_)
            {
            case WifiModePreset::Connected:
                wifiMode_ = WifiModePreset::ApIdle;
                break;
            case WifiModePreset::ApIdle:
                wifiMode_ = WifiModePreset::Connecting;
                break;
            case WifiModePreset::Connecting:
                wifiMode_ = WifiModePreset::Offline;
                break;
            case WifiModePreset::Offline:
            default:
                wifiMode_ = WifiModePreset::Connected;
                break;
            }
            applyWifiModeLocked();
            break;
        case SimulatorCommand::IncreaseRpm:
            telemetryService_.increaseSimulatorRpm();
            break;
        case SimulatorCommand::DecreaseRpm:
            telemetryService_.decreaseSimulatorRpm();
            break;
        case SimulatorCommand::ToggleShift:
            if (!shiftOverrideEnabled_)
            {
                shiftOverrideEnabled_ = true;
                shiftOverrideValue_ = !state_.shift;
            }
            else
            {
                shiftOverrideValue_ = !shiftOverrideValue_;
            }
            break;
        case SimulatorCommand::ToggleAnimation:
            telemetryService_.toggleSimulatorAnimation();
            break;
        case SimulatorCommand::ResetState:
            needsFullReset = true;
            break;
        }

        if (!needsFullReset)
        {
            applyTelemetryFrameLocked();
        }
    }

    if (needsFullReset)
    {
        reset();
    }
}

void SimulatorApp::setBrightness(uint8_t value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_.settings.displayBrightness = value;
}

void SimulatorApp::saveSettings(const UiSettings &settings)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_.settings = settings;
    persistStateLocked();
}

void SimulatorApp::setWebServerPort(uint16_t port)
{
    std::lock_guard<std::mutex> lock(mutex_);
    webServerPort_ = port == 0 ? 8765 : port;
    refreshWebStateLocked();
}

void SimulatorApp::applyLedBarConfig(const SimulatorLedBarConfig &config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ledBarConfig_ = config;
    normalizeLedBarConfigLocked();
    syncLearnedMaxRpmLocked();
    syncObservedMaxRpmLocked();
    updateEffectiveLedMaxRpmLocked();
    persistStateLocked();
}

void SimulatorApp::applySideLedConfig(const SideLedConfig &config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sideLedConfig_ = config;
    normalizeSideLedConfigLocked();
    applyTelemetryFrameLocked();
    persistStateLocked();
}

void SimulatorApp::applyDeviceConfig(const SimulatorDeviceConfig &config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    deviceConfig_ = config;
    deviceConfig_.ambientLightSdaPin = std::clamp(deviceConfig_.ambientLightSdaPin, 0, 48);
    deviceConfig_.ambientLightSclPin = std::clamp(deviceConfig_.ambientLightSclPin, 0, 48);
    deviceConfig_.autoBrightnessStrengthPct = std::clamp(deviceConfig_.autoBrightnessStrengthPct, 25, 200);
    deviceConfig_.autoBrightnessMin = std::clamp(deviceConfig_.autoBrightnessMin, 0, 255);
    deviceConfig_.autoBrightnessResponsePct = std::clamp(deviceConfig_.autoBrightnessResponsePct, 1, 100);
    deviceConfig_.autoBrightnessLuxMin = std::clamp(deviceConfig_.autoBrightnessLuxMin, 0, 120000);
    deviceConfig_.autoBrightnessLuxMax = std::clamp(deviceConfig_.autoBrightnessLuxMax, deviceConfig_.autoBrightnessLuxMin + 1, 120000);
    if (deviceConfig_.apSsid.empty())
    {
        deviceConfig_.apSsid = "ShiftLight";
    }
    if (deviceConfig_.staSsid.empty())
    {
        deviceConfig_.staSsid = "Simulator LAN";
    }

    applyWifiModeLocked();
    persistStateLocked();
}

void SimulatorApp::connectWifi(const std::string &ssid, const std::string &password)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ssid.empty())
    {
        deviceConfig_.staSsid = ssid;
    }
    deviceConfig_.staPassword = password;
    wifiMode_ = WifiModePreset::Connected;
    applyWifiModeLocked();
    persistStateLocked();
}

void SimulatorApp::disconnectWifi()
{
    std::lock_guard<std::mutex> lock(mutex_);
    wifiMode_ = WifiModePreset::Offline;
    applyWifiModeLocked();
}

void SimulatorApp::connectBleDevice(const std::string &name, const std::string &address)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!name.empty())
    {
        bleTargetName_ = name;
    }
    if (!address.empty())
    {
        bleTargetAddress_ = address;
    }

    if (telemetryConfig_.mode == TelemetryInputMode::SimHub)
    {
        persistStateLocked();
        return;
    }

    bleMode_ = BleMode::Connected;
    applyBleModeLocked();
    applyTelemetryFrameLocked();
    persistStateLocked();
}

void SimulatorApp::disconnectBle()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (telemetryConfig_.mode == TelemetryInputMode::SimHub)
    {
        return;
    }

    bleMode_ = BleMode::Disconnected;
    applyBleModeLocked();
    applyTelemetryFrameLocked();
}

void SimulatorApp::updateUiDebugSnapshot(const UiDebugSnapshot &snapshot)
{
    std::lock_guard<std::mutex> lock(mutex_);
    uiDebugSnapshot_ = snapshot;
}

void SimulatorApp::queueUiAction(UiDebugAction action)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingUiActions_.push_back(action);
}

std::vector<UiDebugAction> SimulatorApp::takePendingUiActions()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UiDebugAction> actions;
    actions.swap(pendingUiActions_);
    return actions;
}

const UiRuntimeState &SimulatorApp::state() const
{
    return state_;
}

const TelemetryServiceConfig &SimulatorApp::telemetryConfig() const
{
    return telemetryConfig_;
}

UiRuntimeState SimulatorApp::stateSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

TelemetryServiceConfig SimulatorApp::telemetryConfigSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return telemetryConfig_;
}

SimulatorLedBarConfig SimulatorApp::ledBarConfigSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ledBarConfig_;
}

SideLedConfig SimulatorApp::sideLedConfigSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sideLedConfig_;
}

SimulatorDeviceConfig SimulatorApp::deviceConfigSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceConfig_;
}

SimulatorStatusSnapshot SimulatorApp::statusSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    SimulatorStatusSnapshot snapshot{};
    snapshot.runtime = state_;
    snapshot.ui = uiDebugSnapshot_;
    snapshot.telemetry = telemetryConfig_;
    snapshot.ledBar = ledBarConfig_;
    snapshot.sideLeds = sideLedConfig_;
    snapshot.device = deviceConfig_;
    snapshot.bleTargetName = bleTargetName_;
    snapshot.bleTargetAddress = bleTargetAddress_;
    snapshot.webPort = webServerPort_;
    snapshot.webBaseUrl = "http://" + local_web_host(webServerPort_);
    return snapshot;
}

void SimulatorApp::normalizeLedBarConfigLocked()
{
    ledBarConfig_.mode = simulator_led_mode_from_int(static_cast<int>(ledBarConfig_.mode));
    ledBarConfig_.startRpm = std::clamp(ledBarConfig_.startRpm, 0, kMaxLedStartRpm);
    ledBarConfig_.fixedMaxRpm = clamp_led_max_rpm(ledBarConfig_.fixedMaxRpm, ledBarConfig_.startRpm);
    ledBarConfig_.effectiveMaxRpm = clamp_led_max_rpm(ledBarConfig_.effectiveMaxRpm, ledBarConfig_.startRpm);
    ledBarConfig_.activeLedCount = std::clamp(ledBarConfig_.activeLedCount, 1, 60);
    ledBarConfig_.brightness = std::clamp(ledBarConfig_.brightness, 0, 255);
    ledBarConfig_.greenEndPct = std::clamp(ledBarConfig_.greenEndPct, 0, 100);
    ledBarConfig_.yellowEndPct = std::clamp(ledBarConfig_.yellowEndPct, ledBarConfig_.greenEndPct, 100);
    ledBarConfig_.redEndPct = std::clamp(ledBarConfig_.redEndPct, ledBarConfig_.yellowEndPct, 100);
    ledBarConfig_.blinkStartPct = std::clamp(ledBarConfig_.blinkStartPct, ledBarConfig_.redEndPct, 100);
    ledBarConfig_.blinkSpeedPct = std::clamp(ledBarConfig_.blinkSpeedPct, 0, 100);

    for (size_t i = 0; i < kSimulatorGearCount; ++i)
    {
        ledBarConfig_.fixedMaxRpmByGear[i] = clamp_led_max_rpm(ledBarConfig_.fixedMaxRpmByGear[i], ledBarConfig_.startRpm);
        ledBarConfig_.effectiveMaxRpmByGear[i] = clamp_led_max_rpm(ledBarConfig_.effectiveMaxRpmByGear[i], ledBarConfig_.startRpm);
    }
}

void SimulatorApp::normalizeSideLedConfigLocked()
{
    normalize_side_led_config(sideLedConfig_);
}

void SimulatorApp::syncObservedMaxRpmLocked()
{
    maxObservedRpm_ = std::max(ledBarConfig_.startRpm + 1, ledBarConfig_.effectiveMaxRpm);
    for (size_t i = 0; i < kSimulatorGearCount; ++i)
    {
        const int gearFallback = clamp_led_max_rpm(ledBarConfig_.fixedMaxRpmByGear[i], ledBarConfig_.startRpm);
        if (ledBarConfig_.learnedMaxRpmByGear[i])
        {
            maxObservedRpmByGear_[i] = std::max(ledBarConfig_.startRpm + 1, ledBarConfig_.effectiveMaxRpmByGear[i]);
        }
        else
        {
            maxObservedRpmByGear_[i] = std::max(ledBarConfig_.startRpm + 1, gearFallback);
            ledBarConfig_.effectiveMaxRpmByGear[i] = gearFallback;
        }
    }
}

void SimulatorApp::syncLearnedMaxRpmLocked()
{
    if (!ledBarConfig_.maxRpmPerGearEnabled)
    {
        ledBarConfig_.learnedMaxRpmByGear.fill(false);
    }
}

void SimulatorApp::updateEffectiveLedMaxRpmLocked()
{
    const int baselineMaxRpm = std::clamp(std::max(2000, ledBarConfig_.startRpm + 1), ledBarConfig_.startRpm + 1, kMaxLedMaxRpm);

    if (ledBarConfig_.maxRpmPerGearEnabled && is_supported_drive_gear(state_.gear))
    {
        const size_t gearIndex = gear_index_for(state_.gear);
        const int configuredGearMax = std::max(baselineMaxRpm, clamp_led_max_rpm(ledBarConfig_.fixedMaxRpmByGear[gearIndex], ledBarConfig_.startRpm));
        if (ledBarConfig_.autoScaleMaxRpm)
        {
            maxObservedRpmByGear_[gearIndex] = std::max(maxObservedRpmByGear_[gearIndex], std::max(state_.rpm, configuredGearMax));
            ledBarConfig_.effectiveMaxRpmByGear[gearIndex] = std::clamp(maxObservedRpmByGear_[gearIndex], configuredGearMax, kMaxLedMaxRpm);
            ledBarConfig_.learnedMaxRpmByGear[gearIndex] = true;
        }
        else
        {
            ledBarConfig_.effectiveMaxRpmByGear[gearIndex] = configuredGearMax;
            maxObservedRpmByGear_[gearIndex] = configuredGearMax;
        }
        ledBarConfig_.effectiveMaxRpm = ledBarConfig_.effectiveMaxRpmByGear[gearIndex];
    }
    else
    {
        if (ledBarConfig_.autoScaleMaxRpm)
        {
            maxObservedRpm_ = std::max(maxObservedRpm_, std::max(state_.rpm, baselineMaxRpm));
            ledBarConfig_.effectiveMaxRpm = std::clamp(maxObservedRpm_, baselineMaxRpm, kMaxLedMaxRpm);
        }
        else
        {
            ledBarConfig_.effectiveMaxRpm = std::clamp(ledBarConfig_.fixedMaxRpm, baselineMaxRpm, kMaxLedMaxRpm);
            maxObservedRpm_ = ledBarConfig_.effectiveMaxRpm;
        }
    }

    if (!ledBarConfig_.maxRpmPerGearEnabled)
    {
        for (size_t i = 0; i < kSimulatorGearCount; ++i)
        {
            ledBarConfig_.effectiveMaxRpmByGear[i] = ledBarConfig_.effectiveMaxRpm;
        }
    }
}

void SimulatorApp::loadPersistedState()
{
    SimulatorPersistedState persisted{};
    persisted.settings = UiSettings{};
    persisted.telemetry = telemetryConfig_;
    persisted.ledBar = ledBarConfig_;
    persisted.sideLeds = sideLedConfig_;
    persisted.device = deviceConfig_;
    persisted.bleTargetName = bleTargetName_;
    persisted.bleTargetAddress = bleTargetAddress_;

    std::string errorMessage;
    if (!load_simulator_persisted_state(persisted, &errorMessage))
    {
        if (!errorMessage.empty())
        {
            std::cerr << "[sim] Persisted settings konnten nicht geladen werden: " << errorMessage << '\n';
        }
        return;
    }

    state_.settings = persisted.settings;
    telemetryConfig_ = persisted.telemetry;
    ledBarConfig_ = persisted.ledBar;
    sideLedConfig_ = persisted.sideLeds;
    deviceConfig_ = persisted.device;
    bleTargetName_ = persisted.bleTargetName.empty() ? bleTargetName_ : persisted.bleTargetName;
    bleTargetAddress_ = persisted.bleTargetAddress.empty() ? bleTargetAddress_ : persisted.bleTargetAddress;
    normalizeLedBarConfigLocked();
    normalizeSideLedConfigLocked();
    syncLearnedMaxRpmLocked();
    syncObservedMaxRpmLocked();
}

void SimulatorApp::persistStateLocked()
{
    SimulatorPersistedState persisted{};
    persisted.settings = state_.settings;
    persisted.telemetry = telemetryConfig_;
    persisted.ledBar = ledBarConfig_;
    persisted.sideLeds = sideLedConfig_;
    persisted.device = deviceConfig_;
    persisted.bleTargetName = bleTargetName_;
    persisted.bleTargetAddress = bleTargetAddress_;

    std::string errorMessage;
    if (!save_simulator_persisted_state(persisted, &errorMessage) && !errorMessage.empty())
    {
        std::cerr << "[sim] Persisted settings konnten nicht gespeichert werden: " << errorMessage << '\n';
    }
}

void SimulatorApp::triggerSideLedTest(SideLedTestPattern pattern, uint32_t nowMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sideLedTestState_.active = pattern != SideLedTestPattern::None;
    sideLedTestState_.pattern = pattern;
    sideLedTestState_.untilMs = nowMs + 3500U;
    applyTelemetryFrameLocked();
}

void SimulatorApp::clearSideLedTest()
{
    std::lock_guard<std::mutex> lock(mutex_);
    sideLedTestState_ = SideLedTestState{};
    applyTelemetryFrameLocked();
}
