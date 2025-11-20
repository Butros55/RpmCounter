#include "state.h"
#include "config.h"

BLEClient *g_client = nullptr;
BLERemoteCharacteristic *g_charWrite = nullptr;
BLERemoteCharacteristic *g_charNotify = nullptr;

bool g_connected = false;
String g_serialLine;
String g_obdLine;

int g_currentRpm = 0;
int g_maxSeenRpm = 0;
int g_vehicleSpeedKmh = 0;
int g_estimatedGear = 0;

unsigned long g_lastRpmRequest = 0;

bool g_autoReconnect = true;
bool g_devMode = true;
unsigned long g_lastBleRetryMs = 0;
bool g_forceImmediateReconnect = false;

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

    g_lastRpmRequest = 0;

    g_autoReconnect = true;
    g_devMode = true;
    g_lastBleRetryMs = 0;
    g_forceImmediateReconnect = false;

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

    g_bleScanRunning = false;
    g_bleScanStartMs = 0;
    g_bleScanFinishedMs = 0;
    g_bleScanResults.clear();
}
