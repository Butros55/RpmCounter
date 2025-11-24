#include "ui_main.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <Preferences.h>

namespace
{
    constexpr uint8_t TUTORIAL_MAX_SHOWS = 3;
    constexpr uint32_t TUTORIAL_FADE_MS = 9000;

    lv_obj_t *g_homeScreen = nullptr;
    lv_obj_t *g_pageContainer = nullptr;
    lv_obj_t *g_statusWifi = nullptr;
    lv_obj_t *g_statusBle = nullptr;
    lv_obj_t *g_hint = nullptr;

    bool g_wifiConnected = false;
    bool g_wifiConnecting = false;
    bool g_bleConnected = false;
    bool g_bleBusy = false;

    uint32_t g_hintStart = 0;
    bool g_hintVisible = false;

    uint16_t g_placeholderPressStartX = 0;

    struct PlaceholderInfo
    {
        const char *title;
        const char *subtitle;
    };

    const PlaceholderInfo PLACEHOLDER_BRIGHTNESS{"Brightness", "(coming soon)"};
    const PlaceholderInfo PLACEHOLDER_VEHICLE{"Vehicle Info", "(coming soon)"};
    const PlaceholderInfo PLACEHOLDER_WIFI{"WiFi", "(coming soon)"};
    const PlaceholderInfo PLACEHOLDER_BT{"Bluetooth", "(coming soon)"};

    void placeholder_back()
    {
        if (g_homeScreen)
        {
            lv_disp_load_scr(g_homeScreen);
        }
    }

