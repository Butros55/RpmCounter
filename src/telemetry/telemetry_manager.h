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
bool telemetryShouldAllowObd();
