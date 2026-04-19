#include "state.h"
#include "config.h"

BleClientHandle *g_client = nullptr;
BleRemoteCharacteristicHandle *g_charWrite = nullptr;
BleRemoteCharacteristicHandle *g_charNotify = nullptr;

bool g_connected = false;
String g_serialLine;
String g_obdLine;

int g_currentRpm = 0;
int g_maxSeenRpm = 0;
int g_vehicleSpeedKmh = 0;
int g_estimatedGear = 0;
float g_currentThrottle = 0.0f;
bool g_pitLimiterActive = false;
bool g_shiftBlinkActive = false;
ActiveTelemetrySource g_activeTelemetrySource = ActiveTelemetrySource::None;

int g_obdCurrentRpm = 0;
int g_obdMaxSeenRpm = 0;
int g_obdVehicleSpeedKmh = 0;
int g_obdEstimatedGear = 0;
unsigned long g_lastObdTelemetryMs = 0;

int g_simHubCurrentRpm = 0;
int g_simHubMaxSeenRpm = 0;
int g_simHubVehicleSpeedKmh = 0;
int g_simHubGear = 0;
float g_simHubThrottle = 0.0f;
bool g_simHubPitLimiterActive = false;
UiSessionData g_simHubSessionData{};
SideLedTelemetry g_simHubSideTelemetry{};
unsigned long g_lastSimHubTelemetryMs = 0;
unsigned long g_lastSimHubNetworkTelemetryMs = 0;
bool g_simHubEverReceived = false;
bool g_simHubReachable = false;
bool g_simHubWaitingForData = false;
SimHubConnectionState g_simHubConnectionState = SimHubConnectionState::Disabled;

int g_usbSimCurrentRpm = 0;
int g_usbSimMaxSeenRpm = 0;
int g_usbSimVehicleSpeedKmh = 0;
int g_usbSimGear = 0;
float g_usbSimThrottle = 0.0f;
bool g_usbSimPitLimiterActive = false;
SideLedTelemetry g_usbSimSideTelemetry{};

SideLedTestState g_sideLedTestState{};

bool g_usbSerialConnected = false;
bool g_usbBridgeConnected = false;
bool g_usbBridgeWebActive = false;
bool g_usbTelemetryEverReceived = false;
unsigned long g_lastUsbBridgeHeartbeatMs = 0;
unsigned long g_lastUsbTelemetryMs = 0;
unsigned long g_lastUsbRpcMs = 0;
UsbBridgeConnectionState g_usbBridgeConnectionState = UsbBridgeConnectionState::Disabled;
String g_usbBridgeHost;
String g_usbBridgeLastError;

unsigned long g_lastRpmRequest = 0;

bool g_autoReconnect = true;
bool g_devMode = true;
unsigned long g_lastBleRetryMs = 0;
bool g_forceImmediateReconnect = false;
bool g_autoReconnectPaused = false;

bool g_testActive = false;
unsigned long g_testStartMs = 0;
int g_testMaxRpm = 4000;

bool g_brightnessPreviewActive = false;
unsigned long g_lastBrightnessChangeMs = 0;

bool g_ignitionOn = false;
bool g_engineRunning = false;
unsigned long g_lastObdMs = 0;
unsigned long g_lastLogoMs = 0;
bool g_engineStartLogoShown = false;
bool g_ignitionLogoShown = false;

bool g_animationActive = false;
bool g_logoPlayedThisCycle = false;
bool g_leavingPlayedThisCycle = false;

UsbTelemetryDebugStats g_usbTelemetryDebug{};
SimHubDebugStats g_simHubDebug{};
LedRenderDebugStats g_ledRenderDebug{};

String g_lastTxInfo;
String g_lastObdInfo;
unsigned long g_lastTxLogMs = 0;

unsigned long g_lastHttpMs = 0;
bool g_vehicleInfoRequestRunning = false;
bool g_vehicleInfoAvailable = false;
unsigned long g_vehicleInfoLastUpdate = 0;
String g_vehicleVin;
String g_vehicleModel;
String g_vehicleDiagStatus;

String g_currentTargetAddr;
String g_currentTargetName;
bool g_manualConnectRequested = false;
int g_manualConnectAttempts = 0;
bool g_manualConnectActive = false;
bool g_manualConnectFailed = false;
unsigned long g_manualConnectStartMs = 0;
unsigned long g_manualConnectFinishMs = 0;
bool g_connectTaskRunning = false;
bool g_connectTaskWasManual = false;
bool g_connectTaskResult = false;
unsigned long g_connectTaskStartMs = 0;
unsigned long g_connectTaskFinishedMs = 0;
int g_autoReconnectAttempts = 0;
String g_lastSuccessfulAddr;
bool g_bleConnectInProgress = false;
String g_bleConnectTargetAddr;
String g_bleConnectTargetName;
String g_bleConnectLastError;

bool g_bleScanRunning = false;
unsigned long g_bleScanStartMs = 0;
unsigned long g_bleScanFinishedMs = 0;
std::vector<BleDeviceInfo> g_bleScanResults;

