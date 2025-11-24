#include "ui_main.h"

#include <Arduino.h>
#include <Preferences.h>

extern "C"
{
#include <lvgl.h>
}

namespace
{
    constexpr uint8_t MENU_COUNT = 5;
    const char *MENU_NAMES[MENU_COUNT] = {"Helligkeit", "Fahrzeuginfo", "WLAN", "Bluetooth", "Einstellungen"};
    const char *MENU_ICONS[MENU_COUNT] = {LV_SYMBOL_EYE_OPEN, LV_SYMBOL_LIST, LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_SETTINGS};

    constexpr uint8_t HINT_MAX_VIEWS = 3;
    constexpr uint16_t MENU_SIZE = 120;

    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected
    };

    lv_obj_t *home_screen = nullptr;
    lv_obj_t *detail_screen = nullptr;
    lv_obj_t *page_scroller = nullptr;
    lv_obj_t *hint_label = nullptr;
    lv_obj_t *bluetooth_icon = nullptr;
    lv_obj_t *wifi_container = nullptr;
    lv_obj_t *wifi_bars[3] = {nullptr, nullptr, nullptr};

    lv_timer_t *hint_timer = nullptr;
    lv_timer_t *status_timer = nullptr;

    uint8_t current_page = 0;
    bool hint_already_hidden = false;
    bool bt_blink_on = false;
    uint8_t wifi_anim_step = 0;

    ConnectionState bt_state = ConnectionState::Connecting;
    ConnectionState wifi_state = ConnectionState::Disconnected;

    void refresh_status_icons(lv_timer_t *timer = nullptr)
    {
        LV_UNUSED(timer);

        if (!bluetooth_icon || !wifi_container)
            return;

        const lv_color_t red = lv_color_hex(0xFF4040);
        const lv_color_t orange = lv_color_hex(0xFFA500);
        const lv_color_t blue = lv_color_hex(0x5AC8FA);
        const lv_color_t dimmed = lv_color_hex(0x303030);

        switch (bt_state)
        {
        case ConnectionState::Disconnected:
            lv_obj_set_style_text_color(bluetooth_icon, red, 0);
            break;
        case ConnectionState::Connecting:
            bt_blink_on = !bt_blink_on;
            lv_obj_set_style_text_color(bluetooth_icon, bt_blink_on ? blue : dimmed, 0);
            break;
        case ConnectionState::Connected:
            lv_obj_set_style_text_color(bluetooth_icon, blue, 0);
            break;
        }

        for (uint8_t i = 0; i < 3; ++i)
        {
            if (!wifi_bars[i])
                continue;

            const uint16_t base_height = 6 + i * 4;
            switch (wifi_state)
            {
            case ConnectionState::Disconnected:
                lv_obj_set_height(wifi_bars[i], base_height);
                lv_obj_set_style_bg_color(wifi_bars[i], red, 0);
                lv_obj_set_style_bg_opa(wifi_bars[i], LV_OPA_COVER, 0);
                break;
            case ConnectionState::Connecting:
                lv_obj_set_height(wifi_bars[i], base_height + (i == 2 ? 2 : 0));
                lv_obj_set_style_bg_color(wifi_bars[i], orange, 0);
                lv_obj_set_style_bg_opa(wifi_bars[i], LV_OPA_COVER, 0);
                break;
            case ConnectionState::Connected:
                wifi_anim_step = (wifi_anim_step + 1) % 4;
                lv_obj_set_style_bg_color(wifi_bars[i], blue, 0);
                lv_obj_set_style_bg_opa(wifi_bars[i], wifi_anim_step > i ? LV_OPA_COVER : LV_OPA_30, 0);
                lv_obj_set_height(wifi_bars[i], base_height + (wifi_anim_step == i ? 2 : 0));
                break;
            }
        }
    }

    void hide_hint(lv_timer_t *timer)
    {
        if (hint_label)
        {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_time(&a, 400);
            lv_anim_set_var(&a, hint_label);
            lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
            lv_anim_set_exec_cb(&a, [](void *obj, int32_t v)
                                { lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0); });
            lv_anim_set_ready_cb(&a, [](lv_anim_t *anim)
                                 { lv_obj_add_flag(static_cast<lv_obj_t *>(anim->var), LV_OBJ_FLAG_HIDDEN); });
            lv_anim_start(&a);
        }

        if (timer)
        {
            lv_timer_del(timer);
        }
        hint_timer = nullptr;
        hint_already_hidden = true;
    }

    void on_detail_gesture(lv_event_t *e)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT)
        {
            lv_scr_load_anim(home_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 220, 0, true);
        }
    }

    void show_detail_screen(uint8_t index)
    {
        detail_screen = lv_obj_create(nullptr);
        lv_obj_remove_style_all(detail_screen);
        lv_obj_set_style_bg_color(detail_screen, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(detail_screen, LV_OPA_COVER, 0);

        lv_obj_t *title = lv_label_create(detail_screen);
        lv_label_set_text_fmt(title, "%s", MENU_NAMES[index]);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

        lv_obj_t *subtitle = lv_label_create(detail_screen);
        lv_label_set_text(subtitle, "Coming soon");
        lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0xA0A0A0), 0);
        lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *back_btn = lv_btn_create(detail_screen);
        lv_obj_set_size(back_btn, 44, 44);
        lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x202020), 0);
        lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
        lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, 8);
        lv_obj_add_event_cb(back_btn, [](lv_event_t *evt)
                            { lv_scr_load_anim(home_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 220, 0, true); },
                            LV_EVENT_CLICKED, nullptr);

        lv_obj_t *back_label = lv_label_create(back_btn);
        lv_label_set_text(back_label, LV_SYMBOL_LEFT);
        lv_obj_center(back_label);

        lv_obj_add_event_cb(detail_screen, on_detail_gesture, LV_EVENT_GESTURE, nullptr);

        lv_scr_load_anim(detail_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 220, 0, true);
    }

    void on_menu_clicked(lv_event_t *e)
    {
        uint8_t idx = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
        show_detail_screen(idx);
    }

    lv_obj_t *create_menu_item(lv_obj_t *parent, uint8_t index)
    {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_size(btn, MENU_SIZE, MENU_SIZE);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x202020), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(btn, 12, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
        lv_obj_set_style_shadow_spread(btn, 2, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_add_event_cb(btn, on_menu_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));

        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, MENU_ICONS[index]);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(icon, lv_color_white(), 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -10);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, MENU_NAMES[index]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xDADADA), 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 28);

        return btn;
    }

    void create_status_icons(lv_obj_t *parent)
    {
        lv_obj_t *container = lv_obj_create(parent);
        lv_obj_remove_style_all(container);
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        lv_obj_set_style_pad_row(container, 0, 0);
        lv_obj_set_style_pad_column(container, 10, 0);
        lv_obj_set_layout(container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(container, LV_ALIGN_TOP_RIGHT, -12, 10);

        wifi_container = lv_obj_create(container);
        lv_obj_remove_style_all(wifi_container);
        lv_obj_set_style_bg_opa(wifi_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(wifi_container, 0, 0);
        lv_obj_set_layout(wifi_container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(wifi_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(wifi_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
        for (uint8_t i = 0; i < 3; ++i)
        {
            wifi_bars[i] = lv_obj_create(wifi_container);
            lv_obj_remove_style_all(wifi_bars[i]);
            lv_obj_set_style_bg_color(wifi_bars[i], lv_color_hex(0x404040), 0);
            lv_obj_set_style_bg_opa(wifi_bars[i], LV_OPA_60, 0);
            lv_obj_set_style_radius(wifi_bars[i], 3, 0);
            lv_obj_set_size(wifi_bars[i], 6, 6 + i * 4);
        }

        bluetooth_icon = lv_label_create(container);
        lv_label_set_text(bluetooth_icon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(bluetooth_icon, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0x404040), 0);

        refresh_status_icons();
    }

    void create_hint_overlay(lv_obj_t *parent)
    {
        hint_label = lv_label_create(parent);
        lv_label_set_text(hint_label, "Tipp: Links/Rechts wischen");
        lv_obj_set_style_text_color(hint_label, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
        lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -12);
    }

    void create_home_screen()
    {
        home_screen = lv_obj_create(nullptr);
        lv_obj_remove_style_all(home_screen);
        lv_obj_set_style_bg_color(home_screen, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(home_screen, LV_OPA_COVER, 0);

        create_status_icons(home_screen);

        page_scroller = lv_obj_create(home_screen);
        lv_obj_remove_style_all(page_scroller);
        lv_obj_set_size(page_scroller, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(page_scroller, 20, 0);
        lv_obj_set_scroll_dir(page_scroller, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(page_scroller, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(page_scroller, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_bg_opa(page_scroller, LV_OPA_TRANSP, 0);
        lv_obj_set_layout(page_scroller, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(page_scroller, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(page_scroller, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *page1 = lv_obj_create(page_scroller);
        lv_obj_remove_style_all(page1);
        lv_obj_set_size(page1, lv_pct(100), lv_pct(100));
        lv_obj_set_layout(page1, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(page1, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(page1, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        create_menu_item(page1, 0);
        create_menu_item(page1, 1);
        create_menu_item(page1, 2);

        lv_obj_t *page2 = lv_obj_create(page_scroller);
        lv_obj_remove_style_all(page2);
        lv_obj_set_size(page2, lv_pct(100), lv_pct(100));
        lv_obj_set_layout(page2, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(page2, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(page2, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        create_menu_item(page2, 3);
        create_menu_item(page2, 4);

        lv_obj_add_event_cb(page_scroller, [](lv_event_t *e)
                            {
                                lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
                                if (dir == LV_DIR_LEFT && current_page < 1)
                                {
                                    current_page++;
                                }
                                else if (dir == LV_DIR_RIGHT && current_page > 0)
                                {
                                    current_page--;
                                }
                            },
                            LV_EVENT_GESTURE, nullptr);

        if (!hint_already_hidden)
        {
            create_hint_overlay(home_screen);
            hint_timer = lv_timer_create(hide_hint, 2500, nullptr);
        }
    }

    void load_hint_preference()
    {
        Preferences prefs;
        if (!prefs.begin("ui", false))
        {
            hint_already_hidden = false;
            return;
        }
        uint8_t viewed = prefs.getUChar("hint_count", 0);
        hint_already_hidden = viewed >= HINT_MAX_VIEWS;
        if (!hint_already_hidden)
        {
            prefs.putUChar("hint_count", viewed + 1);
        }
        prefs.end();
    }
}

void ui_main_init(lv_disp_t *disp)
{
    LV_UNUSED(disp);
    load_hint_preference();
    create_home_screen();

    if (!status_timer)
    {
        status_timer = lv_timer_create(refresh_status_icons, 450, nullptr);
    }

    lv_scr_load(home_screen);
}

void ui_main_update_status(bool wifiConnected, bool wifiConnecting, bool bleConnected, bool bleBusy)
{
    wifi_state = wifiConnected ? ConnectionState::Connected : (wifiConnecting ? ConnectionState::Connecting : ConnectionState::Disconnected);
    bt_state = bleConnected ? ConnectionState::Connected : (bleBusy ? ConnectionState::Connecting : ConnectionState::Disconnected);
    refresh_status_icons();
}

void ui_main_loop()
{
    // Timers and animations keep running through LVGL; nothing extra needed yet.
}
