#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>

extern BLEClient *g_client;
extern BLERemoteCharacteristic *g_charWrite;
extern BLERemoteCharacteristic *g_charNotify;

extern bool g_connected;
extern String g_serialLine;
extern String g_obdLine;

extern int g_currentRpm;
extern int g_maxSeenRpm;

extern unsigned long g_lastRpmRequest;

extern bool g_autoReconnect;
extern bool g_devMode;

extern bool g_testActive;
extern unsigned long g_testStartMs;
extern int g_testMaxRpm;

extern bool g_brightnessPreviewActive;
extern unsigned long g_lastBrightnessChangeMs;
extern bool g_brightnessPreviewFading;
extern unsigned long g_brightnessPreviewFadeStartMs;

extern bool g_ignitionOn;
extern bool g_engineRunning;
extern unsigned long g_lastObdMs;
extern unsigned long g_lastLogoMs;

extern bool g_animationActive;
extern bool g_logoPlayedThisCycle;
extern bool g_leavingPlayedThisCycle;

extern String g_lastTxInfo;
extern String g_lastObdInfo;
extern unsigned long g_lastTxLogMs;

extern unsigned long g_lastHttpMs;

extern bool g_connectLoopActive;
extern bool g_manualConnectLoop;
extern int g_connectLoopRemaining;
extern int g_connectLoopTotal;

extern String g_vehicleVin;
extern String g_vehicleModel;
extern String g_vehicleBrand;
extern bool g_vehicleDiagOk;
extern bool g_vehicleInfoLoaded;

void initGlobalState();

#endif // STATE_H
