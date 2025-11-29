#pragma once

#include <lvgl.h>

struct WifiStatus;

/**
 * Home/Status UI for the ESP32-S3 AMOLED path.
 *
 * ui_s3_init() builds the LVGL screen and loads it immediately.
 * ui_s3_loop() updates status indicators and detail lists from the live
 * WiFi/BLE state provided by the rest of the application.
 */
struct UiDisplayHooks
{
    // Optional hook provided by the display driver to change panel backlight.
    void (*setBrightness)(uint8_t value) = nullptr;
};

void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks);
void ui_s3_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleConnecting);
void ui_s3_set_gear(int gear);
void ui_s3_set_shiftlight(bool active);
void ui_s3_show_logo();
