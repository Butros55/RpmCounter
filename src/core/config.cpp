#include "config.h"
#include "utils.h"
#include <Preferences.h>

namespace
{
    constexpr const char *PREF_NAMESPACE = "rpm_cfg";

    WifiMode clampWifiMode(uint32_t raw)
    {
        if (raw > static_cast<uint32_t>(STA_WITH_AP_FALLBACK))
        {
            return AP_ONLY;
        }
        return static_cast<WifiMode>(raw);
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
    cfg.greenEndPct = 60;
    cfg.yellowEndPct = 85;
    cfg.blinkStartPct = 90;
    cfg.brightness = DEFAULT_BRIGHTNESS;
    cfg.mode = 1;
    cfg.logoOnIgnitionOn = true;
    cfg.logoOnEngineStart = true;
    cfg.logoOnIgnitionOff = true;
    cfg.greenColor = {0, 255, 0};
    cfg.yellowColor = {255, 180, 0};
    cfg.redColor = {255, 0, 0};
    cfg.greenLabel = "Green";
    cfg.yellowLabel = "Yellow";
    cfg.redLabel = "Red";

    cfg.wifiMode = AP_ONLY;
    cfg.staSsid = "";
    cfg.staPassword = "";
    cfg.apSsid = AP_SSID;
    cfg.apPassword = AP_PASS;
}

void loadConfig()
{
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, true))
    {
        return;
    }

    cfg.autoScaleMaxRpm = prefs.getBool("autoscale", cfg.autoScaleMaxRpm);
    cfg.fixedMaxRpm = clampInt(prefs.getInt("fixedMax", cfg.fixedMaxRpm), 1000, 8000);
    cfg.greenEndPct = clampInt(prefs.getInt("greenEnd", cfg.greenEndPct), 0, 100);
    cfg.yellowEndPct = clampInt(prefs.getInt("yellowEnd", cfg.yellowEndPct), 0, 100);
    cfg.blinkStartPct = clampInt(prefs.getInt("blinkStart", cfg.blinkStartPct), 0, 100);
    if (cfg.yellowEndPct < cfg.greenEndPct)
        cfg.yellowEndPct = cfg.greenEndPct;
    if (cfg.blinkStartPct < cfg.yellowEndPct)
        cfg.blinkStartPct = cfg.yellowEndPct;
    cfg.brightness = clampInt(prefs.getInt("brightness", cfg.brightness), 0, 255);
    cfg.mode = clampInt(prefs.getInt("mode", cfg.mode), 0, 2);

    cfg.logoOnIgnitionOn = prefs.getBool("logoIgnOn", cfg.logoOnIgnitionOn);
    cfg.logoOnEngineStart = prefs.getBool("logoEngStart", cfg.logoOnEngineStart);
    cfg.logoOnIgnitionOff = prefs.getBool("logoIgnOff", cfg.logoOnIgnitionOff);

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
    prefs.putInt("greenEnd", cfg.greenEndPct);
    prefs.putInt("yellowEnd", cfg.yellowEndPct);
    prefs.putInt("blinkStart", cfg.blinkStartPct);
    prefs.putInt("brightness", cfg.brightness);
    prefs.putInt("mode", cfg.mode);

    prefs.putBool("logoIgnOn", cfg.logoOnIgnitionOn);
    prefs.putBool("logoEngStart", cfg.logoOnEngineStart);
    prefs.putBool("logoIgnOff", cfg.logoOnIgnitionOff);

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
