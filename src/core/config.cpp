#include "config.h"
#include "utils.h"
#include <Preferences.h>

namespace
{
    constexpr const char *PREF_NAMESPACE = "rpm_cfg";
    constexpr RgbColor DEFAULT_GREEN_COLOR = {0, 255, 0};
    constexpr RgbColor DEFAULT_YELLOW_COLOR = {255, 180, 0};
    constexpr RgbColor DEFAULT_RED_COLOR = {255, 0, 0};
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    constexpr int LEGACY_AMBIENT_SDA_PIN = 21;
    constexpr int LEGACY_AMBIENT_SCL_PIN = 22;
#endif
    bool g_redColorFallbackActive = false;

    WifiMode clampWifiMode(uint32_t raw)
    {
        if (raw > static_cast<uint32_t>(STA_WITH_AP_FALLBACK))
        {
            return AP_ONLY;
        }
        return static_cast<WifiMode>(raw);
    }

    TelemetryPreference clampTelemetryPreference(uint32_t raw)
    {
        if (raw > static_cast<uint32_t>(TelemetryPreference::SimHub))
        {
            return TelemetryPreference::Auto;
        }
        return static_cast<TelemetryPreference>(raw);
    }

    SimTransportPreference clampSimTransportPreference(uint32_t raw)
    {
        if (raw > static_cast<uint32_t>(SimTransportPreference::Network))
        {
            return SimTransportPreference::Auto;
        }
        return static_cast<SimTransportPreference>(raw);
    }

    DisplayFocusMetric clampDisplayFocusMetric(uint32_t raw)
    {
        if (raw > static_cast<uint32_t>(DisplayFocusMetric::Speed))
        {
            return DisplayFocusMetric::Rpm;
        }
        return static_cast<DisplayFocusMetric>(raw);
    }

    SideLedPriorityMode clampSideLedPriorityMode(uint32_t raw)
    {
        if (raw > static_cast<uint32_t>(SideLedPriorityMode::Override))
        {
            return SideLedPriorityMode::Normal;
        }
        return static_cast<SideLedPriorityMode>(raw);
    }

    SideLedWarningPriorityMode clampSideLedWarningPriorityMode(uint32_t raw)
    {
        if (raw > static_cast<uint32_t>(SideLedWarningPriorityMode::AlwaysOverride))
        {
            return SideLedWarningPriorityMode::Normal;
        }
        return static_cast<SideLedWarningPriorityMode>(raw);
    }

    bool shouldMigrateLegacyAmbientPins(int sdaPin, int sclPin)
    {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        return sdaPin == LEGACY_AMBIENT_SDA_PIN && sclPin == LEGACY_AMBIENT_SCL_PIN;
#else
        (void)sdaPin;
        (void)sclPin;
        return false;
#endif
    }

    bool colorLooksUnset(const RgbColor &color)
    {
        return color.r == 0 && color.g == 0 && color.b == 0;
    }

    String sanitizeLabelValue(const String &value, const char *fallback)
    {
        String sanitized = value;
        sanitized.trim();
        if (sanitized.isEmpty())
        {
            sanitized = fallback;
        }
        return sanitized;
    }

    void sanitizeLoadedConfig()
    {
        if (cfg.yellowEndPct < cfg.greenEndPct)
        {
            cfg.yellowEndPct = cfg.greenEndPct;
        }
        if (cfg.redEndPct < cfg.yellowEndPct)
        {
            cfg.redEndPct = cfg.yellowEndPct;
        }
        if (cfg.blinkStartPct < cfg.redEndPct)
        {
            cfg.blinkStartPct = cfg.redEndPct;
        }

        g_redColorFallbackActive = colorLooksUnset(cfg.redColor);
        if (g_redColorFallbackActive)
        {
            cfg.redColor = DEFAULT_RED_COLOR;
        }

        cfg.greenLabel = sanitizeLabelValue(cfg.greenLabel, "Green");
        cfg.yellowLabel = sanitizeLabelValue(cfg.yellowLabel, "Yellow");
        cfg.redLabel = sanitizeLabelValue(cfg.redLabel, "Red");
        normalize_side_led_config(cfg.sideLeds);
    }
}

