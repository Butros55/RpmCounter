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

unsigned long g_lastRpmRequest = 0;

bool g_autoReconnect = true;
bool g_devMode = true;

bool g_testActive = false;
unsigned long g_testStartMs = 0;
int g_testMaxRpm = 4000;

bool g_brightnessPreviewActive = false;
unsigned long g_lastBrightnessChangeMs = 0;
bool g_brightnessPreviewFading = false;
unsigned long g_brightnessPreviewFadeStartMs = 0;

bool g_ignitionOn = false;
bool g_engineRunning = false;
unsigned long g_lastObdMs = 0;
unsigned long g_lastLogoMs = 0;

bool g_animationActive = false;
bool g_logoPlayedThisCycle = false;
bool g_leavingPlayedThisCycle = false;

String g_lastTxInfo;
String g_lastObdInfo;
unsigned long g_lastTxLogMs = 0;

unsigned long g_lastHttpMs = 0;

bool g_connectLoopActive = false;
bool g_manualConnectLoop = false;
int g_connectLoopRemaining = 0;
int g_connectLoopTotal = 0;

String g_vehicleVin;
String g_vehicleModel;
String g_vehicleBrand;
bool g_vehicleDiagOk = false;
bool g_vehicleInfoLoaded = false;

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

    g_lastRpmRequest = 0;

    g_autoReconnect = true;
    g_devMode = true;

    g_testActive = false;
    g_testStartMs = 0;
    g_testMaxRpm = 4000;

    g_brightnessPreviewActive = false;
    g_lastBrightnessChangeMs = 0;
    g_brightnessPreviewFading = false;
    g_brightnessPreviewFadeStartMs = 0;

    g_ignitionOn = false;
    g_engineRunning = false;
    g_lastObdMs = 0;
    g_lastLogoMs = 0;

    g_animationActive = false;
    g_logoPlayedThisCycle = false;
    g_leavingPlayedThisCycle = false;

    g_lastTxInfo = "–";
    g_lastObdInfo = "–";
    g_lastTxLogMs = 0;

    g_lastHttpMs = 0;

    g_connectLoopActive = false;
    g_manualConnectLoop = false;
    g_connectLoopRemaining = 0;
    g_connectLoopTotal = 0;

    g_vehicleVin = "–";
    g_vehicleModel = "–";
    g_vehicleBrand = "–";
    g_vehicleDiagOk = false;
    g_vehicleInfoLoaded = false;
}
