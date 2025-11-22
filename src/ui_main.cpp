#include "ui_main.h"

#include <array>
#include <string>

namespace
{
    struct MenuItem
    {
        const char *icon;
        const char *label;
    };

    constexpr std::array<MenuItem, 5> MENU_ITEMS = {{{LV_SYMBOL_LIGHTBULB, "Helligkeit"}, {LV_SYMBOL_HOME, "Fahrzeug"}, {LV_SYMBOL_WIFI, "WLAN"}, {LV_SYMBOL_BLUETOOTH, "Bluetooth"}, {LV_SYMBOL_SETTINGS, "Einstellungen"}}};

    uint8_t current_index = 0;

    lv_obj_t *home_screen = nullptr;
    lv_obj_t *center_btn = nullptr;
    lv_obj_t *left_hint = nullptr;
    lv_obj_t *right_hint = nullptr;
    lv_obj_t *title_label = nullptr;

    void style_circle_button(lv_obj_t *btn, lv_color_t color, lv_coord_t size)
    {
        lv_obj_set_size(btn, size, size);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, color, 0);
        lv_obj_set_style_bg_grad_color(btn, lv_color_darken(color, LV_OPA_30), 0);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_shadow_width(btn, 20, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
    }

    void update_menu_view()
    {
        const MenuItem &item = MENU_ITEMS[current_index];

        lv_label_set_text(title_label, item.label);

        // Center button icon + text stacked
        lv_obj_t *icon_label = lv_obj_get_child(center_btn, 0);
        lv_label_set_text(icon_label, item.icon);

        // Left / right hints show neighbor icons
        lv_obj_t *left_icon = lv_obj_get_child(left_hint, 0);
        lv_obj_t *right_icon = lv_obj_get_child(right_hint, 0);

        uint8_t left_index = (current_index + MENU_ITEMS.size() - 1) % MENU_ITEMS.size();
        uint8_t right_index = (current_index + 1) % MENU_ITEMS.size();
        lv_label_set_text(left_icon, MENU_ITEMS[left_index].icon);
        lv_label_set_text(right_icon, MENU_ITEMS[right_index].icon);
    }

    void animate_swap(int8_t direction)
    {
        // direction: -1 left swipe, 1 right swipe
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_time(&anim, 200);
        lv_anim_set_var(&anim, center_btn);
        lv_anim_set_values(&anim, 0, direction * -30);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&anim, [](void *obj, int32_t v) {
            lv_obj_set_style_translate_x(static_cast<lv_obj_t *>(obj), v, 0);
        });
        lv_anim_start(&anim);
    }

    void handle_gesture(lv_event_t *e)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT)
        {
            current_index = (current_index + 1) % MENU_ITEMS.size();
            update_menu_view();
            animate_swap(-1);
        }
        else if (dir == LV_DIR_RIGHT)
        {
            current_index = (current_index + MENU_ITEMS.size() - 1) % MENU_ITEMS.size();
            update_menu_view();
            animate_swap(1);
        }
    }

    void open_detail(lv_event_t *e)
    {
        LV_UNUSED(e);
        lv_obj_t *detail = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(detail, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_color(detail, lv_color_black(), 0);
        lv_obj_set_style_text_color(detail, lv_color_white(), 0);
        lv_obj_set_size(detail, LV_HOR_RES, LV_VER_RES);

        lv_obj_t *title = lv_label_create(detail);
        lv_label_set_text_fmt(title, "%s", MENU_ITEMS[current_index].label);
        lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

        lv_obj_t *body = lv_label_create(detail);
        lv_label_set_text(body, "Noch nicht implementiert");
        lv_obj_align(body, LV_ALIGN_CENTER, 0, 0);

        lv_obj_add_event_cb(detail, [](lv_event_t *ev) {
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            if (dir == LV_DIR_RIGHT)
            {
                ui_show_home();
            }
        }, LV_EVENT_GESTURE, nullptr);

        lv_scr_load_anim(detail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
    }

    void build_home_screen()
    {
        home_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(home_screen, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_color(home_screen, lv_color_black(), 0);
        lv_obj_set_style_text_color(home_screen, lv_color_white(), 0);

        // Title label under center button
        title_label = lv_label_create(home_screen);
        lv_obj_set_style_text_font(title_label, LV_FONT_DEFAULT, 0);

        // Center button
        center_btn = lv_btn_create(home_screen);
        style_circle_button(center_btn, lv_palette_main(LV_PALETTE_BLUE), 180);
        lv_obj_align(center_btn, LV_ALIGN_CENTER, 0, -10);
        lv_obj_add_event_cb(center_btn, open_detail, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_flag(center_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *center_icon = lv_label_create(center_btn);
        lv_obj_set_style_text_font(center_icon, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(center_icon, lv_color_white(), 0);
        lv_obj_center(center_icon);

        // Left hint
        left_hint = lv_btn_create(home_screen);
        style_circle_button(left_hint, lv_palette_main(LV_PALETTE_GREY), 110);
        lv_obj_align(left_hint, LV_ALIGN_CENTER, -150, -10);
        lv_obj_add_flag(left_hint, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_style_opa(left_hint, LV_OPA_50, 0);
        lv_obj_set_style_clip_corner(left_hint, true, 0);

        lv_obj_t *left_icon = lv_label_create(left_hint);
        lv_obj_set_style_text_font(left_icon, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(left_icon, lv_color_white(), 0);
        lv_obj_center(left_icon);

        // Right hint
        right_hint = lv_btn_create(home_screen);
        style_circle_button(right_hint, lv_palette_main(LV_PALETTE_GREY), 110);
        lv_obj_align(right_hint, LV_ALIGN_CENTER, 150, -10);
        lv_obj_add_flag(right_hint, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_style_opa(right_hint, LV_OPA_50, 0);
        lv_obj_set_style_clip_corner(right_hint, true, 0);

        lv_obj_t *right_icon = lv_label_create(right_hint);
        lv_obj_set_style_text_font(right_icon, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(right_icon, lv_color_white(), 0);
        lv_obj_center(right_icon);

        lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 120);

        // Gestures on screen
        lv_obj_add_event_cb(home_screen, handle_gesture, LV_EVENT_GESTURE, nullptr);
    }
}

void ui_show_home()
{
    if (home_screen == nullptr)
    {
        build_home_screen();
    }

    update_menu_view();
    lv_scr_load_anim(home_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
}

void ui_init()
{
    ui_show_home();
}