AppConfig cfg;

BLEUUID SERVICE_UUID("0000fff0-0000-1000-8000-00805f9b34fb");
BLEUUID CHAR_UUID_NOTIFY("0000fff1-0000-1000-8000-00805f9b34fb");
BLEUUID CHAR_UUID_WRITE("0000fff2-0000-1000-8000-00805f9b34fb");
const char *TARGET_ADDR = "66:1e:32:9d:2e:5d";

const char *AP_SSID = "ShiftLight";
const char *AP_PASS = "shift1234";

void initConfig()
{
    cfg.autoScaleMaxRpm = true;
    cfg.fixedMaxRpm = 5000;
    cfg.rpmStartRpm = 1000;
    cfg.activeLedCount = NUM_LEDS;
    cfg.greenEndPct = 60;
    cfg.yellowEndPct = 85;
    cfg.redEndPct = 90;
    cfg.blinkStartPct = 90;
    cfg.blinkSpeedPct = 80;
    cfg.brightness = DEFAULT_BRIGHTNESS;
    cfg.autoBrightnessEnabled = false;
    cfg.ambientLightSdaPin = AMBIENT_LIGHT_DEFAULT_SDA;
    cfg.ambientLightSclPin = AMBIENT_LIGHT_DEFAULT_SCL;
    cfg.autoBrightnessStrengthPct = 100;
    cfg.autoBrightnessMin = 18;
    cfg.autoBrightnessResponsePct = 35;
    cfg.autoBrightnessLuxMin = 2;
    cfg.autoBrightnessLuxMax = 5000;
    cfg.mode = 1;
    cfg.logoOnIgnitionOn = true;
    cfg.logoOnEngineStart = true;
    cfg.logoOnIgnitionOff = true;
    cfg.simSessionLedEffectsEnabled = false;
    cfg.gestureControlEnabled = true;
    cfg.displayBrightness = 220;
    cfg.uiTutorialSeen = false;
    cfg.uiLastMenuIndex = 0;
    cfg.uiNightMode = true;
    cfg.uiShowShiftStrip = true;
    cfg.uiDisplayFocus = DisplayFocusMetric::Rpm;
    cfg.sideLeds = side_led_config_for_preset(SideLedPreset::Gt3);
    cfg.useMph = false;
    cfg.telemetryPreference = TelemetryPreference::Auto;
    cfg.simTransportPreference = SimTransportPreference::Auto;
    cfg.simHubHost = "";
    cfg.simHubPort = 8888;
    cfg.simHubPollMs = 75;
    cfg.greenColor = DEFAULT_GREEN_COLOR;
    cfg.yellowColor = DEFAULT_YELLOW_COLOR;
    cfg.redColor = DEFAULT_RED_COLOR;
    cfg.greenLabel = "Green";
    cfg.yellowLabel = "Yellow";
    cfg.redLabel = "Red";

    cfg.wifiMode = STA_WITH_AP_FALLBACK;
    cfg.staSsid = "Larry-LAN";
    cfg.staPassword = "R4di0C0ntroll3d";
    cfg.apSsid = AP_SSID;
    cfg.apPassword = AP_PASS;
}

