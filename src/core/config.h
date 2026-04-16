#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <BLEDevice.h>

constexpr int LED_PIN = 5;
constexpr int NUM_LEDS = 30;
constexpr int DEFAULT_BRIGHTNESS = 80;
constexpr int STATUS_LED_PIN = 2;

#ifndef AMBIENT_LIGHT_DEFAULT_SDA
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define AMBIENT_LIGHT_DEFAULT_SDA 47
#else
#define AMBIENT_LIGHT_DEFAULT_SDA 21
#endif
#endif

#ifndef AMBIENT_LIGHT_DEFAULT_SCL
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define AMBIENT_LIGHT_DEFAULT_SCL 48
#else
#define AMBIENT_LIGHT_DEFAULT_SCL 22
#endif
#endif

constexpr unsigned long RPM_INTERVAL_MS = 100;
constexpr unsigned long TEST_SWEEP_DURATION = 5000;
constexpr unsigned long IGNITION_TIMEOUT_MS = 8000;
constexpr unsigned long LOGO_COOLDOWN_MS = 2000;
constexpr int ENGINE_START_RPM_THRESHOLD = 400;
constexpr unsigned long TX_LOG_INTERVAL_MS = 2500;

struct RgbColor
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

enum WifiMode
{
    AP_ONLY = 0,
    STA_ONLY = 1,
    STA_WITH_AP_FALLBACK = 2
};

enum class TelemetryPreference : uint8_t
{
    Auto = 0,
    Obd = 1,
    SimHub = 2
};

enum class SimTransportPreference : uint8_t
{
    Auto = 0,
    UsbSerial = 1,
    Network = 2
};

enum class DisplayFocusMetric : uint8_t
{
    Rpm = 0,
    Gear = 1,
    Speed = 2
};

struct AppConfig
{
    bool autoScaleMaxRpm;
    int fixedMaxRpm;
    int rpmStartRpm;
    int activeLedCount;
    int greenEndPct;
    int yellowEndPct;
    int redEndPct;
    int blinkStartPct;
    int brightness;
    bool autoBrightnessEnabled;
    int ambientLightSdaPin;
    int ambientLightSclPin;
    int autoBrightnessStrengthPct;
    int autoBrightnessMin;
    int autoBrightnessResponsePct;
    int autoBrightnessLuxMin;
    int autoBrightnessLuxMax;
    int mode;
    bool logoOnIgnitionOn;
    bool logoOnEngineStart;
    bool logoOnIgnitionOff;
    bool simSessionLedEffectsEnabled;
    bool gestureControlEnabled;
    int displayBrightness;
    bool uiTutorialSeen;
    int uiLastMenuIndex;
    bool uiNightMode;
    DisplayFocusMetric uiDisplayFocus;
    bool useMph;
    TelemetryPreference telemetryPreference;
    SimTransportPreference simTransportPreference;
    String simHubHost;
    uint16_t simHubPort;
    uint16_t simHubPollMs;
    RgbColor greenColor;
    RgbColor yellowColor;
    RgbColor redColor;
    String greenLabel;
    String yellowLabel;
    String redLabel;

    WifiMode wifiMode;
    String staSsid;
    String staPassword;
    String apSsid;
    String apPassword;
};

constexpr int MANUAL_CONNECT_RETRY_COUNT = 1;
constexpr unsigned long MANUAL_CONNECT_RETRY_DELAY_MS = 600;
constexpr unsigned long AUTO_RECONNECT_FAST_INTERVAL_MS = 5000;
constexpr unsigned long AUTO_RECONNECT_SLOW_INTERVAL_MS = 12000;
constexpr int AUTO_RECONNECT_FAST_ATTEMPTS = 3;

extern AppConfig cfg;

extern BLEUUID SERVICE_UUID;
extern BLEUUID CHAR_UUID_NOTIFY;
extern BLEUUID CHAR_UUID_WRITE;
extern const char *TARGET_ADDR;

extern const char *AP_SSID;
extern const char *AP_PASS;

void initConfig();
void loadConfig();
void saveConfig();

#endif // CONFIG_H
