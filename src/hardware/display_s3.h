#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <stdbool.h>
#include <driver/i2c_master.h>

#include "telemetry/telemetry_manager.h"

void display_s3_init();
void display_s3_loop();
bool display_s3_ready();

// Wrapper-Funktionen für das UI (werden in display_s3.cpp definiert)
void displayInit();
void displayClear();
void displaySetGear(int gear);
void displaySetShiftBlink(bool active);
void displayShowTestLogo();
void displayShowSimSessionTransition(SimSessionTransitionType transition);
bool display_s3_add_shared_i2c_device(uint8_t address, uint32_t sclSpeedHz, i2c_master_dev_handle_t *outDevice);
void display_s3_remove_shared_i2c_device(i2c_master_dev_handle_t device);
bool display_s3_uses_shared_i2c_pins(int sdaPin, int sclPin);

#endif // CONFIG_IDF_TARGET_ESP32S3
