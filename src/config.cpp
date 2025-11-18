#include "config.h"

ShiftConfig cfg;

BLEUUID SERVICE_UUID("0000fff0-0000-1000-8000-00805f9b34fb");
BLEUUID CHAR_UUID_NOTIFY("0000fff1-0000-1000-8000-00805f9b34fb");
BLEUUID CHAR_UUID_WRITE("0000fff2-0000-1000-8000-00805f9b34fb");
const char *TARGET_ADDR = "66:1e:32:9d:2e:5d";

const char *AP_SSID = "ShiftLight-ESP32";
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
}
