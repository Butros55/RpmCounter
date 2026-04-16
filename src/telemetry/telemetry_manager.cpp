#include "telemetry_manager.h"

#include <Arduino.h>

#include "core/config.h"
#include "core/logging.h"
#include "hardware/display.h"
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
    unsigned long g_lastDecayMs = 0;

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

    const char *sourceName(ActiveTelemetrySource source)
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
    ActiveTelemetrySource nextSource =
        selectTelemetryRuntimeSource(cfg.telemetryPreference, cfg.simTransportPreference, usbFresh, simHubFresh, obdFresh);

    // The hold window is only for Auto transport so short gaps do not flap between USB and network.
    if (telemetrySupportsTransportFallback(cfg.telemetryPreference, cfg.simTransportPreference) &&
        nextSource == ActiveTelemetrySource::None &&
        g_activeTelemetrySource != ActiveTelemetrySource::None)
    {
        const unsigned long lastSampleMs = lastSampleForSource(g_activeTelemetrySource);
        if (lastSampleMs > 0 && (nowMs - lastSampleMs) <= TELEMETRY_SOURCE_HOLD_MS)
        {
            nextSource = g_activeTelemetrySource;
        }
    }

    if (nextSource != g_activeTelemetrySource)
    {
        const SimRuntimeTransportMode mode = resolveSimRuntimeTransportMode(cfg.telemetryPreference, cfg.simTransportPreference);
        LOG_INFO("TELEMETRY", "SOURCE_SWITCH",
                 String("from=") + sourceName(g_activeTelemetrySource) +
                     " to=" + sourceName(nextSource) +
                     " mode=" + simRuntimeTransportModeName(mode) +
                     " usbFresh=" + (usbFresh ? "1" : "0") +
                     " netFresh=" + (simHubFresh ? "1" : "0") +
                     " obdFresh=" + (obdFresh ? "1" : "0"));
        g_activeTelemetrySource = nextSource;
    }

    applyActiveTelemetry(nextSource, nowMs);
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
