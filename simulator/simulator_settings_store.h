#pragma once

#include <string>

#include "simulator_types.h"
#include "telemetry/telemetry_types.h"
#include "ui/ui_runtime.h"

struct SimulatorPersistedState
{
    UiSettings settings{};
    TelemetryServiceConfig telemetry{};
    SimulatorLedBarConfig ledBar{};
    SideLedConfig sideLeds{};
    SimulatorDeviceConfig device{};
    std::string bleTargetName = "OBD-II Dongle";
    std::string bleTargetAddress = "66:1E:32:9D:2E:5D";
};

bool load_simulator_persisted_state(SimulatorPersistedState &state, std::string *errorMessage = nullptr);
bool save_simulator_persisted_state(const SimulatorPersistedState &state, std::string *errorMessage = nullptr);
