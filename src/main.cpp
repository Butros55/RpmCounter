#include <Arduino.h>
#include "core/config.h"
#include "core/wifi.h"
#include "core/state.h"
#include "bluetooth/ble_obd.h"
#include "web/web_ui.h"
#include "hardware/led_bar.h"
#include "hardware/logo_anim.h"
#include "hardware/display.h"
#include "display_s3.h"
#include "touch_s3.h"
#include "ui_main.h"

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
    displayInit();
    display_s3_init();
    touch_s3_init();
    ui_main_init();
}

void loop()
{
    webUiLoop();
    wifiLoop();
    bleObdLoop();
    ledBarLoop();
    logoAnimLoop();
    display_s3_loop();
}
#endif // UNIT_TEST
