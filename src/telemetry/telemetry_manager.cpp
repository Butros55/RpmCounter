#include "telemetry_manager.h"

#include <Arduino.h>

#include "core/config.h"
#include "core/logging.h"
#include "hardware/display.h"
#include "hardware/led_bar.h"
#include "signal_utils.h"
#include "telemetry/simhub_client.h"
#include "telemetry/usb_sim_bridge.h"

namespace
{
    constexpr unsigned long OBD_FRESH_TIMEOUT_MS = 1200;
    constexpr unsigned long SIMHUB_FRESH_TIMEOUT_MS = 2000;
    // When Auto-mode loses its active source, stick with it briefly so a short
    // lull does not rip the LEDs dark. Shortened from 8000 ms — at the old
    // value the stale values could linger for many seconds after the game
    // closed, which looked like "LEDs freeze on last value".
    constexpr unsigned long TELEMETRY_SOURCE_HOLD_MS = 500;
    // Once no source is active, RPM decays linearly toward zero at this rate
    // so the LED bar smoothly empties instead of cutting from a stale value
    // straight to black.
    constexpr float RPM_DECAY_PER_MS = 20.0f; // 20000 RPM / 1000 ms → full bar empties in ~1s
    constexpr unsigned long SIM_SESSION_LIVE_DEBOUNCE_MS = 100;
    constexpr unsigned long SIM_SESSION_WAITING_DEBOUNCE_MS = 1200;
    constexpr unsigned long SIM_SESSION_ERROR_DEBOUNCE_MS = 1500;
    constexpr unsigned long SIM_SESSION_INACTIVE_DEBOUNCE_MS = 1500;
    unsigned long g_lastDecayMs = 0;
    portMUX_TYPE g_telemetrySnapshotMux = portMUX_INITIALIZER_UNLOCKED;
    TelemetryRenderSnapshot g_renderSnapshot{};
    TelemetrySourceTransitionEvent g_sourceHistory[TELEMETRY_DEBUG_HISTORY_LEN]{};
    SimSessionTransitionEvent g_simHistory[TELEMETRY_DEBUG_HISTORY_LEN]{};
    uint8_t g_sourceHistoryCount = 0;
    uint8_t g_sourceHistoryHead = 0;
    uint8_t g_simHistoryCount = 0;
    uint8_t g_simHistoryHead = 0;
    uint32_t g_sourceTransitionCount = 0;
    uint32_t g_simSessionTransitionCount = 0;
    uint32_t g_simSessionSuppressedCount = 0;
    TelemetrySourceTransitionEvent g_lastSourceTransition{};
    SimSessionTransitionEvent g_lastSimSessionTransition{};
    SimSessionState g_lastSimSessionState = SimSessionState::Inactive;
    ActiveTelemetrySource g_lastSimSessionSource = ActiveTelemetrySource::None;
    SimSessionState g_pendingSimSessionState = SimSessionState::Inactive;
    ActiveTelemetrySource g_pendingSimSessionSource = ActiveTelemetrySource::None;
    unsigned long g_pendingSimSessionSinceMs = 0;

    bool isObdFresh(unsigned long nowMs)
    {
        return g_connected && g_lastObdTelemetryMs > 0 && (nowMs - g_lastObdTelemetryMs) <= OBD_FRESH_TIMEOUT_MS;
    }

    bool isSimHubFresh(unsigned long nowMs)
    {
        return g_lastSimHubNetworkTelemetryMs > 0 && (nowMs - g_lastSimHubNetworkTelemetryMs) <= SIMHUB_FRESH_TIMEOUT_MS;
    }

    ActiveTelemetrySource selectSimRuntimeSource(SimRuntimeTransportMode mode, bool usbFresh, bool simHubFresh)
    {
        // Transport policy is evaluated independently from overall OBD fallback.
        switch (mode)
        {
        case SimRuntimeTransportMode::UsbOnly:
            return usbFresh ? ActiveTelemetrySource::UsbSim : ActiveTelemetrySource::None;
        case SimRuntimeTransportMode::NetworkOnly:
            return simHubFresh ? ActiveTelemetrySource::SimHubNetwork : ActiveTelemetrySource::None;
        case SimRuntimeTransportMode::Auto:
            if (usbFresh)
            {
                return ActiveTelemetrySource::UsbSim;
            }
            if (simHubFresh)
            {
                return ActiveTelemetrySource::SimHubNetwork;
            }
            return ActiveTelemetrySource::None;
        case SimRuntimeTransportMode::Disabled:
        default:
            return ActiveTelemetrySource::None;
        }
    }

