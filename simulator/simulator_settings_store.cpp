#include "simulator_settings_store.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
    std::filesystem::path simulator_settings_path()
    {
        const char *explicitPath = std::getenv("SIM_SETTINGS_PATH");
        if (explicitPath != nullptr && explicitPath[0] != '\0')
        {
            return std::filesystem::path(explicitPath);
        }

#ifdef _WIN32
        const char *localAppData = std::getenv("LOCALAPPDATA");
        if (localAppData != nullptr && localAppData[0] != '\0')
        {
            return std::filesystem::path(localAppData) / "RpmCounter" / "simulator_state.cfg";
        }
#endif

        const char *xdgStateHome = std::getenv("XDG_STATE_HOME");
        if (xdgStateHome != nullptr && xdgStateHome[0] != '\0')
        {
            return std::filesystem::path(xdgStateHome) / "rpmcounter" / "simulator_state.cfg";
        }

        const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
        if (xdgConfigHome != nullptr && xdgConfigHome[0] != '\0')
        {
            return std::filesystem::path(xdgConfigHome) / "rpmcounter" / "simulator_state.cfg";
        }

        const char *home = std::getenv("HOME");
        if (home != nullptr && home[0] != '\0')
        {
            return std::filesystem::path(home) / ".config" / "rpmcounter" / "simulator_state.cfg";
        }

        return std::filesystem::current_path() / ".rpmcounter_simulator_state.cfg";
    }

    bool write_bool(std::ostream &stream, const char *key, bool value)
    {
        stream << key << ' ' << (value ? 1 : 0) << '\n';
        return stream.good();
    }

    bool write_int(std::ostream &stream, const char *key, int value)
    {
        stream << key << ' ' << value << '\n';
        return stream.good();
    }

    bool write_uint(std::ostream &stream, const char *key, unsigned int value)
    {
        stream << key << ' ' << value << '\n';
        return stream.good();
    }

    bool write_string(std::ostream &stream, const char *key, const std::string &value)
    {
        stream << key << ' ' << std::quoted(value) << '\n';
        return stream.good();
    }

    bool read_bool(std::istream &stream, bool &value)
    {
        int raw = 0;
        if (!(stream >> raw))
        {
            return false;
        }
        value = raw != 0;
        return true;
    }

    template <typename T>
    bool read_number(std::istream &stream, T &value)
    {
        return static_cast<bool>(stream >> value);
    }

    bool read_string(std::istream &stream, std::string &value)
    {
        return static_cast<bool>(stream >> std::quoted(value));
    }
}

