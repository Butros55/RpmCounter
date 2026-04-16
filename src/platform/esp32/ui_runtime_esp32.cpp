#include "ui_runtime_esp32.h"

#include <Arduino.h>
#include <string>

#include "bluetooth/ble_obd.h"
#include "core/config.h"
#include "core/state.h"
#include "core/wifi.h"
#include "telemetry/telemetry_manager.h"
#include "telemetry/usb_sim_bridge.h"

namespace
{
    std::string toStdString(const String &value)
    {
        return std::string(value.c_str());
    }

    UiWifiMode toUiWifiMode(WifiMode mode)
    {
        switch (mode)
        {
        case STA_ONLY:
            return UiWifiMode::StaOnly;
        case STA_WITH_AP_FALLBACK:
            return UiWifiMode::StaWithApFallback;
        case AP_ONLY:
        default:
            return UiWifiMode::ApOnly;
        }
    }

    UiTelemetryPreference toUiTelemetryPreference(TelemetryPreference preference)
    {
        switch (preference)
        {
        case TelemetryPreference::Obd:
            return UiTelemetryPreference::Obd;
        case TelemetryPreference::SimHub:
            return UiTelemetryPreference::SimHub;
        case TelemetryPreference::Auto:
        default:
            return UiTelemetryPreference::Auto;
        }
    }

    UiSimTransportMode toUiSimTransportMode(SimRuntimeTransportMode mode)
    {
        switch (mode)
        {
        case SimRuntimeTransportMode::UsbOnly:
            return UiSimTransportMode::UsbOnly;
        case SimRuntimeTransportMode::NetworkOnly:
            return UiSimTransportMode::NetworkOnly;
        case SimRuntimeTransportMode::Auto:
            return UiSimTransportMode::Auto;
        case SimRuntimeTransportMode::Disabled:
        default:
            return UiSimTransportMode::Disabled;
        }
    }

    UiSimHubState toUiSimHubState(SimHubConnectionState state)
    {
        switch (state)
        {
        case SimHubConnectionState::WaitingForHost:
            return UiSimHubState::WaitingForHost;
        case SimHubConnectionState::WaitingForNetwork:
            return UiSimHubState::WaitingForNetwork;
        case SimHubConnectionState::WaitingForData:
            return UiSimHubState::WaitingForData;
        case SimHubConnectionState::Live:
            return UiSimHubState::Live;
        case SimHubConnectionState::Error:
            return UiSimHubState::Error;
        case SimHubConnectionState::Disabled:
        default:
            return UiSimHubState::Disabled;
        }
    }

    UiDisplayFocusMetric toUiDisplayFocusMetric(DisplayFocusMetric metric)
    {
        switch (metric)
        {
        case DisplayFocusMetric::Gear:
            return UiDisplayFocusMetric::Gear;
        case DisplayFocusMetric::Speed:
            return UiDisplayFocusMetric::Speed;
        case DisplayFocusMetric::Rpm:
        default:
            return UiDisplayFocusMetric::Rpm;
        }
    }

    UiUsbState toUiUsbState(UsbBridgeConnectionState state)
    {
        switch (state)
        {
        case UsbBridgeConnectionState::Disconnected:
            return UiUsbState::Disconnected;
        case UsbBridgeConnectionState::WaitingForBridge:
            return UiUsbState::WaitingForBridge;
        case UsbBridgeConnectionState::WaitingForData:
            return UiUsbState::WaitingForData;
        case UsbBridgeConnectionState::Live:
            return UiUsbState::Live;
        case UsbBridgeConnectionState::Error:
            return UiUsbState::Error;
        case UsbBridgeConnectionState::Disabled:
        default:
            return UiUsbState::Disabled;
        }
    }
}