    void applyActiveTelemetry(ActiveTelemetrySource source, unsigned long nowMs)
    {
        switch (source)
        {
        case ActiveTelemetrySource::UsbSim:
            g_currentRpm = g_usbSimCurrentRpm;
            g_maxSeenRpm = max(g_usbSimMaxSeenRpm, g_usbSimCurrentRpm);
            g_vehicleSpeedKmh = g_usbSimVehicleSpeedKmh;
            g_estimatedGear = g_usbSimGear;
            g_currentThrottle = g_usbSimThrottle;
            g_pitLimiterActive = g_usbSimPitLimiterActive;
            g_lastObdMs = g_lastUsbTelemetryMs;
            g_ignitionOn = true;
            g_engineRunning = g_currentRpm > ENGINE_START_RPM_THRESHOLD;
            break;
        case ActiveTelemetrySource::SimHubNetwork:
            g_currentRpm = g_simHubCurrentRpm;
            g_maxSeenRpm = max(g_simHubMaxSeenRpm, g_simHubCurrentRpm);
            g_vehicleSpeedKmh = g_simHubVehicleSpeedKmh;
            g_estimatedGear = g_simHubGear;
            g_currentThrottle = g_simHubThrottle;
            g_pitLimiterActive = g_simHubPitLimiterActive;
            g_lastObdMs = g_lastSimHubNetworkTelemetryMs;
            g_ignitionOn = true;
            g_engineRunning = g_currentRpm > ENGINE_START_RPM_THRESHOLD;
            break;
        case ActiveTelemetrySource::Obd:
            g_currentRpm = g_obdCurrentRpm;
            g_maxSeenRpm = max(g_obdMaxSeenRpm, g_obdCurrentRpm);
            g_vehicleSpeedKmh = g_obdVehicleSpeedKmh;
            g_estimatedGear = g_obdEstimatedGear;
            g_currentThrottle = 0.0f;
            g_pitLimiterActive = false;
            g_lastObdMs = g_lastObdTelemetryMs;
            g_ignitionOn = g_lastObdTelemetryMs > 0;
            g_engineRunning = g_currentRpm > ENGINE_START_RPM_THRESHOLD;
            break;
        case ActiveTelemetrySource::None:
        default:
        {
            // Instead of slamming to zero (which used to cause a "freeze then
            // disappear" visual when the 8s hold expired), ramp RPM down at a
            // constant rate. Everything else snaps to zero immediately because
            // they have no natural decay meaning (gear, throttle, speed).
            unsigned long dtMs = (g_lastDecayMs == 0) ? 0 : (nowMs - g_lastDecayMs);
            // Clamp dt so an overflowed/first-call gap cannot wipe the value.
            if (dtMs > 200)
            {
                dtMs = 200;
            }
            const int decay = static_cast<int>(RPM_DECAY_PER_MS * static_cast<float>(dtMs));
            g_currentRpm = (decay >= g_currentRpm) ? 0 : (g_currentRpm - decay);
            g_vehicleSpeedKmh = 0;
            g_estimatedGear = 0;
            g_currentThrottle = 0.0f;
            g_pitLimiterActive = false;
            if (g_lastObdMs > 0 && (nowMs - g_lastObdMs) > IGNITION_TIMEOUT_MS)
            {
                g_ignitionOn = false;
                g_engineRunning = false;
            }
            break;
        }
        }
        // Remember the last apply tick so decay math can use elapsed time.
        g_lastDecayMs = nowMs;
    }

    unsigned long lastSampleForSource(ActiveTelemetrySource source)
    {
        switch (source)
        {
        case ActiveTelemetrySource::Obd:
            return g_lastObdTelemetryMs;
        case ActiveTelemetrySource::SimHubNetwork:
            return g_lastSimHubNetworkTelemetryMs;
        case ActiveTelemetrySource::UsbSim:
            return g_lastUsbTelemetryMs;
        case ActiveTelemetrySource::None:
        default:
            return 0;
        }
    }