void initGlobalState()
{
    Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println("=== ESP32 BLE-OBD ShiftLight + WebUI ===");

    g_client = nullptr;
    g_charWrite = nullptr;
    g_charNotify = nullptr;

    g_connected = false;
    g_serialLine = "";
    g_obdLine = "";

    g_currentRpm = 0;
    g_maxSeenRpm = 0;
    g_vehicleSpeedKmh = 0;
    g_estimatedGear = 0;
    g_currentThrottle = 0.0f;
    g_pitLimiterActive = false;
    g_shiftBlinkActive = false;
    g_activeTelemetrySource = ActiveTelemetrySource::None;

    g_obdCurrentRpm = 0;
    g_obdMaxSeenRpm = 0;
    g_obdVehicleSpeedKmh = 0;
    g_obdEstimatedGear = 0;
    g_lastObdTelemetryMs = 0;

    g_simHubCurrentRpm = 0;
    g_simHubMaxSeenRpm = 0;
    g_simHubVehicleSpeedKmh = 0;
    g_simHubGear = 0;
    g_simHubThrottle = 0.0f;
    g_simHubPitLimiterActive = false;
    g_simHubSessionData = UiSessionData{};
    g_simHubSideTelemetry = SideLedTelemetry{};
    g_lastSimHubTelemetryMs = 0;
    g_lastSimHubNetworkTelemetryMs = 0;
    g_simHubEverReceived = false;
    g_simHubReachable = false;
    g_simHubWaitingForData = false;
    g_simHubConnectionState = SimHubConnectionState::Disabled;

    g_usbSimCurrentRpm = 0;
    g_usbSimMaxSeenRpm = 0;
    g_usbSimVehicleSpeedKmh = 0;
    g_usbSimGear = 0;
    g_usbSimThrottle = 0.0f;
    g_usbSimPitLimiterActive = false;
    g_usbSimSideTelemetry = SideLedTelemetry{};

    g_sideLedTestState = SideLedTestState{};

    g_usbSerialConnected = false;
    g_usbBridgeConnected = false;
    g_usbBridgeWebActive = false;
    g_usbTelemetryEverReceived = false;
    g_lastUsbBridgeHeartbeatMs = 0;
    g_lastUsbTelemetryMs = 0;
    g_lastUsbRpcMs = 0;
    g_usbBridgeConnectionState = UsbBridgeConnectionState::Disabled;
    g_usbBridgeHost = "";
    g_usbBridgeLastError = "";

    g_lastRpmRequest = 0;

    g_autoReconnect = true;
    g_devMode = true;
    g_lastBleRetryMs = 0;
    g_forceImmediateReconnect = false;
    g_autoReconnectPaused = false;

    g_testActive = false;
    g_testStartMs = 0;
    g_testMaxRpm = 4000;

    g_brightnessPreviewActive = false;
    g_lastBrightnessChangeMs = 0;

    g_ignitionOn = false;
    g_engineRunning = false;
    g_lastObdMs = 0;
    g_lastLogoMs = 0;
    g_engineStartLogoShown = false;
    g_ignitionLogoShown = false;

    g_animationActive = false;
    g_logoPlayedThisCycle = false;
    g_leavingPlayedThisCycle = false;

    g_usbTelemetryDebug = UsbTelemetryDebugStats{};
    g_simHubDebug = SimHubDebugStats{};
    g_ledRenderDebug = LedRenderDebugStats{};

    g_lastTxInfo = "–";
    g_lastObdInfo = "–";
    g_lastTxLogMs = 0;

    g_lastHttpMs = 0;
    g_vehicleInfoRequestRunning = false;
    g_vehicleInfoAvailable = false;
    g_vehicleInfoLastUpdate = 0;
    g_vehicleVin = F("Noch nicht gelesen");
    g_vehicleModel = F("Unbekannt");
    g_vehicleDiagStatus = F("Keine Daten");

    g_currentTargetAddr = TARGET_ADDR;
    g_currentTargetName = F("OBD-II Dongle");
    g_manualConnectRequested = false;
    g_manualConnectAttempts = 0;
    g_manualConnectActive = false;
    g_manualConnectFailed = false;
    g_manualConnectStartMs = 0;
    g_manualConnectFinishMs = 0;
    g_connectTaskRunning = false;
    g_connectTaskWasManual = false;
    g_connectTaskResult = false;
    g_connectTaskStartMs = 0;
    g_connectTaskFinishedMs = 0;
    g_autoReconnectAttempts = 0;
    g_lastSuccessfulAddr = "";
    g_bleConnectInProgress = false;
    g_bleConnectTargetAddr = "";
    g_bleConnectTargetName = "";
    g_bleConnectLastError = "";

    g_bleScanRunning = false;
    g_bleScanStartMs = 0;
    g_bleScanFinishedMs = 0;
    g_bleScanResults.clear();
}

unsigned long computeAutoReconnectInterval(int attemptCount)
{
    return (attemptCount < AUTO_RECONNECT_FAST_ATTEMPTS) ? AUTO_RECONNECT_FAST_INTERVAL_MS : AUTO_RECONNECT_SLOW_INTERVAL_MS;
}

bool isHttpGraceElapsed(unsigned long nowMs, unsigned long lastHttpMs, unsigned long graceMs, bool forceImmediateReconnect)
{
    if (forceImmediateReconnect)
    {
        return true;
    }

    if (graceMs == 0)
    {
        return true;
    }

    return (nowMs - lastHttpMs) > graceMs;
}

bool shouldAutoReconnectNow(unsigned long nowMs,
                            bool autoReconnectEnabled,
                            bool autoReconnectPaused,
                            bool connected,
                            bool connectTaskRunning,
                            bool manualConnectActive,
                            unsigned long lastRetryMs,
                            int autoReconnectAttempts,
                            unsigned long graceMs,
                            unsigned long lastHttpMs,
                            bool forceImmediateReconnect)
{
    if (!autoReconnectEnabled || autoReconnectPaused || connected || connectTaskRunning || manualConnectActive)
    {
        return false;
    }

    if (!isHttpGraceElapsed(nowMs, lastHttpMs, graceMs, forceImmediateReconnect))
    {
        return false;
    }

    unsigned long interval = computeAutoReconnectInterval(autoReconnectAttempts);
    return (nowMs - lastRetryMs > interval) || forceImmediateReconnect;
}