UiRuntimeState makeEsp32UiState()
{
    UiRuntimeState state{};
    state.settings.displayBrightness = cfg.displayBrightness;
    state.settings.tutorialSeen = cfg.uiTutorialSeen;
    state.settings.lastMenuIndex = cfg.uiLastMenuIndex;
    state.settings.nightMode = cfg.uiNightMode;
    state.settings.telemetryPreference = toUiTelemetryPreference(cfg.telemetryPreference);
    state.settings.displayFocus = toUiDisplayFocusMetric(cfg.uiDisplayFocus);

    const WifiStatus wifiStatus = getWifiStatus();
    state.wifiMode = toUiWifiMode(wifiStatus.mode);
    state.apActive = wifiStatus.apActive;
    state.apClients = wifiStatus.apClients;
    state.apIp = toStdString(wifiStatus.apIp);
    state.staConnected = wifiStatus.staConnected;
    state.staConnecting = wifiStatus.staConnecting;
    state.staLastError = toStdString(wifiStatus.staLastError);
    state.currentSsid = toStdString(wifiStatus.currentSsid);
    state.staIp = toStdString(wifiStatus.staIp);
    state.ip = toStdString(wifiStatus.ip);
    state.wifiScanRunning = wifiStatus.scanRunning;
    state.wifiScanResults.reserve(wifiStatus.scanResults.size());
    for (const WifiScanResult &item : wifiStatus.scanResults)
    {
        state.wifiScanResults.push_back({toStdString(item.ssid), item.rssi});
    }

    state.bleConnected = g_connected;
    state.bleConnecting = g_bleConnectInProgress;
    state.bleSuppressed = usbSimShouldBlockObd();
    state.simTransportMode = toUiSimTransportMode(resolveSimRuntimeTransportMode(cfg.telemetryPreference, cfg.simTransportPreference));
    state.simHubState = toUiSimHubState(g_simHubConnectionState);
    const std::vector<BleDeviceInfo> &bleScanResults = getBleScanResults();
    state.bleScanResults.reserve(bleScanResults.size());
    for (const BleDeviceInfo &item : bleScanResults)
    {
        state.bleScanResults.push_back({toStdString(item.name), toStdString(item.address)});
    }

    state.gear = g_estimatedGear;
    state.rpm = g_currentRpm;
    state.speedKmh = g_vehicleSpeedKmh;
    state.shift = g_shiftBlinkActive;
    if (g_activeTelemetrySource == ActiveTelemetrySource::UsbSim)
    {
        state.telemetrySource = UiTelemetrySource::UsbBridge;
    }
    else if (g_activeTelemetrySource == ActiveTelemetrySource::SimHubNetwork)
    {
        state.telemetrySource = UiTelemetrySource::SimHubNetwork;
    }
    else if (state.simTransportMode == UiSimTransportMode::UsbOnly)
    {
        state.telemetrySource = UiTelemetrySource::UsbBridge;
    }
    else if (state.simTransportMode == UiSimTransportMode::NetworkOnly ||
             (cfg.telemetryPreference != TelemetryPreference::Obd && cfg.simHubHost.length() > 0))
    {
        state.telemetrySource = UiTelemetrySource::SimHubNetwork;
    }
    else
    {
        state.telemetrySource = UiTelemetrySource::Esp32Obd;
    }
    state.telemetryStale = (g_activeTelemetrySource == ActiveTelemetrySource::None);
    state.telemetryUsingFallback = telemetrySourceIsFallback(g_activeTelemetrySource, cfg.telemetryPreference, cfg.simTransportPreference);
    state.simHubConfigured = cfg.simHubHost.length() > 0;
    state.simHubReachable = g_simHubReachable;
    state.throttle = g_currentThrottle;
    if (g_activeTelemetrySource == ActiveTelemetrySource::UsbSim)
    {
        state.telemetryTimestampMs = g_lastUsbTelemetryMs;
    }
    else if (g_activeTelemetrySource == ActiveTelemetrySource::SimHubNetwork)
    {
        state.telemetryTimestampMs = g_lastSimHubNetworkTelemetryMs;
    }
    else
    {
        state.telemetryTimestampMs = g_lastObdTelemetryMs;
    }
    if (cfg.simHubHost.length() > 0)
    {
        state.simHubEndpoint = toStdString(cfg.simHubHost) + ":" + std::to_string(cfg.simHubPort);
    }
    state.usbState = toUiUsbState(g_usbBridgeConnectionState);
    state.usbConnected = g_usbSerialConnected;
    state.usbBridgeConnected = g_usbBridgeConnected;
    state.wifiSuppressed = isWifiSuspendedForUsb();
    state.usbHost = toStdString(g_usbBridgeHost);
    state.usbError = toStdString(g_usbBridgeLastError);
    return state;
}

void saveEsp32UiSettings(const UiSettings &settings)
{
    cfg.displayBrightness = settings.displayBrightness;
    cfg.uiTutorialSeen = settings.tutorialSeen;
    cfg.uiLastMenuIndex = settings.lastMenuIndex;
    cfg.uiNightMode = settings.nightMode;
    switch (settings.displayFocus)
    {
    case UiDisplayFocusMetric::Gear:
        cfg.uiDisplayFocus = DisplayFocusMetric::Gear;
        break;
    case UiDisplayFocusMetric::Speed:
        cfg.uiDisplayFocus = DisplayFocusMetric::Speed;
        break;
    case UiDisplayFocusMetric::Rpm:
    default:
        cfg.uiDisplayFocus = DisplayFocusMetric::Rpm;
        break;
    }
    switch (settings.telemetryPreference)
    {
    case UiTelemetryPreference::Obd:
        cfg.telemetryPreference = TelemetryPreference::Obd;
        break;
    case UiTelemetryPreference::SimHub:
        cfg.telemetryPreference = TelemetryPreference::SimHub;
        break;
    case UiTelemetryPreference::Auto:
    default:
        cfg.telemetryPreference = TelemetryPreference::Auto;
        break;
    }
    saveConfig();
}
