#include "ui_manager.h"

#include <Arduino.h>
#include <algorithm>
#include <vector>

#include "bluetooth/ble_obd.h"
#include "core/config.h"
#include "core/state.h"
#include "core/vehicle_info.h"
#include "core/utils.h"
#include "hardware/led_bar.h"

namespace
{
    constexpr uint32_t UI_UPDATE_INTERVAL_MS = 60;
    constexpr uint32_t STATUS_BLINK_MS = 420;

    enum class ScreenId
    {
        Driving,
        Menu,
        Brightness,
        Vehicle,
        Wifi,
        Bluetooth,
        Settings
    };

    struct MenuItem
    {
        ScreenId screen;
        const char *title;
        const char *icon;
    };

    const MenuItem MENU_ITEMS[] = {
        {ScreenId::Driving, "Drive", LV_SYMBOL_HOME},
        {ScreenId::Brightness, "Brightness", LV_SYMBOL_EYE_OPEN},
        {ScreenId::Vehicle, "Vehicle", LV_SYMBOL_GPS},
        {ScreenId::Wifi, "Wi-Fi", LV_SYMBOL_WIFI},
        {ScreenId::Bluetooth, "Bluetooth", LV_SYMBOL_BLUETOOTH},
        {ScreenId::Settings, "Settings", LV_SYMBOL_SETTINGS},
    };

    struct LiveData
    {
        int rpm = 0;
        int maxRpm = 4000;
        int speedKmh = 0;
        int gear = 0;
        bool shift = false;
        bool wifiConnected = false;
        bool wifiConnecting = false;
        bool wifiApActive = false;
        int apClients = 0;
        String wifiSsid;
        String staIp;
        String apIp;
        String ip;
        bool bleConnected = false;
        bool bleBusy = false;
        String bleName;
        String bleError;
        bool ignitionOn = false;
        bool engineRunning = false;
        bool vehicleInfoReady = false;
        unsigned long vehicleInfoAge = 0;
        String vehicleVin;
        String vehicleModel;
        String vehicleDiag;
        bool useMph = false;
    };

    struct UiElements
    {
        lv_obj_t *root = nullptr;
        lv_obj_t *driving = nullptr;
        lv_obj_t *menu = nullptr;
        lv_obj_t *brightness = nullptr;
        lv_obj_t *vehicle = nullptr;
        lv_obj_t *wifi = nullptr;
        lv_obj_t *ble = nullptr;
        lv_obj_t *settings = nullptr;

        // Driving
        lv_obj_t *speedValue = nullptr;
        lv_obj_t *speedUnit = nullptr;
        lv_obj_t *rpmBar = nullptr;
        lv_obj_t *gearBadge = nullptr;
        lv_obj_t *gearLabel = nullptr;
        lv_obj_t *shiftLabel = nullptr;
        lv_obj_t *statusLabel = nullptr;
        lv_obj_t *wifiIcon = nullptr;
        lv_obj_t *bleIcon = nullptr;
        lv_obj_t *menuHandle = nullptr;

        // Menu
        lv_obj_t *menuCenter = nullptr;
        lv_obj_t *menuLeft = nullptr;
        lv_obj_t *menuRight = nullptr;
        lv_obj_t *menuTitle = nullptr;
        lv_obj_t *tutorialOverlay = nullptr;

        // Brightness
        lv_obj_t *displaySlider = nullptr;
        lv_obj_t *displayValue = nullptr;
        lv_obj_t *ledSlider = nullptr;
        lv_obj_t *ledValue = nullptr;
        lv_obj_t *nightSwitch = nullptr;

        // Vehicle
        lv_obj_t *vehRpm = nullptr;
        lv_obj_t *vehSpeed = nullptr;
        lv_obj_t *vehGear = nullptr;
        lv_obj_t *vehVin = nullptr;
        lv_obj_t *vehModel = nullptr;
        lv_obj_t *vehDiag = nullptr;
        lv_obj_t *vehAge = nullptr;

        // WiFi
        lv_obj_t *wifiStatus = nullptr;
        lv_obj_t *wifiList = nullptr;
        lv_obj_t *wifiHint = nullptr;
        lv_obj_t *wifiScanBtn = nullptr;

        // BLE
        lv_obj_t *bleStatus = nullptr;
        lv_obj_t *bleScanBtn = nullptr;
        lv_obj_t *bleList = nullptr;
        lv_obj_t *bleErrorLabel = nullptr;

        // Settings
        lv_obj_t *unitsSwitch = nullptr;
        lv_obj_t *tutorialResetBtn = nullptr;
    };

    struct UiState
    {
        LiveData current;
        LiveData previous;
        uint32_t lastUpdateMs = 0;
        uint32_t lastBlinkToggleMs = 0;
        bool blinkOn = false;
        bool tutorialSeen = false;
        int menuIndex = 0;
        ScreenId active = ScreenId::Driving;
        bool logoVisible = false;
        lv_obj_t *logoOverlay = nullptr;
        uint32_t logoHideDeadline = 0;
    };

    UiElements g_ui;
    UiState g_state;
    UiDisplayHooks g_hooks;

    lv_style_t styleScreen;
    lv_style_t styleCard;
    lv_style_t styleMuted;
    lv_style_t styleBadge;
    lv_style_t stylePill;

    lv_color_t color_bg = lv_color_hex(0x050607);
    lv_color_t color_card = lv_color_hex(0x0F1116);
    lv_color_t color_accent = lv_color_hex(0x2D9CDB);
    lv_color_t color_ok = lv_color_hex(0x56F38A);
    lv_color_t color_warn = lv_color_hex(0xF5A524);
    lv_color_t color_error = lv_color_hex(0xF55E61);

