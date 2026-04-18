#include "simulator_app.h"

#include <algorithm>
#include <string>
#include <utility>

namespace
{
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
}

SimulatorApp::SimulatorApp()
{
    telemetryConfig_.mode = TelemetryInputMode::Simulator;
    telemetryConfig_.simHubTransport = SimHubTransport::HttpApi;
    telemetryConfig_.udpPort = 20888;
    telemetryConfig_.httpPort = 8888;
    telemetryConfig_.pollIntervalMs = 25;
    telemetryConfig_.staleTimeoutMs = 2000;
    telemetryConfig_.debugLogging = false;
    telemetryConfig_.allowSimulatorFallback = false;
    ledBarConfig_ = default_led_bar_config();
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
    state_.wifiMode = UiWifiMode::StaWithApFallback;
    state_.simTransportMode = telemetryConfig_.mode == TelemetryInputMode::SimHub ? UiSimTransportMode::NetworkOnly : UiSimTransportMode::Auto;
    state_.usbState = UiUsbState::Disabled;

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
        state_.currentSsid = "Simulator AP";
        state_.staIp.clear();
        break;
    case WifiModePreset::Connecting:
        state_.wifiMode = UiWifiMode::StaWithApFallback;
        state_.apActive = true;
        state_.apClients = 1;
        state_.apIp.clear();
        state_.staConnected = false;
        state_.staConnecting = true;
        state_.currentSsid = "Simulator LAN";
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
        state_.currentSsid = "Simulator LAN";
        state_.staIp.clear();
        break;
    }

    refreshWebStateLocked();
}

void SimulatorApp::applyTelemetryFrameLocked()
{
    const NormalizedTelemetryFrame &frame = telemetryService_.frame();
    state_.telemetrySource = to_ui_telemetry_source(telemetryConfig_.mode, frame.usingFallback);
    state_.telemetryStale = frame.stale;
    state_.telemetryUsingFallback = frame.usingFallback;
    state_.telemetryTimestampMs = frame.timestampMs;
    state_.throttle = frame.throttle;
    state_.rpm = frame.rpm;
    state_.speedKmh = frame.speedKmh;
    state_.gear = frame.gear;
    state_.shift = frame.rpm >= 5600;
    state_.simHubConfigured = telemetryConfig_.mode == TelemetryInputMode::SimHub;
    state_.simHubReachable = !frame.stale;
    state_.simHubEndpoint = telemetryConfig_.mode == TelemetryInputMode::SimHub
                                ? local_web_host(telemetryConfig_.simHubTransport == SimHubTransport::HttpApi ? telemetryConfig_.httpPort : telemetryConfig_.udpPort)
                                : std::string();
    state_.usbConnected = false;
    state_.usbBridgeConnected = false;
    state_.wifiSuppressed = false;
    state_.usbState = UiUsbState::Disabled;

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
    ledBarConfig_.mode = simulator_led_mode_from_int(static_cast<int>(ledBarConfig_.mode));
    ledBarConfig_.activeLedCount = std::clamp(ledBarConfig_.activeLedCount, 1, 60);
    ledBarConfig_.brightness = std::clamp(ledBarConfig_.brightness, 0, 255);
    ledBarConfig_.startRpm = std::clamp(ledBarConfig_.startRpm, 0, 12000);
    ledBarConfig_.maxRpm = std::clamp(ledBarConfig_.maxRpm, ledBarConfig_.startRpm + 1, 14000);
    ledBarConfig_.greenEndPct = std::clamp(ledBarConfig_.greenEndPct, 0, 100);
    ledBarConfig_.yellowEndPct = std::clamp(ledBarConfig_.yellowEndPct, ledBarConfig_.greenEndPct, 100);
    ledBarConfig_.redEndPct = std::clamp(ledBarConfig_.redEndPct, ledBarConfig_.yellowEndPct, 100);
    ledBarConfig_.blinkStartPct = std::clamp(ledBarConfig_.blinkStartPct, ledBarConfig_.redEndPct, 100);
    ledBarConfig_.blinkSpeedPct = std::clamp(ledBarConfig_.blinkSpeedPct, 0, 100);
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

SimulatorStatusSnapshot SimulatorApp::statusSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    SimulatorStatusSnapshot snapshot{};
    snapshot.runtime = state_;
    snapshot.ui = uiDebugSnapshot_;
    snapshot.telemetry = telemetryConfig_;
    snapshot.ledBar = ledBarConfig_;
    snapshot.webPort = webServerPort_;
    snapshot.webBaseUrl = "http://" + local_web_host(webServerPort_);
    return snapshot;
}