void loadConfig()
{
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, true))
    {
        // Namespace missing yet – open writable once to create it so future reads don't fail
        if (prefs.begin(PREF_NAMESPACE, false))
        {
            prefs.end();
            prefs.begin(PREF_NAMESPACE, true);
        }
        else
        {
            return;
        }
    }

    cfg.autoScaleMaxRpm = prefs.getBool("autoscale", cfg.autoScaleMaxRpm);
    cfg.fixedMaxRpm = clampInt(prefs.getInt("fixedMax", cfg.fixedMaxRpm), 1000, 8000);
    cfg.rpmStartRpm = clampInt(prefs.getInt("rpmStart", cfg.rpmStartRpm), 0, 12000);
    cfg.activeLedCount = clampInt(prefs.getInt("ledCount", cfg.activeLedCount), 1, NUM_LEDS);
    cfg.greenEndPct = clampInt(prefs.getInt("greenEnd", cfg.greenEndPct), 0, 100);
    cfg.yellowEndPct = clampInt(prefs.getInt("yellowEnd", cfg.yellowEndPct), 0, 100);
    cfg.redEndPct = clampInt(prefs.getInt("redEnd", cfg.redEndPct), 0, 100);
    cfg.blinkStartPct = clampInt(prefs.getInt("blinkStart", cfg.blinkStartPct), 0, 100);
    cfg.blinkSpeedPct = clampInt(prefs.getInt("blinkSpeed", cfg.blinkSpeedPct), 0, 100);
    cfg.brightness = clampInt(prefs.getInt("brightness", cfg.brightness), 0, 255);
    cfg.autoBrightnessEnabled = prefs.getBool("autoBright", cfg.autoBrightnessEnabled);
    cfg.ambientLightSdaPin = clampInt(prefs.getInt("ambSda", cfg.ambientLightSdaPin), 0, 48);
    cfg.ambientLightSclPin = clampInt(prefs.getInt("ambScl", cfg.ambientLightSclPin), 0, 48);
    if (shouldMigrateLegacyAmbientPins(cfg.ambientLightSdaPin, cfg.ambientLightSclPin))
    {
        cfg.ambientLightSdaPin = AMBIENT_LIGHT_DEFAULT_SDA;
        cfg.ambientLightSclPin = AMBIENT_LIGHT_DEFAULT_SCL;
    }
    cfg.autoBrightnessStrengthPct = clampInt(prefs.getInt("autoBrPct", cfg.autoBrightnessStrengthPct), 25, 200);
    cfg.autoBrightnessMin = clampInt(prefs.getInt("autoBrMin", cfg.autoBrightnessMin), 0, 255);
    cfg.autoBrightnessResponsePct = clampInt(prefs.getInt("autoResp", cfg.autoBrightnessResponsePct), 1, 100);
    cfg.autoBrightnessLuxMin = clampInt(prefs.getInt("autoLuxMin", cfg.autoBrightnessLuxMin), 0, 120000);
    cfg.autoBrightnessLuxMax =
        clampInt(prefs.getInt("autoLuxMax", cfg.autoBrightnessLuxMax), cfg.autoBrightnessLuxMin + 1, 120000);
    if (cfg.autoBrightnessMin > cfg.brightness)
        cfg.autoBrightnessMin = cfg.brightness;
    cfg.mode = clampInt(prefs.getInt("mode", cfg.mode), 0, 3);
    cfg.displayBrightness = clampInt(prefs.getInt("dispBright", cfg.displayBrightness), 10, 255);
    cfg.uiTutorialSeen = prefs.getBool("uiTutSeen", cfg.uiTutorialSeen);
    cfg.uiLastMenuIndex = clampInt(prefs.getInt("uiMenuIdx", cfg.uiLastMenuIndex), 0, 5);
    cfg.uiNightMode = prefs.getBool("uiNight", cfg.uiNightMode);
    cfg.uiShowShiftStrip = prefs.getBool("uiShiftLed", cfg.uiShowShiftStrip);
    cfg.uiDisplayFocus = clampDisplayFocusMetric(prefs.getUInt("uiFocus", static_cast<uint32_t>(cfg.uiDisplayFocus)));
    cfg.sideLeds.enabled = prefs.getBool("sideEn", cfg.sideLeds.enabled);
    cfg.sideLeds.preset = side_led_preset_from_int(static_cast<int>(prefs.getUInt("sidePreset", static_cast<uint32_t>(cfg.sideLeds.preset))));
    cfg.sideLeds.ledCountPerSide = static_cast<uint8_t>(clampInt(prefs.getUInt("sideCount", cfg.sideLeds.ledCountPerSide),
                                                                  static_cast<int>(SIDE_LED_MIN_COUNT_PER_SIDE),
                                                                  static_cast<int>(SIDE_LED_MAX_COUNT_PER_SIDE)));
    cfg.sideLeds.brightness = static_cast<uint8_t>(clampInt(prefs.getUInt("sideBright", cfg.sideLeds.brightness), 0, 255));
    cfg.sideLeds.allowSpotter = prefs.getBool("sideSpot", cfg.sideLeds.allowSpotter);
    cfg.sideLeds.allowFlags = prefs.getBool("sideFlag", cfg.sideLeds.allowFlags);
    cfg.sideLeds.allowWarnings = prefs.getBool("sideWarn", cfg.sideLeds.allowWarnings);
    cfg.sideLeds.allowTraction = prefs.getBool("sideTract", cfg.sideLeds.allowTraction);
    cfg.sideLeds.blinkSpeedSlowMs = static_cast<uint16_t>(clampInt(prefs.getUInt("sideBlinkS", cfg.sideLeds.blinkSpeedSlowMs), 80, 1500));
    cfg.sideLeds.blinkSpeedFastMs = static_cast<uint16_t>(clampInt(prefs.getUInt("sideBlinkF", cfg.sideLeds.blinkSpeedFastMs), 40, 900));
    cfg.sideLeds.blueFlagPriority = clampSideLedPriorityMode(prefs.getUInt("sideBluePr", static_cast<uint32_t>(cfg.sideLeds.blueFlagPriority)));
    cfg.sideLeds.yellowFlagPriority = clampSideLedPriorityMode(prefs.getUInt("sideYellPr", static_cast<uint32_t>(cfg.sideLeds.yellowFlagPriority)));
    cfg.sideLeds.warningPriorityMode = clampSideLedWarningPriorityMode(prefs.getUInt("sideWarnPr", static_cast<uint32_t>(cfg.sideLeds.warningPriorityMode)));
    cfg.sideLeds.invertLeftRight = prefs.getBool("sideInvert", cfg.sideLeds.invertLeftRight);
    cfg.sideLeds.mirrorMode = prefs.getBool("sideMirror", cfg.sideLeds.mirrorMode);
    cfg.sideLeds.closeCarBlinkingEnabled = prefs.getBool("sideClose", cfg.sideLeds.closeCarBlinkingEnabled);
    cfg.sideLeds.severityLevelsEnabled = prefs.getBool("sideSev", cfg.sideLeds.severityLevelsEnabled);
    cfg.sideLeds.idleAnimationEnabled = prefs.getBool("sideIdle", cfg.sideLeds.idleAnimationEnabled);
    cfg.sideLeds.testMode = prefs.getBool("sideTest", cfg.sideLeds.testMode);
    cfg.sideLeds.minimumFlagHoldMs = static_cast<uint16_t>(clampInt(prefs.getUInt("sideHoldF", cfg.sideLeds.minimumFlagHoldMs), 0, 2000));
    cfg.sideLeds.minimumWarningHoldMs = static_cast<uint16_t>(clampInt(prefs.getUInt("sideHoldW", cfg.sideLeds.minimumWarningHoldMs), 0, 2000));
    cfg.sideLeds.minimumSpotterHoldMs = static_cast<uint16_t>(clampInt(prefs.getUInt("sideHoldS", cfg.sideLeds.minimumSpotterHoldMs), 0, 2000));
    cfg.sideLeds.colors.dim = prefs.getUInt("sideColDim", cfg.sideLeds.colors.dim);
    cfg.sideLeds.colors.spotter = prefs.getUInt("sideColSpot", cfg.sideLeds.colors.spotter);
    cfg.sideLeds.colors.spotterClose = prefs.getUInt("sideColSpotC", cfg.sideLeds.colors.spotterClose);
    cfg.sideLeds.colors.tractionLow = prefs.getUInt("sideColTrL", cfg.sideLeds.colors.tractionLow);
    cfg.sideLeds.colors.tractionMedium = prefs.getUInt("sideColTrM", cfg.sideLeds.colors.tractionMedium);
    cfg.sideLeds.colors.tractionHigh = prefs.getUInt("sideColTrH", cfg.sideLeds.colors.tractionHigh);
    cfg.sideLeds.colors.flagGreen = prefs.getUInt("sideColFG", cfg.sideLeds.colors.flagGreen);
    cfg.sideLeds.colors.flagYellow = prefs.getUInt("sideColFY", cfg.sideLeds.colors.flagYellow);
    cfg.sideLeds.colors.flagBlue = prefs.getUInt("sideColFB", cfg.sideLeds.colors.flagBlue);
    cfg.sideLeds.colors.flagRed = prefs.getUInt("sideColFR", cfg.sideLeds.colors.flagRed);
    cfg.sideLeds.colors.flagWhite = prefs.getUInt("sideColFW", cfg.sideLeds.colors.flagWhite);
    cfg.sideLeds.colors.flagBlack = prefs.getUInt("sideColFK", cfg.sideLeds.colors.flagBlack);
    cfg.sideLeds.colors.flagCheckered = prefs.getUInt("sideColFC", cfg.sideLeds.colors.flagCheckered);
    cfg.sideLeds.colors.warningLowFuel = prefs.getUInt("sideColWL", cfg.sideLeds.colors.warningLowFuel);
    cfg.sideLeds.colors.warningEngine = prefs.getUInt("sideColWE", cfg.sideLeds.colors.warningEngine);
    cfg.sideLeds.colors.warningOil = prefs.getUInt("sideColWO", cfg.sideLeds.colors.warningOil);
    cfg.sideLeds.colors.warningWater = prefs.getUInt("sideColWW", cfg.sideLeds.colors.warningWater);
    cfg.sideLeds.colors.warningDamage = prefs.getUInt("sideColWD", cfg.sideLeds.colors.warningDamage);
    cfg.sideLeds.colors.warningPitLimiter = prefs.getUInt("sideColWP", cfg.sideLeds.colors.warningPitLimiter);
    cfg.useMph = prefs.getBool("useMph", cfg.useMph);
    cfg.telemetryPreference = clampTelemetryPreference(prefs.getUInt("telePref", static_cast<uint32_t>(cfg.telemetryPreference)));
    cfg.simTransportPreference = clampSimTransportPreference(prefs.getUInt("simTrans", static_cast<uint32_t>(cfg.simTransportPreference)));
    cfg.simHubHost = prefs.getString("simHost", cfg.simHubHost);
    cfg.simHubPort = static_cast<uint16_t>(clampInt(prefs.getUInt("simPort", cfg.simHubPort), 1, 65535));
    cfg.simHubPollMs = static_cast<uint16_t>(clampInt(prefs.getUInt("simPoll", cfg.simHubPollMs), 25, 1000));

    cfg.logoOnIgnitionOn = prefs.getBool("logoIgnOn", cfg.logoOnIgnitionOn);
    cfg.logoOnEngineStart = prefs.getBool("logoEngStart", cfg.logoOnEngineStart);
    cfg.logoOnIgnitionOff = prefs.getBool("logoIgnOff", cfg.logoOnIgnitionOff);
    cfg.simSessionLedEffectsEnabled = prefs.getBool("simFxLed", cfg.simSessionLedEffectsEnabled);
    cfg.gestureControlEnabled = prefs.getBool("gestureCtl", cfg.gestureControlEnabled);

    cfg.greenColor.r = prefs.getUChar("gR", cfg.greenColor.r);
    cfg.greenColor.g = prefs.getUChar("gG", cfg.greenColor.g);
    cfg.greenColor.b = prefs.getUChar("gB", cfg.greenColor.b);

    cfg.yellowColor.r = prefs.getUChar("yR", cfg.yellowColor.r);
    cfg.yellowColor.g = prefs.getUChar("yG", cfg.yellowColor.g);
    cfg.yellowColor.b = prefs.getUChar("yB", cfg.yellowColor.b);

    cfg.redColor.r = prefs.getUChar("rR", cfg.redColor.r);
    cfg.redColor.g = prefs.getUChar("rG", cfg.redColor.g);
    cfg.redColor.b = prefs.getUChar("rB", cfg.redColor.b);

    cfg.greenLabel = prefs.getString("gLabel", cfg.greenLabel);
    cfg.yellowLabel = prefs.getString("yLabel", cfg.yellowLabel);
    cfg.redLabel = prefs.getString("rLabel", cfg.redLabel);

    cfg.wifiMode = clampWifiMode(prefs.getUInt("wifiMode", static_cast<uint32_t>(cfg.wifiMode)));
    cfg.staSsid = prefs.getString("staSsid", cfg.staSsid);
    cfg.staPassword = prefs.getString("staPass", cfg.staPassword);
    cfg.apSsid = prefs.getString("apSsid", cfg.apSsid);
    cfg.apPassword = prefs.getString("apPass", cfg.apPassword);

    prefs.end();
    sanitizeLoadedConfig();
}

