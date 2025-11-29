#include "ui_s3_main.h"

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <vector>

#include "bluetooth/ble_obd.h"
#include "core/config.h"
#include "core/state.h"
#include "core/utils.h"
#include "core/wifi.h"

namespace
{
    enum class CardScreen
    {
        Brightness,
        Vehicle,
        Wifi,
        Bluetooth,
        Settings
    };

    struct CardDef
    {
        CardScreen screen;
        const char *title;
        const char *symbol;
    };

    constexpr CardDef CARDS[] = {
        {CardScreen::Brightness, "Brightness", LV_SYMBOL_EYE_OPEN},
        {CardScreen::Vehicle, "Vehicle", LV_SYMBOL_GPS},
        {CardScreen::Wifi, "WiFi", LV_SYMBOL_WIFI},
        {CardScreen::Bluetooth, "Bluetooth", LV_SYMBOL_BLUETOOTH},
        {CardScreen::Settings, "Settings", LV_SYMBOL_SETTINGS},
    };

    constexpr size_t CARD_COUNT = sizeof(CARDS) / sizeof(CARDS[0]);

    struct CardWidgets
    {
        lv_obj_t *container = nullptr;
        lv_obj_t *circle = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *label = nullptr;
    };

    struct UiRefs
    {
        lv_disp_t *disp = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *statusBar = nullptr;
        lv_obj_t *wifiIcon = nullptr;
        lv_obj_t *bleIcon = nullptr;
        lv_obj_t *title = nullptr;
        lv_obj_t *carousel = nullptr;
        lv_obj_t *pageIndicator = nullptr;
        lv_obj_t *tutorial = nullptr;
        lv_obj_t *tutorialLabel = nullptr;
        lv_obj_t *gearBadge = nullptr;
        lv_obj_t *shiftBadge = nullptr;
        lv_obj_t *logoOverlay = nullptr;
        lv_obj_t *detail = nullptr;
        lv_obj_t *detailContent = nullptr;
        lv_obj_t *wifiList = nullptr;
        lv_obj_t *bleList = nullptr;
        lv_obj_t *brightnessSlider = nullptr;
        lv_obj_t *brightnessValue = nullptr;
        std::array<CardWidgets, CARD_COUNT> cards{};
    };

    struct UiState
    {
        int cardIndex = 0;
        bool inDetail = false;
        bool tutorialVisible = true;
        bool hasInteracted = false;
        int gear = 0;
        bool shift = false;
        WifiStatus lastWifi{};
        bool bleConnected = false;
        bool bleConnecting = false;
    };

    UiRefs g_ui;
    UiState g_state;
    UiDisplayHooks g_hooks;

    lv_style_t styleBg;
    lv_style_t styleCard;
    lv_style_t styleMuted;
    lv_style_t styleBadge;
    lv_style_t styleCircle;
    lv_style_t styleDot;
    lv_style_t styleTutorial;

    const lv_color_t color_bg = lv_color_hex(0x000000);
    const lv_color_t color_card = lv_color_hex(0x10131b);
    const lv_color_t color_card_accent = lv_color_hex(0x2D9CDB);
    const lv_color_t color_muted = lv_color_hex(0x9AA2AD);
    const lv_color_t color_ok = lv_color_hex(0x56F38A);
    const lv_color_t color_warn = lv_color_hex(0xF5A524);
    const lv_color_t color_error = lv_color_hex(0xF55E61);
    const lv_color_t color_dot = lv_color_hex(0x2b2f38);
    const lv_color_t color_dot_active = lv_color_hex(0x5AC8FA);

    const CardDef &current_card()
    {
        const int count = static_cast<int>(CARD_COUNT);
        g_state.cardIndex = (g_state.cardIndex % count + count) % count;
        return CARDS[g_state.cardIndex];
    }

