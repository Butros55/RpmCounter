#include "ui_main.h"

#include <Arduino.h>

extern "C"
{
#include <lvgl.h>
}

namespace
{
    constexpr uint8_t MENU_COUNT = 5;
    const char *MENU_NAMES[MENU_COUNT] = {"Helligkeit", "Fahrzeuginfo", "WLAN", "Bluetooth", "Einstellungen"};
    const char *MENU_ICONS[MENU_COUNT] = {LV_SYMBOL_EYE_OPEN, LV_SYMBOL_LIST, LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_SETTINGS};

    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected
    };

    lv_obj_t *home_screen = nullptr;
    lv_obj_t *detail_screen = nullptr;
    lv_obj_t *main_button = nullptr;
    lv_obj_t *main_icon = nullptr;
    lv_obj_t *main_label = nullptr;
    lv_obj_t *hint_label = nullptr;
    lv_obj_t *left_hint = nullptr;
    lv_obj_t *right_hint = nullptr;
    lv_obj_t *bluetooth_icon = nullptr;
    lv_obj_t *wifi_container = nullptr;
    lv_obj_t *wifi_bars[3] = {nullptr, nullptr, nullptr};

    lv_timer_t *hint_timer = nullptr;
    lv_timer_t *status_timer = nullptr;

    uint8_t current_index = 0;
    bool hint_already_hidden = false;
    bool bt_blink_on = false;
    uint8_t wifi_anim_step = 0;

    ConnectionState bt_state = ConnectionState::Connecting;
    ConnectionState wifi_state = ConnectionState::Connected;

    void update_home_menu();
    void show_detail_screen(uint8_t index);

    void refresh_status_icons(lv_timer_t *timer)
    {
        LV_UNUSED(timer);

        if (!bluetooth_icon || !wifi_container)
            return;

        lv_color_t red = lv_color_hex(0xFF4040);
        lv_color_t orange = lv_color_hex(0xFFA500);
        lv_color_t blue = lv_color_hex(0x5AC8FA);
        lv_color_t dimmed = lv_color_hex(0x303030);

        // Bluetooth state: red (off), blinking blue (connecting), blue (connected)
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

        // WiFi state: red (off), orange (connecting), animated bars (connected)
        for (uint8_t i = 0; i < 3; ++i)
        {
            if (!wifi_bars[i])
                continue;

            uint16_t base_height = 6 + i * 4;
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
                                 {
                                     lv_obj_add_flag(static_cast<lv_obj_t *>(anim->var), LV_OBJ_FLAG_HIDDEN);
                                 });
            lv_anim_start(&a);
        }

        if (timer)
        {
            lv_timer_del(timer);
        }
        hint_timer = nullptr;
        hint_already_hidden = true;
    }

    void update_side_hints()
    {
        if (!left_hint || !right_hint)
            return;

        lv_obj_clear_flag(left_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(right_hint, LV_OBJ_FLAG_HIDDEN);

        if (MENU_COUNT == 0)
            return;

        if (current_index == 0)
        {
            lv_obj_add_flag(left_hint, LV_OBJ_FLAG_HIDDEN);
        }

        if (current_index == MENU_COUNT - 1)
        {
            lv_obj_add_flag(right_hint, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void update_home_menu()
    {
        if (!main_button || !main_icon || !main_label)
            return;

        lv_label_set_text(main_icon, MENU_ICONS[current_index]);
        lv_label_set_text(main_label, MENU_NAMES[current_index]);

        update_side_hints();
    }

    void on_gesture(lv_event_t *e)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT)
        {
            current_index = (current_index + 1) % MENU_COUNT;
            update_home_menu();
        }
        else if (dir == LV_DIR_RIGHT)
        {
            current_index = (current_index + MENU_COUNT - 1) % MENU_COUNT;
            update_home_menu();
        }
    }

    void on_main_button(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_detail_screen(current_index);
    }

    void on_detail_gesture(lv_event_t *e)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT)
        {
            ui_main_show_home();
        }
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
        lv_obj_align(container, LV_ALIGN_TOP_RIGHT, -12, 10);

        bluetooth_icon = lv_label_create(container);
        lv_label_set_text(bluetooth_icon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(bluetooth_icon, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0xFF4040), 0);

        wifi_container = lv_obj_create(container);
        lv_obj_remove_style_all(wifi_container);
        lv_obj_set_style_bg_opa(wifi_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(wifi_container, 0, 0);
        lv_obj_set_layout(wifi_container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(wifi_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(wifi_container, 3, 0);
        lv_obj_set_style_align(wifi_container, LV_ALIGN_CENTER, 0);

        for (uint8_t i = 0; i < 3; ++i)
        {
            wifi_bars[i] = lv_obj_create(wifi_container);
            lv_obj_remove_style_all(wifi_bars[i]);
            lv_obj_set_style_bg_color(wifi_bars[i], lv_color_hex(0xFF4040), 0);
            lv_obj_set_style_bg_opa(wifi_bars[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(wifi_bars[i], 4, 0);
            lv_obj_set_size(wifi_bars[i], 6, 8 + i * 4);
        }

        status_timer = lv_timer_create(refresh_status_icons, 350, nullptr);
        refresh_status_icons(nullptr);
    }

    void create_home_screen()
    {
        home_screen = lv_obj_create(nullptr);
        lv_obj_remove_style_all(home_screen);
        lv_obj_set_style_bg_color(home_screen, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(home_screen, LV_OPA_COVER, 0);

        create_status_icons(home_screen);

        main_button = lv_btn_create(home_screen);
        lv_obj_set_size(main_button, 180, 180);
        lv_obj_center(main_button);
        lv_obj_set_style_radius(main_button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(main_button, lv_color_hex(0x202020), 0);
        lv_obj_set_style_bg_grad_color(main_button, lv_color_hex(0x2E9AFE), 0);
        lv_obj_set_style_bg_grad_dir(main_button, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_shadow_width(main_button, 16, 0);
        lv_obj_set_style_shadow_color(main_button, lv_color_hex(0x103A6F), 0);
        lv_obj_add_event_cb(main_button, on_main_button, LV_EVENT_CLICKED, nullptr);

        main_icon = lv_label_create(main_button);
        lv_obj_set_style_text_font(main_icon, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(main_icon, lv_color_white(), 0);
        lv_obj_align(main_icon, LV_ALIGN_CENTER, 0, -22);

        main_label = lv_label_create(home_screen);
        lv_obj_set_style_text_font(main_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(main_label, lv_color_hex(0xDADADA), 0);
        lv_obj_align(main_label, LV_ALIGN_CENTER, 0, 70);

        left_hint = lv_obj_create(home_screen);
        lv_obj_remove_style_all(left_hint);
        lv_obj_set_style_bg_color(left_hint, lv_color_hex(0x404040), 0);
        lv_obj_set_style_bg_opa(left_hint, LV_OPA_70, 0);
        lv_obj_set_style_radius(left_hint, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(left_hint, 18, 18);
        lv_obj_align(left_hint, LV_ALIGN_LEFT_MID, -6, 0);

        right_hint = lv_obj_create(home_screen);
        lv_obj_remove_style_all(right_hint);
        lv_obj_set_style_bg_color(right_hint, lv_color_hex(0x404040), 0);
        lv_obj_set_style_bg_opa(right_hint, LV_OPA_70, 0);
        lv_obj_set_style_radius(right_hint, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(right_hint, 18, 18);
        lv_obj_align(right_hint, LV_ALIGN_RIGHT_MID, 6, 0);

        hint_label = lv_label_create(home_screen);
        lv_label_set_text(hint_label, "Nach links/rechts wischen");
        lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_16, 0);
        lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 12);

        lv_obj_add_event_cb(home_screen, on_gesture, LV_EVENT_GESTURE, nullptr);

        update_home_menu();

        if (!hint_already_hidden)
        {
            hint_timer = lv_timer_create(hide_hint, 2000, nullptr);
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
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

        lv_obj_t *subtitle = lv_label_create(detail_screen);
        lv_label_set_text(subtitle, "Noch nicht implementiert");
        lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0xA0A0A0), 0);
        lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 0);

        lv_obj_add_event_cb(detail_screen, on_detail_gesture, LV_EVENT_GESTURE, nullptr);

        lv_scr_load_anim(detail_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 220, 0, true);
    }
}

void ui_main_init()
{
    ui_main_show_home();
}

void ui_main_show_home()
{
    if (!home_screen)
    {
        create_home_screen();
    }

    if (hint_label && hint_already_hidden)
    {
        lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_scr_load_anim(home_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 220, 0, true);
}
