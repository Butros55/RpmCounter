#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class UiWifiMode : uint8_t
{
    ApOnly = 0,
    StaOnly = 1,
    StaWithApFallback = 2
};

enum class UiScreenId : uint8_t
{
    Home = 0,
    Brightness,
    Vehicle,
    Wifi,
    Bluetooth,
    Settings
};

enum class UiDebugAction : uint8_t
{
    PreviousCard = 0,
    NextCard,
    OpenSelectedCard,
    GoHome,
    ShowLogo
};

enum class UiTelemetrySource : uint8_t
{
    Esp32Obd = 0,
    Simulator,
    SimHubUdp
};

struct UiSettings
{
    int displayBrightness = 220;
    bool tutorialSeen = false;
    int lastMenuIndex = 0;
    bool nightMode = true;
};

struct UiWifiScanItem
{
    std::string ssid;
    int32_t rssi = 0;
};

struct UiBleScanItem
{
    std::string name;
    std::string address;
};

struct UiRuntimeState
{
    UiSettings settings{};

    UiWifiMode wifiMode = UiWifiMode::ApOnly;
    bool apActive = false;
    int apClients = 0;
    std::string apIp;

    bool staConnected = false;
    bool staConnecting = false;
    std::string staLastError;
    std::string currentSsid;
    std::string staIp;
    std::string ip;

    bool wifiScanRunning = false;
    std::vector<UiWifiScanItem> wifiScanResults;

    bool bleConnected = false;
    bool bleConnecting = false;
    std::vector<UiBleScanItem> bleScanResults;

    UiTelemetrySource telemetrySource = UiTelemetrySource::Esp32Obd;
    bool telemetryStale = false;
    bool telemetryUsingFallback = false;
    float throttle = 0.0f;
    uint32_t telemetryTimestampMs = 0;

    int gear = 0;
    int rpm = 0;
    int speedKmh = 0;
    bool shift = false;
};

struct UiDebugSnapshot
{
    UiScreenId activeScreen = UiScreenId::Home;
    int selectedCardIndex = 0;
    bool inDetail = false;
    int displayBrightness = 220;
    int gear = 0;
    int rpm = 0;
    int speedKmh = 0;
    bool shift = false;
    bool bleConnected = false;
    bool staConnected = false;
    UiTelemetrySource telemetrySource = UiTelemetrySource::Esp32Obd;
    bool telemetryStale = false;
    bool telemetryUsingFallback = false;
};

struct UiDisplayHooks
{
    void (*setBrightness)(uint8_t value, void *userData) = nullptr;
    void (*saveSettings)(const UiSettings &settings, void *userData) = nullptr;
    void *userData = nullptr;
};