    void placeholder_gesture_cb(lv_event_t *e)
    {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_GESTURE)
        {
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            if (dir == LV_DIR_RIGHT)
            {
                placeholder_back();
            }
        }
        else if (code == LV_EVENT_PRESSED)
        {
            lv_point_t p;
            lv_indev_get_point(lv_indev_get_act(), &p);
            g_placeholderPressStartX = p.x;
        }
        else if (code == LV_EVENT_RELEASED)
        {
            lv_point_t p;
            lv_indev_get_point(lv_indev_get_act(), &p);
            if (g_placeholderPressStartX < 28 && p.x > g_placeholderPressStartX)
            {
                placeholder_back();
            }
        }
    }

    void back_btn_cb(lv_event_t *e)
    {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED)
        {
            placeholder_back();
        }
    }

    lv_obj_t *create_placeholder_screen(const PlaceholderInfo *info)
    {
        lv_obj_t *scr = lv_obj_create(nullptr);
        lv_obj_remove_style_all(scr);
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_event_cb(scr, placeholder_gesture_cb, LV_EVENT_ALL, nullptr);

        lv_obj_t *back = lv_btn_create(scr);
        lv_obj_set_size(back, 46, 46);
        lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x1c1c1c), 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_80, 0);
        lv_obj_set_style_shadow_width(back, 10, 0);
        lv_obj_set_style_shadow_opa(back, LV_OPA_30, 0);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *backLabel = lv_label_create(back);
        lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
        lv_obj_center(backLabel);

        lv_obj_t *title = lv_label_create(scr);
        lv_label_set_text_fmt(title, "%s %s", info->title, info->subtitle);
        lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(title, LV_THEME_DEFAULT_FONT_TITLE, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

        lv_obj_t *hint = lv_label_create(scr);
        lv_label_set_text(hint, "Swipe from the left edge or tap Back to return.");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x9a9a9a), 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

        return scr;
    }

    void open_placeholder(lv_event_t *e)
    {
        const PlaceholderInfo *info = static_cast<const PlaceholderInfo *>(lv_event_get_user_data(e));
        lv_obj_t *scr = create_placeholder_screen(info);
        lv_disp_load_scr(scr);
    }

    lv_obj_t *create_menu_card(lv_obj_t *parent, const char *icon, const char *label, const PlaceholderInfo *info)
    {
        lv_obj_t *wrapper = lv_obj_create(parent);
        lv_obj_remove_style_all(wrapper);
        lv_obj_set_size(wrapper, 110, 140);
        lv_obj_set_layout(wrapper, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(wrapper, 0, 0);

        lv_obj_t *btn = lv_btn_create(wrapper);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 96, 96);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1d1d1d), 0);
        lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x252525), 0);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_shadow_width(btn, 16, 0);
        lv_obj_set_style_shadow_spread(btn, 2, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_35, 0);
        lv_obj_add_event_cb(btn, open_placeholder, LV_EVENT_CLICKED, (void *)info);

        lv_obj_t *iconLabel = lv_label_create(btn);
        lv_obj_set_style_text_color(iconLabel, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(iconLabel, LV_THEME_DEFAULT_FONT_TITLE, 0);
        lv_label_set_text(iconLabel, icon);
        lv_obj_center(iconLabel);

        lv_obj_t *caption = lv_label_create(wrapper);
        lv_label_set_text(caption, label);
        lv_obj_set_style_text_color(caption, lv_color_hex(0xc7c7c7), 0);
        lv_obj_set_style_text_font(caption, LV_THEME_DEFAULT_FONT_SMALL, 0);
        lv_obj_center(caption);

        return wrapper;
    }

    void build_pages(lv_obj_t *container)
    {
        lv_coord_t pageW = lv_disp_get_hor_res(lv_disp_get_default());
        lv_coord_t pageH = lv_disp_get_ver_res(lv_disp_get_default());

        lv_obj_t *page1 = lv_obj_create(container);
        lv_obj_remove_style_all(page1);
        lv_obj_set_size(page1, pageW, pageH);
        lv_obj_set_layout(page1, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(page1, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(page1, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(page1, 18, 0);
        lv_obj_set_style_pad_column(page1, 12, 0);
        lv_obj_set_style_pad_all(page1, 18, 0);
        lv_obj_set_scrollbar_mode(page1, LV_SCROLLBAR_MODE_OFF);

        create_menu_card(page1, LV_SYMBOL_EYE_OPEN, "Brightness", &PLACEHOLDER_BRIGHTNESS);
        create_menu_card(page1, LV_SYMBOL_HOME, "Vehicle", &PLACEHOLDER_VEHICLE);
        create_menu_card(page1, LV_SYMBOL_WIFI, "WiFi", &PLACEHOLDER_WIFI);
        create_menu_card(page1, LV_SYMBOL_BLUETOOTH, "Bluetooth", &PLACEHOLDER_BT);

        lv_obj_t *page2 = lv_obj_create(container);
        lv_obj_remove_style_all(page2);
        lv_obj_set_size(page2, pageW, pageH);
        lv_obj_set_layout(page2, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(page2, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(page2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(page2, 18, 0);
        lv_obj_set_scrollbar_mode(page2, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t *coming = lv_label_create(page2);
        lv_label_set_text(coming, "More screens coming soon…");
        lv_obj_set_style_text_color(coming, lv_color_hex(0x9a9a9a), 0);
        lv_obj_set_style_text_font(coming, LV_THEME_DEFAULT_FONT_NORMAL, 0);
    }

    void create_status_bar(lv_obj_t *parent)
    {
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_remove_style_all(bar);
        lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(bar, 8, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
        lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, -12, 8);

        g_wifiConnected = false;
        g_wifiConnecting = false;
        g_bleConnected = false;
        g_bleBusy = false;

        g_statusWifi = lv_label_create(bar);
        lv_label_set_text(g_statusWifi, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(g_statusWifi, lv_color_hex(0x803030), 0);

        g_statusBle = lv_label_create(bar);
        lv_label_set_text(g_statusBle, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(g_statusBle, lv_color_hex(0x803030), 0);
    }

    void create_hint()
    {
        Preferences prefs;
        prefs.begin("ui", false);
        uint8_t shown = prefs.getUChar("tutorial_shown", 0);
        if (shown >= TUTORIAL_MAX_SHOWS)
        {
            prefs.end();
            return;
        }
        prefs.putUChar("tutorial_shown", shown + 1);
        prefs.end();

        g_hint = lv_obj_create(g_homeScreen);
        lv_obj_remove_style_all(g_hint);
        lv_obj_set_style_bg_color(g_hint, lv_color_hex(0x202020), 0);
        lv_obj_set_style_bg_opa(g_hint, LV_OPA_80, 0);
        lv_obj_set_style_radius(g_hint, 12, 0);
        lv_obj_set_style_pad_all(g_hint, 10, 0);
        lv_obj_set_width(g_hint, lv_pct(90));
        lv_obj_align(g_hint, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_add_flag(g_hint, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t *text = lv_label_create(g_hint);
        lv_label_set_text(text, "Tip: Swipe left/right to change menus. Swipe from the left edge or tap Back to return.");
        lv_obj_set_style_text_color(text, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(text);

        lv_obj_add_event_cb(g_hint, [](lv_event_t *ev) {
            if (lv_event_get_code(ev) == LV_EVENT_CLICKED)
            {
                lv_obj_add_flag(lv_event_get_target(ev), LV_OBJ_FLAG_HIDDEN);
                g_hintVisible = false;
            }
        }, LV_EVENT_ALL, nullptr);

        g_hintStart = lv_tick_get();
        g_hintVisible = true;
    }
}

void ui_main_init(lv_disp_t *disp)
{
    lv_disp_set_default(disp);
    lv_theme_default_init(disp, lv_color_black(), lv_color_hex(0x3c9dfa), true, LV_THEME_DEFAULT_FONT_NORMAL);

    g_homeScreen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(g_homeScreen);
    lv_obj_set_style_bg_color(g_homeScreen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_homeScreen, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(g_homeScreen, LV_SCROLLBAR_MODE_OFF);

    g_pageContainer = lv_obj_create(g_homeScreen);
    lv_obj_remove_style_all(g_pageContainer);
    lv_obj_set_size(g_pageContainer, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_dir(g_pageContainer, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(g_pageContainer, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(g_pageContainer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(g_pageContainer, 0, 0);
    lv_obj_set_style_pad_column(g_pageContainer, 0, 0);
    lv_obj_set_style_pad_all(g_pageContainer, 0, 0);
    lv_obj_set_style_bg_opa(g_pageContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(g_pageContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_pageContainer, LV_FLEX_FLOW_ROW);

    build_pages(g_pageContainer);
    create_status_bar(g_homeScreen);
    create_hint();

    lv_disp_load_scr(g_homeScreen);
}

void ui_main_update_status(bool wifiConnected, bool wifiConnecting, bool bleConnected, bool bleBusy)
{
    g_wifiConnected = wifiConnected;
    g_wifiConnecting = wifiConnecting;
    g_bleConnected = bleConnected;
    g_bleBusy = bleBusy;

    if (g_statusWifi)
    {
        lv_color_t c = lv_color_hex(0x883030);
        if (g_wifiConnected)
            c = lv_color_hex(0x8ae5ff);
        else if (g_wifiConnecting)
            c = lv_color_hex(0xffc857);
        lv_obj_set_style_text_color(g_statusWifi, c, 0);
    }

    if (g_statusBle)
    {
        lv_color_t c = lv_color_hex(0x80305a);
        if (g_bleConnected)
            c = lv_color_hex(0x6ea8ff);
        else if (g_bleBusy)
            c = lv_color_hex(0xffc857);
        lv_obj_set_style_text_color(g_statusBle, c, 0);
    }
}

void ui_main_loop()
{
    const uint32_t tick = lv_tick_get();
    bool blinkPhase = ((tick / 500) % 2) == 0;

    if (g_statusWifi)
    {
        uint8_t opa = (g_wifiConnecting && !g_wifiConnected && blinkPhase) ? LV_OPA_100 : LV_OPA_COVER;
        lv_obj_set_style_opa(g_statusWifi, opa, 0);
    }

    if (g_statusBle)
    {
        uint8_t opa = (g_bleBusy && !g_bleConnected && blinkPhase) ? LV_OPA_100 : LV_OPA_COVER;
        lv_obj_set_style_opa(g_statusBle, opa, 0);
    }

    if (g_hintVisible && lv_tick_elaps(g_hintStart) > TUTORIAL_FADE_MS)
    {
        lv_obj_add_flag(g_hint, LV_OBJ_FLAG_HIDDEN);
        g_hintVisible = false;
    }
}

#endif

