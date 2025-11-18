#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <BLEDevice.h>

constexpr int LED_PIN = 5;
constexpr int NUM_LEDS = 30;
constexpr int DEFAULT_BRIGHTNESS = 80;
constexpr int STATUS_LED_PIN = 2;

constexpr unsigned long RPM_INTERVAL_MS = 100;
constexpr unsigned long TEST_SWEEP_DURATION = 5000;
constexpr unsigned long IGNITION_TIMEOUT_MS = 8000;
constexpr unsigned long LOGO_COOLDOWN_MS = 2000;
constexpr int ENGINE_START_RPM_THRESHOLD = 400;
constexpr unsigned long TX_LOG_INTERVAL_MS = 2500;

struct ShiftConfig
{
    bool autoScaleMaxRpm;
    int fixedMaxRpm;
    int greenEndPct;
    int yellowEndPct;
    int blinkStartPct;
    int brightness;
    int mode;
    bool logoOnIgnitionOn;
    bool logoOnEngineStart;
    bool logoOnIgnitionOff;
};

extern ShiftConfig cfg;

extern BLEUUID SERVICE_UUID;
extern BLEUUID CHAR_UUID_NOTIFY;
extern BLEUUID CHAR_UUID_WRITE;
extern const char *TARGET_ADDR;

extern const char *AP_SSID;
extern const char *AP_PASS;

void initConfig();

#endif // CONFIG_H
