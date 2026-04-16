#pragma once

#include "core/config.h"
#include "core/state.h"

enum class SimRuntimeTransportMode : uint8_t
{
    Disabled = 0,
    Auto,
    UsbOnly,
    NetworkOnly
};

enum class SimSessionState : uint8_t
{
    Inactive = 0,
    WaitingForData,
    Live,
    Error
};

enum class SimSessionTransitionType : uint8_t
{
    None = 0,
    BecameLive,
    BecameWaiting,
    BecameError
};

struct TelemetryRenderSnapshot
{
    uint32_t version = 0;
    ActiveTelemetrySource source = ActiveTelemetrySource::None;
    int rpm = 0;
    int maxSeenRpm = 0;
    int speedKmh = 0;
    int gear = 0;
    float throttle = 0.0f;
    bool pitLimiter = false;
    unsigned long sampleTimestampMs = 0;
    bool telemetryFresh = false;
    bool fallbackActive = false;
    SimSessionState simSessionState = SimSessionState::Inactive;
};

struct TelemetrySourceTransitionEvent
{
    unsigned long timestampMs = 0;
    ActiveTelemetrySource fromSource = ActiveTelemetrySource::None;
    ActiveTelemetrySource toSource = ActiveTelemetrySource::None;
    bool usbFresh = false;
    bool simHubFresh = false;
    bool obdFresh = false;
    bool holdApplied = false;
    bool fallbackActive = false;
};

struct SimSessionTransitionEvent
{
    unsigned long timestampMs = 0;
    SimSessionState fromState = SimSessionState::Inactive;
    SimSessionState toState = SimSessionState::Inactive;
    SimSessionTransitionType transition = SimSessionTransitionType::None;
    ActiveTelemetrySource source = ActiveTelemetrySource::None;
};

constexpr size_t TELEMETRY_DEBUG_HISTORY_LEN = 6;

struct TelemetryDebugInfo
{
    TelemetryRenderSnapshot snapshot{};
    TelemetrySourceTransitionEvent lastSourceTransition{};
    SimSessionTransitionEvent lastSimSessionTransition{};
    uint32_t sourceTransitionCount = 0;
    uint32_t simSessionTransitionCount = 0;
    uint32_t simSessionSuppressedCount = 0;
    uint8_t sourceHistoryCount = 0;
    uint8_t simHistoryCount = 0;
    TelemetrySourceTransitionEvent sourceHistory[TELEMETRY_DEBUG_HISTORY_LEN]{};
    SimSessionTransitionEvent simHistory[TELEMETRY_DEBUG_HISTORY_LEN]{};
};

void initTelemetry();
void telemetryLoop();
void telemetryOnObdSample(int rpm, int speedKmh, int gear, unsigned long sampleMs);
void telemetryOnObdDisconnected();
TelemetryPreference nextTelemetryPreference(TelemetryPreference current);
SimRuntimeTransportMode resolveSimRuntimeTransportMode(TelemetryPreference preference, SimTransportPreference transportPreference);
const char *simRuntimeTransportModeName(SimRuntimeTransportMode mode);
bool telemetryAllowsUsbSim(TelemetryPreference preference, SimTransportPreference transportPreference);
bool telemetryAllowsNetworkSim(TelemetryPreference preference, SimTransportPreference transportPreference);
bool telemetrySupportsTransportFallback(TelemetryPreference preference, SimTransportPreference transportPreference);
bool telemetrySourceIsFallback(ActiveTelemetrySource source, TelemetryPreference preference, SimTransportPreference transportPreference);
ActiveTelemetrySource selectTelemetryRuntimeSource(TelemetryPreference preference, SimTransportPreference transportPreference, bool usbFresh, bool simHubFresh, bool obdFresh);
unsigned long simSessionStateDebounceMs(SimSessionState state);
bool telemetrySimSessionCandidateReady(SimSessionState candidateState, unsigned long pendingSinceMs, unsigned long nowMs);
bool telemetryShouldAllowObd();
TelemetryRenderSnapshot telemetryCopyRenderSnapshot();
uint32_t telemetryGetRenderSnapshotVersion();
TelemetryDebugInfo telemetryGetDebugInfo();
const char *telemetrySourceName(ActiveTelemetrySource source);
const char *simSessionStateName(SimSessionState state);
const char *simSessionTransitionTypeName(SimSessionTransitionType type);
