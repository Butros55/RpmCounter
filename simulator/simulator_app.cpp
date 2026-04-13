#include "simulator_app.h"

#include <algorithm>
#include <string>

namespace
{
    UiTelemetrySource to_ui_telemetry_source(TelemetryInputMode mode)
    {
        return mode == TelemetryInputMode::SimHub ? UiTelemetrySource::SimHubNetwork : UiTelemetrySource::Simulator;
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
    reset();
}

void SimulatorApp::configureTelemetry(const TelemetryServiceConfig &config)
{
    telemetryConfig_ = config;
    telemetryService_.configure(telemetryConfig_);
    reset();
}

void SimulatorApp::reset()
{
    state_ = UiRuntimeState{};
    state_.settings.displayBrightness = 220;
    state_.settings.tutorialSeen = false;
    state_.settings.lastMenuIndex = 0;
    state_.settings.nightMode = true;
    state_.settings.telemetryPreference = (telemetryConfig_.mode == TelemetryInputMode::SimHub) ? UiTelemetryPreference::SimHub : UiTelemetryPreference::Auto;
    state_.wifiMode = UiWifiMode::StaWithApFallback;

    shiftOverrideEnabled_ = false;
    shiftOverrideValue_ = false;
    bleMode_ = BleMode::Connected;
    wifiMode_ = WifiModePreset::Connected;

    populateStaticLists();
    applyBleMode();
    applyWifiMode();

    telemetryService_.configure(telemetryConfig_);
    telemetryService_.tick(0);
    applyTelemetryFrame();
}

void SimulatorApp::populateStaticLists()
{
    state_.wifiScanResults = {
        {"ShiftLight Lab", -38},
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

void SimulatorApp::applyBleMode()
{
    state_.bleConnected = (bleMode_ == BleMode::Connected);
    state_.bleConnecting = (bleMode_ == BleMode::Connecting);
}

void SimulatorApp::applyWifiMode()
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
        state_.ip.clear();
        state_.staLastError = "offline";
        break;
    case WifiModePreset::ApIdle:
        state_.wifiMode = UiWifiMode::ApOnly;
        state_.apActive = true;
        state_.apClients = 0;
        state_.apIp = "192.168.4.1";
        state_.staConnected = false;
        state_.staConnecting = false;
        state_.currentSsid = "ShiftLight";
        state_.staIp.clear();
        state_.ip = state_.apIp;
        break;
    case WifiModePreset::Connecting:
        state_.wifiMode = UiWifiMode::StaWithApFallback;
        state_.apActive = true;
        state_.apClients = 1;
        state_.apIp = "192.168.4.1";
        state_.staConnected = false;
        state_.staConnecting = true;
        state_.currentSsid = "Garage 5G";
        state_.staIp.clear();
        state_.ip = state_.apIp;
        break;
    case WifiModePreset::Connected:
    default:
        state_.wifiMode = UiWifiMode::StaWithApFallback;
        state_.apActive = true;
        state_.apClients = 1;
        state_.apIp = "192.168.4.1";
        state_.staConnected = true;
        state_.staConnecting = false;
        state_.currentSsid = "Garage 5G";
        state_.staIp = "192.168.178.88";
        state_.ip = state_.staIp;
        break;
    }
}

void SimulatorApp::applyTelemetryFrame()
{
    const NormalizedTelemetryFrame &frame = telemetryService_.frame();
    state_.telemetrySource = to_ui_telemetry_source(telemetryConfig_.mode);
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
                                ? (telemetryConfig_.simHubTransport == SimHubTransport::HttpApi ? "127.0.0.1:" + std::to_string(telemetryConfig_.httpPort)
                                                                                                 : "127.0.0.1:" + std::to_string(telemetryConfig_.udpPort))
                                : std::string();

    if (shiftOverrideEnabled_)
    {
        state_.shift = shiftOverrideValue_;
    }

    if (telemetryConfig_.mode == TelemetryInputMode::SimHub)
    {
        state_.bleConnected = !frame.stale;
        state_.bleConnecting = false;
    }
    else
    {
        applyBleMode();
    }
}

void SimulatorApp::tick(uint32_t nowMs)
{
    telemetryService_.tick(nowMs);
    applyTelemetryFrame();
}

void SimulatorApp::execute(SimulatorCommand command)
{
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
        applyBleMode();
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
        applyWifiMode();
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
        reset();
        break;
    }
}

void SimulatorApp::setBrightness(uint8_t value)
{
    state_.settings.displayBrightness = value;
}

void SimulatorApp::saveSettings(const UiSettings &settings)
{
    state_.settings = settings;
}

const UiRuntimeState &SimulatorApp::state() const
{
    return state_;
}

const TelemetryServiceConfig &SimulatorApp::telemetryConfig() const
{
    return telemetryConfig_;
}
