#pragma once

#include <lvgl.h>
#include "core/wifi.h"

/*
    UI Architecture (ESP32-S3 AMOLED, landscape 456x280):
    - Root screen hosts stacked full-screen containers; only one is visible at a time.
    - Screens:
        * Driving: large speed/gear/RPM bar, shift alert, Wi-Fi/BLE status icons.
        * Main Menu: carousel-style icons (Drive, Brightness, Vehicle, Wi-Fi, Bluetooth, Settings) with swipe left/right and tap-to-open.
        * Brightness: sliders for display + LED brightness, optional night-mode toggle, persisted to NVS.
        * Vehicle Info: live RPM/speed/gear plus VIN/model/diag snippets.
        * Wi-Fi: connection state, AP/STA IPs, quick scan list, hint for smartphone WebUI.
        * Bluetooth: OBD status, target name, last error, recent scan results with manual scan button.
        * Settings: units (km/h vs mph) and tutorial reset.
    - Gestures:
        * Driving: swipe up or tap bottom handle -> Main Menu.
        * Main Menu: swipe left/right to change item; tap center to open detail; swipe down/back returns to Driving.
        * Detail screens: swipe left (or tap back pill) -> Main Menu.
    - Tutorial overlay:
        * Shown until the user opens the menu once and performs a swipe; persisted via cfg.uiTutorialSeen.
    - Updates:
        * Non-blocking LVGL timers; data is polled from global state each loop.
        * Status icons blink while connecting, solid on connected, red on error/offline.
*/

struct UiDisplayHooks
{
    void (*setBrightness)(uint8_t value) = nullptr;
};

void ui_manager_init(lv_disp_t *disp, const UiDisplayHooks &hooks);
void ui_manager_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleBusy);
void ui_manager_set_shiftlight(bool active);
void ui_manager_set_gear(int gear);
void ui_manager_show_logo();
