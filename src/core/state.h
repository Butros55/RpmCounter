#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <vector>

struct BleDeviceInfo
{
    String name;
    String address;
};

extern BLEClient *g_client;
extern BLERemoteCharacteristic *g_charWrite;
extern BLERemoteCharacteristic *g_charNotify;

extern bool g_connected;
extern String g_serialLine;
extern String g_obdLine;

extern int g_currentRpm;
extern int g_maxSeenRpm;
extern int g_vehicleSpeedKmh;
extern int g_estimatedGear;

extern unsigned long g_lastRpmRequest;

extern bool g_autoReconnect;
extern bool g_devMode;
extern unsigned long g_lastBleRetryMs;
extern bool g_forceImmediateReconnect;
extern bool g_autoReconnectPaused;

extern bool g_testActive;
extern unsigned long g_testStartMs;
extern int g_testMaxRpm;

extern bool g_brightnessPreviewActive;
extern unsigned long g_lastBrightnessChangeMs;

extern bool g_ignitionOn;
extern bool g_engineRunning;
extern unsigned long g_lastObdMs;
extern unsigned long g_lastLogoMs;
extern bool g_engineStartLogoShown;
extern bool g_ignitionLogoShown;

extern String g_currentTargetAddr;
extern String g_currentTargetName;
extern bool g_manualConnectRequested;
extern int g_manualConnectAttempts;
extern bool g_manualConnectActive;
extern bool g_manualConnectFailed;
extern unsigned long g_manualConnectStartMs;
extern unsigned long g_manualConnectFinishMs;
extern bool g_connectTaskRunning;
extern bool g_connectTaskWasManual;
extern bool g_connectTaskResult;
extern unsigned long g_connectTaskStartMs;
extern unsigned long g_connectTaskFinishedMs;
extern int g_autoReconnectAttempts;
extern String g_lastSuccessfulAddr;
extern bool g_bleConnectInProgress;
extern String g_bleConnectTargetAddr;
extern String g_bleConnectTargetName;
extern String g_bleConnectLastError;

extern bool g_bleScanRunning;
extern unsigned long g_bleScanStartMs;
extern unsigned long g_bleScanFinishedMs;
extern std::vector<BleDeviceInfo> g_bleScanResults;

extern bool g_animationActive;
extern bool g_logoPlayedThisCycle;
extern bool g_leavingPlayedThisCycle;

extern String g_lastTxInfo;
extern String g_lastObdInfo;
extern unsigned long g_lastTxLogMs;

extern unsigned long g_lastHttpMs;
extern bool g_vehicleInfoRequestRunning;
extern bool g_vehicleInfoAvailable;
extern unsigned long g_vehicleInfoLastUpdate;
extern String g_vehicleVin;
extern String g_vehicleModel;
extern String g_vehicleDiagStatus;

void initGlobalState();

#endif // STATE_H
