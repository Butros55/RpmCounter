#include "ui_s3_main.h"

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <vector>

#include "bluetooth/ble_obd.h"
#include "core/config.h"
#include "core/state.h"
#include "core/wifi.h"

namespace
{
    enum class HomeCard
    {
        Info,
        Wifi,
        Bluetooth,
        Brightness,
        Settings
    };

    struct CardDef
    {
        HomeCard card;
        const char *title;
        const char *icon;
    };

    constexpr std::array<CardDef, 5> CARD_DEFS{{
        {HomeCard::Info, "Info", LV_SYMBOL_HOME},
        {HomeCard::Wifi, "WiFi", LV_SYMBOL_WIFI},
        {HomeCard::Bluetooth, "Bluetooth", LV_SYMBOL_BLUETOOTH},
        {HomeCard::Brightness, "Brightness", LV_SYMBOL_SETTINGS},
        {HomeCard::Settings, "Settings", LV_SYMBOL_SETTINGS},
    }};

    const CardDef &card_from_type(HomeCard card)
    {
        for (const auto &def : CARD_DEFS)
        {
            if (def.card == card)
            {
                return def;
            }
        }
        return CARD_DEFS.front();
    }

    struct CardWidgets
    {
        lv_obj_t *container = nullptr;
        lv_obj_t *circle = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *label = nullptr;
    };

    struct DataPage
    {
        lv_obj_t *value = nullptr;
        lv_obj_t *caption = nullptr;
        const char *label = nullptr;
    };

    struct UiRefs
    {
        lv_disp_t *disp = nullptr;
        lv_obj_t *home = nullptr;
        lv_obj_t *statusBar = nullptr;
        lv_obj_t *statusTitle = nullptr;
        lv_obj_t *wifiIcon = nullptr;
        lv_obj_t *btIcon = nullptr;
        lv_obj_t *carousel = nullptr;
        lv_obj_t *pageIndicator = nullptr;
        lv_obj_t *tutorial = nullptr;
        lv_obj_t *gearBadge = nullptr;
        lv_obj_t *shiftBadge = nullptr;
        lv_obj_t *logoOverlay = nullptr;

        // Detail screens
        lv_obj_t *detail = nullptr;
        lv_obj_t *detailBody = nullptr;
        lv_obj_t *wifiList = nullptr;
        lv_obj_t *btList = nullptr;
        lv_obj_t *brightnessSlider = nullptr;
        lv_obj_t *brightnessValue = nullptr;
        lv_obj_t *dataPager = nullptr;

        std::array<CardWidgets, CARD_DEFS.size()> cards{};
        std::vector<DataPage> dataPages;
    };

    struct UiState
    {
        int cardIndex = 0;
        bool inDetail = false;
        bool tutorialVisible = true;
        bool hasInteracted = false;
        int gear = 0;
        bool shift = false;
        WifiStatus wifiStatus{};
        bool bleConnected = false;
        bool bleConnecting = false;
        bool pendingDetailOpen = false;
        HomeCard activeDetail = HomeCard::Info;
    };

    UiRefs g_ui;
    UiState g_state;
    UiDisplayHooks g_hooks;

    lv_style_t styleBg;
    lv_style_t styleMuted;
    lv_style_t styleCard;
    lv_style_t styleCircle;
    lv_style_t styleDot;
    lv_style_t styleTutorial;
    lv_style_t styleBadge;
    lv_style_t styleHeader;
    lv_style_t styleValue;

    const lv_color_t COLOR_BG = lv_color_hex(0x000000);
    const lv_color_t COLOR_MUTED = lv_color_hex(0x8A94A7);
    const lv_color_t COLOR_ACCENT = lv_color_hex(0x5AC8FA);
    const lv_color_t COLOR_WARN = lv_color_hex(0xF5A524);
    const lv_color_t COLOR_ERROR = lv_color_hex(0xE84855);
    const lv_color_t COLOR_GOOD = lv_color_hex(0x56F38A);