void saveConfig()
{
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, false))
    {
        return;
    }

    prefs.putBool("autoscale", cfg.autoScaleMaxRpm);
    prefs.putInt("fixedMax", cfg.fixedMaxRpm);
    prefs.putInt("rpmStart", cfg.rpmStartRpm);
    prefs.putInt("ledCount", cfg.activeLedCount);
    prefs.putInt("greenEnd", cfg.greenEndPct);
    prefs.putInt("yellowEnd", cfg.yellowEndPct);
    prefs.putInt("redEnd", cfg.redEndPct);
    prefs.putInt("blinkStart", cfg.blinkStartPct);
    prefs.putInt("blinkSpeed", cfg.blinkSpeedPct);
    prefs.putInt("brightness", cfg.brightness);
    prefs.putBool("autoBright", cfg.autoBrightnessEnabled);
    prefs.putInt("ambSda", cfg.ambientLightSdaPin);
    prefs.putInt("ambScl", cfg.ambientLightSclPin);
    prefs.putInt("autoBrPct", cfg.autoBrightnessStrengthPct);
    prefs.putInt("autoBrMin", cfg.autoBrightnessMin);
    prefs.putInt("autoResp", cfg.autoBrightnessResponsePct);
    prefs.putInt("autoLuxMin", cfg.autoBrightnessLuxMin);
    prefs.putInt("autoLuxMax", cfg.autoBrightnessLuxMax);
    prefs.putInt("mode", cfg.mode);
    prefs.putInt("dispBright", cfg.displayBrightness);
    prefs.putBool("uiTutSeen", cfg.uiTutorialSeen);
    prefs.putInt("uiMenuIdx", cfg.uiLastMenuIndex);
    prefs.putBool("uiNight", cfg.uiNightMode);
    prefs.putBool("uiShiftLed", cfg.uiShowShiftStrip);
    prefs.putUInt("uiFocus", static_cast<uint32_t>(cfg.uiDisplayFocus));
    prefs.putBool("sideEn", cfg.sideLeds.enabled);
    prefs.putUInt("sidePreset", static_cast<uint32_t>(cfg.sideLeds.preset));
    prefs.putUInt("sideCount", cfg.sideLeds.ledCountPerSide);
    prefs.putUInt("sideBright", cfg.sideLeds.brightness);
    prefs.putBool("sideSpot", cfg.sideLeds.allowSpotter);
    prefs.putBool("sideFlag", cfg.sideLeds.allowFlags);
    prefs.putBool("sideWarn", cfg.sideLeds.allowWarnings);
    prefs.putBool("sideTract", cfg.sideLeds.allowTraction);
    prefs.putUInt("sideBlinkS", cfg.sideLeds.blinkSpeedSlowMs);
    prefs.putUInt("sideBlinkF", cfg.sideLeds.blinkSpeedFastMs);
    prefs.putUInt("sideBluePr", static_cast<uint32_t>(cfg.sideLeds.blueFlagPriority));
    prefs.putUInt("sideYellPr", static_cast<uint32_t>(cfg.sideLeds.yellowFlagPriority));
    prefs.putUInt("sideWarnPr", static_cast<uint32_t>(cfg.sideLeds.warningPriorityMode));
    prefs.putBool("sideInvert", cfg.sideLeds.invertLeftRight);
    prefs.putBool("sideMirror", cfg.sideLeds.mirrorMode);
    prefs.putBool("sideClose", cfg.sideLeds.closeCarBlinkingEnabled);
    prefs.putBool("sideSev", cfg.sideLeds.severityLevelsEnabled);
    prefs.putBool("sideIdle", cfg.sideLeds.idleAnimationEnabled);
    prefs.putBool("sideTest", cfg.sideLeds.testMode);
    prefs.putUInt("sideHoldF", cfg.sideLeds.minimumFlagHoldMs);
    prefs.putUInt("sideHoldW", cfg.sideLeds.minimumWarningHoldMs);
    prefs.putUInt("sideHoldS", cfg.sideLeds.minimumSpotterHoldMs);
    prefs.putUInt("sideColDim", cfg.sideLeds.colors.dim);
    prefs.putUInt("sideColSpot", cfg.sideLeds.colors.spotter);
    prefs.putUInt("sideColSpotC", cfg.sideLeds.colors.spotterClose);
    prefs.putUInt("sideColTrL", cfg.sideLeds.colors.tractionLow);
    prefs.putUInt("sideColTrM", cfg.sideLeds.colors.tractionMedium);
    prefs.putUInt("sideColTrH", cfg.sideLeds.colors.tractionHigh);
    prefs.putUInt("sideColFG", cfg.sideLeds.colors.flagGreen);
    prefs.putUInt("sideColFY", cfg.sideLeds.colors.flagYellow);
    prefs.putUInt("sideColFB", cfg.sideLeds.colors.flagBlue);
    prefs.putUInt("sideColFR", cfg.sideLeds.colors.flagRed);
    prefs.putUInt("sideColFW", cfg.sideLeds.colors.flagWhite);
    prefs.putUInt("sideColFK", cfg.sideLeds.colors.flagBlack);
    prefs.putUInt("sideColFC", cfg.sideLeds.colors.flagCheckered);
    prefs.putUInt("sideColWL", cfg.sideLeds.colors.warningLowFuel);
    prefs.putUInt("sideColWE", cfg.sideLeds.colors.warningEngine);
    prefs.putUInt("sideColWO", cfg.sideLeds.colors.warningOil);
    prefs.putUInt("sideColWW", cfg.sideLeds.colors.warningWater);
    prefs.putUInt("sideColWD", cfg.sideLeds.colors.warningDamage);
    prefs.putUInt("sideColWP", cfg.sideLeds.colors.warningPitLimiter);
    prefs.putBool("useMph", cfg.useMph);
    prefs.putUInt("telePref", static_cast<uint32_t>(cfg.telemetryPreference));
    prefs.putUInt("simTrans", static_cast<uint32_t>(cfg.simTransportPreference));
    prefs.putString("simHost", cfg.simHubHost);
    prefs.putUInt("simPort", cfg.simHubPort);
    prefs.putUInt("simPoll", cfg.simHubPollMs);

    prefs.putBool("logoIgnOn", cfg.logoOnIgnitionOn);
    prefs.putBool("logoEngStart", cfg.logoOnEngineStart);
    prefs.putBool("logoIgnOff", cfg.logoOnIgnitionOff);
    prefs.putBool("simFxLed", cfg.simSessionLedEffectsEnabled);
    prefs.putBool("gestureCtl", cfg.gestureControlEnabled);

    prefs.putUChar("gR", cfg.greenColor.r);
    prefs.putUChar("gG", cfg.greenColor.g);
    prefs.putUChar("gB", cfg.greenColor.b);

    prefs.putUChar("yR", cfg.yellowColor.r);
    prefs.putUChar("yG", cfg.yellowColor.g);
    prefs.putUChar("yB", cfg.yellowColor.b);

    prefs.putUChar("rR", cfg.redColor.r);
    prefs.putUChar("rG", cfg.redColor.g);
    prefs.putUChar("rB", cfg.redColor.b);

    prefs.putString("gLabel", cfg.greenLabel);
    prefs.putString("yLabel", cfg.yellowLabel);
    prefs.putString("rLabel", cfg.redLabel);

    prefs.putUInt("wifiMode", static_cast<uint32_t>(cfg.wifiMode));
    prefs.putString("staSsid", cfg.staSsid);
    prefs.putString("staPass", cfg.staPassword);
    prefs.putString("apSsid", cfg.apSsid);
    prefs.putString("apPass", cfg.apPassword);

    prefs.end();
}

RgbColor effectiveRedColor()
{
    return redColorFallbackActive() ? DEFAULT_RED_COLOR : cfg.redColor;
}

bool redColorFallbackActive()
{
    return g_redColorFallbackActive || colorLooksUnset(cfg.redColor);
}
