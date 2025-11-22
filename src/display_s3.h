#pragma once

/**
 * Initialize the AMOLED display and LVGL runtime for the ESP32-S3 board.
 */
void display_s3_init();

/**
 * Regularly call the LVGL handler and tick logic.
 */
void display_s3_loop();