    void mark_interacted()
    {
        if (g_state.hasInteracted)
            return;
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
        lv_style_set_bg_color(&styleBg, COLOR_BG);
        lv_style_set_bg_opa(&styleBg, LV_OPA_COVER);
        lv_style_set_pad_all(&styleBg, 0);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, COLOR_MUTED);

        lv_style_init(&styleCard);
        lv_style_set_bg_color(&styleCard, lv_color_hex(0x0D0F14));
        lv_style_set_bg_opa(&styleCard, LV_OPA_90);
        lv_style_set_radius(&styleCard, 14);
        lv_style_set_border_width(&styleCard, 1);
        lv_style_set_border_color(&styleCard, lv_color_hex(0x1c1f27));
        lv_style_set_shadow_width(&styleCard, 18);
        lv_style_set_shadow_spread(&styleCard, 4);
        lv_style_set_shadow_color(&styleCard, lv_color_hex(0x05070b));
        lv_style_set_shadow_opa(&styleCard, LV_OPA_60);
        lv_style_set_pad_all(&styleCard, 12);

        lv_style_init(&styleCircle);
        lv_style_set_radius(&styleCircle, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleCircle, lv_color_hex(0x10141d));
        lv_style_set_border_width(&styleCircle, 2);
        lv_style_set_border_color(&styleCircle, COLOR_ACCENT);
        lv_style_set_shadow_width(&styleCircle, 12);
        lv_style_set_shadow_color(&styleCircle, lv_color_hex(0x0d1b24));
        lv_style_set_shadow_opa(&styleCircle, LV_OPA_50);

        lv_style_init(&styleDot);
        lv_style_set_radius(&styleDot, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleDot, lv_color_hex(0x1d2129));
        lv_style_set_bg_opa(&styleDot, LV_OPA_COVER);

        lv_style_init(&styleTutorial);
        lv_style_set_radius(&styleTutorial, 12);
        lv_style_set_bg_color(&styleTutorial, lv_color_hex(0x1A1E26));
        lv_style_set_bg_opa(&styleTutorial, LV_OPA_70);
        lv_style_set_pad_all(&styleTutorial, 10);
        lv_style_set_border_width(&styleTutorial, 1);
        lv_style_set_border_color(&styleTutorial, lv_color_hex(0x2c323f));

        lv_style_init(&styleBadge);
        lv_style_set_bg_color(&styleBadge, lv_color_hex(0x11141b));
        lv_style_set_bg_opa(&styleBadge, LV_OPA_COVER);
        lv_style_set_radius(&styleBadge, 12);
        lv_style_set_pad_hor(&styleBadge, 10);
        lv_style_set_pad_ver(&styleBadge, 6);
        lv_style_set_border_width(&styleBadge, 1);
        lv_style_set_border_color(&styleBadge, lv_color_hex(0x1f2430));
        lv_style_set_shadow_width(&styleBadge, 10);
        lv_style_set_shadow_color(&styleBadge, lv_color_hex(0x050608));
        lv_style_set_shadow_opa(&styleBadge, LV_OPA_40);

        lv_style_init(&styleHeader);
        lv_style_set_bg_opa(&styleHeader, LV_OPA_TRANSP);
        lv_style_set_pad_all(&styleHeader, 0);
        lv_style_set_text_color(&styleHeader, lv_color_hex(0xF0F1F3));

