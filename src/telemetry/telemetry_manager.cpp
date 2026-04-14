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
    constexpr unsigned long TELEMETRY_SOURCE_HOLD_MS = 8000;

    bool isObdFresh(unsigned long nowMs)
    {
        return g_connected && g_lastObdTelemetryMs > 0 && (nowMs - g_lastObdTelemetryMs) <= OBD_FRESH_TIMEOUT_MS;
    }

    bool isSimHubFresh(unsigned long nowMs)
    {
        return g_simHubEverReceived && g_lastSimHubTelemetryMs > 0 && (nowMs - g_lastSimHubTelemetryMs) <= SIMHUB_FRESH_TIMEOUT_MS;
    }

    void applyActiveTelemetry(ActiveTelemetrySource source, unsigned long nowMs)
    {
        switch (source)
        {
        case ActiveTelemetrySource::SimHubNetwork:
        case ActiveTelemetrySource::UsbSim:
            g_currentRpm = g_simHubCurrentRpm;
            g_maxSeenRpm = max(g_simHubMaxSeenRpm, g_simHubCurrentRpm);
            g_vehicleSpeedKmh = g_simHubVehicleSpeedKmh;
            g_estimatedGear = g_simHubGear;
            g_currentThrottle = g_simHubThrottle;
            g_pitLimiterActive = g_simHubPitLimiterActive;
            g_lastObdMs = g_lastSimHubTelemetryMs;
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
            g_currentRpm = 0;
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

    unsigned long lastSampleForSource(ActiveTelemetrySource source)
    {
        switch (source)
        {
        case ActiveTelemetrySource::Obd:
            return g_lastObdTelemetryMs;
        case ActiveTelemetrySource::SimHubNetwork:
        case ActiveTelemetrySource::UsbSim:
            return g_lastSimHubTelemetryMs;
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

    bool preferUsbTransport()
    {
        if (cfg.simTransportPreference == SimTransportPreference::UsbSerial)
        {
            return true;
        }
        if (cfg.simTransportPreference == SimTransportPreference::Network)
        {
            return false;
        }
        return usbSimBridgeOnline();
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

ActiveTelemetrySource selectTelemetryRuntimeSource(TelemetryPreference preference, bool usbFresh, bool simHubFresh, bool obdFresh)
{
    switch (preference)
    {
    case TelemetryPreference::Obd:
        return obdFresh ? ActiveTelemetrySource::Obd : ActiveTelemetrySource::None;
    case TelemetryPreference::SimHub:
        if (preferUsbTransport())
        {
            return usbFresh ? ActiveTelemetrySource::UsbSim : ActiveTelemetrySource::None;
        }
        return simHubFresh ? ActiveTelemetrySource::SimHubNetwork : ActiveTelemetrySource::None;
    case TelemetryPreference::Auto:
    default:
        if (usbFresh)
        {
            return ActiveTelemetrySource::UsbSim;
        }
        if (simHubFresh)
        {
            return ActiveTelemetrySource::SimHubNetwork;
        }
        if (obdFresh)
        {
            return ActiveTelemetrySource::Obd;
        }
        return ActiveTelemetrySource::None;
    }
}

void initTelemetry()
{
    initUsbSimBridge();
    initSimHubClient();
    simHubClientUpdateConfig();
    usbSimBridgeUpdateConfig();
}

void telemetryLoop()
{
    usbSimBridgeUpdateConfig();
    simHubClientUpdateConfig();

    const unsigned long nowMs = millis();
    ActiveTelemetrySource nextSource = selectTelemetryRuntimeSource(cfg.telemetryPreference, usbSimTelemetryFresh(nowMs), isSimHubFresh(nowMs), isObdFresh(nowMs));

    if (nextSource == ActiveTelemetrySource::None && g_activeTelemetrySource != ActiveTelemetrySource::None)
    {
        const unsigned long lastSampleMs = lastSampleForSource(g_activeTelemetrySource);
        if (lastSampleMs > 0 && (nowMs - lastSampleMs) <= TELEMETRY_SOURCE_HOLD_MS)
        {
            nextSource = g_activeTelemetrySource;
        }
    }

    if (nextSource != g_activeTelemetrySource)
    {
        LOG_INFO("TELEMETRY", "SOURCE_SWITCH", String("active=") + sourceName(nextSource));
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
