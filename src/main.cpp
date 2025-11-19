#include <Arduino.h>
#include "config.h"
#include "state.h"
#include "ble_obd.h"
#include "web_ui.h"
#include "led_bar.h"
#include "logo_anim.h"
#include "display.h"

void setup() {
    initConfig();
    initGlobalState();
    initLeds();
    initWifiAP();
    initWebUi();
    initBle();
    displayInit();
}

void loop() {
    webUiLoop();
    bleObdLoop();
    ledBarLoop();
    logoAnimLoop();
}