    bool sourceFreshForSnapshot(ActiveTelemetrySource source, bool usbFresh, bool simHubFresh, bool obdFresh)
    {
        switch (source)
        {
        case ActiveTelemetrySource::UsbSim:
            return usbFresh;
        case ActiveTelemetrySource::SimHubNetwork:
            return simHubFresh;
        case ActiveTelemetrySource::Obd:
            return obdFresh;
        case ActiveTelemetrySource::None:
        default:
            return false;
        }
    }

    void pushSourceTransition(const TelemetrySourceTransitionEvent &event)
    {
        g_lastSourceTransition = event;
        g_sourceHistory[g_sourceHistoryHead] = event;
        g_sourceHistoryHead = static_cast<uint8_t>((g_sourceHistoryHead + 1U) % TELEMETRY_DEBUG_HISTORY_LEN);
        if (g_sourceHistoryCount < TELEMETRY_DEBUG_HISTORY_LEN)
        {
            ++g_sourceHistoryCount;
        }
        ++g_sourceTransitionCount;
    }

    void pushSimTransition(const SimSessionTransitionEvent &event)
    {
        g_lastSimSessionTransition = event;
        g_simHistory[g_simHistoryHead] = event;
        g_simHistoryHead = static_cast<uint8_t>((g_simHistoryHead + 1U) % TELEMETRY_DEBUG_HISTORY_LEN);
        if (g_simHistoryCount < TELEMETRY_DEBUG_HISTORY_LEN)
        {
            ++g_simHistoryCount;
        }
        ++g_simSessionTransitionCount;
    }

    SimSessionState mapUsbSessionState(UsbBridgeConnectionState state)
    {
        switch (state)
        {
        case UsbBridgeConnectionState::Live:
            return SimSessionState::Live;
        case UsbBridgeConnectionState::Error:
            return SimSessionState::Error;
        case UsbBridgeConnectionState::Disabled:
        case UsbBridgeConnectionState::Disconnected:
            return SimSessionState::Inactive;
        case UsbBridgeConnectionState::WaitingForBridge:
        case UsbBridgeConnectionState::WaitingForData:
        default:
            return SimSessionState::WaitingForData;
        }
    }

    SimSessionState mapSimHubSessionState(SimHubConnectionState state)
    {
        switch (state)
        {
        case SimHubConnectionState::Live:
            return SimSessionState::Live;
        case SimHubConnectionState::Error:
            return SimSessionState::Error;
        case SimHubConnectionState::Disabled:
            return SimSessionState::Inactive;
        case SimHubConnectionState::WaitingForHost:
        case SimHubConnectionState::WaitingForNetwork:
        case SimHubConnectionState::WaitingForData:
        default:
            return SimSessionState::WaitingForData;
        }
    }

    ActiveTelemetrySource chooseSimSessionSource(ActiveTelemetrySource activeSource, SimRuntimeTransportMode mode)
    {
        if (activeSource == ActiveTelemetrySource::UsbSim || activeSource == ActiveTelemetrySource::SimHubNetwork)
        {
            return activeSource;
        }

        if (cfg.telemetryPreference == TelemetryPreference::SimHub)
        {
            switch (mode)
            {
            case SimRuntimeTransportMode::UsbOnly:
                return ActiveTelemetrySource::UsbSim;
            case SimRuntimeTransportMode::NetworkOnly:
                return ActiveTelemetrySource::SimHubNetwork;
            case SimRuntimeTransportMode::Auto:
                if (g_lastSimSessionSource == ActiveTelemetrySource::UsbSim)
                {
                    return ActiveTelemetrySource::UsbSim;
                }
                return ActiveTelemetrySource::SimHubNetwork;
            case SimRuntimeTransportMode::Disabled:
            default:
                return ActiveTelemetrySource::None;
            }
        }

        if (g_lastSimSessionSource == ActiveTelemetrySource::UsbSim || g_lastSimSessionSource == ActiveTelemetrySource::SimHubNetwork)
        {
            return g_lastSimSessionSource;
        }

        return ActiveTelemetrySource::None;
    }

