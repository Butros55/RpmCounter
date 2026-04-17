#include <Arduino.h>
#include "core/config.h"
#include "core/wifi.h"
#include "core/state.h"
#include "bluetooth/ble_obd.h"
#include "web/web_ui.h"
#include "hardware/ambient_light.h"
#include "hardware/gesture_sensor.h"
#include "hardware/led_bar.h"
#include "hardware/logo_anim.h"
#include "telemetry/telemetry_manager.h"
#include "telemetry/usb_sim_bridge.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "hardware/display_s3.h"
#else
#include "hardware/display.h"
#endif

#ifndef UNIT_TEST
void setup()
{
    Serial.begin(115200);
    // Bump the CDC RX buffer well above the default (256 B). The Python bridge
    // can emit a TELEMETRY frame every ~16 ms (~150 B each); if the main loop
    // is briefly busy with WiFi/BLE work, several frames can queue up, and
    // the default buffer would drop bytes which breaks line framing.
    Serial.setRxBufferSize(2048);
    delay(100); // Wait for serial to stabilize

    Serial.println();
    Serial.println("============================================");
    Serial.println("       SHIFTLIGHT ESP32-S3 STARTING");
    Serial.println("============================================");
    Serial.println();

    Serial.println("[BOOT] Loading config...");
    initConfig();
    loadConfig();

    Serial.println("[BOOT] Initializing state...");
    initGlobalState();

    Serial.println("[BOOT] Initializing LEDs...");
    initLeds();

    Serial.println("[BOOT] Initializing ambient light sensor...");
    initAmbientLight();
    ledBarRefreshBrightness();

    Serial.println("[BOOT] Setting up WiFi...");
    setupWifiFromConfig(cfg);

    Serial.println("[BOOT] Initializing telemetry...");
    initTelemetry();

    Serial.println("[BOOT] Starting WebServer...");
    initWebUi();

    Serial.println("[BOOT] Initializing BLE...");
    initBle();

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    Serial.println("[BOOT] Initializing S3 AMOLED Display...");
    display_s3_init();
    displayShowTestLogo();
#else
    Serial.println("[BOOT] Initializing Display...");
    displayInit();
#endif

    Serial.println("[BOOT] Initializing gesture sensor...");
    initGestureSensor();

    // --- Spawn dedicated worker tasks --------------------------------------
    // The USB serial bridge runs on Core 0 at priority 3 so incoming sim data
    // is drained roughly every 1 ms regardless of what the main loop is
    // doing. Telemetry source selection and snapshot publishing run on Core 1
    // at a fixed ~3 ms cadence, and the LED renderer follows at ~200 Hz, so
    // USB/SimHub RPM changes propagate without waiting for the Arduino loop.
    Serial.println("[BOOT] Starting USB bridge task...");
    startUsbSimBridgeTask();

    Serial.println("[BOOT] Starting telemetry task...");
    startTelemetryTask();

    Serial.println("[BOOT] Starting LED bar task...");
    startLedBarTask();

    Serial.println();
    Serial.println("============================================");
    Serial.println("          BOOT COMPLETE - READY");
    Serial.println("============================================");
    Serial.println();
}

void loop()
{
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    display_s3_loop();
#endif
    // usbSimBridgeLoop() is now driven by its own task; the call is kept as a
    // safety net and is a no-op once the task is running.
    usbSimBridgeLoop();
    webUiLoop();
    wifiLoop();
    telemetryLoop();
    bleObdLoop();
    ambientLightLoop();
    gestureSensorLoop();
    // ledBarLoop() is likewise owned by the dedicated LED task — the call
    // here is a no-op while the task is running.
    ledBarLoop();
    logoAnimLoop();
}
#endif // UNIT_TEST
