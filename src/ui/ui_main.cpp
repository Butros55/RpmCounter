#include "ui_main.h"

#include <Arduino.h>

extern "C"
{
#include <lvgl.h>
}

static lv_obj_t *g_screen = nullptr;
static lv_obj_t *g_wifiLabel = nullptr;
static lv_obj_t *g_bleLabel = nullptr;
static lv_obj_t *g_rpmLabel = nullptr;
static lv_obj_t *g_gearLabel = nullptr;
static lv_obj_t *g_speedLabel = nullptr;
static lv_obj_t *g_footerLabel = nullptr;
static lv_obj_t *g_activityBar = nullptr;
static lv_obj_t *g_logoOverlay = nullptr;
static uint32_t g_lastAnimTick = 0;
static uint32_t g_logoHideDeadline = 0;
static int g_barDirection = 1;
static int g_lastGear = -1;
static bool g_shiftActive = false;

static void set_status_label(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (!label || !text)
        return;

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
}

void ui_main_init(lv_disp_t *disp)
{
    LV_UNUSED(disp);

    g_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(0x0E1624), 0);
    lv_obj_set_style_bg_grad_color(g_screen, lv_color_hex(0x1B2C3C), 0);
    lv_obj_set_style_bg_grad_dir(g_screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_color(g_screen, lv_color_hex(0xE6EDF7), 0);
    lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *statusBar = lv_obj_create(g_screen);
    lv_obj_set_size(statusBar, LV_PCT(100), 32);
    lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(statusBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(statusBar, 8, 0);
    lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(statusBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statusBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_wifiLabel = lv_label_create(statusBar);
    lv_label_set_text(g_wifiLabel, "WiFi: --");
    lv_obj_set_style_text_color(g_wifiLabel, lv_palette_main(LV_PALETTE_GREY), 0);

    g_bleLabel = lv_label_create(statusBar);
    lv_label_set_text(g_bleLabel, "BLE: --");
    lv_obj_set_style_text_color(g_bleLabel, lv_palette_main(LV_PALETTE_GREY), 0);

    lv_obj_t *mainArea = lv_obj_create(g_screen);
    lv_obj_set_size(mainArea, LV_PCT(100), LV_PCT(60));
    lv_obj_align(mainArea, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_opa(mainArea, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(mainArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(mainArea, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mainArea, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(mainArea, 6, 0);
    lv_obj_set_style_text_color(mainArea, lv_color_hex(0xE6EDF7), 0);

    g_rpmLabel = lv_label_create(mainArea);
    lv_obj_set_style_text_font(g_rpmLabel, &lv_font_montserrat_24, 0);
    lv_label_set_text(g_rpmLabel, "RPM ---");

    g_gearLabel = lv_label_create(mainArea);
    lv_obj_set_style_text_font(g_gearLabel, &lv_font_montserrat_24, 0);
    lv_label_set_text(g_gearLabel, "Gear N");

    g_speedLabel = lv_label_create(mainArea);
    lv_obj_set_style_text_font(g_speedLabel, &lv_font_montserrat_24, 0);
    lv_label_set_text(g_speedLabel, "Speed 0 km/h");

    g_activityBar = lv_bar_create(mainArea);
    lv_obj_set_size(g_activityBar, LV_PCT(80), 8);
    lv_bar_set_range(g_activityBar, 0, 100);
    lv_bar_set_value(g_activityBar, 20, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(g_activityBar, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(g_activityBar, lv_color_hex(0x233447), 0);
    lv_obj_set_style_bg_color(g_activityBar, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_activityBar, LV_OPA_COVER, LV_PART_INDICATOR);

    g_footerLabel = lv_label_create(g_screen);
    lv_label_set_text(g_footerLabel, "ShiftLight Ready");
    lv_obj_align(g_footerLabel, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_color(g_footerLabel, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);

    lv_disp_load_scr(g_screen);
    const uint32_t now = millis();
    g_lastAnimTick = now;
    g_logoHideDeadline = 0;
    g_lastGear = -1;
    g_shiftActive = false;
}

void ui_main_update_status(bool wifiConnected, bool wifiConnecting, bool bleConnected, bool bleBusy)
{
    if (!g_screen)
        return;

    const char *wifiText = "WiFi: off";
    lv_color_t wifiColor = lv_palette_main(LV_PALETTE_GREY);
    if (wifiConnected)
    {
        wifiText = "WiFi: connected";
        wifiColor = lv_palette_main(LV_PALETTE_GREEN);
    }
    else if (wifiConnecting)
    {
        wifiText = "WiFi: connecting";
        wifiColor = lv_palette_main(LV_PALETTE_AMBER);
    }
    set_status_label(g_wifiLabel, wifiText, wifiColor);

    const char *bleText = "BLE: idle";
    lv_color_t bleColor = lv_palette_main(LV_PALETTE_GREY);
    if (bleConnected)
    {
        bleText = "BLE: connected";
        bleColor = lv_palette_main(LV_PALETTE_GREEN);
    }
    else if (bleBusy)
    {
        bleText = "BLE: connecting";
        bleColor = lv_palette_main(LV_PALETTE_AMBER);
    }
    set_status_label(g_bleLabel, bleText, bleColor);
}

void ui_main_set_gear(int gear)
{
    if (!g_gearLabel)
        return;

    if (gear < 0)
        gear = 0;
    if (gear > 9)
        gear = 9;

    if (g_lastGear == gear)
        return;

    g_lastGear = gear;
    if (gear == 0)
    {
        lv_label_set_text(g_gearLabel, "Gear N");
    }
    else
    {
        lv_label_set_text_fmt(g_gearLabel, "Gear %d", gear);
    }
}

void ui_main_set_shiftlight(bool active)
{
    if (!g_footerLabel)
        return;

    if (g_shiftActive == active)
        return;

    g_shiftActive = active;
    if (active)
    {
        lv_label_set_text(g_footerLabel, "SHIFT!");
        lv_obj_set_style_text_color(g_footerLabel, lv_palette_main(LV_PALETTE_RED), 0);
    }
    else
    {
        lv_label_set_text(g_footerLabel, "ShiftLight Ready");
        lv_obj_set_style_text_color(g_footerLabel, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
    }
}

void ui_main_show_test_logo()
{
    if (!g_screen)
        return;

    if (!g_logoOverlay)
    {
        g_logoOverlay = lv_obj_create(g_screen);
        lv_obj_set_size(g_logoOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(g_logoOverlay, lv_color_hex(0x103050), 0);
        lv_obj_set_style_bg_grad_color(g_logoOverlay, lv_color_hex(0x205080), 0);
        lv_obj_set_style_bg_grad_dir(g_logoOverlay, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(g_logoOverlay, LV_OPA_90, 0);
        lv_obj_set_style_border_width(g_logoOverlay, 0, 0);
        lv_obj_clear_flag(g_logoOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(g_logoOverlay, 0, 0);

        lv_obj_t *title = lv_label_create(g_logoOverlay);
        lv_label_set_text(title, "AMOLED TEST");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -12);

        lv_obj_t *subtitle = lv_label_create(g_logoOverlay);
        lv_label_set_text(subtitle, "Waveshare 1.64\"");
        lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 18);
    }

    lv_obj_clear_flag(g_logoOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_logoOverlay);
    g_logoHideDeadline = millis() + 2000;
}

void ui_main_loop()
{
    if (!g_screen)
        return;

    uint32_t now = millis();
    if (g_activityBar && now - g_lastAnimTick >= 40)
    {
        int32_t val = lv_bar_get_value(g_activityBar);
        if (val >= 100)
        {
            g_barDirection = -1;
        }
        else if (val <= 0)
        {
            g_barDirection = 1;
        }
        lv_bar_set_value(g_activityBar, val + (g_barDirection * 2), LV_ANIM_OFF);
        g_lastAnimTick = now;
    }

    if (g_logoOverlay && g_logoHideDeadline != 0)
    {
        if ((int32_t)(now - g_logoHideDeadline) >= 0)
        {
            lv_obj_add_flag(g_logoOverlay, LV_OBJ_FLAG_HIDDEN);
            g_logoHideDeadline = 0;
        }
    }
}
