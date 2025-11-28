#pragma once

#include <lvgl.h>
#include "core/wifi.h"

struct UiDisplayHooks
{
    void (*setBrightness)(uint8_t value) = nullptr;
};

// Simplified S3-only home experience with swipeable cards
void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks);
void ui_s3_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleConnecting);
void ui_s3_set_gear(int gear);
void ui_s3_set_shiftlight(bool active);
void ui_s3_show_logo();
