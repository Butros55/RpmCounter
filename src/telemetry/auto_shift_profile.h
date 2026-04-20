#pragma once

#include "core/config.h"
#include "core/state.h"

struct TelemetryRenderSnapshot;
struct TelemetryVehicleLearningInfo;
struct ResolvedShiftProfileRuntime;

void autoShiftProfileInit();
void autoShiftProfileOnSourceMeta(ActiveTelemetrySource source, const VehicleIdentity &identity, unsigned long sampleMs);
void autoShiftProfileOnResolvedTelemetry(const TelemetryRenderSnapshot &snapshot, unsigned long nowMs);
void autoShiftProfileGetInfo(TelemetryVehicleLearningInfo &out);
void autoShiftProfileCopyRuntime(ResolvedShiftProfileRuntime &out);
bool autoShiftProfileResetCurrent();
void autoShiftProfileResetAll();
bool autoShiftProfileCopyCurrentToManualAdvanced();
bool autoShiftProfileSetCurrentLocked(bool locked);
bool autoShiftProfileCurrentLocked();
