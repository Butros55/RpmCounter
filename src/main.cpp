#include <Arduino.h>
#include "core/config.h"
#include "core/wifi.h"
#include "core/state.h"
#include "bluetooth/ble_obd.h"
#include "web/web_ui.h"
#include "hardware/led_bar.h"
#include "hardware/logo_anim.h"
#include "hardware/display.h"

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
}

void loop()
{
    webUiLoop();
    wifiLoop();
    bleObdLoop();
    ledBarLoop();
    logoAnimLoop();
}
#endif // UNIT_TEST