    SimSessionState deriveSimSessionState(ActiveTelemetrySource sessionSource)
    {
        switch (sessionSource)
        {
        case ActiveTelemetrySource::UsbSim:
            return mapUsbSessionState(g_usbBridgeConnectionState);
        case ActiveTelemetrySource::SimHubNetwork:
            return mapSimHubSessionState(g_simHubConnectionState);
        case ActiveTelemetrySource::Obd:
        case ActiveTelemetrySource::None:
        default:
            return SimSessionState::Inactive;
        }
    }

    SimSessionTransitionType classifySimTransition(SimSessionState from, SimSessionState to)
    {
        if (to == SimSessionState::Live && from != SimSessionState::Live)
        {
            return SimSessionTransitionType::BecameLive;
        }
        if (to == SimSessionState::Error && from != SimSessionState::Error)
        {
            return SimSessionTransitionType::BecameError;
        }
        if (to == SimSessionState::WaitingForData && from == SimSessionState::Live)
        {
            return SimSessionTransitionType::BecameWaiting;
        }
        return SimSessionTransitionType::None;
    }

    SimSessionState trackSimSessionTransition(unsigned long nowMs, ActiveTelemetrySource source, SimSessionState candidateState)
    {
        if (source == ActiveTelemetrySource::UsbSim || source == ActiveTelemetrySource::SimHubNetwork)
        {
            g_lastSimSessionSource = source;
        }
        else if (candidateState == SimSessionState::Inactive)
        {
            g_lastSimSessionSource = ActiveTelemetrySource::None;
        }

        if (candidateState == g_lastSimSessionState)
        {
            g_pendingSimSessionState = candidateState;
            g_pendingSimSessionSource = source;
            g_pendingSimSessionSinceMs = nowMs;
            return g_lastSimSessionState;
        }

        if (candidateState != g_pendingSimSessionState || source != g_pendingSimSessionSource)
        {
            if (g_pendingSimSessionState != g_lastSimSessionState)
            {
                ++g_simSessionSuppressedCount;
            }
            g_pendingSimSessionState = candidateState;
            g_pendingSimSessionSource = source;
            g_pendingSimSessionSinceMs = nowMs;
            return g_lastSimSessionState;
        }

        if (!telemetrySimSessionCandidateReady(candidateState, g_pendingSimSessionSinceMs, nowMs))
        {
            return g_lastSimSessionState;
        }

        const SimSessionTransitionType transition = classifySimTransition(g_lastSimSessionState, candidateState);
        if (transition != SimSessionTransitionType::None)
        {
            SimSessionTransitionEvent event{};
            event.timestampMs = nowMs;
            event.fromState = g_lastSimSessionState;
            event.toState = candidateState;
            event.transition = transition;
            event.source = source;
            portENTER_CRITICAL(&g_telemetrySnapshotMux);
            pushSimTransition(event);
            portEXIT_CRITICAL(&g_telemetrySnapshotMux);
            ledBarRequestSimSessionTransition(transition);
            displayShowSimSessionTransition(transition);
        }

        g_lastSimSessionState = candidateState;
        g_pendingSimSessionState = candidateState;
        g_pendingSimSessionSource = source;
        g_pendingSimSessionSinceMs = nowMs;
        return g_lastSimSessionState;
    }

    void publishRenderSnapshot(const TelemetryRenderSnapshot &snapshot)
    {
        portENTER_CRITICAL(&g_telemetrySnapshotMux);
        g_renderSnapshot = snapshot;
        portEXIT_CRITICAL(&g_telemetrySnapshotMux);
    }

    void recordSourceTransition(unsigned long nowMs,
                                ActiveTelemetrySource fromSource,
                                ActiveTelemetrySource toSource,
                                bool usbFresh,
                                bool simHubFresh,
                                bool obdFresh,
                                bool holdApplied,
                                bool fallbackActive)
    {
        TelemetrySourceTransitionEvent event{};
        event.timestampMs = nowMs;
        event.fromSource = fromSource;
        event.toSource = toSource;
        event.usbFresh = usbFresh;
        event.simHubFresh = simHubFresh;
        event.obdFresh = obdFresh;
        event.holdApplied = holdApplied;
        event.fallbackActive = fallbackActive;
        portENTER_CRITICAL(&g_telemetrySnapshotMux);
        pushSourceTransition(event);
        portEXIT_CRITICAL(&g_telemetrySnapshotMux);
    }

}