    void mark_interacted()
    {
        g_state.hasInteracted = true;
        if (g_state.tutorialVisible && g_ui.tutorial)
        {
            g_state.tutorialVisible = false;
            lv_obj_add_flag(g_ui.tutorial, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void apply_styles()
    {
        lv_style_init(&styleBg);
        lv_style_set_bg_color(&styleBg, color_bg);
        lv_style_set_bg_opa(&styleBg, LV_OPA_COVER);
        lv_style_set_pad_all(&styleBg, 0);

        lv_style_init(&styleCard);
        lv_style_set_bg_color(&styleCard, color_card);
        lv_style_set_bg_opa(&styleCard, LV_OPA_COVER);
        lv_style_set_radius(&styleCard, 16);
        lv_style_set_pad_all(&styleCard, 12);
        lv_style_set_border_width(&styleCard, 1);
        lv_style_set_border_color(&styleCard, lv_color_hex(0x1a1f28));
        lv_style_set_shadow_width(&styleCard, 16);
        lv_style_set_shadow_color(&styleCard, lv_color_hex(0x0a0d12));
        lv_style_set_shadow_opa(&styleCard, LV_OPA_40);
        lv_style_set_shadow_spread(&styleCard, 4);

        lv_style_init(&styleCircle);
        lv_style_set_radius(&styleCircle, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleCircle, lv_color_hex(0x121722));
        lv_style_set_bg_opa(&styleCircle, LV_OPA_COVER);
        lv_style_set_border_width(&styleCircle, 2);
        lv_style_set_border_color(&styleCircle, color_card_accent);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, color_muted);

        lv_style_init(&styleBadge);
        lv_style_set_radius(&styleBadge, 12);
        lv_style_set_bg_color(&styleBadge, lv_color_hex(0x1C2028));
        lv_style_set_bg_opa(&styleBadge, LV_OPA_COVER);
        lv_style_set_pad_all(&styleBadge, 6);
        lv_style_set_text_color(&styleBadge, lv_color_hex(0xE8EAED));

        lv_style_init(&styleDot);
        lv_style_set_radius(&styleDot, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleDot, color_dot);
        lv_style_set_bg_opa(&styleDot, LV_OPA_COVER);
        lv_style_set_border_width(&styleDot, 0);

        lv_style_init(&styleTutorial);
        lv_style_set_radius(&styleTutorial, 12);
        lv_style_set_bg_color(&styleTutorial, lv_color_hex(0x1A1E26));
        lv_style_set_bg_opa(&styleTutorial, LV_OPA_80);
        lv_style_set_pad_all(&styleTutorial, 10);
        lv_style_set_border_width(&styleTutorial, 1);
        lv_style_set_border_color(&styleTutorial, lv_color_hex(0x2b313d));
    }

    void update_page_indicator()
    {
        if (!g_ui.pageIndicator)
            return;

        uint32_t childCount = lv_obj_get_child_cnt(g_ui.pageIndicator);
        for (uint32_t i = 0; i < childCount; ++i)
        {
            lv_obj_t *dot = lv_obj_get_child(g_ui.pageIndicator, i);
            bool active = static_cast<int>(i) == g_state.cardIndex;
            lv_obj_set_style_bg_color(dot, active ? color_dot_active : color_dot, 0);
            lv_obj_set_style_opa(dot, active ? LV_OPA_COVER : LV_OPA_50, 0);
            lv_obj_set_width(dot, active ? 14 : 8);
        }
    }

    void update_carousel_visuals()
    {
        if (!g_ui.carousel)
            return;

        const lv_coord_t contWidth = lv_obj_get_width(g_ui.carousel);
        const lv_coord_t centerX = -lv_obj_get_scroll_x(g_ui.carousel) + (contWidth / 2);

        int closestIdx = g_state.cardIndex;
        lv_coord_t closestDist = LV_COORD_MAX;

        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            CardWidgets &cw = g_ui.cards[i];
            if (!cw.container)
                continue;

            lv_coord_t cardCenter = lv_obj_get_x(cw.container) + (lv_obj_get_width(cw.container) / 2);
            lv_coord_t delta = LV_ABS(centerX - cardCenter);

            if (delta < closestDist)
            {
                closestDist = delta;
                closestIdx = static_cast<int>(i);
            }

            float ratio = static_cast<float>(delta) / static_cast<float>(contWidth / 2 + 1);
            ratio = std::min(1.0f, ratio);

            int zoom = static_cast<int>(256 * (1.18f - 0.25f * ratio)); // 118% center -> ~88% edge
            zoom = std::max(200, std::min(zoom, 310));
            lv_obj_set_style_transform_zoom(cw.container, zoom, 0);
            lv_obj_set_style_opa(cw.container, 255 - static_cast<int>(140 * ratio), 0);
            lv_obj_set_style_translate_y(cw.container, static_cast<lv_coord_t>(-10 * (1.0f - ratio)), 0);
        }

        if (g_state.cardIndex != closestIdx)
        {
            g_state.cardIndex = closestIdx;
            if (g_ui.title)
            {
                lv_label_set_text(g_ui.title, current_card().title);
            }
            update_page_indicator();
        }
    }

    void update_status_icons()
    {
        if (g_ui.wifiIcon)
        {
            // WiFi status visualization:
            // - Green solid: STA connected OR AP active with clients
            // - Green blinking: AP active but no clients (waiting for connection)
            // - Yellow blinking: STA connecting
            // - Red: AP and STA both inactive (error state)

            bool staConnected = g_state.lastWifi.staConnected;
            bool apActive = g_state.lastWifi.apActive;
            bool staConnecting = g_state.lastWifi.staConnecting;
            int apClients = g_state.lastWifi.apClients;

            lv_color_t col;
            lv_opa_t opa = LV_OPA_COVER;

            if (staConnected)
            {
                col = color_ok;
            }
            else if (apActive)
            {
                col = color_ok;
                opa = ((millis() / 800) % 2) ? LV_OPA_COVER : LV_OPA_60;
                if (apClients > 0)
                {
                    opa = LV_OPA_COVER;
                }
            }
            else if (staConnecting)
            {
                col = color_warn;
                opa = ((millis() / 300) % 2) ? LV_OPA_COVER : LV_OPA_40;
            }
            else
            {
                col = color_error;
            }

            lv_obj_set_style_text_color(g_ui.wifiIcon, col, 0);
            lv_obj_set_style_opa(g_ui.wifiIcon, opa, 0);
        }

        if (g_ui.bleIcon)
        {
            lv_color_t col = g_state.bleConnected ? lv_color_hex(0x4DA3FF) : (g_state.bleConnecting ? color_warn : color_error);
            lv_opa_t opa = (g_state.bleConnecting && (millis() / 400) % 2) ? LV_OPA_40 : LV_OPA_COVER;
            lv_obj_set_style_text_color(g_ui.bleIcon, col, 0);
            lv_obj_set_style_opa(g_ui.bleIcon, opa, 0);
        }
    }

    void update_badges()
    {
        if (g_ui.gearBadge)
        {
            lv_label_set_text(g_ui.gearBadge, g_state.gear <= 0 ? "N" : String(g_state.gear).c_str());
        }
        if (g_ui.shiftBadge)
        {
            lv_label_set_text(g_ui.shiftBadge, g_state.shift ? "SHIFT" : "Ready");
            lv_obj_set_style_bg_color(g_ui.shiftBadge, g_state.shift ? color_warn : lv_color_hex(0x1C2028), 0);
            lv_obj_set_style_text_color(g_ui.shiftBadge, g_state.shift ? lv_color_black() : lv_color_hex(0xE8EAED), 0);
        }
    }

    void show_home();

    void on_back(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_home();
    }

    void on_detail_gesture(lv_event_t *e)
    {
        if (lv_event_get_code(e) == LV_EVENT_GESTURE)
        {
            if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
            {
                show_home();
            }
        }
    }

    void attach_back_handler(lv_obj_t *obj)
    {
        lv_obj_add_event_cb(obj, on_detail_gesture, LV_EVENT_GESTURE, nullptr);
    }

    void scroll_to_card(int idx, lv_anim_enable_t anim)
    {
        if (!g_ui.carousel)
            return;
        const int count = static_cast<int>(CARD_COUNT);
        idx = (idx % count + count) % count;
        lv_obj_t *target = g_ui.cards[static_cast<size_t>(idx)].container;
        if (target)
        {
            lv_obj_scroll_to_view(target, anim);
        }
    }

    void open_detail(const CardDef &def);

    void on_card_click(lv_event_t *e)
    {
        lv_obj_t *target = lv_event_get_target(e);
        size_t idx = 0;
        for (; idx < CARD_COUNT; ++idx)
        {
            if (g_ui.cards[idx].container == target)
                break;
        }

        if (idx >= CARD_COUNT)
            return;

        mark_interacted();

        if (static_cast<int>(idx) != g_state.cardIndex)
        {
            scroll_to_card(static_cast<int>(idx), LV_ANIM_ON);
            return;
        }

        open_detail(CARDS[idx]);
    }

    void on_carousel_scroll(lv_event_t *e)
    {
        LV_UNUSED(e);
        mark_interacted();
        update_carousel_visuals();
    }

    void on_gesture(lv_event_t *e)
    {
        mark_interacted();
        if (g_state.inDetail)
            return;

        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT)
        {
            scroll_to_card(g_state.cardIndex + 1, LV_ANIM_ON);
        }
        else if (dir == LV_DIR_RIGHT)
        {
            scroll_to_card(g_state.cardIndex - 1, LV_ANIM_ON);
        }
    }

