#pragma once

#include <Arduino.h>

// Initialize the built-in S3 display and LVGL stack.
void display_s3_init();
// Should be called frequently to keep LVGL responsive.
void display_s3_loop();
bool display_s3_ready();