const char *telemetrySourceName(ActiveTelemetrySource source)
{
    switch (source)
    {
    case ActiveTelemetrySource::Obd:
        return "OBD";
    case ActiveTelemetrySource::SimHubNetwork:
        return "SimHubNet";
    case ActiveTelemetrySource::UsbSim:
        return "UsbSim";
    case ActiveTelemetrySource::None:
    default:
        return "None";
    }
}

const char *simSessionStateName(SimSessionState state)
{
    switch (state)
    {
    case SimSessionState::WaitingForData:
        return "waiting";
    case SimSessionState::Live:
        return "live";
    case SimSessionState::Error:
        return "error";
    case SimSessionState::Inactive:
    default:
        return "inactive";
    }
}

const char *simSessionTransitionTypeName(SimSessionTransitionType type)
{
    switch (type)
    {
    case SimSessionTransitionType::BecameLive:
        return "became-live";
    case SimSessionTransitionType::BecameWaiting:
        return "became-waiting";
    case SimSessionTransitionType::BecameError:
        return "became-error";
    case SimSessionTransitionType::None:
    default:
        return "none";
    }
}

unsigned long simSessionStateDebounceMs(SimSessionState state)
{
    switch (state)
    {
    case SimSessionState::Live:
        return SIM_SESSION_LIVE_DEBOUNCE_MS;
    case SimSessionState::WaitingForData:
        return SIM_SESSION_WAITING_DEBOUNCE_MS;
    case SimSessionState::Error:
        return SIM_SESSION_ERROR_DEBOUNCE_MS;
    case SimSessionState::Inactive:
    default:
        return SIM_SESSION_INACTIVE_DEBOUNCE_MS;
    }
}

bool telemetrySimSessionCandidateReady(SimSessionState candidateState, unsigned long pendingSinceMs, unsigned long nowMs)
{
    return pendingSinceMs > 0 && (nowMs >= pendingSinceMs) &&
           (nowMs - pendingSinceMs) >= simSessionStateDebounceMs(candidateState);
}

namespace
{
}

TelemetryPreference nextTelemetryPreference(TelemetryPreference current)
{
    switch (current)
    {
    case TelemetryPreference::Auto:
        return TelemetryPreference::Obd;
    case TelemetryPreference::Obd:
        return TelemetryPreference::SimHub;
    case TelemetryPreference::SimHub:
    default:
        return TelemetryPreference::Auto;
    }
}

SimRuntimeTransportMode resolveSimRuntimeTransportMode(TelemetryPreference preference, SimTransportPreference transportPreference)
{
    if (preference == TelemetryPreference::Obd)
    {
        return SimRuntimeTransportMode::Disabled;
    }

    switch (transportPreference)
    {
    case SimTransportPreference::UsbSerial:
        return SimRuntimeTransportMode::UsbOnly;
    case SimTransportPreference::Network:
        return SimRuntimeTransportMode::NetworkOnly;
    case SimTransportPreference::Auto:
    default:
        return SimRuntimeTransportMode::Auto;
    }
}

const char *simRuntimeTransportModeName(SimRuntimeTransportMode mode)
{
    switch (mode)
    {
    case SimRuntimeTransportMode::UsbOnly:
        return "USB_ONLY";
    case SimRuntimeTransportMode::NetworkOnly:
        return "NETWORK_ONLY";
    case SimRuntimeTransportMode::Auto:
        return "AUTO";
    case SimRuntimeTransportMode::Disabled:
    default:
        return "DISABLED";
    }
}

bool telemetryAllowsUsbSim(TelemetryPreference preference, SimTransportPreference transportPreference)
{
    const SimRuntimeTransportMode mode = resolveSimRuntimeTransportMode(preference, transportPreference);
    return mode == SimRuntimeTransportMode::Auto || mode == SimRuntimeTransportMode::UsbOnly;
}

bool telemetryAllowsNetworkSim(TelemetryPreference preference, SimTransportPreference transportPreference)
{
    const SimRuntimeTransportMode mode = resolveSimRuntimeTransportMode(preference, transportPreference);
    return mode == SimRuntimeTransportMode::Auto || mode == SimRuntimeTransportMode::NetworkOnly;
}