    lv_obj_t *make_detail_base(const char *title)
    {
        lv_obj_t *scr = lv_obj_create(nullptr);
        lv_obj_remove_style_all(scr);
        lv_obj_add_style(scr, &styleBg, 0);
        lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(scr, 12, 0);

        lv_obj_add_event_cb(scr, on_detail_gesture, LV_EVENT_GESTURE, nullptr);

        lv_obj_t *header = lv_obj_create(scr);
        lv_obj_remove_style_all(header);
        lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_layout(header, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *titleLbl = lv_label_create(header);
        lv_label_set_text(titleLbl, title);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_24, 0);

        lv_obj_t *back = lv_label_create(header);
        lv_label_set_text(back, LV_SYMBOL_LEFT " Back");
        lv_obj_add_style(back, &styleMuted, 0);
        lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, nullptr);
        attach_back_handler(back);

        lv_obj_t *body = lv_obj_create(scr);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_top(body, 8, 0);
        lv_obj_set_layout(body, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(body, 10, 0);

        return body;
    }

    CardWidgets make_icon_card(size_t idx)
    {
        const CardDef &def = CARDS[idx];
        CardWidgets w{};

        w.container = lv_obj_create(g_ui.carousel);
        lv_obj_remove_style_all(w.container);
        lv_obj_add_style(w.container, &styleCard, 0);
        lv_obj_set_size(w.container, 128, 156);
        lv_obj_set_style_bg_opa(w.container, LV_OPA_90, 0);
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_layout(w.container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(w.container, 10, 0);
        lv_obj_add_event_cb(w.container, on_card_click, LV_EVENT_CLICKED, nullptr);

        w.circle = lv_obj_create(w.container);
        lv_obj_remove_style_all(w.circle);
        lv_obj_add_style(w.circle, &styleCircle, 0);
        lv_obj_set_size(w.circle, 84, 84);
        lv_obj_clear_flag(w.circle, LV_OBJ_FLAG_SCROLLABLE);

        w.icon = lv_label_create(w.circle);
        lv_label_set_text(w.icon, def.symbol);
        lv_obj_set_style_text_font(w.icon, &lv_font_montserrat_32, 0);
        lv_obj_center(w.icon);

        w.label = lv_label_create(w.container);
        lv_label_set_text(w.label, def.title);
        lv_obj_add_style(w.label, &styleMuted, 0);
        lv_obj_set_style_text_font(w.label, &lv_font_montserrat_16, 0);

        return w;
    }

    void build_tutorial()
    {
        g_ui.tutorial = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.tutorial);
        lv_obj_add_style(g_ui.tutorial, &styleTutorial, 0);
        lv_obj_set_width(g_ui.tutorial, LV_PCT(94));
        lv_obj_align(g_ui.tutorial, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_set_style_text_color(g_ui.tutorial, color_muted, 0);
        lv_obj_clear_flag(g_ui.tutorial, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.tutorialLabel = lv_label_create(g_ui.tutorial);
        lv_label_set_text(g_ui.tutorialLabel, "Swipe left/right to browse.\nTap the big icon to open.\nSwipe right in menus to go back.");
        lv_label_set_long_mode(g_ui.tutorialLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(g_ui.tutorialLabel, LV_PCT(100));
        lv_obj_add_style(g_ui.tutorialLabel, &styleMuted, 0);
    }

    void build_page_indicator()
    {
        g_ui.pageIndicator = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.pageIndicator);
        lv_obj_set_size(g_ui.pageIndicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g_ui.pageIndicator, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.pageIndicator, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.pageIndicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.pageIndicator, 6, 0);
        lv_obj_align(g_ui.pageIndicator, LV_ALIGN_BOTTOM_MID, 0, -36);

        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            lv_obj_t *dot = lv_obj_create(g_ui.pageIndicator);
            lv_obj_remove_style_all(dot);
            lv_obj_add_style(dot, &styleDot, 0);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    void build_home(lv_disp_t *disp)
    {
        g_ui.root = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.root);
        lv_obj_add_style(g_ui.root, &styleBg, 0);
        lv_obj_set_size(g_ui.root, lv_disp_get_hor_res(disp), lv_disp_get_ver_res(disp));
        lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(g_ui.root, on_gesture, LV_EVENT_GESTURE, nullptr);

        g_ui.statusBar = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.statusBar);
        lv_obj_set_size(g_ui.statusBar, LV_PCT(100), 26);
        lv_obj_set_layout(g_ui.statusBar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.statusBar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.statusBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(g_ui.statusBar, 12, 0);
        lv_obj_set_style_pad_right(g_ui.statusBar, 12, 0);
        lv_obj_set_style_pad_top(g_ui.statusBar, 6, 0);
        lv_obj_set_style_pad_bottom(g_ui.statusBar, 2, 0);

        lv_obj_t *brand = lv_label_create(g_ui.statusBar);
        lv_label_set_text(brand, "ShiftLight");
        lv_obj_set_style_text_color(brand, lv_color_hex(0xE5E7EB), 0);

        lv_obj_t *statusIcons = lv_obj_create(g_ui.statusBar);
        lv_obj_remove_style_all(statusIcons);
        lv_obj_set_layout(statusIcons, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(statusIcons, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(statusIcons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(statusIcons, 8, 0);

        g_ui.wifiIcon = lv_label_create(statusIcons);
        lv_label_set_text(g_ui.wifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(g_ui.wifiIcon, &lv_font_montserrat_16, 0);

        g_ui.bleIcon = lv_label_create(statusIcons);
        lv_label_set_text(g_ui.bleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.bleIcon, &lv_font_montserrat_16, 0);

        g_ui.title = lv_label_create(g_ui.root);
        lv_label_set_text(g_ui.title, current_card().title);
        lv_obj_set_style_text_font(g_ui.title, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(g_ui.title, lv_color_hex(0xE5E7EB), 0);
        lv_obj_align(g_ui.title, LV_ALIGN_TOP_MID, 0, 34);

        g_ui.carousel = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.carousel);
        lv_obj_set_size(g_ui.carousel, LV_PCT(100), LV_PCT(64));
        lv_obj_set_style_pad_hor(g_ui.carousel, 18, 0);
        lv_obj_set_style_pad_ver(g_ui.carousel, 6, 0);
        lv_obj_set_layout(g_ui.carousel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.carousel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.carousel, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.carousel, 18, 0);
        lv_obj_set_scroll_dir(g_ui.carousel, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(g_ui.carousel, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(g_ui.carousel, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(g_ui.carousel, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_add_event_cb(g_ui.carousel, on_carousel_scroll, LV_EVENT_SCROLL, nullptr);

        g_ui.cards.fill(CardWidgets{});
        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            g_ui.cards[i] = make_icon_card(i);
        }

        g_ui.gearBadge = lv_label_create(g_ui.root);
        lv_obj_add_style(g_ui.gearBadge, &styleBadge, 0);
        lv_obj_align(g_ui.gearBadge, LV_ALIGN_BOTTOM_LEFT, 8, -8);

        g_ui.shiftBadge = lv_label_create(g_ui.root);
        lv_obj_add_style(g_ui.shiftBadge, &styleBadge, 0);
        lv_obj_align(g_ui.shiftBadge, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

        build_page_indicator();
        build_tutorial();

        update_carousel_visuals();
        update_status_icons();
        update_badges();
        update_page_indicator();
    }

    void show_home()
    {
        if (!g_ui.root)
            return;
        lv_disp_load_scr(g_ui.root);
        g_state.inDetail = false;
        update_carousel_visuals();
        update_status_icons();
        update_badges();
        update_page_indicator();
    }

    void open_brightness()
    {
        g_ui.detailContent = make_detail_base("Brightness");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        lv_obj_t *slider = lv_slider_create(g_ui.detailContent);
        lv_obj_set_width(slider, LV_PCT(100));
        lv_slider_set_range(slider, 10, 255);
        lv_slider_set_value(slider, cfg.displayBrightness, LV_ANIM_OFF);
        g_ui.brightnessSlider = slider;

        lv_obj_t *value = lv_label_create(g_ui.detailContent);
        lv_obj_add_style(value, &styleMuted, 0);
        lv_label_set_text_fmt(value, "%d", cfg.displayBrightness);
        g_ui.brightnessValue = value;

        lv_obj_add_event_cb(slider, [](lv_event_t *e)
                            {
            int val = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
            cfg.displayBrightness = val;
            if (g_ui.brightnessValue)
            {
                lv_label_set_text_fmt(g_ui.brightnessValue, "%d", val);
            }
            if (g_hooks.setBrightness)
            {
                g_hooks.setBrightness(static_cast<uint8_t>(val));
            } }, LV_EVENT_VALUE_CHANGED, nullptr);

        lv_obj_add_event_cb(slider, [](lv_event_t *)
                            { saveConfig(); }, LV_EVENT_RELEASED, nullptr);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    void open_vehicle()
    {
        g_ui.detailContent = make_detail_base("Vehicle");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        lv_obj_t *info = lv_label_create(g_ui.detailContent);
        lv_obj_add_style(info, &styleMuted, 0);
        lv_label_set_text_fmt(info, "Gear: %s\nShift: %s\nRPM: %s\nSpeed: %s",
                              g_state.gear <= 0 ? "N" : String(g_state.gear).c_str(),
                              g_state.shift ? "ON" : "off",
                              "n/a", // TODO: feed live RPM from core module
                              "n/a"); // TODO: feed live speed from core module

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    void open_wifi()
    {
        g_ui.detailContent = make_detail_base("WiFi");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        lv_obj_t *status = lv_label_create(g_ui.detailContent);
        lv_label_set_text(status, "Scan nearby networks or configure via WebUI.");
        lv_obj_add_style(status, &styleMuted, 0);

        g_ui.wifiList = lv_label_create(g_ui.detailContent);
        lv_obj_add_style(g_ui.wifiList, &styleMuted, 0);
        lv_label_set_text(g_ui.wifiList, "No scan yet");

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    void open_ble()
    {
        g_ui.detailContent = make_detail_base("Bluetooth");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        g_ui.bleList = lv_label_create(g_ui.detailContent);
        lv_obj_add_style(g_ui.bleList, &styleMuted, 0);
        lv_label_set_text(g_ui.bleList, "OBD dongle pairing via phone");

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    void open_settings()
    {
        g_ui.detailContent = make_detail_base("Settings");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        lv_obj_t *txt = lv_label_create(g_ui.detailContent);
        lv_label_set_text(txt, "ShiftLight / RpmCounter\nVersion: debug build\nSwipe right to go back");
        lv_obj_add_style(txt, &styleMuted, 0);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    void open_detail(const CardDef &def)
    {
        switch (def.screen)
        {
        case CardScreen::Brightness:
            open_brightness();
            break;
        case CardScreen::Vehicle:
            open_vehicle();
            break;
        case CardScreen::Wifi:
            open_wifi();
            break;
        case CardScreen::Bluetooth:
            open_ble();
            break;
        case CardScreen::Settings:
        default:
            open_settings();
            break;
        }
    }
} // namespace

void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks)
{
    g_hooks = hooks;
    apply_styles();
    build_home(disp);
    lv_disp_load_scr(g_ui.root);
}

void ui_s3_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleConnecting)
{
    g_state.lastWifi = wifiStatus;
    g_state.bleConnected = bleConnected;
    g_state.bleConnecting = bleConnecting;

    update_status_icons();
    update_badges();

    if (g_state.inDetail && g_ui.wifiList)
    {
        if (wifiStatus.scanResults.empty())
        {
            lv_label_set_text(g_ui.wifiList, "Nearby networks: none");
        }
        else
        {
            String lines = "Nearby:\n";
            size_t count = std::min<size_t>(wifiStatus.scanResults.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                lines += "- ";
                lines += wifiStatus.scanResults[i].ssid;
                lines += " (";
                lines += wifiStatus.scanResults[i].rssi;
                lines += "dBm)\n";
            }
            lv_label_set_text(g_ui.wifiList, lines.c_str());
        }
    }

    if (g_state.inDetail && g_ui.bleList)
    {
        const auto &res = getBleScanResults();
        if (res.empty())
        {
            lv_label_set_text(g_ui.bleList, "Scan via phone to pair OBD dongle");
        }
        else
        {
            String lines = "Nearby:\n";
            size_t count = std::min<size_t>(res.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                lines += "- ";
                lines += res[i].name;
                lines += " (";
                lines += res[i].address;
                lines += ")\n";
            }
            lv_label_set_text(g_ui.bleList, lines.c_str());
        }
    }
}

void ui_s3_set_gear(int gear)
{
    g_state.gear = gear;
    update_badges();
}

void ui_s3_set_shiftlight(bool active)
{
    g_state.shift = active;
    update_badges();
}

void ui_s3_show_logo()
{
    if (!g_ui.root)
        return;
    if (!g_ui.logoOverlay)
    {
        g_ui.logoOverlay = lv_label_create(g_ui.root);
        lv_label_set_text(g_ui.logoOverlay, "ShiftLight");
        lv_obj_set_style_text_font(g_ui.logoOverlay, &lv_font_montserrat_32, 0);
        lv_obj_align(g_ui.logoOverlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_color(g_ui.logoOverlay, lv_color_hex(0xE5E7EB), 0);
    }
    lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);
}