    uint32_t mphFromKmh(int kmh)
    {
        return static_cast<uint32_t>(roundf(kmh * 0.621371f));
    }

    void mark_tutorial_seen()
    {
        if (g_state.tutorialSeen)
            return;
        g_state.tutorialSeen = true;
        cfg.uiTutorialSeen = true;
        saveConfig();
        if (g_ui.tutorialOverlay)
        {
            lv_obj_add_flag(g_ui.tutorialOverlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void set_screen(ScreenId id)
    {
        g_state.active = id;
        lv_obj_add_flag(g_ui.driving, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.brightness, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.vehicle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.ble, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui.settings, LV_OBJ_FLAG_HIDDEN);

        switch (id)
        {
        case ScreenId::Driving:
            lv_obj_clear_flag(g_ui.driving, LV_OBJ_FLAG_HIDDEN);
            break;
        case ScreenId::Menu:
            lv_obj_clear_flag(g_ui.menu, LV_OBJ_FLAG_HIDDEN);
            break;
        case ScreenId::Brightness:
            lv_obj_clear_flag(g_ui.brightness, LV_OBJ_FLAG_HIDDEN);
            break;
        case ScreenId::Vehicle:
            lv_obj_clear_flag(g_ui.vehicle, LV_OBJ_FLAG_HIDDEN);
            break;
        case ScreenId::Wifi:
            lv_obj_clear_flag(g_ui.wifi, LV_OBJ_FLAG_HIDDEN);
            break;
        case ScreenId::Bluetooth:
            lv_obj_clear_flag(g_ui.ble, LV_OBJ_FLAG_HIDDEN);
            break;
        case ScreenId::Settings:
        default:
            lv_obj_clear_flag(g_ui.settings, LV_OBJ_FLAG_HIDDEN);
            break;
        }
    }

    void apply_style_defaults()
    {
        lv_style_init(&styleScreen);
        lv_style_set_bg_color(&styleScreen, color_bg);
        lv_style_set_bg_opa(&styleScreen, LV_OPA_COVER);
        lv_style_set_text_color(&styleScreen, lv_color_hex(0xF1F2F5));
        lv_style_set_pad_all(&styleScreen, 12);

        lv_style_init(&styleCard);
        lv_style_set_bg_color(&styleCard, color_card);
        lv_style_set_bg_opa(&styleCard, LV_OPA_COVER);
        lv_style_set_radius(&styleCard, 10);
        lv_style_set_pad_all(&styleCard, 10);
        lv_style_set_border_width(&styleCard, 0);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, lv_color_hex(0x8A9099));

        lv_style_init(&styleBadge);
        lv_style_set_radius(&styleBadge, 12);
        lv_style_set_pad_left(&styleBadge, 8);
        lv_style_set_pad_right(&styleBadge, 8);
        lv_style_set_pad_top(&styleBadge, 6);
        lv_style_set_pad_bottom(&styleBadge, 6);
        lv_style_set_bg_color(&styleBadge, color_card);
        lv_style_set_bg_opa(&styleBadge, LV_OPA_COVER);

        lv_style_init(&stylePill);
        lv_style_set_radius(&stylePill, 16);
        lv_style_set_bg_color(&stylePill, lv_color_hex(0x10141A));
        lv_style_set_bg_opa(&stylePill, LV_OPA_70);
        lv_style_set_pad_left(&stylePill, 12);
        lv_style_set_pad_right(&stylePill, 12);
        lv_style_set_pad_top(&stylePill, 8);
        lv_style_set_pad_bottom(&stylePill, 8);
    }

    lv_obj_t *make_screen()
    {
        lv_obj_t *scr = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(scr);
        lv_obj_add_style(scr, &styleScreen, 0);
        lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        return scr;
    }

    lv_obj_t *make_card(lv_obj_t *parent)
    {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_remove_style_all(card);
        lv_obj_add_style(card, &styleCard, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        return card;
    }

    lv_obj_t *make_icon_circle(lv_obj_t *parent, const char *symbol, lv_color_t color, lv_coord_t size)
    {
        lv_obj_t *circle = lv_obj_create(parent);
        lv_obj_remove_style_all(circle);
        lv_obj_set_size(circle, size, size);
        lv_obj_set_style_bg_color(circle, color_card, 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(circle, size / 2, 0);
        lv_obj_set_style_border_width(circle, 2, 0);
        lv_obj_set_style_border_color(circle, lv_color_mix(color, lv_color_hex(0xFFFFFF), 90), 0);
        lv_obj_set_style_pad_all(circle, 0, 0);
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *label = lv_label_create(circle);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(label, color, 0);
        lv_label_set_text(label, symbol);
        lv_obj_center(label);
        return circle;
    }

    void update_status_icon(lv_obj_t *label, const char *symbol, lv_color_t color, lv_opa_t opa)
    {
        if (!label)
            return;
        lv_label_set_text(label, symbol);
        lv_obj_set_style_text_color(label, color, 0);
        lv_obj_set_style_opa(label, opa, 0);
    }

    void on_brightness_changed(lv_event_t *e)
    {
        LV_UNUSED(e);
        if (!g_ui.displaySlider)
            return;
        int val = lv_slider_get_value(g_ui.displaySlider);
        if (g_ui.displayValue)
        {
            lv_label_set_text_fmt(g_ui.displayValue, "%d", val);
        }
        cfg.displayBrightness = val;
        if (g_hooks.setBrightness)
        {
            g_hooks.setBrightness(static_cast<uint8_t>(val));
        }
    }

    void on_brightness_released(lv_event_t *e)
    {
        LV_UNUSED(e);
        saveConfig();
    }

    void on_led_changed(lv_event_t *e)
    {
        bool userTriggered = (e != nullptr);
        if (!g_ui.ledSlider)
            return;
        int val = lv_slider_get_value(g_ui.ledSlider);
        if (g_ui.ledValue)
        {
            lv_label_set_text_fmt(g_ui.ledValue, "%d", val);
        }
        cfg.brightness = clampInt(val, 0, 255);
        strip.setBrightness(cfg.brightness);
        strip.show();
        if (userTriggered)
        {
            rememberPreviewPixels();
            g_brightnessPreviewActive = true;
            g_lastBrightnessChangeMs = millis();
        }
    }

    void on_led_released(lv_event_t *e)
    {
        LV_UNUSED(e);
        saveConfig();
    }

    void on_night_toggle(lv_event_t *e)
    {
        LV_UNUSED(e);
        cfg.uiNightMode = lv_obj_has_state(g_ui.nightSwitch, LV_STATE_CHECKED);
        color_bg = cfg.uiNightMode ? lv_color_hex(0x050607) : lv_color_hex(0x0B0C0E);
        color_card = cfg.uiNightMode ? lv_color_hex(0x0F1116) : lv_color_hex(0x12141A);
        apply_style_defaults();
        saveConfig();
    }

    void on_units_toggle(lv_event_t *e)
    {
        LV_UNUSED(e);
        cfg.useMph = lv_obj_has_state(g_ui.unitsSwitch, LV_STATE_CHECKED);
        g_state.current.useMph = cfg.useMph;
        saveConfig();
    }

    void on_wifi_scan(lv_event_t *e)
    {
        LV_UNUSED(e);
        startWifiScan();
    }

    void on_ble_scan(lv_event_t *e)
    {
        LV_UNUSED(e);
        startBleScan();
    }

    void on_reset_tutorial(lv_event_t *e)
    {
        LV_UNUSED(e);
        cfg.uiTutorialSeen = false;
        g_state.tutorialSeen = false;
        saveConfig();
        if (g_ui.tutorialOverlay)
        {
            lv_obj_clear_flag(g_ui.tutorialOverlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void update_menu_carousel()
    {
        const size_t count = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
        if (count == 0)
            return;
        if (g_state.menuIndex < 0)
            g_state.menuIndex = 0;
        if (g_state.menuIndex >= static_cast<int>(count))
            g_state.menuIndex = 0;

        const MenuItem &cur = MENU_ITEMS[g_state.menuIndex];
        const MenuItem &prev = MENU_ITEMS[(g_state.menuIndex + count - 1) % count];
        const MenuItem &next = MENU_ITEMS[(g_state.menuIndex + 1) % count];

        if (g_ui.menuCenter)
        {
            lv_obj_t *lbl = lv_obj_get_child(g_ui.menuCenter, 0);
            lv_label_set_text(lbl, cur.icon);
        }
        if (g_ui.menuLeft)
        {
            lv_obj_t *lbl = lv_obj_get_child(g_ui.menuLeft, 0);
            lv_label_set_text(lbl, prev.icon);
        }
        if (g_ui.menuRight)
        {
            lv_obj_t *lbl = lv_obj_get_child(g_ui.menuRight, 0);
            lv_label_set_text(lbl, next.icon);
        }
        if (g_ui.menuTitle)
        {
            lv_label_set_text(g_ui.menuTitle, cur.title);
        }
    }

    void open_menu()
    {
        set_screen(ScreenId::Menu);
        update_menu_carousel();
        if (!g_state.tutorialSeen && g_ui.tutorialOverlay)
        {
            lv_obj_clear_flag(g_ui.tutorialOverlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void open_from_menu()
    {
        const MenuItem &cur = MENU_ITEMS[g_state.menuIndex];
        cfg.uiLastMenuIndex = g_state.menuIndex;
        saveConfig();
        if (cur.screen == ScreenId::Driving)
        {
            set_screen(ScreenId::Driving);
        }
        else
        {
            set_screen(cur.screen);
        }
        mark_tutorial_seen();
    }

    void handle_menu_gesture(lv_dir_t dir)
    {
        const size_t count = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
        if (dir == LV_DIR_LEFT)
        {
            g_state.menuIndex = (g_state.menuIndex + 1) % static_cast<int>(count);
            update_menu_carousel();
        }
        else if (dir == LV_DIR_RIGHT)
        {
            g_state.menuIndex = (g_state.menuIndex + count - 1) % static_cast<int>(count);
            update_menu_carousel();
        }
        else if (dir == LV_DIR_BOTTOM)
        {
            set_screen(ScreenId::Driving);
            mark_tutorial_seen();
        }
    }

    void on_menu_event(lv_event_t *e)
    {
        if (lv_event_get_code(e) == LV_EVENT_GESTURE)
        {
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            handle_menu_gesture(dir);
            if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)
            {
                mark_tutorial_seen();
            }
        }
        else if (lv_event_get_code(e) == LV_EVENT_CLICKED)
        {
            open_from_menu();
        }
    }

    void on_detail_swipe_back(lv_event_t *e)
    {
        if (lv_event_get_code(e) != LV_EVENT_GESTURE)
            return;
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT)
        {
            set_screen(ScreenId::Menu);
        }
    }

    void on_driving_event(lv_event_t *e)
    {
        if (lv_event_get_code(e) == LV_EVENT_GESTURE)
        {
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            if (dir == LV_DIR_TOP)
            {
                open_menu();
            }
        }
    }

    void on_menu_handle_click(lv_event_t *e)
    {
        LV_UNUSED(e);
        open_menu();
    }

    lv_obj_t *create_header_row(lv_obj_t *parent, const char *title)
    {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_left(row, 0, 0);
        lv_obj_set_style_pad_right(row, 0, 0);
        lv_obj_set_style_pad_top(row, 0, 0);
        lv_obj_set_style_pad_bottom(row, 6, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);

        lv_obj_t *back = lv_label_create(row);
        lv_label_set_text(back, LV_SYMBOL_LEFT " Back");
        lv_obj_add_style(back, &styleMuted, 0);
        lv_obj_add_event_cb(back, on_detail_swipe_back, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(back, [](lv_event_t *) { set_screen(ScreenId::Menu); }, LV_EVENT_CLICKED, nullptr);
        return row;
    }

    void build_driving()
    {
        g_ui.driving = make_screen();
        lv_obj_add_event_cb(g_ui.driving, on_driving_event, LV_EVENT_GESTURE, nullptr);

        lv_obj_t *top = lv_obj_create(g_ui.driving);
        lv_obj_remove_style_all(top);
        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_layout(top, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        g_ui.statusLabel = lv_label_create(top);
        lv_label_set_text(g_ui.statusLabel, "ShiftLight Ready");
        lv_obj_add_style(g_ui.statusLabel, &styleMuted, 0);

        lv_obj_t *icons = lv_obj_create(top);
        lv_obj_remove_style_all(icons);
        lv_obj_set_layout(icons, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(icons, 10, 0);
        g_ui.wifiIcon = lv_label_create(icons);
        g_ui.bleIcon = lv_label_create(icons);
        update_status_icon(g_ui.wifiIcon, LV_SYMBOL_WIFI, color_error, LV_OPA_80);
        update_status_icon(g_ui.bleIcon, LV_SYMBOL_BLUETOOTH, color_error, LV_OPA_80);

        lv_obj_t *mainArea = lv_obj_create(g_ui.driving);
        lv_obj_remove_style_all(mainArea);
        lv_obj_set_size(mainArea, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(mainArea, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(mainArea, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *leftCol = lv_obj_create(mainArea);
        lv_obj_remove_style_all(leftCol);
        lv_obj_set_size(leftCol, LV_PCT(60), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_bottom(leftCol, 6, 0);
        lv_obj_set_layout(leftCol, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(leftCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(leftCol, 4, 0);

        g_ui.speedValue = lv_label_create(leftCol);
        lv_obj_set_style_text_font(g_ui.speedValue, &lv_font_montserrat_48, 0);
        lv_label_set_text(g_ui.speedValue, "0");

        g_ui.speedUnit = lv_label_create(leftCol);
        lv_obj_add_style(g_ui.speedUnit, &styleMuted, 0);
        lv_label_set_text(g_ui.speedUnit, "km/h");

        g_ui.rpmBar = lv_bar_create(g_ui.driving);
        lv_obj_set_size(g_ui.rpmBar, LV_PCT(100), 16);
        lv_bar_set_range(g_ui.rpmBar, 0, 4000);
        lv_bar_set_value(g_ui.rpmBar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(g_ui.rpmBar, lv_color_hex(0x1A1F2A), 0);
        lv_obj_set_style_bg_opa(g_ui.rpmBar, LV_OPA_60, 0);
        lv_obj_set_style_radius(g_ui.rpmBar, 8, 0);
        lv_obj_set_style_bg_color(g_ui.rpmBar, color_accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(g_ui.rpmBar, LV_OPA_COVER, LV_PART_INDICATOR);

        lv_obj_t *rightCol = lv_obj_create(mainArea);
        lv_obj_remove_style_all(rightCol);
        lv_obj_set_size(rightCol, LV_PCT(35), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(rightCol, 0, 0);
        lv_obj_set_style_pad_row(rightCol, 8, 0);
        lv_obj_set_layout(rightCol, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        g_ui.gearBadge = lv_obj_create(rightCol);
        lv_obj_remove_style_all(g_ui.gearBadge);
        lv_obj_set_size(g_ui.gearBadge, 70, 70);
        lv_obj_set_style_bg_color(g_ui.gearBadge, lv_color_hex(0x121622), 0);
        lv_obj_set_style_bg_opa(g_ui.gearBadge, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(g_ui.gearBadge, 35, 0);
        lv_obj_set_style_border_width(g_ui.gearBadge, 2, 0);
        lv_obj_set_style_border_color(g_ui.gearBadge, color_accent, 0);
        lv_obj_clear_flag(g_ui.gearBadge, LV_OBJ_FLAG_SCROLLABLE);
        g_ui.gearLabel = lv_label_create(g_ui.gearBadge);
        lv_obj_set_style_text_font(g_ui.gearLabel, &lv_font_montserrat_32, 0);
        lv_label_set_text(g_ui.gearLabel, "N");
        lv_obj_center(g_ui.gearLabel);

        g_ui.shiftLabel = lv_label_create(rightCol);
        lv_label_set_text(g_ui.shiftLabel, "Shift Ready");
        lv_obj_add_style(g_ui.shiftLabel, &styleBadge, 0);

        g_ui.menuHandle = lv_obj_create(g_ui.driving);
        lv_obj_remove_style_all(g_ui.menuHandle);
        lv_obj_add_style(g_ui.menuHandle, &stylePill, 0);
        lv_obj_set_width(g_ui.menuHandle, LV_PCT(70));
        lv_obj_align(g_ui.menuHandle, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_t *handleLbl = lv_label_create(g_ui.menuHandle);
        lv_label_set_text(handleLbl, "Swipe up or tap for menu " LV_SYMBOL_UP);
        lv_obj_add_event_cb(g_ui.menuHandle, on_menu_handle_click, LV_EVENT_CLICKED, nullptr);
    }

    void build_menu()
    {
        g_ui.menu = make_screen();
        lv_obj_add_event_cb(g_ui.menu, on_menu_event, LV_EVENT_GESTURE, nullptr);

        lv_obj_t *center = lv_obj_create(g_ui.menu);
        lv_obj_remove_style_all(center);
        lv_obj_set_size(center, LV_PCT(100), LV_PCT(100));
        lv_obj_set_layout(center, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(center, 10, 0);

        lv_obj_t *row = lv_obj_create(center);
        lv_obj_remove_style_all(row);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row, LV_PCT(100));

        g_ui.menuLeft = make_icon_circle(row, LV_SYMBOL_LEFT, lv_color_hex(0x5C6370), 68);
        g_ui.menuCenter = make_icon_circle(row, LV_SYMBOL_HOME, color_accent, 96);
        g_ui.menuRight = make_icon_circle(row, LV_SYMBOL_RIGHT, lv_color_hex(0x5C6370), 68);

        g_ui.menuTitle = lv_label_create(center);
        lv_obj_set_style_text_font(g_ui.menuTitle, &lv_font_montserrat_24, 0);
        lv_label_set_text(g_ui.menuTitle, "Menu");

        lv_obj_add_event_cb(g_ui.menuCenter, on_menu_event, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(g_ui.menu, on_menu_event, LV_EVENT_CLICKED, nullptr);

        g_ui.tutorialOverlay = lv_obj_create(g_ui.menu);
        lv_obj_remove_style_all(g_ui.tutorialOverlay);
        lv_obj_set_size(g_ui.tutorialOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(g_ui.tutorialOverlay, lv_color_hex(0x080A0E), 0);
        lv_obj_set_style_bg_opa(g_ui.tutorialOverlay, LV_OPA_80, 0);
        lv_obj_set_style_pad_all(g_ui.tutorialOverlay, 12, 0);
        lv_obj_clear_flag(g_ui.tutorialOverlay, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *tip = lv_label_create(g_ui.tutorialOverlay);
        lv_obj_set_style_text_font(tip, &lv_font_montserrat_24, 0);
        lv_label_set_text(tip, "Swipe left/right to switch\nTap center to open\nSwipe left in detail to go back");
        lv_obj_center(tip);
    }

    void build_brightness()
    {
        g_ui.brightness = make_screen();
        lv_obj_add_event_cb(g_ui.brightness, on_detail_swipe_back, LV_EVENT_GESTURE, nullptr);
        create_header_row(g_ui.brightness, "Brightness");

        lv_obj_t *card = make_card(g_ui.brightness);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_style_pad_row(card, 8, 0);

        lv_obj_t *dispRow = lv_obj_create(card);
        lv_obj_remove_style_all(dispRow);
        lv_obj_set_width(dispRow, LV_PCT(100));
        lv_obj_set_layout(dispRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(dispRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(dispRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *dispLabel = lv_label_create(dispRow);
        lv_label_set_text(dispLabel, "Display");
        g_ui.displayValue = lv_label_create(dispRow);
        lv_obj_add_style(g_ui.displayValue, &styleMuted, 0);

        g_ui.displaySlider = lv_slider_create(card);
        lv_slider_set_range(g_ui.displaySlider, 10, 255);
        lv_slider_set_value(g_ui.displaySlider, cfg.displayBrightness, LV_ANIM_OFF);
        lv_obj_set_width(g_ui.displaySlider, LV_PCT(100));
        lv_obj_add_event_cb(g_ui.displaySlider, on_brightness_changed, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_add_event_cb(g_ui.displaySlider, on_brightness_released, LV_EVENT_RELEASED, nullptr);

        lv_obj_t *ledRow = lv_obj_create(card);
        lv_obj_remove_style_all(ledRow);
        lv_obj_set_width(ledRow, LV_PCT(100));
        lv_obj_set_layout(ledRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(ledRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(ledRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *ledLabel = lv_label_create(ledRow);
        lv_label_set_text(ledLabel, "LED Bar");
        g_ui.ledValue = lv_label_create(ledRow);
        lv_obj_add_style(g_ui.ledValue, &styleMuted, 0);

        g_ui.ledSlider = lv_slider_create(card);
        lv_slider_set_range(g_ui.ledSlider, 0, 255);
        lv_slider_set_value(g_ui.ledSlider, cfg.brightness, LV_ANIM_OFF);
        lv_obj_set_width(g_ui.ledSlider, LV_PCT(100));
        lv_obj_add_event_cb(g_ui.ledSlider, on_led_changed, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_add_event_cb(g_ui.ledSlider, on_led_released, LV_EVENT_RELEASED, nullptr);

        lv_obj_t *nightRow = lv_obj_create(card);
        lv_obj_remove_style_all(nightRow);
        lv_obj_set_width(nightRow, LV_PCT(100));
        lv_obj_set_layout(nightRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(nightRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(nightRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *nightLabel = lv_label_create(nightRow);
        lv_label_set_text(nightLabel, "Night mode");
        g_ui.nightSwitch = lv_switch_create(nightRow);
        if (cfg.uiNightMode)
            lv_obj_add_state(g_ui.nightSwitch, LV_STATE_CHECKED);
        lv_obj_add_event_cb(g_ui.nightSwitch, on_night_toggle, LV_EVENT_VALUE_CHANGED, nullptr);

        on_brightness_changed(nullptr);
        on_led_changed(nullptr);
    }

    void build_vehicle()
    {
        g_ui.vehicle = make_screen();
        lv_obj_add_event_cb(g_ui.vehicle, on_detail_swipe_back, LV_EVENT_GESTURE, nullptr);
        create_header_row(g_ui.vehicle, "Vehicle");

        lv_obj_t *card = make_card(g_ui.vehicle);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_style_pad_row(card, 6, 0);

        g_ui.vehModel = lv_label_create(card);
        g_ui.vehVin = lv_label_create(card);
        g_ui.vehDiag = lv_label_create(card);
        g_ui.vehAge = lv_label_create(card);
        lv_obj_add_style(g_ui.vehVin, &styleMuted, 0);
        lv_obj_add_style(g_ui.vehDiag, &styleMuted, 0);
        lv_obj_add_style(g_ui.vehAge, &styleMuted, 0);

        lv_obj_t *live = make_card(g_ui.vehicle);
        lv_obj_set_width(live, LV_PCT(100));
        lv_obj_set_style_pad_row(live, 4, 0);
        g_ui.vehRpm = lv_label_create(live);
        g_ui.vehSpeed = lv_label_create(live);
        g_ui.vehGear = lv_label_create(live);
    }

    void build_wifi()
    {
        g_ui.wifi = make_screen();
        lv_obj_add_event_cb(g_ui.wifi, on_detail_swipe_back, LV_EVENT_GESTURE, nullptr);
        create_header_row(g_ui.wifi, "Wi-Fi");

        lv_obj_t *card = make_card(g_ui.wifi);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_style_pad_row(card, 6, 0);

        g_ui.wifiStatus = lv_label_create(card);
        g_ui.wifiList = lv_label_create(card);
        g_ui.wifiHint = lv_label_create(card);
        lv_obj_add_style(g_ui.wifiHint, &styleMuted, 0);
        lv_label_set_text(g_ui.wifiHint, "Tip: Configure Wi-Fi via smartphone WebUI");

        g_ui.wifiScanBtn = lv_btn_create(card);
        lv_obj_t *lbl = lv_label_create(g_ui.wifiScanBtn);
        lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Scan");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(g_ui.wifiScanBtn, on_wifi_scan, LV_EVENT_CLICKED, nullptr);
    }

    void build_ble()
    {
        g_ui.ble = make_screen();
        lv_obj_add_event_cb(g_ui.ble, on_detail_swipe_back, LV_EVENT_GESTURE, nullptr);
        create_header_row(g_ui.ble, "Bluetooth");

        lv_obj_t *card = make_card(g_ui.ble);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_style_pad_row(card, 6, 0);

        g_ui.bleStatus = lv_label_create(card);
        g_ui.bleErrorLabel = lv_label_create(card);
        lv_obj_add_style(g_ui.bleErrorLabel, &styleMuted, 0);
        g_ui.bleList = lv_label_create(card);
        lv_obj_add_style(g_ui.bleList, &styleMuted, 0);

        g_ui.bleScanBtn = lv_btn_create(card);
        lv_obj_t *lbl = lv_label_create(g_ui.bleScanBtn);
        lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Scan OBD");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(g_ui.bleScanBtn, on_ble_scan, LV_EVENT_CLICKED, nullptr);
    }

    void build_settings()
    {
        g_ui.settings = make_screen();
        lv_obj_add_event_cb(g_ui.settings, on_detail_swipe_back, LV_EVENT_GESTURE, nullptr);
        create_header_row(g_ui.settings, "Settings");

        lv_obj_t *card = make_card(g_ui.settings);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_style_pad_row(card, 8, 0);

        lv_obj_t *unitsRow = lv_obj_create(card);
        lv_obj_remove_style_all(unitsRow);
        lv_obj_set_width(unitsRow, LV_PCT(100));
        lv_obj_set_layout(unitsRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(unitsRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(unitsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *unitsLabel = lv_label_create(unitsRow);
        lv_label_set_text(unitsLabel, "Use mph");
        g_ui.unitsSwitch = lv_switch_create(unitsRow);
        if (cfg.useMph)
            lv_obj_add_state(g_ui.unitsSwitch, LV_STATE_CHECKED);
        lv_obj_add_event_cb(g_ui.unitsSwitch, on_units_toggle, LV_EVENT_VALUE_CHANGED, nullptr);

        g_ui.tutorialResetBtn = lv_btn_create(card);
        lv_obj_t *lbl = lv_label_create(g_ui.tutorialResetBtn);
        lv_label_set_text(lbl, LV_SYMBOL_WARNING " Reset tutorial");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(g_ui.tutorialResetBtn, on_reset_tutorial, LV_EVENT_CLICKED, nullptr);
    }

    void build_ui()
    {
        g_ui.root = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.root);
        lv_obj_add_style(g_ui.root, &styleScreen, 0);
        lv_obj_set_size(g_ui.root, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);

        build_driving();
        build_menu();
        build_brightness();
        build_vehicle();
        build_wifi();
        build_ble();
        build_settings();
    }

    void update_driving_view(const LiveData &d)
    {
        if (g_ui.speedValue)
        {
            int speed = d.useMph ? mphFromKmh(d.speedKmh) : d.speedKmh;
            if (g_state.previous.speedKmh != d.speedKmh || g_state.previous.useMph != d.useMph)
            {
                lv_label_set_text_fmt(g_ui.speedValue, "%d", speed);
            }
            if (g_ui.speedUnit && (g_state.previous.useMph != d.useMph))
            {
                lv_label_set_text(g_ui.speedUnit, d.useMph ? "mph" : "km/h");
            }
        }

        if (g_ui.gearLabel && (g_state.previous.gear != d.gear))
        {
            if (d.gear <= 0)
                lv_label_set_text(g_ui.gearLabel, "N");
            else
                lv_label_set_text_fmt(g_ui.gearLabel, "%d", d.gear);
        }

        if (g_ui.rpmBar)
        {
            if (g_state.previous.maxRpm != d.maxRpm)
            {
                lv_bar_set_range(g_ui.rpmBar, 0, d.maxRpm);
            }
            int rpm = std::min(d.rpm, d.maxRpm);
            lv_bar_set_value(g_ui.rpmBar, rpm, LV_ANIM_OFF);
            lv_color_t barColor = d.shift ? color_warn : color_accent;
            lv_obj_set_style_bg_color(g_ui.rpmBar, barColor, LV_PART_INDICATOR);
        }

        if (g_ui.shiftLabel && (g_state.previous.shift != d.shift))
        {
            if (d.shift)
            {
                lv_label_set_text(g_ui.shiftLabel, "SHIFT");
                lv_obj_set_style_bg_color(g_ui.shiftLabel, color_warn, 0);
                lv_obj_set_style_text_color(g_ui.shiftLabel, lv_color_black(), 0);
            }
            else
            {
                lv_label_set_text(g_ui.shiftLabel, "Shift Ready");
                lv_obj_set_style_bg_color(g_ui.shiftLabel, color_card, 0);
                lv_obj_set_style_text_color(g_ui.shiftLabel, lv_color_hex(0xE8EAED), 0);
            }
        }

        if (g_ui.statusLabel)
        {
            const char *status = d.bleConnected ? "OBD connected" : (d.bleBusy ? "OBD connecting..." : "OBD idle");
            lv_label_set_text_fmt(g_ui.statusLabel, "%s | %s", status, d.wifiConnected ? "Wi-Fi on" : "Wi-Fi off");
        }

        lv_opa_t wifiOpa = (d.wifiConnecting && g_state.blinkOn) ? LV_OPA_40 : LV_OPA_COVER;
        lv_color_t wifiColor = d.wifiConnected ? color_ok : (d.wifiConnecting ? color_warn : color_error);
        update_status_icon(g_ui.wifiIcon, LV_SYMBOL_WIFI, wifiColor, wifiOpa);

        lv_opa_t bleOpa = (d.bleBusy && g_state.blinkOn) ? LV_OPA_40 : LV_OPA_COVER;
        lv_color_t bleColor = d.bleConnected ? lv_color_hex(0x4DA3FF) : (d.bleBusy ? color_warn : color_error);
        update_status_icon(g_ui.bleIcon, LV_SYMBOL_BLUETOOTH, bleColor, bleOpa);
    }

    void update_vehicle_view(const LiveData &d)
    {
        if (g_ui.vehRpm)
            lv_label_set_text_fmt(g_ui.vehRpm, "RPM: %d", d.rpm);
        if (g_ui.vehSpeed)
        {
            int speed = d.useMph ? mphFromKmh(d.speedKmh) : d.speedKmh;
            lv_label_set_text_fmt(g_ui.vehSpeed, "Speed: %d %s", speed, d.useMph ? "mph" : "km/h");
        }
        if (g_ui.vehGear)
            lv_label_set_text_fmt(g_ui.vehGear, "Gear: %s", d.gear <= 0 ? "N" : String(d.gear).c_str());
        if (g_ui.vehModel)
            lv_label_set_text_fmt(g_ui.vehModel, "Model: %s", d.vehicleModel.c_str());
        if (g_ui.vehVin)
            lv_label_set_text_fmt(g_ui.vehVin, "VIN: %s", d.vehicleVin.c_str());
        if (g_ui.vehDiag)
            lv_label_set_text_fmt(g_ui.vehDiag, "Diag: %s", d.vehicleDiag.c_str());
        if (g_ui.vehAge)
            lv_label_set_text_fmt(g_ui.vehAge, "Age: %lus", d.vehicleInfoAge / 1000UL);
    }

    void update_wifi_view(const WifiStatus &wifi)
    {
        if (g_ui.wifiStatus)
        {
            lv_label_set_text_fmt(g_ui.wifiStatus, "Mode: %s | AP: %s (%d) | STA: %s %s",
                                  wifi.mode == STA_ONLY ? "STA" : (wifi.mode == AP_ONLY ? "AP" : "AP+STA"),
                                  wifi.apActive ? wifi.apIp.c_str() : "-",
                                  wifi.apClients,
                                  wifi.staConnected ? wifi.currentSsid.c_str() : "none",
                                  wifi.staConnected ? wifi.staIp.c_str() : "");
        }

        if (g_ui.wifiList)
        {
            if (wifi.scanResults.empty())
            {
                lv_label_set_text(g_ui.wifiList, "Nearby: (no scan yet)");
            }
            else
            {
                String list = "Nearby:\n";
                size_t count = std::min<size_t>(wifi.scanResults.size(), 6);
                for (size_t i = 0; i < count; ++i)
                {
                    list += "• ";
                    list += wifi.scanResults[i].ssid;
                    list += " (";
                    list += wifi.scanResults[i].rssi;
                    list += "dBm)\n";
                }
                lv_label_set_text(g_ui.wifiList, list.c_str());
            }
        }
    }

    void update_ble_view(const LiveData &d)
    {
        if (g_ui.bleStatus)
        {
            lv_label_set_text_fmt(g_ui.bleStatus, "OBD: %s%s",
                                  d.bleConnected ? "Connected" : (d.bleBusy ? "Connecting..." : "Disconnected"),
                                  d.bleName.length() ? String(" to ") + d.bleName : "");
        }
        if (g_ui.bleErrorLabel)
        {
            lv_label_set_text_fmt(g_ui.bleErrorLabel, "Last error: %s", d.bleError.length() ? d.bleError.c_str() : "none");
        }
        if (g_ui.bleList)
        {
            const auto &res = getBleScanResults();
            if (res.empty())
            {
                lv_label_set_text(g_ui.bleList, "Scan for nearby OBD dongles to connect.");
            }
            else
            {
                String list = "Nearby:\n";
                size_t count = std::min<size_t>(res.size(), 6);
                for (size_t i = 0; i < count; ++i)
                {
                    list += "• ";
                    list += res[i].name;
                    list += " (";
                    list += res[i].address;
                    list += ")\n";
                }
                lv_label_set_text(g_ui.bleList, list.c_str());
            }
        }
    }

    void update_settings_view()
    {
        if (g_ui.unitsSwitch)
        {
            if (cfg.useMph)
                lv_obj_add_state(g_ui.unitsSwitch, LV_STATE_CHECKED);
            else
                lv_obj_clear_state(g_ui.unitsSwitch, LV_STATE_CHECKED);
        }
    }

    void update_logo_overlay()
    {
        if (!g_state.logoVisible)
            return;
        if (g_state.logoHideDeadline != 0 && (int32_t)(millis() - g_state.logoHideDeadline) >= 0)
        {
            if (g_state.logoOverlay)
            {
                lv_obj_add_flag(g_state.logoOverlay, LV_OBJ_FLAG_HIDDEN);
            }
            g_state.logoVisible = false;
            g_state.logoHideDeadline = 0;
            return;
        }
        if (!g_state.logoOverlay)
        {
            g_state.logoOverlay = lv_obj_create(g_ui.driving);
            lv_obj_remove_style_all(g_state.logoOverlay);
            lv_obj_set_size(g_state.logoOverlay, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(g_state.logoOverlay, lv_color_hex(0x0E2A4A), 0);
            lv_obj_set_style_bg_opa(g_state.logoOverlay, LV_OPA_80, 0);
            lv_obj_clear_flag(g_state.logoOverlay, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *lbl = lv_label_create(g_state.logoOverlay);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
            lv_label_set_text(lbl, "AMOLED TEST");
            lv_obj_center(lbl);
        }
        lv_obj_move_foreground(g_state.logoOverlay);
        lv_obj_clear_flag(g_state.logoOverlay, LV_OBJ_FLAG_HIDDEN);
    }

    void refresh_ui(const WifiStatus &wifi)
    {
        update_driving_view(g_state.current);
        update_vehicle_view(g_state.current);
        update_wifi_view(wifi);
        update_ble_view(g_state.current);
        update_settings_view();
        update_logo_overlay();
        g_state.previous = g_state.current;
    }

    int compute_max_rpm()
    {
        if (cfg.autoScaleMaxRpm)
        {
            int maxRpm = g_maxSeenRpm > 0 ? g_maxSeenRpm : 4000;
            return std::max(maxRpm, 2000);
        }
        return (cfg.fixedMaxRpm > 500) ? cfg.fixedMaxRpm : 4000;
    }

    void pull_live_data(const WifiStatus &wifi, bool bleConnected, bool bleBusy)
    {
        g_state.current.rpm = g_currentRpm;
        g_state.current.maxRpm = compute_max_rpm();
        g_state.current.speedKmh = g_vehicleSpeedKmh;
        g_state.current.gear = g_estimatedGear;
        g_state.current.wifiConnected = wifi.staConnected || wifi.apActive;
        g_state.current.wifiConnecting = wifi.staConnecting || wifi.scanRunning;
        g_state.current.wifiApActive = wifi.apActive;
        g_state.current.apClients = wifi.apClients;
        g_state.current.wifiSsid = wifi.currentSsid;
        g_state.current.staIp = wifi.staIp;
        g_state.current.apIp = wifi.apIp;
        g_state.current.ip = wifi.ip;
        g_state.current.bleConnected = bleConnected;
        g_state.current.bleBusy = bleBusy;
        g_state.current.bleName = g_bleConnectTargetName.length() ? g_bleConnectTargetName : g_currentTargetName;
        g_state.current.bleError = g_bleConnectLastError;
        g_state.current.ignitionOn = g_ignitionOn;
        g_state.current.engineRunning = g_engineRunning;
        g_state.current.vehicleInfoReady = g_vehicleInfoAvailable;
        g_state.current.vehicleInfoAge = g_vehicleInfoLastUpdate;
        g_state.current.vehicleVin = g_vehicleVin;
        g_state.current.vehicleModel = g_vehicleModel;
        g_state.current.vehicleDiag = g_vehicleDiagStatus;
        g_state.current.useMph = cfg.useMph;
    }

} // namespace

void ui_manager_init(lv_disp_t *disp, const UiDisplayHooks &hooks)
{
    LV_UNUSED(disp);
    g_hooks = hooks;
    g_state.tutorialSeen = cfg.uiTutorialSeen;
    g_state.menuIndex = clampInt(cfg.uiLastMenuIndex, 0, static_cast<int>(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])) - 1);
    g_state.active = ScreenId::Driving;

    apply_style_defaults();
    build_ui();
    lv_disp_load_scr(g_ui.root);

    if (g_hooks.setBrightness)
    {
        g_hooks.setBrightness(static_cast<uint8_t>(cfg.displayBrightness));
    }
}

void ui_manager_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleBusy)
{
    uint32_t now = millis();
    if (now - g_state.lastUpdateMs < UI_UPDATE_INTERVAL_MS)
        return;
    g_state.lastUpdateMs = now;

    if (now - g_state.lastBlinkToggleMs > STATUS_BLINK_MS)
    {
        g_state.lastBlinkToggleMs = now;
        g_state.blinkOn = !g_state.blinkOn;
    }

    pull_live_data(wifiStatus, bleConnected, bleBusy);
    refresh_ui(wifiStatus);
}

void ui_manager_set_shiftlight(bool active)
{
    g_state.current.shift = active;
}

void ui_manager_set_gear(int gear)
{
    g_state.current.gear = gear;
}

void ui_manager_show_logo()
{
    g_state.logoVisible = true;
    g_state.logoHideDeadline = millis() + 2000;
    update_logo_overlay();
}
