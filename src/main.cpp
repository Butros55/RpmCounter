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
    initConfig();
    loadConfig();
    initGlobalState();
    initLeds();
    setupWifiFromConfig(cfg);
    initWebUi();
    initBle();
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    display_s3_init();
#else
    displayInit();
#endif
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
