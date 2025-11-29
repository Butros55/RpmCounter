#include "ui_s3_main.h"

#include <Arduino.h>
#include <vector>

#include "core/config.h"
#include "core/state.h"
#include "core/utils.h"
#include "bluetooth/ble_obd.h"

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

    const CardDef CARDS[] = {
        {CardScreen::Brightness, "Brightness", LV_SYMBOL_EYE_OPEN},
        {CardScreen::Vehicle, "Vehicle", LV_SYMBOL_GPS},
        {CardScreen::Wifi, "WiFi", LV_SYMBOL_WIFI},
        {CardScreen::Bluetooth, "Bluetooth", LV_SYMBOL_BLUETOOTH},
        {CardScreen::Settings, "Settings", LV_SYMBOL_SETTINGS},
    };

    struct UiRefs
    {
        lv_disp_t *disp = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *statusRow = nullptr;
        lv_obj_t *wifiIcon = nullptr;
        lv_obj_t *bleIcon = nullptr;
        lv_obj_t *title = nullptr;
        lv_obj_t *carousel = nullptr;
        lv_obj_t *cardLeft = nullptr;
        lv_obj_t *cardCenter = nullptr;
        lv_obj_t *cardRight = nullptr;
        lv_obj_t *hint = nullptr;
        lv_obj_t *gearBadge = nullptr;
        lv_obj_t *shiftBadge = nullptr;
        lv_obj_t *logoOverlay = nullptr;
        lv_obj_t *detail = nullptr;
        lv_obj_t *detailContent = nullptr;
        lv_obj_t *wifiList = nullptr;
        lv_obj_t *bleList = nullptr;
        lv_obj_t *brightnessSlider = nullptr;
        lv_obj_t *brightnessValue = nullptr;
    };

    struct UiState
    {
        int cardIndex = 0;
        bool inDetail = false;
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
    lv_style_t styleIconCircle;
    lv_style_t styleCard;
    lv_style_t styleMuted;
    lv_style_t styleBadge;

    lv_color_t color_ok = lv_color_hex(0x56F38A);
    lv_color_t color_warn = lv_color_hex(0xF5A524);
    lv_color_t color_error = lv_color_hex(0xF55E61);
    lv_color_t color_card = lv_color_hex(0x12141A);

    const CardDef &current_card()
    {
        const size_t count = sizeof(CARDS) / sizeof(CARDS[0]);
        g_state.cardIndex = (g_state.cardIndex % static_cast<int>(count) + static_cast<int>(count)) % static_cast<int>(count);
        return CARDS[g_state.cardIndex];
    }

    const CardDef &card_at_offset(int offset)
    {
        const size_t count = sizeof(CARDS) / sizeof(CARDS[0]);
        int idx = (g_state.cardIndex + static_cast<int>(count) + offset) % static_cast<int>(count);
        return CARDS[idx];
    }

    void apply_styles()
    {
        lv_style_init(&styleBg);
        lv_style_set_bg_color(&styleBg, lv_color_black());
        lv_style_set_bg_opa(&styleBg, LV_OPA_COVER);

        lv_style_init(&styleIconCircle);
        lv_style_set_radius(&styleIconCircle, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleIconCircle, color_card);
        lv_style_set_bg_opa(&styleIconCircle, LV_OPA_COVER);
        lv_style_set_border_width(&styleIconCircle, 2);
        lv_style_set_border_color(&styleIconCircle, lv_color_hex(0x2D9CDB));

        lv_style_init(&styleCard);
        lv_style_set_bg_color(&styleCard, color_card);
        lv_style_set_bg_opa(&styleCard, LV_OPA_COVER);
        lv_style_set_radius(&styleCard, 12);
        lv_style_set_pad_all(&styleCard, 10);
        lv_style_set_border_width(&styleCard, 0);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, lv_color_hex(0x9AA2AD));

        lv_style_init(&styleBadge);
        lv_style_set_radius(&styleBadge, 12);
        lv_style_set_bg_color(&styleBadge, lv_color_hex(0x1C2028));
        lv_style_set_bg_opa(&styleBadge, LV_OPA_COVER);
        lv_style_set_pad_all(&styleBadge, 6);
    }

    lv_obj_t *make_icon_card(lv_obj_t *parent, const CardDef &def, bool focus)
    {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_remove_style_all(card);
        lv_obj_add_style(card, &styleCard, 0);
        lv_obj_set_size(card, focus ? 140 : 110, focus ? 170 : 140);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(card, 8, 0);

        lv_obj_t *circle = lv_obj_create(card);
        lv_obj_remove_style_all(circle);
        lv_obj_add_style(circle, &styleIconCircle, 0);
        lv_obj_set_size(circle, focus ? 86 : 72, focus ? 86 : 72);
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *icon = lv_label_create(circle);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
        lv_label_set_text(icon, def.symbol);
        lv_obj_center(icon);

        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, def.title);
        lv_obj_add_style(label, &styleMuted, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

        return card;
    }

    // Forward declaration to fix usage before definition
    void on_card_click(lv_event_t *e);

    void update_carousel()
    {
        if (!g_ui.carousel)
            return;

        lv_obj_clean(g_ui.carousel);
        g_ui.cardLeft = make_icon_card(g_ui.carousel, card_at_offset(-1), false);
        g_ui.cardCenter = make_icon_card(g_ui.carousel, current_card(), true);
        g_ui.cardRight = make_icon_card(g_ui.carousel, card_at_offset(1), false);

        lv_obj_add_event_cb(g_ui.cardCenter, on_card_click, LV_EVENT_CLICKED, nullptr);

        if (g_ui.title)
        {
            lv_label_set_text(g_ui.title, current_card().title);
        }
    }

    void show_home();

    void on_back(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_home();
    }

    void attach_back_handler(lv_obj_t *obj)
    {
        lv_obj_add_event_cb(obj, [](lv_event_t *e)
                            {
            if (lv_event_get_code(e) == LV_EVENT_GESTURE)
            {
                if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
                {
                    show_home();
                }
            } }, LV_EVENT_GESTURE, nullptr);
    }

    lv_obj_t *make_detail_base(const char *title)
    {
        lv_obj_t *scr = lv_obj_create(nullptr);
        lv_obj_remove_style_all(scr);
        lv_obj_add_style(scr, &styleBg, 0);
        lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(scr, 10, 0);

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
        lv_obj_set_style_pad_top(body, 6, 0);
        lv_obj_set_layout(body, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(body, 10, 0);

        return body;
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
        lv_label_set_text_fmt(info, "Gear: %s\nShift: %s", g_state.gear <= 0 ? "N" : String(g_state.gear).c_str(), g_state.shift ? "ON" : "off");
        lv_obj_add_style(info, &styleMuted, 0);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    void open_wifi()
    {
        g_ui.detailContent = make_detail_base("WiFi");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        lv_obj_t *status = lv_label_create(g_ui.detailContent);
        lv_label_set_text(status, "Scanning / connect via phone app");
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
        lv_label_set_text(txt, "More options coming soon\nSwipe right to go back");
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

    void update_status_icons()
    {
        if (g_ui.wifiIcon)
        {
            // WiFi is OK if: STA connected OR AP is active (with or without clients)
            bool wifiOk = g_state.lastWifi.staConnected || g_state.lastWifi.apActive;
            bool wifiConnecting = g_state.lastWifi.staConnecting;

            lv_color_t col = wifiOk ? color_ok : (wifiConnecting ? color_warn : color_error);
            lv_opa_t opa = (wifiConnecting && (millis() / 400) % 2) ? LV_OPA_40 : LV_OPA_COVER;
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

    void show_home()
    {
        if (!g_ui.root)
            return;
        lv_disp_load_scr(g_ui.root);
        g_state.inDetail = false;
        update_carousel();
        update_status_icons();
        update_badges();
    }

    void on_gesture(lv_event_t *e)
    {
        if (g_state.inDetail)
            return;

        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT)
        {
            g_state.cardIndex++;
            update_carousel();
        }
        else if (dir == LV_DIR_RIGHT)
        {
            g_state.cardIndex--;
            update_carousel();
        }
    }

    void on_card_click(lv_event_t *e)
    {
        LV_UNUSED(e);
        open_detail(current_card());
    }

    void build_home(lv_disp_t *disp)
    {
        g_ui.root = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.root);
        lv_obj_add_style(g_ui.root, &styleBg, 0);
        lv_obj_set_size(g_ui.root, lv_disp_get_hor_res(disp), lv_disp_get_ver_res(disp));
        lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(g_ui.root, on_gesture, LV_EVENT_GESTURE, nullptr);

        g_ui.statusRow = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.statusRow);
        lv_obj_set_size(g_ui.statusRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_layout(g_ui.statusRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.statusRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.statusRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(g_ui.statusRow, 8, 0);
        lv_obj_set_style_pad_column(g_ui.statusRow, 8, 0);

        g_ui.wifiIcon = lv_label_create(g_ui.statusRow);
        lv_label_set_text(g_ui.wifiIcon, LV_SYMBOL_WIFI);
        g_ui.bleIcon = lv_label_create(g_ui.statusRow);
        lv_label_set_text(g_ui.bleIcon, LV_SYMBOL_BLUETOOTH);
        update_status_icons();

        g_ui.carousel = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.carousel);
        lv_obj_set_size(g_ui.carousel, LV_PCT(100), LV_PCT(70));
        lv_obj_set_style_pad_all(g_ui.carousel, 6, 0);
        lv_obj_set_layout(g_ui.carousel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.carousel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.carousel, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        g_ui.cardLeft = make_icon_card(g_ui.carousel, card_at_offset(-1), false);
        g_ui.cardCenter = make_icon_card(g_ui.carousel, current_card(), true);
        g_ui.cardRight = make_icon_card(g_ui.carousel, card_at_offset(1), false);

        lv_obj_add_event_cb(g_ui.cardCenter, on_card_click, LV_EVENT_CLICKED, nullptr);

        g_ui.title = lv_label_create(g_ui.root);
        lv_obj_align(g_ui.title, LV_ALIGN_BOTTOM_MID, 0, -46);
        lv_obj_set_style_text_font(g_ui.title, &lv_font_montserrat_24, 0);

        g_ui.gearBadge = lv_label_create(g_ui.root);
        lv_obj_add_style(g_ui.gearBadge, &styleBadge, 0);
        lv_obj_align(g_ui.gearBadge, LV_ALIGN_BOTTOM_LEFT, 8, -8);

        g_ui.shiftBadge = lv_label_create(g_ui.root);
        lv_obj_add_style(g_ui.shiftBadge, &styleBadge, 0);
        lv_obj_align(g_ui.shiftBadge, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

        update_carousel();
        update_badges();
    }
}

void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks)
{
    g_hooks = hooks;
    apply_styles();
    build_home(disp);
    lv_disp_load_scr(g_ui.root);
}

void ui_s3_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleConnecting)
{
    // Always update state from parameters
    g_state.lastWifi = wifiStatus;
    g_state.bleConnected = bleConnected;
    g_state.bleConnecting = bleConnecting;

    // Update status icons every frame - this ensures BLE/WiFi indicators
    // refresh immediately when connection state changes, not just when
    // accessing webserver
    update_status_icons();
    update_badges(); // Also update gear/shift badges in case they changed

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
                lines += "• ";
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
                lines += "• ";
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
    }
    lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);
}
