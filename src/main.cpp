#include <Arduino.h>
#include "core/config.h"
#include "core/wifi.h"
#include "core/state.h"
#include "bluetooth/ble_obd.h"
#include "web/web_ui.h"
#include "hardware/led_bar.h"
#include "hardware/logo_anim.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "hardware/display_s3.h"
#else
#include "hardware/display.h"
#endif

#ifndef UNIT_TEST
void setup()
{
    Serial.begin(115200);
    delay(100);  // Wait for serial to stabilize
    
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
    
    Serial.println("[BOOT] Setting up WiFi...");
    setupWifiFromConfig(cfg);
    
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
    webUiLoop();
    wifiLoop();
    bleObdLoop();
    ledBarLoop();
    logoAnimLoop();
}
#endif // UNIT_TEST
