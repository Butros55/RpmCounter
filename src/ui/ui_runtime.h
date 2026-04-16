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
    Settings,
    Focus,
    DisplayPicker,
    SourcePicker,
    WebLink
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
    SimHubNetwork,
    UsbBridge
};

enum class UiSimTransportMode : uint8_t
{
    Disabled = 0,
    Auto,
    UsbOnly,
    NetworkOnly
};

enum class UiTelemetryPreference : uint8_t
{
    Auto = 0,
    Obd,
    SimHub
};

enum class UiSimHubState : uint8_t
{
    Disabled = 0,
    WaitingForHost,
    WaitingForNetwork,
    WaitingForData,
    Live,
    Error
};

enum class UiDisplayFocusMetric : uint8_t
{
    Rpm = 0,
    Gear,
    Speed
};

enum class UiUsbState : uint8_t
{
    Disabled = 0,
    Disconnected,
    WaitingForBridge,
    WaitingForData,
    Live,
    Error
};

struct UiSettings
{
    int displayBrightness = 220;
    bool tutorialSeen = false;
    int lastMenuIndex = 0;
    bool nightMode = true;
    UiTelemetryPreference telemetryPreference = UiTelemetryPreference::Auto;
    UiDisplayFocusMetric displayFocus = UiDisplayFocusMetric::Rpm;
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
    UiSimTransportMode simTransportMode = UiSimTransportMode::Disabled;
    bool telemetryStale = false;
    bool telemetryUsingFallback = false;
    bool simHubConfigured = false;
    UiSimHubState simHubState = UiSimHubState::Disabled;
    bool simHubReachable = false;
    float throttle = 0.0f;
    uint32_t telemetryTimestampMs = 0;
    std::string simHubEndpoint;
    UiUsbState usbState = UiUsbState::Disabled;
    bool usbConnected = false;
    bool usbBridgeConnected = false;
    bool wifiSuppressed = false;
    bool bleSuppressed = false;
    std::string usbHost;
    std::string usbError;

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
    UiTelemetryPreference telemetryPreference = UiTelemetryPreference::Auto;
    bool telemetryStale = false;
    bool telemetryUsingFallback = false;
};

struct UiDisplayHooks
{
    void (*setBrightness)(uint8_t value, void *userData) = nullptr;
    void (*saveSettings)(const UiSettings &settings, void *userData) = nullptr;
    void *userData = nullptr;
};