bool load_simulator_persisted_state(SimulatorPersistedState &state, std::string *errorMessage)
{
    const std::filesystem::path path = simulator_settings_path();
    if (!std::filesystem::exists(path))
    {
        return false;
    }

    std::ifstream stream(path);
    if (!stream.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Unable to open persisted simulator state";
        }
        return false;
    }

    std::string key;
    while (stream >> key)
    {
        if (key == "version")
        {
            int version = 0;
            read_number(stream, version);
        }
        else if (key == "settings.displayBrightness")
        {
            read_number(stream, state.settings.displayBrightness);
        }
        else if (key == "settings.tutorialSeen")
        {
            read_bool(stream, state.settings.tutorialSeen);
        }
        else if (key == "settings.lastMenuIndex")
        {
            read_number(stream, state.settings.lastMenuIndex);
        }
        else if (key == "settings.nightMode")
        {
            read_bool(stream, state.settings.nightMode);
        }
        else if (key == "settings.showShiftStrip")
        {
            read_bool(stream, state.settings.showShiftStrip);
        }
        else if (key == "settings.telemetryPreference")
        {
            int raw = 0;
            read_number(stream, raw);
            state.settings.telemetryPreference = static_cast<UiTelemetryPreference>(raw);
        }
        else if (key == "settings.displayFocus")
        {
            int raw = 0;
            read_number(stream, raw);
            state.settings.displayFocus = static_cast<UiDisplayFocusMetric>(raw);
        }
        else if (key == "telemetry.mode")
        {
            int raw = 0;
            read_number(stream, raw);
            state.telemetry.mode = static_cast<TelemetryInputMode>(raw);
        }
        else if (key == "telemetry.simHubTransport")
        {
            int raw = 0;
            read_number(stream, raw);
            state.telemetry.simHubTransport = static_cast<SimHubTransport>(raw);
        }
        else if (key == "telemetry.udpPort")
        {
            unsigned int raw = 0;
            read_number(stream, raw);
            state.telemetry.udpPort = static_cast<uint16_t>(raw);
        }
        else if (key == "telemetry.httpPort")
        {
            unsigned int raw = 0;
            read_number(stream, raw);
            state.telemetry.httpPort = static_cast<uint16_t>(raw);
        }
        else if (key == "telemetry.pollIntervalMs")
        {
            read_number(stream, state.telemetry.pollIntervalMs);
        }
        else if (key == "telemetry.staleTimeoutMs")
        {
            read_number(stream, state.telemetry.staleTimeoutMs);
        }
        else if (key == "telemetry.debugLogging")
        {
            read_bool(stream, state.telemetry.debugLogging);
        }
        else if (key == "telemetry.allowSimulatorFallback")
        {
            read_bool(stream, state.telemetry.allowSimulatorFallback);
        }
        else if (key == "led.mode")
        {
            int raw = 0;
            read_number(stream, raw);
            state.ledBar.mode = simulator_led_mode_from_int(raw);
        }
        else if (key == "led.autoScaleMaxRpm")
        {
            read_bool(stream, state.ledBar.autoScaleMaxRpm);
        }
        else if (key == "led.maxRpmPerGearEnabled")
        {
            read_bool(stream, state.ledBar.maxRpmPerGearEnabled);
        }
        else if (key == "led.fixedMaxRpm")
        {
            read_number(stream, state.ledBar.fixedMaxRpm);
        }
        else if (key.rfind("led.fixedMaxRpmByGear.", 0) == 0)
        {
            const int gear = std::atoi(key.substr(22).c_str());
            if (gear >= 1 && gear <= static_cast<int>(kSimulatorGearCount))
            {
                read_number(stream, state.ledBar.fixedMaxRpmByGear[static_cast<size_t>(gear - 1)]);
            }
        }
        else if (key.rfind("led.effectiveMaxRpmByGear.", 0) == 0)
        {
            const int gear = std::atoi(key.substr(26).c_str());
            if (gear >= 1 && gear <= static_cast<int>(kSimulatorGearCount))
            {
                read_number(stream, state.ledBar.effectiveMaxRpmByGear[static_cast<size_t>(gear - 1)]);
            }
        }
        else if (key.rfind("led.learnedMaxRpmByGear.", 0) == 0)
        {
            const int gear = std::atoi(key.substr(24).c_str());
            if (gear >= 1 && gear <= static_cast<int>(kSimulatorGearCount))
            {
                read_bool(stream, state.ledBar.learnedMaxRpmByGear[static_cast<size_t>(gear - 1)]);
            }
        }
        else if (key == "led.activeLedCount")
        {
            read_number(stream, state.ledBar.activeLedCount);
        }
        else if (key == "led.brightness")
        {
            read_number(stream, state.ledBar.brightness);
        }
        else if (key == "led.startRpm")
        {
            read_number(stream, state.ledBar.startRpm);
        }
        else if (key == "led.greenEndPct")
        {
            read_number(stream, state.ledBar.greenEndPct);
        }
        else if (key == "led.yellowEndPct")
        {
            read_number(stream, state.ledBar.yellowEndPct);
        }
        else if (key == "led.redEndPct")
        {
            read_number(stream, state.ledBar.redEndPct);
        }
        else if (key == "led.blinkStartPct")
        {
            read_number(stream, state.ledBar.blinkStartPct);
        }
        else if (key == "led.blinkSpeedPct")
        {
            read_number(stream, state.ledBar.blinkSpeedPct);
        }
        else if (key == "led.greenColor")
        {
            read_number(stream, state.ledBar.greenColor);
        }
        else if (key == "led.yellowColor")
        {
            read_number(stream, state.ledBar.yellowColor);
        }
        else if (key == "led.redColor")
        {
            read_number(stream, state.ledBar.redColor);
        }
        else if (key == "led.greenLabel")
        {
            read_string(stream, state.ledBar.greenLabel);
        }
        else if (key == "led.yellowLabel")
        {
            read_string(stream, state.ledBar.yellowLabel);
        }
        else if (key == "led.redLabel")
        {
            read_string(stream, state.ledBar.redLabel);
        }
        else if (key == "side.enabled")
        {
            read_bool(stream, state.sideLeds.enabled);
        }
        else if (key == "side.preset")
        {
            int raw = 0;
            read_number(stream, raw);
            state.sideLeds.preset = side_led_preset_from_int(raw);
        }
        else if (key == "side.ledCountPerSide")
        {
            int raw = 0;
            read_number(stream, raw);
            state.sideLeds.ledCountPerSide = static_cast<uint8_t>(std::clamp(raw,
                                                                              static_cast<int>(SIDE_LED_MIN_COUNT_PER_SIDE),
                                                                              static_cast<int>(SIDE_LED_MAX_COUNT_PER_SIDE)));
        }
        else if (key == "side.brightness")
        {
            int raw = 0;
            read_number(stream, raw);
            state.sideLeds.brightness = static_cast<uint8_t>(std::clamp(raw, 0, 255));
        }
        else if (key == "side.allowSpotter")
        {
            read_bool(stream, state.sideLeds.allowSpotter);
        }
        else if (key == "side.allowFlags")
        {
            read_bool(stream, state.sideLeds.allowFlags);
        }
        else if (key == "side.allowWarnings")
        {
            read_bool(stream, state.sideLeds.allowWarnings);
        }
        else if (key == "side.allowTraction")
        {
            read_bool(stream, state.sideLeds.allowTraction);
        }
        else if (key == "side.blinkSpeedSlowMs")
        {
            read_number(stream, state.sideLeds.blinkSpeedSlowMs);
        }
        else if (key == "side.blinkSpeedFastMs")
        {
            read_number(stream, state.sideLeds.blinkSpeedFastMs);
        }
        else if (key == "side.blueFlagPriority")
        {
            int raw = 0;
            read_number(stream, raw);
            state.sideLeds.blueFlagPriority = static_cast<SideLedPriorityMode>(raw);
        }
        else if (key == "side.yellowFlagPriority")
        {
            int raw = 0;
            read_number(stream, raw);
            state.sideLeds.yellowFlagPriority = static_cast<SideLedPriorityMode>(raw);
        }
        else if (key == "side.warningPriorityMode")
        {
            int raw = 0;
            read_number(stream, raw);
            state.sideLeds.warningPriorityMode = static_cast<SideLedWarningPriorityMode>(raw);
        }
        else if (key == "side.invertLeftRight")
        {
            read_bool(stream, state.sideLeds.invertLeftRight);
        }
        else if (key == "side.mirrorMode")
        {
            read_bool(stream, state.sideLeds.mirrorMode);
        }
        else if (key == "side.closeCarBlinkingEnabled")
        {
            read_bool(stream, state.sideLeds.closeCarBlinkingEnabled);
        }
        else if (key == "side.severityLevelsEnabled")
        {
            read_bool(stream, state.sideLeds.severityLevelsEnabled);
        }
        else if (key == "side.idleAnimationEnabled")
        {
            read_bool(stream, state.sideLeds.idleAnimationEnabled);
        }
        else if (key == "side.testMode")
        {
            read_bool(stream, state.sideLeds.testMode);
        }
        else if (key == "side.minimumFlagHoldMs")
        {
            read_number(stream, state.sideLeds.minimumFlagHoldMs);
        }
        else if (key == "side.minimumWarningHoldMs")
        {
            read_number(stream, state.sideLeds.minimumWarningHoldMs);
        }
        else if (key == "side.minimumSpotterHoldMs")
        {
            read_number(stream, state.sideLeds.minimumSpotterHoldMs);
        }
        else if (key == "side.colors.dim")
        {
            read_number(stream, state.sideLeds.colors.dim);
        }
        else if (key == "side.colors.spotter")
        {
            read_number(stream, state.sideLeds.colors.spotter);
        }
        else if (key == "side.colors.spotterClose")
        {
            read_number(stream, state.sideLeds.colors.spotterClose);
        }
        else if (key == "side.colors.tractionLow")
        {
            read_number(stream, state.sideLeds.colors.tractionLow);
        }
        else if (key == "side.colors.tractionMedium")
        {
            read_number(stream, state.sideLeds.colors.tractionMedium);
        }
        else if (key == "side.colors.tractionHigh")
        {
            read_number(stream, state.sideLeds.colors.tractionHigh);
        }
        else if (key == "side.colors.flagGreen")
        {
            read_number(stream, state.sideLeds.colors.flagGreen);
        }
        else if (key == "side.colors.flagYellow")
        {
            read_number(stream, state.sideLeds.colors.flagYellow);
        }
        else if (key == "side.colors.flagBlue")
        {
            read_number(stream, state.sideLeds.colors.flagBlue);
        }
        else if (key == "side.colors.flagRed")
        {
            read_number(stream, state.sideLeds.colors.flagRed);
        }
        else if (key == "side.colors.flagWhite")
        {
            read_number(stream, state.sideLeds.colors.flagWhite);
        }
        else if (key == "side.colors.flagBlack")
        {
            read_number(stream, state.sideLeds.colors.flagBlack);
        }
        else if (key == "side.colors.flagCheckered")
        {
            read_number(stream, state.sideLeds.colors.flagCheckered);
        }
        else if (key == "side.colors.warningLowFuel")
        {
            read_number(stream, state.sideLeds.colors.warningLowFuel);
        }
        else if (key == "side.colors.warningEngine")
        {
            read_number(stream, state.sideLeds.colors.warningEngine);
        }
        else if (key == "side.colors.warningOil")
        {
            read_number(stream, state.sideLeds.colors.warningOil);
        }
        else if (key == "side.colors.warningWater")
        {
            read_number(stream, state.sideLeds.colors.warningWater);
        }
        else if (key == "side.colors.warningDamage")
        {
            read_number(stream, state.sideLeds.colors.warningDamage);
        }
        else if (key == "side.colors.warningPitLimiter")
        {
            read_number(stream, state.sideLeds.colors.warningPitLimiter);
        }
        else if (key == "device.autoBrightnessEnabled")
        {
            read_bool(stream, state.device.autoBrightnessEnabled);
        }
        else if (key == "device.ambientLightSdaPin")
        {
            read_number(stream, state.device.ambientLightSdaPin);
        }
        else if (key == "device.ambientLightSclPin")
        {
            read_number(stream, state.device.ambientLightSclPin);
        }
        else if (key == "device.autoBrightnessStrengthPct")
        {
            read_number(stream, state.device.autoBrightnessStrengthPct);
        }
        else if (key == "device.autoBrightnessMin")
        {
            read_number(stream, state.device.autoBrightnessMin);
        }
        else if (key == "device.autoBrightnessResponsePct")
        {
            read_number(stream, state.device.autoBrightnessResponsePct);
        }
        else if (key == "device.autoBrightnessLuxMin")
        {
            read_number(stream, state.device.autoBrightnessLuxMin);
        }
        else if (key == "device.autoBrightnessLuxMax")
        {
            read_number(stream, state.device.autoBrightnessLuxMax);
        }
        else if (key == "device.logoOnIgnitionOn")
        {
            read_bool(stream, state.device.logoOnIgnitionOn);
        }
        else if (key == "device.logoOnEngineStart")
        {
            read_bool(stream, state.device.logoOnEngineStart);
        }
        else if (key == "device.logoOnIgnitionOff")
        {
            read_bool(stream, state.device.logoOnIgnitionOff);
        }
        else if (key == "device.simSessionLedEffectsEnabled")
        {
            read_bool(stream, state.device.simSessionLedEffectsEnabled);
        }
        else if (key == "device.gestureControlEnabled")
        {
            read_bool(stream, state.device.gestureControlEnabled);
        }
        else if (key == "device.useMph")
        {
            read_bool(stream, state.device.useMph);
        }
        else if (key == "device.autoReconnect")
        {
            read_bool(stream, state.device.autoReconnect);
        }
        else if (key == "device.wifiModePreference")
        {
            int raw = 0;
            read_number(stream, raw);
            state.device.wifiModePreference = static_cast<UiWifiMode>(raw);
        }
        else if (key == "device.staSsid")
        {
            read_string(stream, state.device.staSsid);
        }
        else if (key == "device.staPassword")
        {
            read_string(stream, state.device.staPassword);
        }
        else if (key == "device.apSsid")
        {
            read_string(stream, state.device.apSsid);
        }
        else if (key == "device.apPassword")
        {
            read_string(stream, state.device.apPassword);
        }
        else if (key == "ble.targetName")
        {
            read_string(stream, state.bleTargetName);
        }
        else if (key == "ble.targetAddress")
        {
            read_string(stream, state.bleTargetAddress);
        }
        else
        {
            std::string ignored;
            std::getline(stream, ignored);
        }
    }

    normalize_side_led_config(state.sideLeds);
    return true;
}

