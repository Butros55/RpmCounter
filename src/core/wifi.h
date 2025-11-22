#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <vector>

#include "config.h"

struct WifiScanResult
{
    String ssid;
    int32_t rssi;
};

struct WifiStatus
{
    WifiMode mode;
    bool apActive;
    int apClients;
    bool staConnected;
    bool staConnecting;
    String staLastError;
    String currentSsid;
    String ip;
    bool scanRunning;
    std::vector<WifiScanResult> scanResults;
};

bool startApMode(const AppConfig &config);
bool startStaMode(const AppConfig &config, uint32_t timeoutMs = 15000);
void setupWifiFromConfig(const AppConfig &config);
bool startWifiScan();
void wifiLoop();
WifiStatus getWifiStatus();
bool requestWifiConnect(const String &ssid, const String &password, WifiMode mode);
void requestWifiDisconnect();

#endif // WIFI_MANAGER_H
