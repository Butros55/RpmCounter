#pragma once

#include "core/config.h"
#include "core/state.h"

void initTelemetry();
void telemetryLoop();
void telemetryOnObdSample(int rpm, int speedKmh, int gear, unsigned long sampleMs);
void telemetryOnObdDisconnected();
TelemetryPreference nextTelemetryPreference(TelemetryPreference current);
ActiveTelemetrySource selectTelemetryRuntimeSource(TelemetryPreference preference, bool usbFresh, bool simHubFresh, bool obdFresh);
bool telemetryShouldAllowObd();
