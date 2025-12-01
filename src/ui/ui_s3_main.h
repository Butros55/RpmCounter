#pragma once

#include <lvgl.h>

struct WifiStatus;

/**
 * Modern carousel-based home UI for the ESP32-S3 AMOLED display.
 *
 * Features:
 * - Status bar with WiFi/BLE/LED status icons
 * - Horizontal carousel with smooth 90%-130% zoom animation
 * - watchOS-style widget cards with soft shadows and depth effects
 * - Full-screen data pages (RPM, Speed, Gear, Coolant, Oil Temp) with arc gauges
 * - Clean Apple-style dark theme (#000000 background, iOS accent colors)
 * - Pill-shaped page indicators with animated width and brightness
 * - LED test screen for running LED bar animations
 * - Integrated WiFi and BLE scan functionality with throttling
 * - Gesture debouncing and memory cleanup for robustness
 *
 * ui_s3_init() builds the LVGL screen and loads it immediately.
 * ui_s3_loop() updates status indicators, scans, and data values.
 */
struct UiDisplayHooks
{
    // Optional hook provided by the display driver to change panel backlight.
    void (*setBrightness)(uint8_t value) = nullptr;
};

// Core UI lifecycle
void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks);
void ui_s3_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleConnecting);

// Vehicle data setters for live updates
void ui_s3_set_rpm(int rpm);
void ui_s3_set_speed(int speed);
void ui_s3_set_gear(int gear);
void ui_s3_set_coolant(int temp);
void ui_s3_set_oil_temp(int temp);  // NEW: Oil temperature setter

// UI state control
void ui_s3_set_shiftlight(bool active);
void ui_s3_show_logo();
void ui_s3_show_led_test();  // NEW: Navigate to LED test screen programmatically