bool save_simulator_persisted_state(const SimulatorPersistedState &state, std::string *errorMessage)
{
    const std::filesystem::path path = simulator_settings_path();
    std::error_code createError;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), createError);
    }
    if (createError)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = createError.message();
        }
        return false;
    }

    std::ofstream stream(path, std::ios::trunc);
    if (!stream.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Unable to write persisted simulator state";
        }
        return false;
    }

    write_int(stream, "version", 4);
    write_int(stream, "settings.displayBrightness", state.settings.displayBrightness);
    write_bool(stream, "settings.tutorialSeen", state.settings.tutorialSeen);
    write_int(stream, "settings.lastMenuIndex", state.settings.lastMenuIndex);
    write_bool(stream, "settings.nightMode", state.settings.nightMode);
    write_bool(stream, "settings.showShiftStrip", state.settings.showShiftStrip);
    write_int(stream, "settings.telemetryPreference", static_cast<int>(state.settings.telemetryPreference));
    write_int(stream, "settings.displayFocus", static_cast<int>(state.settings.displayFocus));

    write_int(stream, "telemetry.mode", static_cast<int>(state.telemetry.mode));
    write_int(stream, "telemetry.simHubTransport", static_cast<int>(state.telemetry.simHubTransport));
    write_uint(stream, "telemetry.udpPort", state.telemetry.udpPort);
    write_uint(stream, "telemetry.httpPort", state.telemetry.httpPort);
    write_uint(stream, "telemetry.pollIntervalMs", state.telemetry.pollIntervalMs);
    write_uint(stream, "telemetry.staleTimeoutMs", state.telemetry.staleTimeoutMs);
    write_bool(stream, "telemetry.debugLogging", state.telemetry.debugLogging);
    write_bool(stream, "telemetry.allowSimulatorFallback", state.telemetry.allowSimulatorFallback);

    write_int(stream, "led.mode", static_cast<int>(state.ledBar.mode));
    write_bool(stream, "led.autoScaleMaxRpm", state.ledBar.autoScaleMaxRpm);
    write_bool(stream, "led.maxRpmPerGearEnabled", state.ledBar.maxRpmPerGearEnabled);
    write_int(stream, "led.fixedMaxRpm", state.ledBar.fixedMaxRpm);
    for (size_t i = 0; i < kSimulatorGearCount; ++i)
    {
        write_int(stream, ("led.fixedMaxRpmByGear." + std::to_string(i + 1)).c_str(), state.ledBar.fixedMaxRpmByGear[i]);
        write_int(stream, ("led.effectiveMaxRpmByGear." + std::to_string(i + 1)).c_str(), state.ledBar.effectiveMaxRpmByGear[i]);
        write_bool(stream, ("led.learnedMaxRpmByGear." + std::to_string(i + 1)).c_str(), state.ledBar.learnedMaxRpmByGear[i]);
    }
    write_int(stream, "led.activeLedCount", state.ledBar.activeLedCount);
    write_int(stream, "led.brightness", state.ledBar.brightness);
    write_int(stream, "led.startRpm", state.ledBar.startRpm);
    write_int(stream, "led.greenEndPct", state.ledBar.greenEndPct);
    write_int(stream, "led.yellowEndPct", state.ledBar.yellowEndPct);
    write_int(stream, "led.redEndPct", state.ledBar.redEndPct);
    write_int(stream, "led.blinkStartPct", state.ledBar.blinkStartPct);
    write_int(stream, "led.blinkSpeedPct", state.ledBar.blinkSpeedPct);
    write_uint(stream, "led.greenColor", state.ledBar.greenColor);
    write_uint(stream, "led.yellowColor", state.ledBar.yellowColor);
    write_uint(stream, "led.redColor", state.ledBar.redColor);
    write_string(stream, "led.greenLabel", state.ledBar.greenLabel);
    write_string(stream, "led.yellowLabel", state.ledBar.yellowLabel);
    write_string(stream, "led.redLabel", state.ledBar.redLabel);

    write_bool(stream, "side.enabled", state.sideLeds.enabled);
    write_int(stream, "side.preset", static_cast<int>(state.sideLeds.preset));
    write_int(stream, "side.ledCountPerSide", state.sideLeds.ledCountPerSide);
    write_int(stream, "side.brightness", state.sideLeds.brightness);
    write_bool(stream, "side.allowSpotter", state.sideLeds.allowSpotter);
    write_bool(stream, "side.allowFlags", state.sideLeds.allowFlags);
    write_bool(stream, "side.allowWarnings", state.sideLeds.allowWarnings);
    write_bool(stream, "side.allowTraction", state.sideLeds.allowTraction);
    write_uint(stream, "side.blinkSpeedSlowMs", state.sideLeds.blinkSpeedSlowMs);
    write_uint(stream, "side.blinkSpeedFastMs", state.sideLeds.blinkSpeedFastMs);
    write_int(stream, "side.blueFlagPriority", static_cast<int>(state.sideLeds.blueFlagPriority));
    write_int(stream, "side.yellowFlagPriority", static_cast<int>(state.sideLeds.yellowFlagPriority));
    write_int(stream, "side.warningPriorityMode", static_cast<int>(state.sideLeds.warningPriorityMode));
    write_bool(stream, "side.invertLeftRight", state.sideLeds.invertLeftRight);
    write_bool(stream, "side.mirrorMode", state.sideLeds.mirrorMode);
    write_bool(stream, "side.closeCarBlinkingEnabled", state.sideLeds.closeCarBlinkingEnabled);
    write_bool(stream, "side.severityLevelsEnabled", state.sideLeds.severityLevelsEnabled);
    write_bool(stream, "side.idleAnimationEnabled", state.sideLeds.idleAnimationEnabled);
    write_bool(stream, "side.testMode", state.sideLeds.testMode);
    write_uint(stream, "side.minimumFlagHoldMs", state.sideLeds.minimumFlagHoldMs);
    write_uint(stream, "side.minimumWarningHoldMs", state.sideLeds.minimumWarningHoldMs);
    write_uint(stream, "side.minimumSpotterHoldMs", state.sideLeds.minimumSpotterHoldMs);
    write_uint(stream, "side.colors.dim", state.sideLeds.colors.dim);
    write_uint(stream, "side.colors.spotter", state.sideLeds.colors.spotter);
    write_uint(stream, "side.colors.spotterClose", state.sideLeds.colors.spotterClose);
    write_uint(stream, "side.colors.tractionLow", state.sideLeds.colors.tractionLow);
    write_uint(stream, "side.colors.tractionMedium", state.sideLeds.colors.tractionMedium);
    write_uint(stream, "side.colors.tractionHigh", state.sideLeds.colors.tractionHigh);
    write_uint(stream, "side.colors.flagGreen", state.sideLeds.colors.flagGreen);
    write_uint(stream, "side.colors.flagYellow", state.sideLeds.colors.flagYellow);
    write_uint(stream, "side.colors.flagBlue", state.sideLeds.colors.flagBlue);
    write_uint(stream, "side.colors.flagRed", state.sideLeds.colors.flagRed);
    write_uint(stream, "side.colors.flagWhite", state.sideLeds.colors.flagWhite);
    write_uint(stream, "side.colors.flagBlack", state.sideLeds.colors.flagBlack);
    write_uint(stream, "side.colors.flagCheckered", state.sideLeds.colors.flagCheckered);
    write_uint(stream, "side.colors.warningLowFuel", state.sideLeds.colors.warningLowFuel);
    write_uint(stream, "side.colors.warningEngine", state.sideLeds.colors.warningEngine);
    write_uint(stream, "side.colors.warningOil", state.sideLeds.colors.warningOil);
    write_uint(stream, "side.colors.warningWater", state.sideLeds.colors.warningWater);
    write_uint(stream, "side.colors.warningDamage", state.sideLeds.colors.warningDamage);
    write_uint(stream, "side.colors.warningPitLimiter", state.sideLeds.colors.warningPitLimiter);

    write_bool(stream, "device.autoBrightnessEnabled", state.device.autoBrightnessEnabled);
    write_int(stream, "device.ambientLightSdaPin", state.device.ambientLightSdaPin);
    write_int(stream, "device.ambientLightSclPin", state.device.ambientLightSclPin);
    write_int(stream, "device.autoBrightnessStrengthPct", state.device.autoBrightnessStrengthPct);
    write_int(stream, "device.autoBrightnessMin", state.device.autoBrightnessMin);
    write_int(stream, "device.autoBrightnessResponsePct", state.device.autoBrightnessResponsePct);
    write_int(stream, "device.autoBrightnessLuxMin", state.device.autoBrightnessLuxMin);
    write_int(stream, "device.autoBrightnessLuxMax", state.device.autoBrightnessLuxMax);
    write_bool(stream, "device.logoOnIgnitionOn", state.device.logoOnIgnitionOn);
    write_bool(stream, "device.logoOnEngineStart", state.device.logoOnEngineStart);
    write_bool(stream, "device.logoOnIgnitionOff", state.device.logoOnIgnitionOff);
    write_bool(stream, "device.simSessionLedEffectsEnabled", state.device.simSessionLedEffectsEnabled);
    write_bool(stream, "device.gestureControlEnabled", state.device.gestureControlEnabled);
    write_bool(stream, "device.useMph", state.device.useMph);
    write_bool(stream, "device.autoReconnect", state.device.autoReconnect);
    write_int(stream, "device.wifiModePreference", static_cast<int>(state.device.wifiModePreference));
    write_string(stream, "device.staSsid", state.device.staSsid);
    write_string(stream, "device.staPassword", state.device.staPassword);
    write_string(stream, "device.apSsid", state.device.apSsid);
    write_string(stream, "device.apPassword", state.device.apPassword);

    write_string(stream, "ble.targetName", state.bleTargetName);
    write_string(stream, "ble.targetAddress", state.bleTargetAddress);

    if (!stream.good())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Unable to flush persisted simulator state";
        }
        return false;
    }

    return true;
}