bool telemetrySupportsTransportFallback(TelemetryPreference preference, SimTransportPreference transportPreference)
{
    return resolveSimRuntimeTransportMode(preference, transportPreference) == SimRuntimeTransportMode::Auto;
}

bool telemetrySourceIsFallback(ActiveTelemetrySource source, TelemetryPreference preference, SimTransportPreference transportPreference)
{
    if (source == ActiveTelemetrySource::None)
    {
        return false;
    }

    if (telemetrySupportsTransportFallback(preference, transportPreference) &&
        source == ActiveTelemetrySource::SimHubNetwork)
    {
        return true;
    }

    return preference == TelemetryPreference::Auto && source == ActiveTelemetrySource::Obd;
}

ActiveTelemetrySource selectTelemetryRuntimeSource(TelemetryPreference preference,
                                                   SimTransportPreference transportPreference,
                                                   bool usbFresh,
                                                   bool simHubFresh,
                                                   bool obdFresh)
{
    const ActiveTelemetrySource simSource =
        selectSimRuntimeSource(resolveSimRuntimeTransportMode(preference, transportPreference), usbFresh, simHubFresh);

    switch (preference)
    {
    case TelemetryPreference::Obd:
        return obdFresh ? ActiveTelemetrySource::Obd : ActiveTelemetrySource::None;
    case TelemetryPreference::SimHub:
        return simSource;
    case TelemetryPreference::Auto:
    default:
        return simSource != ActiveTelemetrySource::None
                   ? simSource
                   : (obdFresh ? ActiveTelemetrySource::Obd : ActiveTelemetrySource::None);
    }
}

void initTelemetry()
{
    initUsbSimBridge();
    initSimHubClient();
    usbSimBridgeUpdateConfig();
    simHubClientUpdateConfig();
}

void telemetryLoop()
{
    usbSimBridgeUpdateConfig();
    simHubClientUpdateConfig();

    const unsigned long nowMs = millis();
    const bool usbFresh = usbSimTelemetryFresh(nowMs);
    const bool simHubFresh = isSimHubFresh(nowMs);
    const bool obdFresh = isObdFresh(nowMs);
    const SimRuntimeTransportMode mode = resolveSimRuntimeTransportMode(cfg.telemetryPreference, cfg.simTransportPreference);
    ActiveTelemetrySource nextSource =
        selectTelemetryRuntimeSource(cfg.telemetryPreference, cfg.simTransportPreference, usbFresh, simHubFresh, obdFresh);
    bool holdApplied = false;

    // The hold window is only for Auto transport so short gaps do not flap between USB and network.
    if (telemetrySupportsTransportFallback(cfg.telemetryPreference, cfg.simTransportPreference) &&
        nextSource == ActiveTelemetrySource::None &&
        g_activeTelemetrySource != ActiveTelemetrySource::None)
    {
        const unsigned long lastSampleMs = lastSampleForSource(g_activeTelemetrySource);
        if (lastSampleMs > 0 && (nowMs - lastSampleMs) <= TELEMETRY_SOURCE_HOLD_MS)
        {
            nextSource = g_activeTelemetrySource;
            holdApplied = true;
        }
    }

    if (nextSource != g_activeTelemetrySource)
    {
        LOG_INFO("TELEMETRY", "SOURCE_SWITCH",
                 String("from=") + telemetrySourceName(g_activeTelemetrySource) +
                     " to=" + telemetrySourceName(nextSource) +
                     " mode=" + simRuntimeTransportModeName(mode) +
                     " usbFresh=" + (usbFresh ? "1" : "0") +
                     " netFresh=" + (simHubFresh ? "1" : "0") +
                     " obdFresh=" + (obdFresh ? "1" : "0"));
        recordSourceTransition(nowMs,
                               g_activeTelemetrySource,
                               nextSource,
                               usbFresh,
                               simHubFresh,
                               obdFresh,
                               holdApplied,
                               telemetrySourceIsFallback(nextSource, cfg.telemetryPreference, cfg.simTransportPreference));
        g_activeTelemetrySource = nextSource;
    }

    applyActiveTelemetry(nextSource, nowMs);

    const ActiveTelemetrySource simSessionSource = chooseSimSessionSource(nextSource, mode);
    const SimSessionState simSessionState = trackSimSessionTransition(nowMs, simSessionSource, deriveSimSessionState(simSessionSource));

    TelemetryRenderSnapshot snapshot{};
    snapshot.version = telemetryGetRenderSnapshotVersion() + 1U;
    snapshot.source = nextSource;
    snapshot.rpm = g_currentRpm;
    snapshot.maxSeenRpm = g_maxSeenRpm;
    snapshot.speedKmh = g_vehicleSpeedKmh;
    snapshot.gear = g_estimatedGear;
    snapshot.throttle = g_currentThrottle;
    snapshot.pitLimiter = g_pitLimiterActive;
    snapshot.sampleTimestampMs = lastSampleForSource(nextSource);
    snapshot.telemetryFresh = sourceFreshForSnapshot(nextSource, usbFresh, simHubFresh, obdFresh);
    snapshot.fallbackActive = telemetrySourceIsFallback(nextSource, cfg.telemetryPreference, cfg.simTransportPreference);
    snapshot.simSessionState = simSessionState;
    publishRenderSnapshot(snapshot);
}

