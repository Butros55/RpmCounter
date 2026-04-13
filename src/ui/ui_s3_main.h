#pragma once

#include <lvgl.h>

#include "ui_runtime.h"

/**
 * Shared LVGL UI used by the ESP32-S3 AMOLED path and the desktop simulator.
 *
 * ui_s3_init() builds the LVGL screen and loads it immediately.
 * ui_s3_loop() refreshes the widgets from a platform-neutral snapshot.
 */
void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks, const UiRuntimeState &initialState);
void ui_s3_loop(const UiRuntimeState &state);
void ui_s3_set_gear(int gear);
void ui_s3_set_shiftlight(bool active);
void ui_s3_show_logo();
void ui_s3_debug_dispatch(UiDebugAction action);
UiDebugSnapshot ui_s3_debug_snapshot();
