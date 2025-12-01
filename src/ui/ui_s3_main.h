#pragma once

#include <lvgl.h>

struct WifiStatus;

/**
 * Modern carousel-based home UI for the ESP32-S3 AMOLED display.
 *
 * Features:
 * - Horizontal (landscape) layout with status bar + LED indicator
 * - Carousel-style cards with zoom/opacity animations and pill page dots
 * - Full-screen data pages (RPM, Speed, Gear, Temp) with swipe navigation and mini gauge arcs
 * - Dedicated screens for WiFi/Bluetooth scans, brightness preview, and LED tests
 * - Clean Apple-style dark theme optimized for AMOLED
 *
 * ui_s3_init() builds the LVGL screen and loads it immediately.
 * ui_s3_loop() updates status indicators and data values.
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

// Vehicle data setters for live updates
void ui_s3_set_rpm(int rpm);
void ui_s3_set_speed(int speed);
void ui_s3_set_coolant(int temp);
