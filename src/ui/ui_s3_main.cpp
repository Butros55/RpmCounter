#include "ui_s3_main.h"

#include <algorithm>
#include <array>
#include <string>

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
        UiScreenId screenId;
        const char *title;
        const char *menuLabel;
        const char *symbol;
    };

    constexpr CardDef CARDS[] = {
        {CardScreen::Brightness, UiScreenId::Brightness, "Brightness", "Display", LV_SYMBOL_EYE_OPEN},
        {CardScreen::Vehicle, UiScreenId::Vehicle, "Vehicle", "Vehicle", LV_SYMBOL_GPS},
        {CardScreen::Wifi, UiScreenId::Wifi, "WiFi", "WiFi", LV_SYMBOL_WIFI},
        {CardScreen::Bluetooth, UiScreenId::Bluetooth, "Bluetooth", "BLE", LV_SYMBOL_BLUETOOTH},
        {CardScreen::Settings, UiScreenId::Settings, "Settings", "Settings", LV_SYMBOL_SETTINGS},
    };

    constexpr size_t CARD_COUNT = sizeof(CARDS) / sizeof(CARDS[0]);

    struct TileWidgets
    {
        lv_obj_t *button = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *label = nullptr;
    };

    struct UiRefs
    {
        lv_disp_t *disp = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *wifiIcon = nullptr;
        lv_obj_t *bleIcon = nullptr;
        lv_obj_t *heroMeta = nullptr;
        lv_obj_t *rpmValue = nullptr;
        lv_obj_t *rpmUnit = nullptr;
        lv_obj_t *heroSummary = nullptr;
        lv_obj_t *gearBadge = nullptr;
        lv_obj_t *speedBadge = nullptr;
        lv_obj_t *shiftBadge = nullptr;
        lv_obj_t *tutorial = nullptr;
        lv_obj_t *tutorialLabel = nullptr;
        lv_obj_t *detail = nullptr;
        lv_obj_t *detailPanel = nullptr;
        lv_obj_t *detailValue = nullptr;
        lv_obj_t *detailNote = nullptr;
        lv_obj_t *brightnessSlider = nullptr;
        lv_obj_t *brightnessValue = nullptr;
        lv_obj_t *logoOverlay = nullptr;
        std::array<TileWidgets, CARD_COUNT> tiles{};
    };

    struct UiState
    {
        UiRuntimeState runtime{};
        int selectedIndex = 0;
        bool inDetail = false;
        bool tutorialVisible = true;
        bool hasInteracted = false;
        UiScreenId activeScreen = UiScreenId::Home;
    };

    UiRefs g_ui;
    UiState g_state;
    UiDisplayHooks g_hooks;

    lv_style_t styleBg;
    lv_style_t stylePanel;
    lv_style_t stylePanelAccent;
    lv_style_t styleTile;
    lv_style_t styleTileSelected;
    lv_style_t styleBadge;
    lv_style_t styleMuted;
    lv_style_t styleTutorial;
    lv_style_t styleBackButton;

    const lv_color_t color_bg = lv_color_hex(0x05070A);
    const lv_color_t color_panel = lv_color_hex(0x11151C);
    const lv_color_t color_panel_alt = lv_color_hex(0x181F29);
    const lv_color_t color_border = lv_color_hex(0x1F2733);
    const lv_color_t color_accent = lv_color_hex(0x52C7EA);
    const lv_color_t color_ok = lv_color_hex(0x56F38A);
    const lv_color_t color_warn = lv_color_hex(0xF5B025);
    const lv_color_t color_error = lv_color_hex(0xF55E61);
    const lv_color_t color_text = lv_color_hex(0xEEF2F7);
    const lv_color_t color_muted = lv_color_hex(0x9CA7B5);

    int clamp_card_index(int index)
    {
        const int count = static_cast<int>(CARD_COUNT);
        return (index % count + count) % count;
    }

    uint32_t now_ms()
    {
        return lv_tick_get();
    }

    const CardDef &current_card()
    {
        g_state.selectedIndex = clamp_card_index(g_state.selectedIndex);
        return CARDS[g_state.selectedIndex];
    }

    void set_label_text(lv_obj_t *label, const std::string &text)
    {
        if (label)
        {
            lv_label_set_text(label, text.c_str());
        }
    }

    std::string gear_text(int gear)
    {
        return (gear <= 0) ? "N" : std::to_string(gear);
    }

    std::string bool_text(bool value, const char *whenTrue, const char *whenFalse)
    {
        return value ? whenTrue : whenFalse;
    }

    void sync_runtime_settings()
    {
        g_state.runtime.settings.lastMenuIndex = g_state.selectedIndex;
        g_state.runtime.settings.tutorialSeen = !g_state.tutorialVisible;
    }

    void persist_settings()
    {
        sync_runtime_settings();
        if (g_hooks.saveSettings)
        {
            g_hooks.saveSettings(g_state.runtime.settings, g_hooks.userData);
        }
    }

    void apply_runtime_state(const UiRuntimeState &state)
    {
        g_state.runtime = state;
        sync_runtime_settings();
    }

    void apply_styles()
    {
        lv_style_init(&styleBg);
        lv_style_set_bg_color(&styleBg, color_bg);
        lv_style_set_bg_opa(&styleBg, LV_OPA_COVER);
        lv_style_set_pad_all(&styleBg, 0);

        lv_style_init(&stylePanel);
        lv_style_set_bg_color(&stylePanel, color_panel);
        lv_style_set_bg_opa(&stylePanel, LV_OPA_COVER);
        lv_style_set_radius(&stylePanel, 18);
        lv_style_set_border_width(&stylePanel, 1);
        lv_style_set_border_color(&stylePanel, color_border);
        lv_style_set_pad_all(&stylePanel, 12);
        lv_style_set_shadow_width(&stylePanel, 12);
        lv_style_set_shadow_color(&stylePanel, lv_color_hex(0x020305));
        lv_style_set_shadow_opa(&stylePanel, LV_OPA_30);
        lv_style_set_shadow_spread(&stylePanel, 2);

        lv_style_init(&stylePanelAccent);
        lv_style_set_bg_color(&stylePanelAccent, color_panel_alt);
        lv_style_set_bg_opa(&stylePanelAccent, LV_OPA_COVER);
        lv_style_set_radius(&stylePanelAccent, 14);
        lv_style_set_border_width(&stylePanelAccent, 2);
        lv_style_set_border_color(&stylePanelAccent, color_accent);
        lv_style_set_pad_all(&stylePanelAccent, 10);

        lv_style_init(&styleTile);
        lv_style_set_bg_color(&styleTile, color_panel);
        lv_style_set_bg_opa(&styleTile, LV_OPA_COVER);
        lv_style_set_radius(&styleTile, 16);
        lv_style_set_border_width(&styleTile, 1);
        lv_style_set_border_color(&styleTile, color_border);
        lv_style_set_pad_all(&styleTile, 8);

        lv_style_init(&styleTileSelected);
        lv_style_set_bg_color(&styleTileSelected, color_panel_alt);
        lv_style_set_bg_opa(&styleTileSelected, LV_OPA_COVER);
        lv_style_set_border_width(&styleTileSelected, 2);
        lv_style_set_border_color(&styleTileSelected, color_accent);

        lv_style_init(&styleBadge);
        lv_style_set_radius(&styleBadge, 12);
        lv_style_set_bg_color(&styleBadge, lv_color_hex(0x1D2430));
        lv_style_set_bg_opa(&styleBadge, LV_OPA_COVER);
        lv_style_set_pad_left(&styleBadge, 10);
        lv_style_set_pad_right(&styleBadge, 10);
        lv_style_set_pad_top(&styleBadge, 6);
        lv_style_set_pad_bottom(&styleBadge, 6);
        lv_style_set_text_color(&styleBadge, color_text);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, color_muted);

        lv_style_init(&styleTutorial);
        lv_style_set_radius(&styleTutorial, 12);
        lv_style_set_bg_color(&styleTutorial, lv_color_hex(0x171B22));
        lv_style_set_bg_opa(&styleTutorial, LV_OPA_80);
        lv_style_set_border_width(&styleTutorial, 1);
        lv_style_set_border_color(&styleTutorial, color_border);
        lv_style_set_pad_all(&styleTutorial, 10);

        lv_style_init(&styleBackButton);
        lv_style_set_bg_color(&styleBackButton, lv_color_hex(0x171C25));
        lv_style_set_bg_opa(&styleBackButton, LV_OPA_COVER);
        lv_style_set_radius(&styleBackButton, 12);
        lv_style_set_border_width(&styleBackButton, 1);
        lv_style_set_border_color(&styleBackButton, color_border);
        lv_style_set_pad_all(&styleBackButton, 0);
        lv_style_set_text_color(&styleBackButton, color_text);
    }

    void update_status_icons()
    {
        if (g_ui.wifiIcon)
        {
            lv_color_t col = color_error;
            lv_opa_t opa = LV_OPA_COVER;
            if (g_state.runtime.staConnected)
            {
                col = color_ok;
            }
            else if (g_state.runtime.apActive)
            {
                col = color_ok;
                opa = (g_state.runtime.apClients > 0 || ((now_ms() / 800U) % 2U == 0U)) ? LV_OPA_COVER : LV_OPA_40;
            }
            else if (g_state.runtime.staConnecting)
            {
                col = color_warn;
                opa = ((now_ms() / 300U) % 2U == 0U) ? LV_OPA_COVER : LV_OPA_40;
            }

            lv_obj_set_style_text_color(g_ui.wifiIcon, col, 0);
            lv_obj_set_style_opa(g_ui.wifiIcon, opa, 0);
        }

        if (g_ui.bleIcon)
        {
            lv_color_t col = color_error;
            lv_opa_t opa = LV_OPA_COVER;
            if (g_state.runtime.bleConnected)
            {
                col = lv_color_hex(0x4DA3FF);
            }
            else if (g_state.runtime.bleConnecting)
            {
                col = color_warn;
                opa = ((now_ms() / 400U) % 2U == 0U) ? LV_OPA_COVER : LV_OPA_40;
            }

            lv_obj_set_style_text_color(g_ui.bleIcon, col, 0);
            lv_obj_set_style_opa(g_ui.bleIcon, opa, 0);
        }
    }

    void update_tile_selection()
    {
        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            const bool selected = static_cast<int>(i) == g_state.selectedIndex;
            TileWidgets &tile = g_ui.tiles[i];
            if (!tile.button)
            {
                continue;
            }

            lv_obj_set_style_bg_color(tile.button, selected ? color_panel_alt : color_panel, 0);
            lv_obj_set_style_border_color(tile.button, selected ? color_accent : color_border, 0);
            lv_obj_set_style_border_width(tile.button, selected ? 2 : 1, 0);
            lv_obj_set_style_text_color(tile.icon, selected ? color_accent : color_text, 0);
            lv_obj_set_style_text_color(tile.label, selected ? color_text : color_muted, 0);
        }
    }

    std::string hero_meta_text()
    {
        std::string meta;
        switch (g_state.runtime.telemetrySource)
        {
        case UiTelemetrySource::SimHubUdp:
            if (g_state.runtime.telemetryUsingFallback)
            {
                meta = "SimHub fallback";
            }
            else if (g_state.runtime.telemetryStale)
            {
                meta = g_state.runtime.telemetryTimestampMs == 0 ? "SimHub waiting" : "SimHub stale";
            }
            else
            {
                meta = "SimHub live";
            }
            break;
        case UiTelemetrySource::Simulator:
            meta = "Simulator";
            break;
        case UiTelemetrySource::Esp32Obd:
        default:
            meta = g_state.runtime.bleConnected ? "OBD connected" : (g_state.runtime.bleConnecting ? "OBD connecting" : "OBD offline");
            break;
        }

        meta += "  |  ";
        if (g_state.runtime.staConnected)
        {
            meta += "WiFi ";
            meta += g_state.runtime.currentSsid.empty() ? "online" : g_state.runtime.currentSsid;
        }
        else if (g_state.runtime.apActive)
        {
            meta += "AP ";
            meta += g_state.runtime.apIp.empty() ? "active" : g_state.runtime.apIp;
        }
        else if (g_state.runtime.staConnecting)
        {
            meta += "WiFi connecting";
        }
        else
        {
            meta += "WiFi offline";
        }
        return meta;
    }

    std::string hero_summary_text()
    {
        if (!g_state.runtime.ip.empty())
        {
            return "IP " + g_state.runtime.ip;
        }
        if (g_state.runtime.apActive)
        {
            return "AP clients: " + std::to_string(g_state.runtime.apClients);
        }
        if (g_state.runtime.staConnecting)
        {
            return "Connecting to vehicle network";
        }
        return "Ready for live telemetry";
    }

    void update_home_metrics()
    {
        if (g_ui.rpmValue)
        {
            lv_label_set_text_fmt(g_ui.rpmValue, "%d", g_state.runtime.rpm);
            lv_obj_set_style_text_color(g_ui.rpmValue, g_state.runtime.shift ? color_warn : color_text, 0);
        }
        if (g_ui.rpmUnit)
        {
            lv_label_set_text(g_ui.rpmUnit, "RPM");
        }
        if (g_ui.heroMeta)
        {
            set_label_text(g_ui.heroMeta, hero_meta_text());
        }
        if (g_ui.heroSummary)
        {
            set_label_text(g_ui.heroSummary, hero_summary_text());
        }
        if (g_ui.gearBadge)
        {
            set_label_text(g_ui.gearBadge, "G " + gear_text(g_state.runtime.gear));
        }
        if (g_ui.speedBadge)
        {
            set_label_text(g_ui.speedBadge, std::to_string(g_state.runtime.speedKmh) + " km/h");
        }
        if (g_ui.shiftBadge)
        {
            lv_label_set_text(g_ui.shiftBadge, g_state.runtime.shift ? "SHIFT" : "Ready");
            lv_obj_set_style_bg_color(g_ui.shiftBadge, g_state.runtime.shift ? color_warn : lv_color_hex(0x1D2430), 0);
            lv_obj_set_style_text_color(g_ui.shiftBadge, g_state.runtime.shift ? lv_color_black() : color_text, 0);
        }
    }

    std::string format_vehicle_info()
    {
        std::string lines = "RPM: " + std::to_string(g_state.runtime.rpm);
        lines += "\nGear: " + gear_text(g_state.runtime.gear);
        lines += "\nSpeed: " + std::to_string(g_state.runtime.speedKmh) + " km/h";
        lines += "\nThrottle: " + std::to_string(static_cast<int>(g_state.runtime.throttle * 100.0f)) + "%";
        lines += "\nShift light: " + bool_text(g_state.runtime.shift, "active", "ready");
        if (g_state.runtime.telemetrySource == UiTelemetrySource::SimHubUdp)
        {
            lines += "\nTelemetry: ";
            if (g_state.runtime.telemetryUsingFallback)
            {
                lines += "SimHub fallback simulator";
            }
            else if (g_state.runtime.telemetryStale)
            {
                lines += g_state.runtime.telemetryTimestampMs == 0 ? "waiting for data" : "SimHub stale";
            }
            else
            {
                lines += "SimHub live";
            }
        }
        else if (g_state.runtime.telemetrySource == UiTelemetrySource::Simulator)
        {
            lines += "\nTelemetry: internal simulator";
        }
        else
        {
            lines += "\nBLE: " + std::string(g_state.runtime.bleConnected ? "connected" : (g_state.runtime.bleConnecting ? "connecting" : "offline"));
        }
        return lines;
    }

    std::string format_wifi_info()
    {
        std::string lines = "Mode: ";
        switch (g_state.runtime.wifiMode)
        {
        case UiWifiMode::StaOnly:
            lines += "STA only";
            break;
        case UiWifiMode::StaWithApFallback:
            lines += "STA + AP fallback";
            break;
        case UiWifiMode::ApOnly:
        default:
            lines += "AP only";
            break;
        }

        if (g_state.runtime.staConnected)
        {
            lines += "\nConnected: " + g_state.runtime.currentSsid;
            if (!g_state.runtime.staIp.empty())
            {
                lines += "\nIP: " + g_state.runtime.staIp;
            }
        }
        else if (g_state.runtime.staConnecting)
        {
            lines += "\nConnecting to " + (g_state.runtime.currentSsid.empty() ? std::string("network") : g_state.runtime.currentSsid);
        }
        else if (!g_state.runtime.staLastError.empty())
        {
            lines += "\nLast error: " + g_state.runtime.staLastError;
        }

        if (g_state.runtime.apActive)
        {
            lines += "\nAP: " + (g_state.runtime.apIp.empty() ? std::string("active") : g_state.runtime.apIp);
            lines += "\nClients: " + std::to_string(g_state.runtime.apClients);
        }

        if (!g_state.runtime.wifiScanResults.empty())
        {
            lines += "\n\nNearby:";
            const size_t count = std::min<size_t>(g_state.runtime.wifiScanResults.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                const UiWifiScanItem &item = g_state.runtime.wifiScanResults[i];
                lines += "\n- " + item.ssid + " (" + std::to_string(item.rssi) + " dBm)";
            }
        }

        return lines;
    }

    std::string format_ble_info()
    {
        std::string lines = "Status: ";
        lines += g_state.runtime.bleConnected ? "connected" : (g_state.runtime.bleConnecting ? "connecting" : "offline");

        if (!g_state.runtime.bleScanResults.empty())
        {
            lines += "\n\nDevices:";
            const size_t count = std::min<size_t>(g_state.runtime.bleScanResults.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                const UiBleScanItem &item = g_state.runtime.bleScanResults[i];
                lines += "\n- " + item.name + " (" + item.address + ")";
            }
        }

        return lines;
    }

    std::string format_settings_info()
    {
        std::string lines = "Brightness: " + std::to_string(g_state.runtime.settings.displayBrightness);
        lines += "\nNight mode: " + std::string(g_state.runtime.settings.nightMode ? "on" : "off");
        lines += "\nTutorial: " + std::string(g_state.runtime.settings.tutorialSeen ? "hidden" : "shown");
        lines += "\nLast menu: " + std::string(current_card().title);
        if (g_state.runtime.telemetrySource == UiTelemetrySource::SimHubUdp)
        {
            lines += "\nTelemetry: SimHub";
        }
        else if (g_state.runtime.telemetrySource == UiTelemetrySource::Simulator)
        {
            lines += "\nTelemetry: internal simulator";
        }
        else
        {
            lines += "\nTelemetry: ESP32 OBD";
        }
        return lines;
    }

    void clear_detail_refs()
    {
        g_ui.detail = nullptr;
        g_ui.detailPanel = nullptr;
        g_ui.detailValue = nullptr;
        g_ui.detailNote = nullptr;
        g_ui.brightnessSlider = nullptr;
        g_ui.brightnessValue = nullptr;
    }

    void destroy_detail_screen()
    {
        if (g_ui.detail)
        {
            lv_obj_del(g_ui.detail);
        }
        clear_detail_refs();
    }

    void set_active_screen(UiScreenId screen)
    {
        g_state.activeScreen = screen;
        g_state.inDetail = (screen != UiScreenId::Home);
    }

    void update_detail_views()
    {
        if (!g_state.inDetail)
        {
            return;
        }

        switch (g_state.activeScreen)
        {
        case UiScreenId::Vehicle:
            if (g_ui.detailValue)
            {
                set_label_text(g_ui.detailValue, format_vehicle_info());
            }
            break;
        case UiScreenId::Wifi:
            if (g_ui.detailValue)
            {
                set_label_text(g_ui.detailValue, format_wifi_info());
            }
            break;
        case UiScreenId::Bluetooth:
            if (g_ui.detailValue)
            {
                set_label_text(g_ui.detailValue, format_ble_info());
            }
            break;
        case UiScreenId::Settings:
            if (g_ui.detailValue)
            {
                set_label_text(g_ui.detailValue, format_settings_info());
            }
            break;
        case UiScreenId::Brightness:
            if (g_ui.brightnessSlider)
            {
                const int currentValue = lv_slider_get_value(g_ui.brightnessSlider);
                if (currentValue != g_state.runtime.settings.displayBrightness)
                {
                    lv_slider_set_value(g_ui.brightnessSlider, g_state.runtime.settings.displayBrightness, LV_ANIM_OFF);
                }
            }
            if (g_ui.brightnessValue)
            {
                lv_label_set_text_fmt(g_ui.brightnessValue, "%d", g_state.runtime.settings.displayBrightness);
            }
            break;
        case UiScreenId::Home:
        default:
            break;
        }
    }

    void mark_interacted()
    {
        g_state.hasInteracted = true;
        if (g_state.tutorialVisible && g_ui.tutorial)
        {
            g_state.tutorialVisible = false;
            lv_obj_add_flag(g_ui.tutorial, LV_OBJ_FLAG_HIDDEN);
            persist_settings();
        }
    }

    void set_selected_index(int index)
    {
        g_state.selectedIndex = clamp_card_index(index);
        sync_runtime_settings();
        update_tile_selection();
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
        {
            return;
        }

        lv_indev_t *indev = lv_indev_get_act();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT)
        {
            show_home();
        }
    }

    lv_obj_t *make_chip(lv_obj_t *parent)
    {
        lv_obj_t *chip = lv_label_create(parent);
        lv_obj_add_style(chip, &styleBadge, 0);
        return chip;
    }

    void build_tutorial()
    {
        g_ui.tutorial = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.tutorial);
        lv_obj_add_style(g_ui.tutorial, &styleTutorial, 0);
        lv_obj_set_size(g_ui.tutorial, 264, 44);
        lv_obj_align(g_ui.tutorial, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_clear_flag(g_ui.tutorial, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.tutorialLabel = lv_label_create(g_ui.tutorial);
        lv_label_set_text(g_ui.tutorialLabel, "Tap a tile to open. Back returns home.");
        lv_obj_set_width(g_ui.tutorialLabel, LV_PCT(100));
        lv_label_set_long_mode(g_ui.tutorialLabel, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(g_ui.tutorialLabel, &styleMuted, 0);

        if (!g_state.tutorialVisible)
        {
            lv_obj_add_flag(g_ui.tutorial, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void open_detail(const CardDef &def);

    void on_tile_click(lv_event_t *e)
    {
        lv_obj_t *target = lv_event_get_target(e);
        for (size_t idx = 0; idx < CARD_COUNT; ++idx)
        {
            if (g_ui.tiles[idx].button == target)
            {
                mark_interacted();
                set_selected_index(static_cast<int>(idx));
                return;
            }
        }
    }

    void on_tile_released(lv_event_t *e)
    {
        lv_obj_t *target = lv_event_get_target(e);
        for (size_t idx = 0; idx < CARD_COUNT; ++idx)
        {
            if (g_ui.tiles[idx].button == target)
            {
                open_detail(CARDS[idx]);
                return;
            }
        }
    }

    TileWidgets make_tile(lv_obj_t *parent, size_t idx)
    {
        const CardDef &def = CARDS[idx];
        TileWidgets tile{};

        tile.button = lv_btn_create(parent);
        lv_obj_remove_style_all(tile.button);
        lv_obj_add_style(tile.button, &styleTile, 0);
        lv_obj_set_size(tile.button, 80, 62);
        lv_obj_clear_flag(tile.button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(tile.button, on_tile_click, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(tile.button, on_tile_released, LV_EVENT_CLICKED, nullptr);

        tile.icon = lv_label_create(tile.button);
        lv_label_set_text(tile.icon, def.symbol);
        lv_obj_set_style_text_font(tile.icon, &lv_font_montserrat_24, 0);
        lv_obj_align(tile.icon, LV_ALIGN_TOP_MID, 0, 4);

        tile.label = lv_label_create(tile.button);
        lv_label_set_text(tile.label, def.menuLabel);
        lv_obj_set_style_text_font(tile.label, &lv_font_montserrat_16, 0);
        lv_obj_align(tile.label, LV_ALIGN_BOTTOM_MID, 0, -6);

        return tile;
    }

    void build_home(lv_disp_t *disp)
    {
        g_ui.root = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.root);
        lv_obj_add_style(g_ui.root, &styleBg, 0);
        lv_obj_set_size(g_ui.root, lv_disp_get_hor_res(disp), lv_disp_get_ver_res(disp));
        lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *header = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(header);
        lv_obj_set_size(header, 264, 28);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_set_layout(header, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *brand = lv_label_create(header);
        lv_label_set_text(brand, "ShiftLight");
        lv_obj_set_style_text_font(brand, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(brand, color_text, 0);

        lv_obj_t *statusRow = lv_obj_create(header);
        lv_obj_remove_style_all(statusRow);
        lv_obj_set_layout(statusRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(statusRow, 8, 0);

        g_ui.wifiIcon = lv_label_create(statusRow);
        lv_label_set_text(g_ui.wifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(g_ui.wifiIcon, &lv_font_montserrat_24, 0);

        g_ui.bleIcon = lv_label_create(statusRow);
        lv_label_set_text(g_ui.bleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.bleIcon, &lv_font_montserrat_24, 0);

        lv_obj_t *hero = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(hero);
        lv_obj_add_style(hero, &stylePanelAccent, 0);
        lv_obj_set_size(hero, 264, 170);
        lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 44);
        lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.heroMeta = lv_label_create(hero);
        lv_obj_add_style(g_ui.heroMeta, &styleMuted, 0);
        lv_obj_set_width(g_ui.heroMeta, 236);
        lv_label_set_long_mode(g_ui.heroMeta, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.heroMeta, LV_ALIGN_TOP_LEFT, 0, 0);

        g_ui.rpmValue = lv_label_create(hero);
        lv_obj_set_style_text_font(g_ui.rpmValue, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(g_ui.rpmValue, color_text, 0);
        lv_obj_align(g_ui.rpmValue, LV_ALIGN_TOP_LEFT, 0, 42);

        g_ui.rpmUnit = lv_label_create(hero);
        lv_obj_add_style(g_ui.rpmUnit, &styleMuted, 0);
        lv_obj_set_style_text_font(g_ui.rpmUnit, &lv_font_montserrat_16, 0);
        lv_obj_align(g_ui.rpmUnit, LV_ALIGN_TOP_RIGHT, 0, 64);

        g_ui.heroSummary = lv_label_create(hero);
        lv_obj_add_style(g_ui.heroSummary, &styleMuted, 0);
        lv_obj_set_width(g_ui.heroSummary, 236);
        lv_label_set_long_mode(g_ui.heroSummary, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.heroSummary, LV_ALIGN_TOP_LEFT, 0, 96);

        lv_obj_t *badgeRow = lv_obj_create(hero);
        lv_obj_remove_style_all(badgeRow);
        lv_obj_set_size(badgeRow, 236, LV_SIZE_CONTENT);
        lv_obj_align(badgeRow, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_set_layout(badgeRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(badgeRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(badgeRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        g_ui.gearBadge = make_chip(badgeRow);
        g_ui.speedBadge = make_chip(badgeRow);
        g_ui.shiftBadge = make_chip(badgeRow);

        lv_obj_t *menuLabel = lv_label_create(g_ui.root);
        lv_label_set_text(menuLabel, "Quick Menu");
        lv_obj_set_style_text_font(menuLabel, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(menuLabel, color_text, 0);
        lv_obj_align(menuLabel, LV_ALIGN_TOP_LEFT, 12, 226);

        lv_obj_t *tileGrid = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(tileGrid);
        lv_obj_set_size(tileGrid, 264, 140);
        lv_obj_align(tileGrid, LV_ALIGN_TOP_MID, 0, 262);
        lv_obj_set_layout(tileGrid, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tileGrid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(tileGrid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(tileGrid, 10, 0);
        lv_obj_set_style_pad_column(tileGrid, 12, 0);
        lv_obj_clear_flag(tileGrid, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.tiles.fill(TileWidgets{});
        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            g_ui.tiles[i] = make_tile(tileGrid, i);
        }

        build_tutorial();
        update_status_icons();
        update_home_metrics();
        update_tile_selection();
    }

    lv_obj_t *make_detail_base(const char *title)
    {
        destroy_detail_screen();

        lv_obj_t *scr = lv_obj_create(nullptr);
        lv_obj_remove_style_all(scr);
        lv_obj_add_style(scr, &styleBg, 0);
        lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(scr, on_detail_gesture, LV_EVENT_GESTURE, nullptr);

        lv_obj_t *header = lv_obj_create(scr);
        lv_obj_remove_style_all(header);
        lv_obj_set_size(header, 264, 40);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t *backBtn = lv_btn_create(header);
        lv_obj_remove_style_all(backBtn);
        lv_obj_add_style(backBtn, &styleBackButton, 0);
        lv_obj_set_size(backBtn, 72, 36);
        lv_obj_align(backBtn, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(backBtn, on_back, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *backLabel = lv_label_create(backBtn);
        lv_label_set_text(backLabel, LV_SYMBOL_LEFT " Back");
        lv_obj_center(backLabel);

        lv_obj_t *titleLabel = lv_label_create(header);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(titleLabel, color_text, 0);
        lv_obj_align(titleLabel, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_t *panel = lv_obj_create(scr);
        lv_obj_remove_style_all(panel);
        lv_obj_add_style(panel, &stylePanel, 0);
        lv_obj_set_size(panel, 264, 368);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 60);
        lv_obj_set_scroll_dir(panel, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(panel, 10, 0);

        g_ui.detail = scr;
        g_ui.detailPanel = panel;
        return panel;
    }

    void show_home()
    {
        if (!g_ui.root)
        {
            return;
        }

        lv_disp_load_scr(g_ui.root);
        set_active_screen(UiScreenId::Home);
        update_status_icons();
        update_home_metrics();
        update_tile_selection();
    }

    void open_brightness()
    {
        lv_obj_t *panel = make_detail_base("Brightness");

        lv_obj_t *title = lv_label_create(panel);
        lv_label_set_text(title, "Display brightness");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(title, color_text, 0);

        g_ui.brightnessValue = lv_label_create(panel);
        lv_obj_set_style_text_font(g_ui.brightnessValue, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(g_ui.brightnessValue, color_accent, 0);
        lv_label_set_text_fmt(g_ui.brightnessValue, "%d", g_state.runtime.settings.displayBrightness);

        g_ui.brightnessSlider = lv_slider_create(panel);
        lv_obj_set_width(g_ui.brightnessSlider, 240);
        lv_slider_set_range(g_ui.brightnessSlider, 10, 255);
        lv_slider_set_value(g_ui.brightnessSlider, g_state.runtime.settings.displayBrightness, LV_ANIM_OFF);

        lv_obj_add_event_cb(g_ui.brightnessSlider, [](lv_event_t *e)
                            {
            const int value = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
            g_state.runtime.settings.displayBrightness = value;
            if (g_ui.brightnessValue)
            {
                lv_label_set_text_fmt(g_ui.brightnessValue, "%d", value);
            }
            if (g_hooks.setBrightness)
            {
                g_hooks.setBrightness(static_cast<uint8_t>(value), g_hooks.userData);
            } }, LV_EVENT_VALUE_CHANGED, nullptr);

        lv_obj_add_event_cb(g_ui.brightnessSlider, [](lv_event_t *)
                            { persist_settings(); }, LV_EVENT_RELEASED, nullptr);

        g_ui.detailNote = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailNote, 240);
        lv_label_set_long_mode(g_ui.detailNote, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(g_ui.detailNote, &styleMuted, 0);
        lv_label_set_text(g_ui.detailNote, "The ESP32 panel brightness and the simulator preview use the same shared value.");

        lv_disp_load_scr(g_ui.detail);
        set_active_screen(UiScreenId::Brightness);
        update_detail_views();
    }

    void open_vehicle()
    {
        lv_obj_t *panel = make_detail_base("Vehicle");

        g_ui.detailValue = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailValue, 240);
        lv_label_set_long_mode(g_ui.detailValue, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.detailValue, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(g_ui.detailValue, color_text, 0);

        g_ui.detailNote = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailNote, 240);
        lv_label_set_long_mode(g_ui.detailNote, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(g_ui.detailNote, &styleMuted, 0);
        lv_label_set_text(g_ui.detailNote, "Live telemetry is shared between ESP32 and simulator. This view is intended for quick validation during UI work.");

        lv_disp_load_scr(g_ui.detail);
        set_active_screen(UiScreenId::Vehicle);
        update_detail_views();
    }

    void open_wifi()
    {
        lv_obj_t *panel = make_detail_base("WiFi");

        g_ui.detailValue = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailValue, 240);
        lv_label_set_long_mode(g_ui.detailValue, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(g_ui.detailValue, color_text, 0);

        g_ui.detailNote = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailNote, 240);
        lv_label_set_long_mode(g_ui.detailNote, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(g_ui.detailNote, &styleMuted, 0);
        lv_label_set_text(g_ui.detailNote, "Use the Web UI on hardware for configuration. The simulator exposes representative connection states and scan results.");

        lv_disp_load_scr(g_ui.detail);
        set_active_screen(UiScreenId::Wifi);
        update_detail_views();
    }

    void open_ble()
    {
        lv_obj_t *panel = make_detail_base("Bluetooth");

        g_ui.detailValue = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailValue, 240);
        lv_label_set_long_mode(g_ui.detailValue, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(g_ui.detailValue, color_text, 0);

        g_ui.detailNote = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailNote, 240);
        lv_label_set_long_mode(g_ui.detailNote, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(g_ui.detailNote, &styleMuted, 0);
        lv_label_set_text(g_ui.detailNote, "The simulator keeps BLE behavior deterministic, which makes UI states and later tests reproducible.");

        lv_disp_load_scr(g_ui.detail);
        set_active_screen(UiScreenId::Bluetooth);
        update_detail_views();
    }

    void open_settings()
    {
        lv_obj_t *panel = make_detail_base("Settings");

        g_ui.detailValue = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailValue, 240);
        lv_label_set_long_mode(g_ui.detailValue, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(g_ui.detailValue, color_text, 0);

        g_ui.detailNote = lv_label_create(panel);
        lv_obj_set_width(g_ui.detailNote, 240);
        lv_label_set_long_mode(g_ui.detailNote, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(g_ui.detailNote, &styleMuted, 0);
        lv_label_set_text(g_ui.detailNote, "This page summarizes the UI-facing settings. Extend the shared runtime state here when additional simulator controls are needed.");

        lv_disp_load_scr(g_ui.detail);
        set_active_screen(UiScreenId::Settings);
        update_detail_views();
    }

    void open_detail(const CardDef &def)
    {
        mark_interacted();
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

    void delete_logo_overlay(lv_timer_t *timer)
    {
        lv_obj_t *overlay = static_cast<lv_obj_t *>(timer->user_data);
        if (overlay)
        {
            lv_obj_del(overlay);
        }
        if (g_ui.logoOverlay == overlay)
        {
            g_ui.logoOverlay = nullptr;
        }
        lv_timer_del(timer);
    }
} // namespace

void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks, const UiRuntimeState &initialState)
{
    g_hooks = hooks;
    g_ui = UiRefs{};
    g_state = UiState{};
    g_state.runtime = initialState;
    g_state.selectedIndex = clamp_card_index(initialState.settings.lastMenuIndex);
    g_state.tutorialVisible = !initialState.settings.tutorialSeen;
    g_ui.disp = disp;
    sync_runtime_settings();

    apply_styles();
    build_home(disp);
    lv_disp_load_scr(g_ui.root);
    set_active_screen(UiScreenId::Home);
    update_status_icons();
    update_home_metrics();
    update_tile_selection();
}

void ui_s3_loop(const UiRuntimeState &state)
{
    apply_runtime_state(state);
    update_status_icons();
    update_home_metrics();
    update_tile_selection();
    update_detail_views();
}

void ui_s3_set_gear(int gear)
{
    g_state.runtime.gear = gear;
    update_home_metrics();
    update_detail_views();
}

void ui_s3_set_shiftlight(bool active)
{
    g_state.runtime.shift = active;
    update_home_metrics();
    update_detail_views();
}

void ui_s3_show_logo()
{
    if (g_ui.logoOverlay)
    {
        lv_obj_del(g_ui.logoOverlay);
        g_ui.logoOverlay = nullptr;
    }

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_remove_style_all(panel);
    lv_obj_add_style(panel, &stylePanelAccent, 0);
    lv_obj_set_size(panel, 200, 104);
    lv_obj_center(panel);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "ShiftLight");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, color_text, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *subtitle = lv_label_create(panel);
    lv_label_set_text(subtitle, "UI preview");
    lv_obj_add_style(subtitle, &styleMuted, 0);
    lv_obj_align(subtitle, LV_ALIGN_BOTTOM_MID, 0, -14);

    g_ui.logoOverlay = overlay;
    lv_timer_t *timer = lv_timer_create(delete_logo_overlay, 1200, overlay);
    lv_timer_set_repeat_count(timer, 1);
}

void ui_s3_debug_dispatch(UiDebugAction action)
{
    switch (action)
    {
    case UiDebugAction::PreviousCard:
        if (g_state.inDetail)
        {
            show_home();
        }
        else
        {
            set_selected_index(g_state.selectedIndex - 1);
        }
        break;
    case UiDebugAction::NextCard:
        if (!g_state.inDetail)
        {
            set_selected_index(g_state.selectedIndex + 1);
        }
        break;
    case UiDebugAction::OpenSelectedCard:
        if (!g_state.inDetail)
        {
            open_detail(current_card());
        }
        break;
    case UiDebugAction::GoHome:
        show_home();
        break;
    case UiDebugAction::ShowLogo:
        ui_s3_show_logo();
        break;
    }
}

UiDebugSnapshot ui_s3_debug_snapshot()
{
    UiDebugSnapshot snapshot{};
    snapshot.activeScreen = g_state.activeScreen;
    snapshot.selectedCardIndex = g_state.selectedIndex;
    snapshot.inDetail = g_state.inDetail;
    snapshot.displayBrightness = g_state.runtime.settings.displayBrightness;
    snapshot.gear = g_state.runtime.gear;
    snapshot.rpm = g_state.runtime.rpm;
    snapshot.speedKmh = g_state.runtime.speedKmh;
    snapshot.shift = g_state.runtime.shift;
    snapshot.bleConnected = g_state.runtime.bleConnected;
    snapshot.staConnected = g_state.runtime.staConnected;
    return snapshot;
}
