#pragma once

#include <lvgl.h>

// Initialize the LVGL UI hierarchy for the ESP32-S3 display.
void ui_main_init(lv_disp_t *disp);

// Update connection indicators and lightweight animations.
void ui_main_update_status(bool wifiConnected, bool wifiConnecting, bool bleConnected, bool bleBusy);

// Update live data shown on the main screen.
void ui_main_set_gear(int gear);
void ui_main_set_shiftlight(bool active);
void ui_main_show_test_logo();

// Periodic housekeeping (blink animations, hint timeout, etc.).
void ui_main_loop();