        lv_style_init(&styleValue);
        lv_style_set_text_color(&styleValue, lv_color_white());
        lv_style_set_text_font(&styleValue, &lv_font_montserrat_42);
    }

    void move_overlays(lv_obj_t *parent)
    {
        if (g_ui.gearBadge)
        {
            lv_obj_set_parent(g_ui.gearBadge, parent);
            lv_obj_align(g_ui.gearBadge, LV_ALIGN_BOTTOM_LEFT, 12, -14);
        }
        if (g_ui.shiftBadge)
        {
            lv_obj_set_parent(g_ui.shiftBadge, parent);
            lv_obj_align(g_ui.shiftBadge, LV_ALIGN_BOTTOM_RIGHT, -12, -14);
        }
        if (g_ui.logoOverlay)
        {
            lv_obj_set_parent(g_ui.logoOverlay, parent);
            lv_obj_center(g_ui.logoOverlay);
        }
    }

    void update_badges()
    {
        if (g_ui.gearBadge)
        {
            const char *gearText = (g_state.gear <= 0) ? "N" : String(g_state.gear).c_str();
            lv_label_set_text(g_ui.gearBadge, gearText);
        }
        if (g_ui.shiftBadge)
        {
            lv_obj_set_style_bg_color(g_ui.shiftBadge, g_state.shift ? COLOR_WARN : lv_color_hex(0x11141b), 0);
            lv_obj_set_style_text_color(g_ui.shiftBadge, g_state.shift ? lv_color_black() : lv_color_hex(0xDDE1E8), 0);
            lv_label_set_text(g_ui.shiftBadge, g_state.shift ? "SHIFT" : "");
            if (!g_state.shift)
            {
                lv_label_set_text(g_ui.shiftBadge, "");
            }
        }
    }

    void update_status_icons()
    {
        if (g_ui.wifiIcon)
        {
            bool wifiConnected = g_state.wifiStatus.staConnected || g_state.wifiStatus.apActive;
            bool wifiConnecting = g_state.wifiStatus.staConnecting || g_state.wifiStatus.scanRunning;
            lv_color_t color = COLOR_ERROR;
            lv_opa_t opa = LV_OPA_COVER;

            if (wifiConnected)
            {
                color = COLOR_GOOD;
            }
            else if (wifiConnecting)
            {
                color = COLOR_WARN;
                opa = ((millis() / 400) % 2) ? LV_OPA_40 : LV_OPA_COVER;
            }

            lv_obj_set_style_text_color(g_ui.wifiIcon, color, 0);
            lv_obj_set_style_opa(g_ui.wifiIcon, opa, 0);
        }

        if (g_ui.btIcon)
        {
            lv_color_t color = COLOR_ERROR;
            lv_opa_t opa = LV_OPA_COVER;

            if (g_state.bleConnected)
            {
                color = lv_color_hex(0x4DA3FF);
            }
            else if (g_state.bleConnecting)
            {
                color = lv_color_hex(0x4DA3FF);
                opa = ((millis() / 350) % 2) ? LV_OPA_40 : LV_OPA_COVER;
            }

            lv_obj_set_style_text_color(g_ui.btIcon, color, 0);
            lv_obj_set_style_opa(g_ui.btIcon, opa, 0);
        }
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
            lv_obj_set_width(dot, active ? 14 : 8);
            lv_obj_set_style_bg_color(dot, active ? COLOR_ACCENT : lv_color_hex(0x1d2129), 0);
            lv_obj_set_style_opa(dot, active ? LV_OPA_COVER : LV_OPA_60, 0);
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

        for (size_t i = 0; i < CARD_DEFS.size(); ++i)
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
            int zoom = static_cast<int>(256 * (1.25f - 0.40f * ratio));
            zoom = std::max(200, std::min(zoom, 340));
            lv_obj_set_style_transform_zoom(cw.container, zoom, 0);
            lv_obj_set_style_opa(cw.container, 255 - static_cast<int>(130 * ratio), 0);
            lv_obj_set_style_translate_y(cw.container, static_cast<lv_coord_t>(-8 * (1.0f - ratio)), 0);
        }

        if (g_state.cardIndex != closestIdx)
        {
            g_state.cardIndex = closestIdx;
            update_page_indicator();
        }
    }

    void scroll_to_card(int idx, lv_anim_enable_t anim)
    {
        if (!g_ui.carousel)
            return;
        const int count = static_cast<int>(CARD_DEFS.size());
        idx = (idx % count + count) % count;
        lv_obj_t *target = g_ui.cards[static_cast<size_t>(idx)].container;
        if (target)
        {
            lv_obj_scroll_to_view(target, anim);
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
        if (lv_event_get_code(e) != LV_EVENT_GESTURE)
            return;
        if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
        {
            show_home();
        }
    }

    void attach_back_handler(lv_obj_t *obj)
    {
        lv_obj_add_event_cb(obj, on_detail_gesture, LV_EVENT_GESTURE, nullptr);
    }

    void on_card_click(lv_event_t *e)
    {
        lv_obj_t *target = lv_event_get_target(e);
        size_t idx = 0;
        for (; idx < CARD_DEFS.size(); ++idx)
        {
            if (g_ui.cards[idx].container == target)
                break;
        }
        if (idx >= CARD_DEFS.size())
            return;

        mark_interacted();

        if (static_cast<int>(idx) != g_state.cardIndex)
        {
            scroll_to_card(static_cast<int>(idx), LV_ANIM_ON);
            return;
        }

        g_state.activeDetail = CARD_DEFS[idx].card;
        g_state.pendingDetailOpen = true;
    }

    void on_carousel_scroll(lv_event_t *e)
    {
        LV_UNUSED(e);
        mark_interacted();
        update_carousel_visuals();
    }

    void on_home_gesture(lv_event_t *e)
    {
        if (g_state.inDetail)
            return;
        mark_interacted();
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

    lv_obj_t *build_badge(lv_obj_t *parent, const char *txt)
    {
        lv_obj_t *badge = lv_label_create(parent);
        lv_obj_add_style(badge, &styleBadge, 0);
        lv_label_set_text(badge, txt);
        return badge;
    }

    void build_status_bar(lv_obj_t *parent)
    {
        g_ui.statusBar = lv_obj_create(parent);
        lv_obj_remove_style_all(g_ui.statusBar);
        lv_obj_add_style(g_ui.statusBar, &styleHeader, 0);
        lv_obj_set_size(g_ui.statusBar, LV_PCT(100), 24);
        lv_obj_set_layout(g_ui.statusBar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.statusBar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.statusBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        g_ui.statusTitle = lv_label_create(g_ui.statusBar);
        lv_label_set_text(g_ui.statusTitle, "RpmCounter");
        lv_obj_add_style(g_ui.statusTitle, &styleMuted, 0);

        lv_obj_t *right = lv_obj_create(g_ui.statusBar);
        lv_obj_remove_style_all(right);
        lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(right, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(right, 8, 0);

        g_ui.wifiIcon = lv_label_create(right);
        lv_label_set_text(g_ui.wifiIcon, LV_SYMBOL_WIFI);
        lv_obj_add_style(g_ui.wifiIcon, &styleMuted, 0);

        g_ui.btIcon = lv_label_create(right);
        lv_label_set_text(g_ui.btIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_add_style(g_ui.btIcon, &styleMuted, 0);
    }

    CardWidgets build_card(lv_obj_t *parent, const CardDef &def)
    {
        CardWidgets w{};

        w.container = lv_obj_create(parent);
        lv_obj_remove_style_all(w.container);
        lv_obj_add_style(w.container, &styleCard, 0);
        lv_obj_set_size(w.container, 124, 152);
        lv_obj_set_layout(w.container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(w.container, 10, 0);
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(w.container, on_card_click, LV_EVENT_CLICKED, nullptr);

        w.circle = lv_obj_create(w.container);
        lv_obj_remove_style_all(w.circle);
        lv_obj_add_style(w.circle, &styleCircle, 0);
        lv_obj_set_size(w.circle, 86, 86);
        lv_obj_clear_flag(w.circle, LV_OBJ_FLAG_SCROLLABLE);

        w.icon = lv_label_create(w.circle);
        lv_label_set_text(w.icon, def.icon);
        lv_obj_set_style_text_font(w.icon, &lv_font_montserrat_32, 0);
        lv_obj_center(w.icon);

        w.label = lv_label_create(w.container);
        lv_label_set_text(w.label, def.title);
        lv_obj_add_style(w.label, &styleMuted, 0);
        lv_obj_set_style_text_font(w.label, &lv_font_montserrat_16, 0);

        return w;
    }

    void build_indicator(lv_obj_t *parent)
    {
        g_ui.pageIndicator = lv_obj_create(parent);
        lv_obj_remove_style_all(g_ui.pageIndicator);
        lv_obj_set_size(g_ui.pageIndicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g_ui.pageIndicator, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.pageIndicator, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.pageIndicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.pageIndicator, 6, 0);
        lv_obj_align(g_ui.pageIndicator, LV_ALIGN_BOTTOM_MID, 0, -18);

        for (size_t i = 0; i < CARD_DEFS.size(); ++i)
        {
            lv_obj_t *dot = lv_obj_create(g_ui.pageIndicator);
            lv_obj_remove_style_all(dot);
            lv_obj_add_style(dot, &styleDot, 0);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }
        update_page_indicator();
    }

    void build_tutorial(lv_obj_t *parent)
    {
        g_ui.tutorial = lv_obj_create(parent);
        lv_obj_remove_style_all(g_ui.tutorial);
        lv_obj_add_style(g_ui.tutorial, &styleTutorial, 0);
        lv_obj_set_width(g_ui.tutorial, LV_PCT(92));
        lv_obj_align(g_ui.tutorial, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_clear_flag(g_ui.tutorial, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(g_ui.tutorial);
        lv_label_set_text(lbl, "Swipe left/right to navigate\nTap center to open");
        lv_obj_add_style(lbl, &styleMuted, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, LV_PCT(100));
    }

    lv_obj_t *make_detail_base(const char *title)
    {
        if (g_ui.detail)
        {
            lv_obj_del(g_ui.detail);
        }
        g_ui.detail = nullptr;
        g_ui.detailBody = nullptr;
        g_ui.wifiList = nullptr;
        g_ui.btList = nullptr;
        g_ui.brightnessSlider = nullptr;
        g_ui.brightnessValue = nullptr;
        g_ui.dataPager = nullptr;
        g_ui.dataPages.clear();

        lv_obj_t *scr = lv_obj_create(nullptr);
        lv_obj_remove_style_all(scr);
        lv_obj_add_style(scr, &styleBg, 0);
        lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(scr, on_detail_gesture, LV_EVENT_GESTURE, nullptr);

        lv_obj_t *header = lv_obj_create(scr);
        lv_obj_remove_style_all(header);
        lv_obj_add_style(header, &styleHeader, 0);
        lv_obj_set_size(header, LV_PCT(100), 42);
        lv_obj_set_layout(header, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(header, 12, 0);
        lv_obj_set_style_pad_right(header, 12, 0);
        lv_obj_set_style_pad_column(header, 10, 0);

        lv_obj_t *back = lv_btn_create(header);
        lv_obj_remove_style_all(back);
        lv_obj_set_size(back, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(back, 6, 0);
        lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, nullptr);
        attach_back_handler(back);

        lv_obj_t *backLbl = lv_label_create(back);
        lv_label_set_text(backLbl, LV_SYMBOL_LEFT);
        lv_obj_add_style(backLbl, &styleHeader, 0);

        lv_obj_t *titleLbl = lv_label_create(header);
        lv_label_set_text(titleLbl, title);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_22, 0);

        lv_obj_t *body = lv_obj_create(scr);
        lv_obj_remove_style_all(body);
        lv_obj_add_style(body, &styleBg, 0);
        lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(body, 14, 0);
        lv_obj_set_layout(body, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(body, 10, 0);

        g_ui.detail = scr;
        g_ui.detailBody = body;
        move_overlays(scr);
        return body;
    }

    void open_settings()
    {
        make_detail_base("Settings");

        lv_obj_t *label = lv_label_create(g_ui.detailBody);
        lv_label_set_text(label, "RpmCounter / ShiftLight\nESP32-S3 System\nFirmware: debug build\nAuthor: Butros55");
        lv_obj_add_style(label, &styleMuted, 0);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
        g_state.activeDetail = HomeCard::Settings;
    }

    void open_brightness()
    {
        make_detail_base("Brightness");

        g_ui.brightnessSlider = lv_slider_create(g_ui.detailBody);
        lv_obj_set_width(g_ui.brightnessSlider, LV_PCT(100));
        lv_slider_set_range(g_ui.brightnessSlider, 5, 255);
        lv_slider_set_value(g_ui.brightnessSlider, cfg.displayBrightness, LV_ANIM_OFF);

        g_ui.brightnessValue = lv_label_create(g_ui.detailBody);
        lv_obj_add_style(g_ui.brightnessValue, &styleMuted, 0);
        lv_label_set_text_fmt(g_ui.brightnessValue, "%d%%", cfg.displayBrightness * 100 / 255);

        lv_obj_add_event_cb(g_ui.brightnessSlider, [](lv_event_t *e) {
            int32_t val = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
            cfg.displayBrightness = val;
            lv_label_set_text_fmt(g_ui.brightnessValue, "%d%%", val * 100 / 255);
            if (g_hooks.setBrightness)
            {
                g_hooks.setBrightness(static_cast<uint8_t>(val));
            }
            // TODO: persist brightness if required
        }, LV_EVENT_VALUE_CHANGED, nullptr);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
        g_state.activeDetail = HomeCard::Brightness;
    }

    void open_wifi()
    {
        make_detail_base("WiFi");

        g_ui.wifiList = lv_label_create(g_ui.detailBody);
        lv_obj_add_style(g_ui.wifiList, &styleMuted, 0);
        lv_label_set_text(g_ui.wifiList, "Scanning for networks...");

        // TODO: insert real WiFi scan trigger if available

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
        g_state.activeDetail = HomeCard::Wifi;
    }

    void open_ble()
    {
        make_detail_base("Bluetooth");

        g_ui.btList = lv_label_create(g_ui.detailBody);
        lv_obj_add_style(g_ui.btList, &styleMuted, 0);
        lv_label_set_text(g_ui.btList, "Searching for OBD dongles...");

        // TODO: trigger BLE scan if backend exposes API

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
        g_state.activeDetail = HomeCard::Bluetooth;
    }

    lv_obj_t *build_data_page(lv_obj_t *parent, const char *label)
    {
        lv_obj_t *page = lv_obj_create(parent);
        lv_obj_remove_style_all(page);
        lv_obj_add_style(page, &styleBg, 0);
        lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(page, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(page, 8, 0);

        lv_obj_t *value = lv_label_create(page);
        lv_obj_add_style(value, &styleValue, 0);
        lv_label_set_text(value, "—");

        lv_obj_t *caption = lv_label_create(page);
        lv_label_set_text(caption, label);
        lv_obj_add_style(caption, &styleMuted, 0);
        lv_obj_set_style_text_font(caption, &lv_font_montserrat_18, 0);

        g_ui.dataPages.push_back({value, caption, label});
        return page;
    }

    void open_vehicle()
    {
        make_detail_base("Live Data");

        g_ui.dataPager = lv_obj_create(g_ui.detailBody);
        lv_obj_remove_style_all(g_ui.dataPager);
        lv_obj_add_style(g_ui.dataPager, &styleBg, 0);
        lv_obj_set_size(g_ui.dataPager, LV_PCT(100), LV_PCT(100));
        lv_obj_set_layout(g_ui.dataPager, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.dataPager, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(g_ui.dataPager, 6, 0);
        lv_obj_set_scroll_dir(g_ui.dataPager, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(g_ui.dataPager, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(g_ui.dataPager, LV_SCROLLBAR_MODE_OFF);

        g_ui.dataPages.clear();
        build_data_page(g_ui.dataPager, "RPM");
        build_data_page(g_ui.dataPager, "Speed");
        build_data_page(g_ui.dataPager, "Gear");
        build_data_page(g_ui.dataPager, "Coolant Temp");
        build_data_page(g_ui.dataPager, "Intake Temp");
        build_data_page(g_ui.dataPager, "Voltage");

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
        g_state.activeDetail = HomeCard::Info;
    }

    void open_detail(const CardDef &def)
    {
        switch (def.card)
        {
        case HomeCard::Info:
            open_vehicle();
            break;
        case HomeCard::Wifi:
            open_wifi();
            break;
        case HomeCard::Bluetooth:
            open_ble();
            break;
        case HomeCard::Brightness:
            open_brightness();
            break;
        case HomeCard::Settings:
        default:
            open_settings();
            break;
        }
    }

    void update_live_data_pages()
    {
        if (!g_state.inDetail || g_state.activeDetail != HomeCard::Info)
            return;
        if (g_ui.dataPages.size() < 6)
            return;

        auto set_value = [](lv_obj_t *lbl, const String &text) {
            if (lbl)
            {
                lv_label_set_text(lbl, text.c_str());
            }
        };

        set_value(g_ui.dataPages[0].value, String(g_currentRpm)); // TODO: insert real RPM from OBD
        set_value(g_ui.dataPages[1].value, String(g_vehicleSpeedKmh));
        set_value(g_ui.dataPages[2].value, g_estimatedGear <= 0 ? "N" : String(g_estimatedGear));
        set_value(g_ui.dataPages[3].value, "—" ); // TODO: insert real coolant temp
        set_value(g_ui.dataPages[4].value, "—" ); // TODO: insert real intake temp
        set_value(g_ui.dataPages[5].value, "—" ); // TODO: insert real system voltage
    }

    void update_wifi_list()
    {
        if (!g_state.inDetail || g_state.activeDetail != HomeCard::Wifi || !g_ui.wifiList)
            return;

        if (g_state.wifiStatus.scanResults.empty())
        {
            lv_label_set_text(g_ui.wifiList, "Configure via smartphone\nNo networks listed");
            return;
        }

        String lines;
        size_t count = std::min<size_t>(g_state.wifiStatus.scanResults.size(), 5);
        for (size_t i = 0; i < count; ++i)
        {
            lines += LV_SYMBOL_WIFI;
            lines += "  ";
            lines += g_state.wifiStatus.scanResults[i].ssid;
            lines += "  (";
            lines += g_state.wifiStatus.scanResults[i].rssi;
            lines += "dBm)\n";
        }
        lv_label_set_text(g_ui.wifiList, lines.c_str());
    }

    void update_ble_list()
    {
        if (!g_state.inDetail || g_state.activeDetail != HomeCard::Bluetooth || !g_ui.btList)
            return;

        const auto &res = getBleScanResults();
        if (res.empty())
        {
            lv_label_set_text(g_ui.btList, "OBD dongle pairing via phone\nNo devices yet");
            return;
        }

        String lines;
        size_t count = std::min<size_t>(res.size(), 5);
        for (size_t i = 0; i < count; ++i)
        {
            lines += LV_SYMBOL_BLUETOOTH;
            lines += "  ";
            lines += res[i].name;
            lines += "\n";
        }
        lv_label_set_text(g_ui.btList, lines.c_str());
    }

    void build_home(lv_disp_t *disp)
    {
        g_ui.disp = disp;
        g_ui.home = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.home);
        lv_obj_add_style(g_ui.home, &styleBg, 0);
        lv_obj_set_size(g_ui.home, lv_disp_get_hor_res(disp), lv_disp_get_ver_res(disp));
        lv_obj_clear_flag(g_ui.home, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(g_ui.home, on_home_gesture, LV_EVENT_GESTURE, nullptr);

        build_status_bar(g_ui.home);

        lv_obj_t *mainArea = lv_obj_create(g_ui.home);
        lv_obj_remove_style_all(mainArea);
        lv_obj_add_style(mainArea, &styleBg, 0);
        lv_obj_set_size(mainArea, LV_PCT(100), LV_PCT(100));
        lv_obj_set_layout(mainArea, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(mainArea, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(mainArea, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_top(mainArea, 16, 0);
        lv_obj_set_style_pad_row(mainArea, 16, 0);

        g_ui.carousel = lv_obj_create(mainArea);
        lv_obj_remove_style_all(g_ui.carousel);
        lv_obj_add_style(g_ui.carousel, &styleBg, 0);
        lv_obj_set_size(g_ui.carousel, LV_PCT(100), 260);
        lv_obj_set_layout(g_ui.carousel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.carousel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.carousel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.carousel, 16, 0);
        lv_obj_set_scroll_dir(g_ui.carousel, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(g_ui.carousel, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(g_ui.carousel, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_event_cb(g_ui.carousel, on_carousel_scroll, LV_EVENT_SCROLL, nullptr);

        for (size_t i = 0; i < CARD_DEFS.size(); ++i)
        {
            g_ui.cards[i] = build_card(g_ui.carousel, CARD_DEFS[i]);
        }

        build_indicator(mainArea);
        build_tutorial(g_ui.home);

        g_ui.gearBadge = build_badge(g_ui.home, "N");
        lv_obj_align(g_ui.gearBadge, LV_ALIGN_BOTTOM_LEFT, 12, -14);

        g_ui.shiftBadge = build_badge(g_ui.home, "");
        lv_obj_align(g_ui.shiftBadge, LV_ALIGN_BOTTOM_RIGHT, -12, -14);

        update_carousel_visuals();
        update_page_indicator();
        update_badges();
    }

    void show_home()
    {
        g_state.inDetail = false;
        g_state.pendingDetailOpen = false;
        move_overlays(g_ui.home);
        lv_disp_load_scr(g_ui.home);
        update_carousel_visuals();
        update_page_indicator();
        update_badges();
    }
}

void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks)
{
    g_hooks = hooks;
    apply_styles();
    build_home(disp);
    lv_disp_load_scr(g_ui.home);
}

void ui_s3_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleConnecting)
{
    g_state.wifiStatus = wifiStatus;
    g_state.bleConnected = bleConnected;
    g_state.bleConnecting = bleConnecting;

    update_status_icons();
    update_badges();

    if (g_state.hasInteracted && g_ui.tutorial && g_state.tutorialVisible)
    {
        lv_obj_add_flag(g_ui.tutorial, LV_OBJ_FLAG_HIDDEN);
        g_state.tutorialVisible = false;
    }

    update_wifi_list();
    update_ble_list();
    update_live_data_pages();

    if (!g_state.inDetail && g_state.pendingDetailOpen)
    {
        open_detail(card_from_type(g_state.activeDetail));
        g_state.pendingDetailOpen = false;
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
    lv_obj_t *parent = lv_scr_act();
    if (!parent)
        return;

    if (!g_ui.logoOverlay)
    {
        g_ui.logoOverlay = lv_label_create(parent);
        lv_obj_set_style_text_font(g_ui.logoOverlay, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(g_ui.logoOverlay, lv_color_hex(0xF2F4F7), 0);
    }

    lv_obj_set_parent(g_ui.logoOverlay, parent);
    lv_label_set_text(g_ui.logoOverlay, "ShiftLight");
    lv_obj_center(g_ui.logoOverlay);
    lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);
}
