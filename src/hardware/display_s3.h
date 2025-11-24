#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <Arduino.h>

// Initialize the built-in S3 display and LVGL stack.
void display_s3_init();
// Should be called frequently to keep LVGL responsive.
void display_s3_loop();
bool display_s3_ready();
#else
inline void display_s3_init() {}
inline void display_s3_loop() {}
inline bool display_s3_ready() { return false; }
#endif

