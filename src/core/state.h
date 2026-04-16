#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include <vector>

#if !defined(RPMCOUNTER_USE_NIMBLE) && __has_include(<NimBLEDevice.h>)
#define RPMCOUNTER_USE_NIMBLE 1
#endif

#if RPMCOUNTER_USE_NIMBLE
#include <NimBLEDevice.h>
using BleClientHandle = NimBLEClient;
using BleRemoteCharacteristicHandle = NimBLERemoteCharacteristic;
#else
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
using BleClientHandle = BLEClient;
using BleRemoteCharacteristicHandle = BLERemoteCharacteristic;
#endif

struct BleDeviceInfo
{
    String name;
    String address;
};

struct UsbTelemetryDebugStats
{
    uint32_t framesReceived = 0;
    uint32_t parseErrors = 0;
    uint32_t glitchRejects = 0;
    uint32_t glitchRejectUpCount = 0;
    uint32_t glitchRejectDownCount = 0;
    uint32_t gapEvents = 0;
    uint32_t seqGapEvents = 0;
    uint32_t seqGapFrames = 0;
    uint32_t seqDuplicates = 0;
    uint32_t lineOverflows = 0;
    unsigned long lastFrameMs = 0;
    unsigned long lastGapMs = 0;
    unsigned long maxGapMs = 0;
    uint32_t lastSeq = 0;
    int lastRawRpm = 0;
    int lastAcceptedRpm = 0;
    int lastRejectedRpm = 0;
};

struct SimHubDebugStats
{
    uint32_t pollSuccessCount = 0;
    uint32_t pollErrorCount = 0;
    uint32_t suppressedWhileUsbCount = 0;
    unsigned long lastSuccessMs = 0;
    unsigned long lastErrorMs = 0;
    String lastError;
};

struct LedRenderDebugStats
{
    uint32_t renderCalls = 0;
    uint32_t frameShowCount = 0;
    uint32_t frameSkipCount = 0;
    uint32_t brightnessUpdateCount = 0;
    int lastRawRpm = 0;
    int lastFilteredRpm = 0;
    int lastStartRpm = 0;
    int lastDisplayedLeds = 0;
    int lastDesiredLevel = 0;
    int lastDisplayedLevel = 0;
    int lastLevelCount = 0;
    uint8_t lastAppliedBrightness = 0;
    bool lastShiftBlink = false;
    bool pitLimiterOnly = false;
    uint8_t lastRenderMode = 0;
    uint32_t filterAdjustCount = 0;
    unsigned long lastShowMs = 0;
};

enum class ActiveTelemetrySource : uint8_t
{
    None = 0,
    Obd,
    SimHubNetwork,
    UsbSim
};

enum class SimHubConnectionState : uint8_t
{
    Disabled = 0,
    WaitingForHost,
    WaitingForNetwork,
    WaitingForData,
    Live,
    Error
};

enum class UsbBridgeConnectionState : uint8_t
{
    Disabled = 0,
    Disconnected,
    WaitingForBridge,
    WaitingForData,
    Live,
    Error
};

extern BleClientHandle *g_client;
extern BleRemoteCharacteristicHandle *g_charWrite;
extern BleRemoteCharacteristicHandle *g_charNotify;

extern bool g_connected;
extern String g_serialLine;
extern String g_obdLine;

extern int g_currentRpm;
extern int g_maxSeenRpm;
extern int g_vehicleSpeedKmh;
extern int g_estimatedGear;
extern float g_currentThrottle;
extern bool g_pitLimiterActive;
extern bool g_shiftBlinkActive;
extern ActiveTelemetrySource g_activeTelemetrySource;

extern int g_obdCurrentRpm;
extern int g_obdMaxSeenRpm;
extern int g_obdVehicleSpeedKmh;
extern int g_obdEstimatedGear;
extern unsigned long g_lastObdTelemetryMs;

extern int g_simHubCurrentRpm;
extern int g_simHubMaxSeenRpm;
extern int g_simHubVehicleSpeedKmh;
extern int g_simHubGear;
extern float g_simHubThrottle;
extern bool g_simHubPitLimiterActive;
extern unsigned long g_lastSimHubTelemetryMs;
extern unsigned long g_lastSimHubNetworkTelemetryMs;
extern bool g_simHubEverReceived;
extern bool g_simHubReachable;
extern bool g_simHubWaitingForData;
extern SimHubConnectionState g_simHubConnectionState;

extern int g_usbSimCurrentRpm;
extern int g_usbSimMaxSeenRpm;
extern int g_usbSimVehicleSpeedKmh;
extern int g_usbSimGear;
extern float g_usbSimThrottle;
extern bool g_usbSimPitLimiterActive;

extern bool g_usbSerialConnected;
extern bool g_usbBridgeConnected;
extern bool g_usbBridgeWebActive;
extern bool g_usbTelemetryEverReceived;
extern unsigned long g_lastUsbBridgeHeartbeatMs;
extern unsigned long g_lastUsbTelemetryMs;
extern unsigned long g_lastUsbRpcMs;
extern UsbBridgeConnectionState g_usbBridgeConnectionState;
extern String g_usbBridgeHost;
extern String g_usbBridgeLastError;

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

extern UsbTelemetryDebugStats g_usbTelemetryDebug;
extern SimHubDebugStats g_simHubDebug;
extern LedRenderDebugStats g_ledRenderDebug;

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

// Helper functions for retry/backoff policies (test-friendly).
unsigned long computeAutoReconnectInterval(int attemptCount);
bool isHttpGraceElapsed(unsigned long nowMs, unsigned long lastHttpMs, unsigned long graceMs, bool forceImmediateReconnect);
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
                            bool forceImmediateReconnect);

void initGlobalState();

#endif // STATE_H
