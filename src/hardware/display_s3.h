#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <stdbool.h>

void display_s3_init();
void display_s3_loop();
bool display_s3_ready();

// Wrapper-Funktionen für das UI (werden in display_s3.cpp definiert)
void displayInit();
void displayClear();
void displaySetGear(int gear);
void displaySetShiftBlink(bool active);
void displayShowTestLogo();

#endif // CONFIG_IDF_TARGET_ESP32S3
