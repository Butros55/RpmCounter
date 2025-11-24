#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <lvgl.h>

// Initialize the LVGL UI hierarchy for the ESP32-S3 display.
void ui_main_init(lv_disp_t *disp);

// Update connection indicators and lightweight animations.
void ui_main_update_status(bool wifiConnected, bool wifiConnecting, bool bleConnected, bool bleBusy);

// Periodic housekeeping (blink animations, hint timeout, etc.).
void ui_main_loop();

#endif

