#include "ui_runtime_esp32.h"

#include <Arduino.h>

#include "bluetooth/ble_obd.h"
#include "core/config.h"
#include "core/state.h"
#include "core/wifi.h"

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
}

UiRuntimeState makeEsp32UiState()
{
    UiRuntimeState state{};
    state.settings.displayBrightness = cfg.displayBrightness;
    state.settings.tutorialSeen = cfg.uiTutorialSeen;
    state.settings.lastMenuIndex = cfg.uiLastMenuIndex;
    state.settings.nightMode = cfg.uiNightMode;

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
    state.telemetrySource = UiTelemetrySource::Esp32Obd;
    state.telemetryStale = false;
    state.telemetryUsingFallback = false;
    state.throttle = 0.0f;
    state.telemetryTimestampMs = millis();
    return state;
}

void saveEsp32UiSettings(const UiSettings &settings)
{
    cfg.displayBrightness = settings.displayBrightness;
    cfg.uiTutorialSeen = settings.tutorialSeen;
    cfg.uiLastMenuIndex = settings.lastMenuIndex;
    cfg.uiNightMode = settings.nightMode;
    saveConfig();
}