void telemetryOnObdSample(int rpm, int speedKmh, int gear, unsigned long sampleMs)
{
    g_obdCurrentRpm = max(0, rpm);
    g_obdMaxSeenRpm = max(g_obdMaxSeenRpm, g_obdCurrentRpm);
    g_obdVehicleSpeedKmh = max(0, speedKmh);
    g_obdEstimatedGear = max(0, gear);
    g_lastObdTelemetryMs = sampleMs;
}

void telemetryOnObdDisconnected()
{
    g_obdCurrentRpm = 0;
    g_obdVehicleSpeedKmh = 0;
    g_obdEstimatedGear = 0;
}

bool telemetryShouldAllowObd()
{
    return !usbSimShouldBlockObd();
}

TelemetryRenderSnapshot telemetryCopyRenderSnapshot()
{
    TelemetryRenderSnapshot snapshot{};
    portENTER_CRITICAL(&g_telemetrySnapshotMux);
    snapshot = g_renderSnapshot;
    portEXIT_CRITICAL(&g_telemetrySnapshotMux);
    return snapshot;
}

uint32_t telemetryGetRenderSnapshotVersion()
{
    uint32_t version = 0;
    portENTER_CRITICAL(&g_telemetrySnapshotMux);
    version = g_renderSnapshot.version;
    portEXIT_CRITICAL(&g_telemetrySnapshotMux);
    return version;
}

TelemetryDebugInfo telemetryGetDebugInfo()
{
    TelemetryDebugInfo info{};
    portENTER_CRITICAL(&g_telemetrySnapshotMux);
    info.snapshot = g_renderSnapshot;
    info.lastSourceTransition = g_lastSourceTransition;
    info.lastSimSessionTransition = g_lastSimSessionTransition;
    info.sourceTransitionCount = g_sourceTransitionCount;
    info.simSessionTransitionCount = g_simSessionTransitionCount;
    info.simSessionSuppressedCount = g_simSessionSuppressedCount;
    info.sourceHistoryCount = g_sourceHistoryCount;
    info.simHistoryCount = g_simHistoryCount;
    for (uint8_t i = 0; i < g_sourceHistoryCount; ++i)
    {
        const uint8_t index = static_cast<uint8_t>((g_sourceHistoryHead + TELEMETRY_DEBUG_HISTORY_LEN - g_sourceHistoryCount + i) % TELEMETRY_DEBUG_HISTORY_LEN);
        info.sourceHistory[i] = g_sourceHistory[index];
    }
    for (uint8_t i = 0; i < g_simHistoryCount; ++i)
    {
        const uint8_t index = static_cast<uint8_t>((g_simHistoryHead + TELEMETRY_DEBUG_HISTORY_LEN - g_simHistoryCount + i) % TELEMETRY_DEBUG_HISTORY_LEN);
        info.simHistory[i] = g_simHistory[index];
    }
    portEXIT_CRITICAL(&g_telemetrySnapshotMux);
    return info;
}
