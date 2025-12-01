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
#include "hardware/led_bar.h"

namespace
{
    // =======================================================================
    // CARD DEFINITIONS - Main menu icons (horizontal layout)
    // =======================================================================
    enum class CardScreen
    {
        Data,
        Brightness,
        Wifi,
        Bluetooth,
        LedTest,
        Settings
    };

    struct CardDef
    {
        CardScreen screen;
        const char *title;
        const char *symbol;
    };

    constexpr CardDef CARDS[] = {
        {CardScreen::Data, "Data", LV_SYMBOL_GPS},
        {CardScreen::Brightness, "Light", LV_SYMBOL_EYE_OPEN},
        {CardScreen::Wifi, "WiFi", LV_SYMBOL_WIFI},
        {CardScreen::Bluetooth, "BT", LV_SYMBOL_BLUETOOTH},
        {CardScreen::LedTest, "LED Test", LV_SYMBOL_LIST},
        {CardScreen::Settings, "Info", LV_SYMBOL_SETTINGS},
    };

    constexpr size_t CARD_COUNT = sizeof(CARDS) / sizeof(CARDS[0]);

    // =======================================================================
    // DATA PAGE DEFINITIONS - Large number displays
    // =======================================================================
    enum class DataPage
    {
        RPM,
        Speed,
        Gear,
        Coolant
    };

    struct DataPageDef
    {
        DataPage page;
        const char *label;
        const char *unit;
        int maxValue;
    };

    constexpr DataPageDef DATA_PAGES[] = {
        {DataPage::RPM, "RPM", "", 8000},
        {DataPage::Speed, "Speed", "km/h", 260},
        {DataPage::Gear, "Gear", "", 8},
        {DataPage::Coolant, "Coolant", "\xc2\xb0"
                                       "C",
         140},
    };

    constexpr size_t DATA_PAGE_COUNT = sizeof(DATA_PAGES) / sizeof(DATA_PAGES[0]);

    // =======================================================================
    // UI WIDGETS
    // =======================================================================
    struct CardWidgets
    {
        lv_obj_t *container = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *label = nullptr;
    };

    struct DataPageWidgets
    {
        lv_obj_t *container = nullptr;
        lv_obj_t *arc = nullptr;
        lv_obj_t *valueLabel = nullptr;
        lv_obj_t *unitLabel = nullptr;
        lv_obj_t *nameLabel = nullptr;
    };

    struct UiRefs
    {
        lv_disp_t *disp = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *statusBar = nullptr;
        lv_obj_t *wifiIcon = nullptr;
        lv_obj_t *bleIcon = nullptr;
        lv_obj_t *ledStatus = nullptr;
        lv_obj_t *carousel = nullptr;
        lv_obj_t *pageIndicator = nullptr;
        lv_obj_t *tutorial = nullptr;
        lv_obj_t *logoOverlay = nullptr;
        lv_obj_t *detail = nullptr;
        lv_obj_t *detailContent = nullptr;
        lv_obj_t *detailWifiIcon = nullptr;
        lv_obj_t *detailBleIcon = nullptr;
        lv_obj_t *wifiList = nullptr;
        lv_obj_t *bleList = nullptr;
        lv_obj_t *wifiSpinner = nullptr;
        lv_obj_t *bleSpinner = nullptr;
        lv_obj_t *wifiHeader = nullptr;
        lv_obj_t *brightnessSlider = nullptr;
        lv_obj_t *brightnessValue = nullptr;
        lv_obj_t *brightnessPreview = nullptr;
        lv_obj_t *dataScreen = nullptr;
        lv_obj_t *dataCarousel = nullptr;
        lv_obj_t *dataIndicator = nullptr;
        lv_obj_t *dataWifiIcon = nullptr;
        lv_obj_t *dataBleIcon = nullptr;
        lv_obj_t *ledTestInfo = nullptr;
        std::array<CardWidgets, CARD_COUNT> cards{};
        std::array<DataPageWidgets, DATA_PAGE_COUNT> dataPages{};
    };

    struct UiState
    {
        int cardIndex = 0;
        int dataPageIndex = 0;
        bool inDetail = false;
        bool inDataView = false;
        bool tutorialVisible = true;
        bool hasInteracted = false;
        int gear = 0;
        int rpm = 0;
        int speed = 0;
        int coolant = 0;
        bool shift = false;
        WifiStatus lastWifi{};
        bool bleConnected = false;
        bool bleConnecting = false;
        uint32_t lastTouchTime = 0;
        bool iconsHidden = false;
        uint32_t lastWifiScanMs = 0;
        uint32_t lastBleScanMs = 0;
        CardScreen detailScreen = CardScreen::Data;
    };

    UiRefs g_ui;
    UiState g_state;
    UiDisplayHooks g_hooks;

    // =======================================================================
    // STYLES - Clean Apple-like dark theme
    // =======================================================================
    lv_style_t styleBg;
    lv_style_t styleCard;
    lv_style_t styleCardSide;
    lv_style_t styleMuted;
    lv_style_t styleIndicator;
    lv_style_t styleTutorial;
    lv_style_t styleDataValue;
    lv_style_t styleDataUnit;
    lv_style_t styleDataName;

    const lv_color_t color_bg = lv_color_hex(0x000000);
    const lv_color_t color_card = lv_color_hex(0x1C1C1E);
    const lv_color_t color_card_grad = lv_color_hex(0x101012);
    const lv_color_t color_card_accent = lv_color_hex(0x0A84FF);
    const lv_color_t color_muted = lv_color_hex(0x8E8E93);
    const lv_color_t color_ok = lv_color_hex(0x30D158);
    const lv_color_t color_warn = lv_color_hex(0xFF9F0A);
    const lv_color_t color_error = lv_color_hex(0xFF453A);
    const lv_color_t color_text = lv_color_hex(0xF2F2F7);
    const lv_color_t color_text_secondary = lv_color_hex(0x8E8E93);

    constexpr uint32_t SCAN_COOLDOWN_MS = 5000;
    constexpr uint32_t TOUCH_DEBOUNCE_MS = 200;

    // =======================================================================
    // HELPER FUNCTIONS
    // =======================================================================
    bool debounce_touch()
    {
        uint32_t now = millis();
        if (now - g_state.lastTouchTime < TOUCH_DEBOUNCE_MS)
        {
            return true;
        }
        g_state.lastTouchTime = now;
        return false;
    }

    const CardDef &current_card()
    {
        const int count = static_cast<int>(CARD_COUNT);
        g_state.cardIndex = (g_state.cardIndex % count + count) % count;
        return CARDS[g_state.cardIndex];
    }

    void show_status_icons();
    void update_page_indicator();
    void update_carousel_visuals();
    void update_data_indicator();
    void update_data_values();

    void mark_interacted()
    {
        g_state.hasInteracted = true;
        g_state.lastTouchTime = millis();

        if (g_state.iconsHidden)
        {
            g_state.iconsHidden = false;
            show_status_icons();
        }

        if (g_state.tutorialVisible && g_ui.tutorial)
        {
            g_state.tutorialVisible = false;
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, g_ui.tutorial);
            lv_anim_set_values(&a, LV_OPA_80, LV_OPA_TRANSP);
            lv_anim_set_time(&a, 300);
            lv_anim_set_exec_cb(&a, [](void *obj, int32_t v)
                                { lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0); });
            lv_anim_set_ready_cb(&a, [](lv_anim_t *anim)
                                 { lv_obj_add_flag(static_cast<lv_obj_t *>(anim->var), LV_OBJ_FLAG_HIDDEN); });
            lv_anim_start(&a);
        }
    }

    // =======================================================================
    // STYLE INITIALIZATION
    // =======================================================================
    void apply_styles()
    {
        lv_style_init(&styleBg);
        lv_style_set_bg_color(&styleBg, color_bg);
        lv_style_set_bg_opa(&styleBg, LV_OPA_COVER);
        lv_style_set_pad_all(&styleBg, 0);

        lv_style_init(&styleCard);
        lv_style_set_bg_color(&styleCard, color_card);
        lv_style_set_bg_grad_color(&styleCard, color_card_grad);
        lv_style_set_bg_grad_dir(&styleCard, LV_GRAD_DIR_VER);
        lv_style_set_bg_opa(&styleCard, LV_OPA_COVER);
        lv_style_set_radius(&styleCard, 26);
        lv_style_set_pad_all(&styleCard, 12);
        lv_style_set_border_width(&styleCard, 0);
        lv_style_set_shadow_width(&styleCard, 16);
        lv_style_set_shadow_spread(&styleCard, 2);
        lv_style_set_shadow_color(&styleCard, lv_color_make(0, 0, 0));
        lv_style_set_shadow_opa(&styleCard, LV_OPA_40);

        lv_style_init(&styleCardSide);
        lv_style_set_bg_color(&styleCardSide, lv_color_hex(0x111114));
        lv_style_set_bg_grad_color(&styleCardSide, lv_color_hex(0x0B0B0D));
        lv_style_set_bg_grad_dir(&styleCardSide, LV_GRAD_DIR_VER);
        lv_style_set_bg_opa(&styleCardSide, LV_OPA_80);
        lv_style_set_radius(&styleCardSide, 24);
        lv_style_set_pad_all(&styleCardSide, 10);
        lv_style_set_shadow_width(&styleCardSide, 10);
        lv_style_set_shadow_color(&styleCardSide, lv_color_make(0, 0, 0));
        lv_style_set_shadow_opa(&styleCardSide, LV_OPA_30);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, color_muted);

        lv_style_init(&styleIndicator);
        lv_style_set_radius(&styleIndicator, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleIndicator, lv_color_hex(0x1C1C1E));
        lv_style_set_bg_opa(&styleIndicator, LV_OPA_COVER);

        lv_style_init(&styleTutorial);
        lv_style_set_radius(&styleTutorial, 14);
        lv_style_set_bg_color(&styleTutorial, lv_color_hex(0x1C1C1E));
        lv_style_set_bg_opa(&styleTutorial, LV_OPA_90);
        lv_style_set_pad_all(&styleTutorial, 12);
        lv_style_set_border_width(&styleTutorial, 0);

        lv_style_init(&styleDataValue);
        lv_style_set_text_color(&styleDataValue, color_text);

        lv_style_init(&styleDataUnit);
        lv_style_set_text_color(&styleDataUnit, color_text_secondary);

        lv_style_init(&styleDataName);
        lv_style_set_text_color(&styleDataName, color_muted);
    }

    // =======================================================================
    // PAGE INDICATOR UPDATE
    // =======================================================================
    void update_page_indicator()
    {
        if (!g_ui.pageIndicator)
            return;

        uint32_t childCount = lv_obj_get_child_cnt(g_ui.pageIndicator);
        for (uint32_t i = 0; i < childCount; ++i)
        {
            lv_obj_t *dot = lv_obj_get_child(g_ui.pageIndicator, i);
            bool active = static_cast<int>(i) == g_state.cardIndex;
            lv_obj_set_style_bg_color(dot, active ? color_card_accent : lv_color_hex(0x2C2C2E), 0);
            lv_obj_set_style_opa(dot, active ? LV_OPA_COVER : LV_OPA_40, 0);

            lv_coord_t targetW = active ? 26 : 10;
            lv_coord_t currentW = lv_obj_get_width(dot);
            if (currentW != targetW)
            {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, dot);
                lv_anim_set_values(&a, currentW, targetW);
                lv_anim_set_time(&a, 220);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_set_exec_cb(&a, [](void *obj, int32_t v)
                                    { lv_obj_set_width(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v)); });
                lv_anim_start(&a);
            }
        }
    }

    void update_data_indicator()
    {
        if (!g_ui.dataIndicator)
            return;

        uint32_t childCount = lv_obj_get_child_cnt(g_ui.dataIndicator);
        for (uint32_t i = 0; i < childCount; ++i)
        {
            lv_obj_t *dot = lv_obj_get_child(g_ui.dataIndicator, i);
            bool active = static_cast<int>(i) == g_state.dataPageIndex;
            lv_obj_set_style_bg_color(dot, active ? color_card_accent : lv_color_hex(0x636366), 0);
            lv_obj_set_width(dot, active ? 20 : 10);
        }
    }

    // =======================================================================
    // CAROUSEL VISUAL UPDATE - Zoom/opacity animation
    // =======================================================================
    void update_carousel_visuals()
    {
        if (!g_ui.carousel)
            return;

        lv_coord_t scrollX = lv_obj_get_scroll_x(g_ui.carousel);
        lv_coord_t viewW = lv_obj_get_width(g_ui.carousel);

        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            CardWidgets &cw = g_ui.cards[i];
            if (!cw.container)
                continue;

            lv_coord_t x = lv_obj_get_x(cw.container) - scrollX;
            lv_coord_t w = lv_obj_get_width(cw.container);
            lv_coord_t center = x + w / 2;
            lv_coord_t dist = LV_ABS(center - viewW / 2);
            float ratio = std::min<float>(1.0f, static_cast<float>(dist) / (viewW / 2.0f));
            float zoom = 0.9f + (1.3f - 0.9f) * (1.0f - ratio);
            lv_coord_t zoomInt = static_cast<lv_coord_t>(zoom * 256);
            lv_obj_set_style_transform_zoom(cw.container, zoomInt, 0);

            lv_color_t bg = lv_color_mix(color_card_accent, color_card, static_cast<uint8_t>(ratio * 255));
            lv_obj_set_style_bg_color(cw.container, bg, 0);

            if (ratio > 0.6f)
            {
                lv_obj_add_style(cw.container, &styleCardSide, 0);
            }
            else
            {
                lv_obj_add_style(cw.container, &styleCard, 0);
            }
        }
    }

    void update_data_carousel_visuals()
    {
        if (!g_ui.dataCarousel)
            return;

        lv_coord_t scrollX = lv_obj_get_scroll_x(g_ui.dataCarousel);
        lv_coord_t pageWidth = lv_disp_get_hor_res(g_ui.disp);
        int newIdx = (scrollX + pageWidth / 2) / pageWidth;
        newIdx = std::max(0, std::min(newIdx, static_cast<int>(DATA_PAGE_COUNT) - 1));

        if (g_state.dataPageIndex != newIdx)
        {
            g_state.dataPageIndex = newIdx;
            update_data_indicator();
        }

        for (size_t i = 0; i < DATA_PAGE_COUNT; ++i)
        {
            DataPageWidgets &pw = g_ui.dataPages[i];
            if (!pw.container)
                continue;

            lv_coord_t x = lv_obj_get_x(pw.container) - scrollX;
            lv_coord_t w = lv_obj_get_width(pw.container);
            lv_coord_t center = x + w / 2;
            lv_coord_t dist = LV_ABS(center - pageWidth / 2);
            float ratio = std::min<float>(1.0f, static_cast<float>(dist) / (pageWidth / 2.0f));
            float zoom = 0.9f + (1.15f - 0.9f) * (1.0f - ratio);
            lv_coord_t zoomInt = static_cast<lv_coord_t>(zoom * 256);
            lv_obj_set_style_transform_zoom(pw.container, zoomInt, 0);
        }
    }

    // =======================================================================
    // STATUS ICONS UPDATE
    // =======================================================================
    void get_wifi_icon_style(lv_color_t &col, lv_opa_t &opa, bool &isConnected)
    {
        bool staConnected = g_state.lastWifi.staConnected;
        bool apActive = g_state.lastWifi.apActive;
        bool staConnecting = g_state.lastWifi.staConnecting;
        int apClients = g_state.lastWifi.apClients;

        const uint32_t now = millis();
        opa = LV_OPA_COVER;
        isConnected = false;

        if (staConnected)
        {
            col = color_ok;
            isConnected = true;
        }
        else if (apActive && apClients > 0)
        {
            col = color_ok;
            isConnected = true;
        }
        else if (apActive || staConnecting)
        {
            col = color_warn;
            float phase = static_cast<float>(now % 1000) / 500.0f;
            opa = (phase < 1.0f) ? LV_OPA_COVER : LV_OPA_30;
        }
        else
        {
            col = color_error;
        }
    }

    void get_ble_icon_style(lv_color_t &col, lv_opa_t &opa, bool &isConnected)
    {
        const uint32_t now = millis();
        opa = LV_OPA_COVER;
        isConnected = false;

        if (g_state.bleConnected)
        {
            col = color_card_accent;
            isConnected = true;
        }
        else if (g_state.bleConnecting)
        {
            col = color_card_accent;
            float phase = static_cast<float>(now % 800) / 400.0f;
            opa = (phase < 1.0f) ? LV_OPA_COVER : LV_OPA_30;
        }
        else
        {
            col = color_error;
            opa = LV_OPA_80;
        }
    }

    void apply_icon_style(lv_obj_t *icon, lv_color_t col, lv_opa_t opa)
    {
        if (!icon)
            return;
        lv_obj_set_style_text_color(icon, col, 0);
        lv_obj_set_style_opa(icon, opa, 0);
    }

    void update_status_icons()
    {
        lv_color_t wifiCol, bleCol;
        lv_opa_t wifiOpa, bleOpa;
        bool wifiConnected, bleConnected;

        get_wifi_icon_style(wifiCol, wifiOpa, wifiConnected);
        get_ble_icon_style(bleCol, bleOpa, bleConnected);

        apply_icon_style(g_ui.wifiIcon, wifiCol, wifiOpa);
        apply_icon_style(g_ui.bleIcon, bleCol, bleOpa);
        apply_icon_style(g_ui.detailWifiIcon, wifiCol, wifiOpa);
        apply_icon_style(g_ui.detailBleIcon, bleCol, bleOpa);
        apply_icon_style(g_ui.dataWifiIcon, wifiCol, wifiOpa);
        apply_icon_style(g_ui.dataBleIcon, bleCol, bleOpa);

        if (g_ui.ledStatus)
        {
            lv_color_t ledCol = g_state.shift ? color_error : color_ok;
            lv_obj_set_style_bg_color(g_ui.ledStatus, ledCol, 0);
            lv_obj_set_style_bg_opa(g_ui.ledStatus, g_state.shift ? LV_OPA_80 : LV_OPA_50, 0);
        }
    }

    void show_status_icons()
    {
        if (g_ui.statusBar)
            lv_obj_set_style_opa(g_ui.statusBar, LV_OPA_COVER, 0);
    }

    // =======================================================================
    // DATA PAGE VALUE UPDATES
    // =======================================================================
    void update_arc_value(DataPageWidgets &w, const DataPageDef &def, int value)
    {
        if (!w.arc)
            return;
        int maxVal = def.maxValue > 0 ? def.maxValue : 1;
        int clamped = std::max(0, std::min(value, maxVal));
        int angle = static_cast<int>((static_cast<float>(clamped) / maxVal) * 270.0f);
        lv_arc_set_value(w.arc, angle);
    }

    void update_data_values()
    {
        if (g_ui.dataPages[0].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[0].valueLabel, "%d", g_state.rpm);
            update_arc_value(g_ui.dataPages[0], DATA_PAGES[0], g_state.rpm);
        }

        if (g_ui.dataPages[1].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[1].valueLabel, "%d", g_state.speed);
            update_arc_value(g_ui.dataPages[1], DATA_PAGES[1], g_state.speed);
        }

        if (g_ui.dataPages[2].valueLabel)
        {
            lv_label_set_text(g_ui.dataPages[2].valueLabel, g_state.gear <= 0 ? "N" : String(g_state.gear).c_str());
            update_arc_value(g_ui.dataPages[2], DATA_PAGES[2], std::max(0, g_state.gear));
        }

        if (g_ui.dataPages[3].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[3].valueLabel, "%d", g_state.coolant);
            update_arc_value(g_ui.dataPages[3], DATA_PAGES[3], g_state.coolant);
        }
    }

    // =======================================================================
    // NAVIGATION HELPERS
    // =======================================================================
    void show_home();

    void on_back(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_home();
    }

    void attach_back_handler(lv_obj_t *obj)
    {
        lv_obj_add_event_cb(obj, [](lv_event_t *evt)
                            {
            if (lv_event_get_code(evt) == LV_EVENT_GESTURE &&
                lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
            {
                show_home();
            } }, LV_EVENT_GESTURE, nullptr);
    }

    void scroll_to_card(int idx, lv_anim_enable_t anim)
    {
        if (!g_ui.carousel)
            return;
        const int count = static_cast<int>(CARD_COUNT);
        idx = (idx % count + count) % count;
        g_state.cardIndex = idx;
        lv_obj_t *target = g_ui.cards[static_cast<size_t>(idx)].container;
        if (target)
        {
            lv_obj_scroll_to_view(target, anim);
        }
        update_page_indicator();
    }

    void cleanup_detail()
    {
        if (g_ui.dataScreen)
        {
            lv_obj_del(g_ui.dataScreen);
            g_ui.dataScreen = nullptr;
            g_ui.dataCarousel = nullptr;
            g_ui.dataIndicator = nullptr;
            g_ui.dataWifiIcon = nullptr;
            g_ui.dataBleIcon = nullptr;
            g_ui.dataPages.fill(DataPageWidgets{});
        }
        if (g_ui.detail)
        {
            lv_obj_del(g_ui.detail);
            g_ui.detail = nullptr;
            g_ui.detailContent = nullptr;
            g_ui.detailWifiIcon = nullptr;
            g_ui.detailBleIcon = nullptr;
            g_ui.wifiList = nullptr;
            g_ui.bleList = nullptr;
            g_ui.wifiSpinner = nullptr;
            g_ui.bleSpinner = nullptr;
            g_ui.wifiHeader = nullptr;
            g_ui.brightnessSlider = nullptr;
            g_ui.brightnessValue = nullptr;
            g_ui.brightnessPreview = nullptr;
            g_ui.ledTestInfo = nullptr;
        }
        g_state.inDetail = false;
        g_state.inDataView = false;
    }

    // =======================================================================
    // DETAIL BASE
    // =======================================================================
    lv_obj_t *make_detail_base(const char *title)
    {
        cleanup_detail();

        lv_obj_t *scr = lv_obj_create(nullptr);
        lv_obj_remove_style_all(scr);
        lv_obj_add_style(scr, &styleBg, 0);
        lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        attach_back_handler(scr);

        lv_obj_t *header = lv_obj_create(scr);
        lv_obj_remove_style_all(header);
        lv_obj_set_size(header, LV_PCT(100), 50);
        lv_obj_set_layout(header, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(header, 12, 0);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);

        lv_obj_t *back = lv_label_create(header);
        lv_label_set_text(back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(back, color_card_accent, 0);
        lv_obj_set_style_text_font(back, &lv_font_montserrat_24, 0);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *titleLbl = lv_label_create(header);
        lv_label_set_text(titleLbl, title);
        lv_obj_set_style_text_color(titleLbl, color_text, 0);
        lv_obj_set_style_text_font(titleLbl, LV_FONT_MONTSERRAT_22, 0);

        lv_obj_t *icons = lv_obj_create(header);
        lv_obj_remove_style_all(icons);
        lv_obj_set_layout(icons, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(icons, 6, 0);
        lv_obj_set_style_bg_opa(icons, LV_OPA_TRANSP, 0);

        g_ui.detailWifiIcon = lv_label_create(icons);
        lv_label_set_text(g_ui.detailWifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(g_ui.detailWifiIcon, LV_FONT_MONTSERRAT_18, 0);

        g_ui.detailBleIcon = lv_label_create(icons);
        lv_label_set_text(g_ui.detailBleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.detailBleIcon, LV_FONT_MONTSERRAT_18, 0);

        lv_obj_t *content = lv_obj_create(scr);
        lv_obj_remove_style_all(content);
        lv_obj_add_style(content, &styleBg, 0);
        lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
        lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_pad_hor(content, 18, 0);
        lv_obj_set_style_pad_bottom(content, 18, 0);
        lv_obj_set_style_pad_top(content, 0, 0);
        lv_obj_set_layout(content, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.detail = scr;
        g_ui.detailContent = content;
        g_state.inDetail = true;
        return content;
    }

    // =======================================================================
    // DATA VIEW
    // =======================================================================
    DataPageWidgets make_data_page(size_t idx)
    {
        const DataPageDef &def = DATA_PAGES[idx];
        DataPageWidgets w{};

        w.container = lv_obj_create(g_ui.dataCarousel);
        lv_obj_remove_style_all(w.container);
        lv_obj_set_size(w.container, lv_disp_get_hor_res(g_ui.disp) - 30, lv_disp_get_ver_res(g_ui.disp) - 120);
        lv_obj_add_style(w.container, &styleCard, 0);
        lv_obj_set_layout(w.container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_SNAPPABLE);

        w.nameLabel = lv_label_create(w.container);
        lv_label_set_text(w.nameLabel, def.label);
        lv_obj_add_style(w.nameLabel, &styleDataName, 0);
        lv_obj_set_style_text_font(w.nameLabel, &lv_font_montserrat_20, 0);

        w.arc = lv_arc_create(w.container);
        lv_obj_set_size(w.arc, 160, 160);
        lv_arc_set_bg_angles(w.arc, 135, 45);
        lv_arc_set_range(w.arc, 0, 270);
        lv_arc_set_value(w.arc, 0);
        lv_obj_set_style_arc_width(w.arc, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_color(w.arc, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
        lv_obj_set_style_arc_width(w.arc, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(w.arc, color_card_accent, LV_PART_INDICATOR);
        lv_obj_clear_flag(w.arc, LV_OBJ_FLAG_CLICKABLE);

        w.valueLabel = lv_label_create(w.container);
        lv_label_set_text(w.valueLabel, "---");
        lv_obj_add_style(w.valueLabel, &styleDataValue, 0);
        lv_obj_set_style_text_font(w.valueLabel, &lv_font_montserrat_48, 0);
        lv_obj_set_style_pad_top(w.valueLabel, 6, 0);

        w.unitLabel = lv_label_create(w.container);
        lv_label_set_text(w.unitLabel, def.unit);
        lv_obj_add_style(w.unitLabel, &styleDataUnit, 0);
        lv_obj_set_style_text_font(w.unitLabel, LV_FONT_MONTSERRAT_18, 0);

        return w;
    }

    void on_data_gesture(lv_event_t *e)
    {
        if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
            lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
        {
            show_home();
        }
    }

    void on_data_scroll(lv_event_t *e)
    {
        LV_UNUSED(e);
        update_data_carousel_visuals();
    }

    void open_data_view()
    {
        g_state.detailScreen = CardScreen::Data;
        cleanup_detail();

        g_ui.dataScreen = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.dataScreen);
        lv_obj_add_style(g_ui.dataScreen, &styleBg, 0);
        lv_obj_set_size(g_ui.dataScreen, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.dataScreen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(g_ui.dataScreen, on_data_gesture, LV_EVENT_GESTURE, nullptr);

        lv_obj_t *statusIcons = lv_obj_create(g_ui.dataScreen);
        lv_obj_remove_style_all(statusIcons);
        lv_obj_set_size(statusIcons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(statusIcons, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(statusIcons, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(statusIcons, 8, 0);
        lv_obj_align(statusIcons, LV_ALIGN_TOP_RIGHT, -12, 12);
        lv_obj_clear_flag(statusIcons, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.dataWifiIcon = lv_label_create(statusIcons);
        lv_label_set_text(g_ui.dataWifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(g_ui.dataWifiIcon, LV_FONT_MONTSERRAT_18, 0);

        g_ui.dataBleIcon = lv_label_create(statusIcons);
        lv_label_set_text(g_ui.dataBleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.dataBleIcon, LV_FONT_MONTSERRAT_18, 0);

        lv_obj_t *backLbl = lv_label_create(g_ui.dataScreen);
        lv_label_set_text(backLbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(backLbl, LV_FONT_MONTSERRAT_22, 0);
        lv_obj_set_style_text_color(backLbl, color_card_accent, 0);
        lv_obj_align(backLbl, LV_ALIGN_TOP_LEFT, 12, 12);
        lv_obj_add_flag(backLbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(backLbl, on_back, LV_EVENT_CLICKED, nullptr);

        g_ui.dataCarousel = lv_obj_create(g_ui.dataScreen);
        lv_obj_remove_style_all(g_ui.dataCarousel);
        lv_obj_set_size(g_ui.dataCarousel, LV_PCT(100), LV_PCT(100));
        lv_obj_align(g_ui.dataCarousel, LV_ALIGN_CENTER, 0, 10);
        lv_obj_set_layout(g_ui.dataCarousel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.dataCarousel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.dataCarousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.dataCarousel, 14, 0);
        lv_obj_set_style_pad_left(g_ui.dataCarousel, 12, 0);
        lv_obj_set_style_pad_right(g_ui.dataCarousel, 12, 0);
        lv_obj_set_scroll_dir(g_ui.dataCarousel, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(g_ui.dataCarousel, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(g_ui.dataCarousel, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(g_ui.dataCarousel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_add_event_cb(g_ui.dataCarousel, on_data_scroll, LV_EVENT_SCROLL, nullptr);

        g_ui.dataPages.fill(DataPageWidgets{});
        for (size_t i = 0; i < DATA_PAGE_COUNT; ++i)
        {
            g_ui.dataPages[i] = make_data_page(i);
        }

        g_ui.dataIndicator = lv_obj_create(g_ui.dataScreen);
        lv_obj_remove_style_all(g_ui.dataIndicator);
        lv_obj_set_size(g_ui.dataIndicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g_ui.dataIndicator, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.dataIndicator, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.dataIndicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.dataIndicator, 10, 0);
        lv_obj_align(g_ui.dataIndicator, LV_ALIGN_BOTTOM_MID, 0, -20);

        for (size_t i = 0; i < DATA_PAGE_COUNT; ++i)
        {
            lv_obj_t *dot = lv_obj_create(g_ui.dataIndicator);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 10, 10);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x636366), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }

        g_state.dataPageIndex = 0;
        update_data_indicator();
        update_data_values();
        update_data_carousel_visuals();
        update_status_icons();

        lv_disp_load_scr(g_ui.dataScreen);
        g_state.inDetail = true;
        g_state.inDataView = true;
    }

    // =======================================================================
    // BRIGHTNESS SCREEN
    // =======================================================================
    void update_preview_bar(int value)
    {
        if (!g_ui.brightnessPreview)
            return;
        lv_coord_t full = lv_obj_get_width(g_ui.brightnessPreview);
        lv_coord_t filled = (full * value) / 255;
        lv_obj_set_style_bg_color(g_ui.brightnessPreview, color_card_accent, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(g_ui.brightnessPreview, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(g_ui.brightnessPreview, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_grad_color(g_ui.brightnessPreview, lv_color_hex(0x0FB6FF), 0);
        lv_obj_set_style_clip_corner(g_ui.brightnessPreview, true, 0);
        lv_obj_set_style_pad_right(g_ui.brightnessPreview, full - filled, 0);
    }

    // Helper: static callback for brightness buttons
    static void brightness_btn_cb(lv_event_t *evt)
    {
        if (lv_event_get_code(evt) != LV_EVENT_CLICKED)
            return;
        lv_obj_t *btn = lv_event_get_target(evt);
        uint8_t value = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn));
        rememberPreviewPixels();
        cfg.displayBrightness = value;
        if (g_ui.brightnessSlider)
        {
            lv_slider_set_value(g_ui.brightnessSlider, value, LV_ANIM_OFF);
        }
        if (g_ui.brightnessValue)
        {
            lv_label_set_text_fmt(g_ui.brightnessValue, "%d%%", (value * 100) / 255);
        }
        update_preview_bar(value);
        if (g_hooks.setBrightness)
            g_hooks.setBrightness(value);
        updateRpmBar(g_state.rpm);
    }

    void open_brightness()
    {
        g_state.detailScreen = CardScreen::Brightness;
        lv_obj_t *content = make_detail_base("Light");

        int pct = (cfg.displayBrightness * 100) / 255;

        lv_obj_t *pctLabel = lv_label_create(content);
        lv_label_set_text_fmt(pctLabel, "%d%%", pct);
        lv_obj_set_style_text_font(pctLabel, LV_FONT_MONTSERRAT_42, 0);
        lv_obj_set_style_text_color(pctLabel, color_text, 0);
        lv_obj_set_style_pad_bottom(pctLabel, 14, 0);
        g_ui.brightnessValue = pctLabel;

        lv_obj_t *previewWrap = lv_obj_create(content);
        lv_obj_remove_style_all(previewWrap);
        lv_obj_set_size(previewWrap, LV_PCT(90), 14);
        lv_obj_set_style_radius(previewWrap, 8, 0);
        lv_obj_set_style_bg_color(previewWrap, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_bg_opa(previewWrap, LV_OPA_60, 0);
        g_ui.brightnessPreview = previewWrap;
        update_preview_bar(cfg.displayBrightness);

        lv_obj_t *slider = lv_slider_create(content);
        lv_obj_set_width(slider, LV_PCT(90));
        lv_obj_set_height(slider, 12);
        lv_slider_set_range(slider, 10, 255);
        lv_slider_set_value(slider, cfg.displayBrightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x1C1C1E), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, color_card_accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, color_text, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
        g_ui.brightnessSlider = slider;

        lv_obj_add_event_cb(slider, [](lv_event_t *e)
                            {
            int val = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
            cfg.displayBrightness = val;
            int pct = (val * 100) / 255;
            if (g_ui.brightnessValue)
            {
                lv_label_set_text_fmt(g_ui.brightnessValue, "%d%%", pct);
            }
            update_preview_bar(val);
            if (g_hooks.setBrightness)
            {
                g_hooks.setBrightness(static_cast<uint8_t>(val));
            } }, LV_EVENT_VALUE_CHANGED, nullptr);

        lv_obj_add_event_cb(slider, [](lv_event_t *)
                            { saveConfig(); }, LV_EVENT_RELEASED, nullptr);

        lv_obj_t *btnRow = lv_obj_create(content);
        lv_obj_remove_style_all(btnRow);
        lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(btnRow, 8, 0);
        lv_obj_set_style_pad_column(btnRow, 10, 0);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);

        auto make_btn = [](const char *txt, uint8_t value)
        {
            lv_obj_t *btn = lv_btn_create(g_ui.brightnessSlider ? lv_obj_get_parent(g_ui.brightnessSlider) : nullptr);
            lv_obj_add_style(btn, &styleCardSide, 0);
            lv_obj_set_size(btn, 110, 42);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, txt);
            lv_obj_center(lbl);
            lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<uintptr_t>(value)));
            lv_obj_add_event_cb(btn, brightness_btn_cb, LV_EVENT_CLICKED, nullptr);
            return btn;
        };

        make_btn("Preview", 255);
        make_btn("Medium", 160);
        make_btn("Low", 60);

        lv_obj_t *resetBtn = lv_btn_create(content);
        lv_obj_add_style(resetBtn, &styleCardSide, 0);
        lv_obj_set_width(resetBtn, LV_PCT(70));
        lv_obj_t *resetLbl = lv_label_create(resetBtn);
        lv_label_set_text(resetLbl, "Reset to Config");
        lv_obj_center(resetLbl);
        lv_obj_add_event_cb(resetBtn, [](lv_event_t *evt)
                            {
            if (lv_event_get_code(evt) != LV_EVENT_CLICKED)
                return;
            if (g_ui.brightnessSlider)
            {
                lv_slider_set_value(g_ui.brightnessSlider, cfg.displayBrightness, LV_ANIM_OFF);
            }
            update_preview_bar(cfg.displayBrightness);
            if (g_hooks.setBrightness)
            {
                g_hooks.setBrightness(static_cast<uint8_t>(cfg.displayBrightness));
            } }, LV_EVENT_CLICKED, nullptr);

        update_status_icons();
        lv_disp_load_scr(g_ui.detail);
    }

    // =======================================================================
    // WIFI SCREEN
    // =======================================================================
    void render_wifi_results(const WifiStatus &wifiStatus)
    {
        if (!g_ui.wifiList)
            return;

        String lines;
        if (wifiStatus.staConnected)
        {
            lines += "Connected: " + wifiStatus.currentSsid + "\nIP: " + wifiStatus.staIp + "\n\n";
        }
        else if (wifiStatus.apActive)
        {
            lines += "AP: ShiftLight (" + wifiStatus.apIp + ")\n\n";
        }
        else
        {
            lines += "No connection\n\n";
        }

        if (wifiStatus.scanRunning)
        {
            lines += "Scanning...";
        }
        else if (wifiStatus.scanResults.empty())
        {
            lines += "Tap Rescan";
        }
        else
        {
            lines += "Nearby:\n";
            size_t count = std::min<size_t>(wifiStatus.scanResults.size(), 6);
            for (size_t i = 0; i < count; ++i)
            {
                lines += wifiStatus.scanResults[i].ssid;
                lines += " (";
                lines += wifiStatus.scanResults[i].rssi;
                lines += ")\n";
            }
        }

        lv_label_set_text(g_ui.wifiList, lines.c_str());
    }

    bool trigger_wifi_scan(bool force = false)
    {
        uint32_t now = millis();
        if (!force && now - g_state.lastWifiScanMs < SCAN_COOLDOWN_MS)
            return false;
        if (startWifiScan())
        {
            g_state.lastWifiScanMs = now;
            if (g_ui.wifiSpinner)
                lv_obj_clear_flag(g_ui.wifiSpinner, LV_OBJ_FLAG_HIDDEN);
            return true;
        }
        return false;
    }

    void open_wifi()
    {
        g_state.detailScreen = CardScreen::Wifi;
        lv_obj_t *content = make_detail_base("WiFi");

        lv_obj_t *header = lv_label_create(content);
        lv_label_set_text(header, "Connection status");
        lv_obj_set_style_text_color(header, color_text_secondary, 0);
        lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
        g_ui.wifiHeader = header;

        g_ui.wifiList = lv_label_create(content);
        lv_obj_set_style_text_font(g_ui.wifiList, LV_FONT_MONTSERRAT_18, 0);
        lv_obj_set_style_text_color(g_ui.wifiList, color_text, 0);
        lv_label_set_text(g_ui.wifiList, "Scanning...");

        g_ui.wifiSpinner = lv_spinner_create(content, 1200, 40);
        lv_obj_set_size(g_ui.wifiSpinner, 32, 32);
        lv_obj_set_style_arc_color(g_ui.wifiSpinner, color_card_accent, LV_PART_INDICATOR);
        lv_obj_align(g_ui.wifiSpinner, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *rescan = lv_btn_create(content);
        lv_obj_add_style(rescan, &styleCardSide, 0);
        lv_obj_set_width(rescan, LV_PCT(60));
        lv_obj_t *lbl = lv_label_create(rescan);
        lv_label_set_text(lbl, "Rescan");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(rescan, [](lv_event_t *evt)
                            {
            if (lv_event_get_code(evt) != LV_EVENT_CLICKED)
                return;
            trigger_wifi_scan(true); }, LV_EVENT_CLICKED, nullptr);

        trigger_wifi_scan(true);
        update_status_icons();
        lv_disp_load_scr(g_ui.detail);
    }

    // =======================================================================
    // BLUETOOTH SCREEN
    // =======================================================================
    void render_ble_results()
    {
        if (!g_ui.bleList)
            return;

        String lines;
        if (g_state.bleConnected)
        {
            lines += "Connected to OBD\n";
        }
        else if (g_state.bleConnecting)
        {
            lines += "Connecting...\n";
        }

        const auto &res = getBleScanResults();
        if (res.empty())
        {
            lines += "Tap Rescan\n";
        }
        else
        {
            lines += "Nearby:\n";
            size_t count = std::min<size_t>(res.size(), 6);
            for (size_t i = 0; i < count; ++i)
            {
                lines += res[i].name;
                lines += "\n";
            }
        }

        lv_label_set_text(g_ui.bleList, lines.c_str());
    }

    bool trigger_ble_scan(bool force = false)
    {
        uint32_t now = millis();
        if (!force && now - g_state.lastBleScanMs < SCAN_COOLDOWN_MS)
            return false;
        if (g_state.bleConnected)
            return false;
        if (startBleScan())
        {
            g_state.lastBleScanMs = now;
            if (g_ui.bleSpinner)
                lv_obj_clear_flag(g_ui.bleSpinner, LV_OBJ_FLAG_HIDDEN);
            return true;
        }
        return false;
    }

    void open_bluetooth()
    {
        g_state.detailScreen = CardScreen::Bluetooth;
        lv_obj_t *content = make_detail_base("Bluetooth");

        g_ui.bleList = lv_label_create(content);
        lv_obj_set_style_text_font(g_ui.bleList, LV_FONT_MONTSERRAT_18, 0);
        lv_obj_set_style_text_color(g_ui.bleList, color_text, 0);
        lv_label_set_text(g_ui.bleList, "Scanning...");

        g_ui.bleSpinner = lv_spinner_create(content, 1000, 40);
        lv_obj_set_size(g_ui.bleSpinner, 32, 32);
        lv_obj_set_style_arc_color(g_ui.bleSpinner, color_card_accent, LV_PART_INDICATOR);
        lv_obj_align(g_ui.bleSpinner, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *rescan = lv_btn_create(content);
        lv_obj_add_style(rescan, &styleCardSide, 0);
        lv_obj_set_width(rescan, LV_PCT(60));
        lv_obj_t *lbl = lv_label_create(rescan);
        lv_label_set_text(lbl, "Rescan");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(rescan, [](lv_event_t *evt)
                            {
            if (lv_event_get_code(evt) != LV_EVENT_CLICKED)
                return;
            trigger_ble_scan(true); }, LV_EVENT_CLICKED, nullptr);

        trigger_ble_scan(true);
        update_status_icons();
        lv_disp_load_scr(g_ui.detail);
    }

    // =======================================================================
    // LED TEST SCREEN (optional card)
    // =======================================================================
    void apply_led_test_status(const char *text)
    {
        if (g_ui.ledTestInfo)
            lv_label_set_text(g_ui.ledTestInfo, text);
    }

    void open_led_test()
    {
        g_state.detailScreen = CardScreen::LedTest;
        lv_obj_t *content = make_detail_base("LED Test");

        g_ui.ledTestInfo = lv_label_create(content);
        lv_obj_set_style_text_color(g_ui.ledTestInfo, color_text_secondary, 0);
        lv_obj_set_style_text_font(g_ui.ledTestInfo, &lv_font_montserrat_16, 0);
        lv_label_set_text(g_ui.ledTestInfo, "Run quick LED patterns");

        lv_obj_t *btnRow = lv_obj_create(content);
        lv_obj_remove_style_all(btnRow);
        lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(btnRow, 8, 0);
        lv_obj_set_style_pad_column(btnRow, 10, 0);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);

        auto make_btn = [&](const char *label, auto cb)
        {
            lv_obj_t *btn = lv_btn_create(btnRow);
            lv_obj_add_style(btn, &styleCardSide, 0);
            lv_obj_set_size(btn, 120, 44);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, label);
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        };

        make_btn("Sweep Test", [](lv_event_t *evt)
                 {
            LV_UNUSED(evt);
            rememberPreviewPixels();
            apply_led_test_status("Sweep preview");
            // TODO: implement dedicated sweep animation hook
            updateRpmBar(g_state.rpm); });

        make_btn("All Green", [](lv_event_t *evt)
                 {
            LV_UNUSED(evt);
            rememberPreviewPixels();
            for (int i = 0; i < strip.numPixels(); ++i)
            {
                strip.setPixelColor(i, strip.Color(0, 255, 0));
            }
            strip.show();
            apply_led_test_status("Set all LEDs green"); });

        make_btn("All Red", [](lv_event_t *evt)
                 {
            LV_UNUSED(evt);
            rememberPreviewPixels();
            for (int i = 0; i < strip.numPixels(); ++i)
            {
                strip.setPixelColor(i, strip.Color(255, 0, 0));
            }
            strip.show();
            apply_led_test_status("Set all LEDs red"); });

        make_btn("Restore", [](lv_event_t *evt)
                 {
            LV_UNUSED(evt);
            strip.clear();
            strip.show();
            updateRpmBar(g_state.rpm);
            setStatusLED(false);
            apply_led_test_status("Restored defaults"); });

        update_status_icons();
        lv_disp_load_scr(g_ui.detail);
    }

    // =======================================================================
    // INFO SCREEN (Settings)
    // =======================================================================
    void open_settings()
    {
        g_state.detailScreen = CardScreen::Settings;
        lv_obj_t *content = make_detail_base("Info");

        lv_obj_t *app = lv_label_create(content);
        lv_label_set_text(app, "RpmCounter\nShiftLight System\n\nVersion: 1.0\nESP32-S3 AMOLED");
        lv_obj_set_style_text_font(app, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(app, color_text, 0);

        update_status_icons();
        lv_disp_load_scr(g_ui.detail);
    }

    // =======================================================================
    // CARD INTERACTION
    // =======================================================================
    void open_detail(const CardDef &def)
    {
        mark_interacted();
        switch (def.screen)
        {
        case CardScreen::Data:
            open_data_view();
            break;
        case CardScreen::Brightness:
            open_brightness();
            break;
        case CardScreen::Wifi:
            open_wifi();
            break;
        case CardScreen::Bluetooth:
            open_bluetooth();
            break;
        case CardScreen::LedTest:
            open_led_test();
            break;
        case CardScreen::Settings:
        default:
            open_settings();
            break;
        }
    }

    void on_card_click(lv_event_t *e)
    {
        if (debounce_touch())
            return;

        lv_obj_t *target = lv_event_get_target(e);
        size_t idx = 0;
        for (; idx < CARD_COUNT; ++idx)
        {
            if (g_ui.cards[idx].container == target ||
                g_ui.cards[idx].icon == target ||
                g_ui.cards[idx].label == target)
                break;
        }

        if (idx >= CARD_COUNT)
            return;

        open_detail(CARDS[idx]);
    }

    void on_carousel_scroll(lv_event_t *e)
    {
        LV_UNUSED(e);
        mark_interacted();
        lv_coord_t scrollX = lv_obj_get_scroll_x(g_ui.carousel);
        lv_coord_t pageW = lv_obj_get_width(g_ui.carousel);
        int newIdx = (scrollX + pageW / 2) / pageW;
        newIdx = std::max(0, std::min(newIdx, static_cast<int>(CARD_COUNT) - 1));
        if (g_state.cardIndex != newIdx)
        {
            g_state.cardIndex = newIdx;
            update_page_indicator();
        }
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

    // =======================================================================
    // HOME SCREEN CREATION
    // =======================================================================
    lv_obj_t *create_status_bar(lv_obj_t *parent)
    {
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, LV_PCT(100), 36);
        lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_hor(bar, 12, 0);

        lv_obj_t *logo = lv_label_create(bar);
        lv_label_set_text(logo, "RpmCounter");
        lv_obj_set_style_text_color(logo, color_text, 0);
        lv_obj_set_style_text_font(logo, LV_FONT_MONTSERRAT_18, 0);

        lv_obj_t *iconRow = lv_obj_create(bar);
        lv_obj_remove_style_all(iconRow);
        lv_obj_set_layout(iconRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(iconRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(iconRow, 8, 0);
        lv_obj_set_style_bg_opa(iconRow, LV_OPA_TRANSP, 0);

        g_ui.wifiIcon = lv_label_create(iconRow);
        lv_label_set_text(g_ui.wifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(g_ui.wifiIcon, LV_FONT_MONTSERRAT_18, 0);

        g_ui.bleIcon = lv_label_create(iconRow);
        lv_label_set_text(g_ui.bleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.bleIcon, LV_FONT_MONTSERRAT_18, 0);

        g_ui.ledStatus = lv_obj_create(iconRow);
        lv_obj_set_size(g_ui.ledStatus, 20, 6);
        lv_obj_set_style_radius(g_ui.ledStatus, 3, 0);
        lv_obj_set_style_bg_color(g_ui.ledStatus, color_muted, 0);
        lv_obj_set_style_bg_opa(g_ui.ledStatus, LV_OPA_60, 0);

        return bar;
    }

    CardWidgets make_card(size_t idx)
    {
        const CardDef &def = CARDS[idx];
        CardWidgets w{};

        w.container = lv_obj_create(g_ui.carousel);
        lv_obj_remove_style_all(w.container);
        lv_obj_add_style(w.container, &styleCardSide, 0);
        lv_obj_set_size(w.container, 170, 170);
        lv_obj_set_style_pad_row(w.container, 8, 0);
        lv_obj_set_style_pad_column(w.container, 6, 0);
        lv_obj_set_style_transform_pivot_x(w.container, 85, 0);
        lv_obj_set_style_transform_pivot_y(w.container, 85, 0);
        lv_obj_set_layout(w.container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_add_event_cb(w.container, on_card_click, LV_EVENT_CLICKED, nullptr);

        w.icon = lv_label_create(w.container);
        lv_label_set_text(w.icon, def.symbol);
        lv_obj_set_style_text_color(w.icon, color_text, 0);
        lv_obj_set_style_text_font(w.icon, LV_FONT_MONTSERRAT_36, 0);

        w.label = lv_label_create(w.container);
        lv_label_set_text(w.label, def.title);
        lv_obj_set_style_text_color(w.label, color_text, 0);
        lv_obj_set_style_text_font(w.label, &lv_font_montserrat_20, 0);

        return w;
    }

    void create_page_indicator()
    {
        g_ui.pageIndicator = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.pageIndicator);
        lv_obj_set_size(g_ui.pageIndicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g_ui.pageIndicator, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.pageIndicator, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.pageIndicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.pageIndicator, 8, 0);
        lv_obj_align(g_ui.pageIndicator, LV_ALIGN_BOTTOM_MID, 0, -10);

        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            lv_obj_t *dot = lv_obj_create(g_ui.pageIndicator);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 12, 6);
            lv_obj_add_style(dot, &styleIndicator, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    void create_tutorial()
    {
        g_ui.tutorial = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.tutorial);
        lv_obj_add_style(g_ui.tutorial, &styleTutorial, 0);
        lv_obj_set_width(g_ui.tutorial, LV_PCT(85));
        lv_obj_align(g_ui.tutorial, LV_ALIGN_BOTTOM_MID, 0, -40);

        lv_obj_t *lbl = lv_label_create(g_ui.tutorial);
        lv_label_set_text(lbl, "Swipe left/right to browse cards.\nTap to open. Swipe up to go back.");
        lv_obj_set_style_text_color(lbl, color_text, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    }

    void build_home()
    {
        cleanup_detail();

        if (!g_ui.root)
        {
            g_ui.root = lv_obj_create(nullptr);
            lv_obj_remove_style_all(g_ui.root);
            lv_obj_add_style(g_ui.root, &styleBg, 0);
            lv_obj_set_size(g_ui.root, LV_PCT(100), LV_PCT(100));
            lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(g_ui.root, on_gesture, LV_EVENT_GESTURE, nullptr);

            g_ui.statusBar = create_status_bar(g_ui.root);
            lv_obj_align(g_ui.statusBar, LV_ALIGN_TOP_MID, 0, 6);

            g_ui.carousel = lv_obj_create(g_ui.root);
            lv_obj_remove_style_all(g_ui.carousel);
            lv_obj_set_size(g_ui.carousel, LV_PCT(100), LV_PCT(75));
            lv_obj_align(g_ui.carousel, LV_ALIGN_CENTER, 0, 10);
            lv_obj_set_layout(g_ui.carousel, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(g_ui.carousel, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(g_ui.carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(g_ui.carousel, 18, 0);
            lv_obj_set_style_pad_left(g_ui.carousel, 16, 0);
            lv_obj_set_style_pad_right(g_ui.carousel, 16, 0);
            lv_obj_set_scroll_dir(g_ui.carousel, LV_DIR_HOR);
            lv_obj_set_scroll_snap_x(g_ui.carousel, LV_SCROLL_SNAP_CENTER);
            lv_obj_set_scrollbar_mode(g_ui.carousel, LV_SCROLLBAR_MODE_OFF);
            lv_obj_add_flag(g_ui.carousel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
            lv_obj_add_event_cb(g_ui.carousel, on_carousel_scroll, LV_EVENT_SCROLL, nullptr);

            g_ui.cards.fill(CardWidgets{});
            for (size_t i = 0; i < CARD_COUNT; ++i)
            {
                g_ui.cards[i] = make_card(i);
            }

            create_page_indicator();
            create_tutorial();
        }

        lv_disp_load_scr(g_ui.root);
        update_carousel_visuals();
        update_status_icons();
        update_page_indicator();
    }

    void show_home()
    {
        build_home();
        g_state.inDetail = false;
        g_state.inDataView = false;
        g_state.detailScreen = current_card().screen;
        if (g_ui.logoOverlay)
        {
            lv_obj_add_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ===========================================================================
// PUBLIC API
// ===========================================================================

void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks)
{
    g_ui.disp = disp;
    g_hooks = hooks;

    apply_styles();

    // Rotate display to horizontal layout if supported
    lv_disp_set_rotation(disp, LV_DISP_ROT_90);

    build_home();
    update_status_icons();
    show_home();
}

void ui_s3_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleConnecting)
{
    g_state.lastWifi = wifiStatus;
    g_state.bleConnected = bleConnected;
    g_state.bleConnecting = bleConnecting;

    update_status_icons();

    if (g_state.inDataView)
    {
        update_data_carousel_visuals();
    }

    if (g_state.inDetail && g_state.detailScreen == CardScreen::Wifi)
    {
        if (g_ui.wifiSpinner)
        {
            if (wifiStatus.scanRunning)
                lv_obj_clear_flag(g_ui.wifiSpinner, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(g_ui.wifiSpinner, LV_OBJ_FLAG_HIDDEN);
        }
        render_wifi_results(wifiStatus);
    }

    if (g_state.inDetail && g_state.detailScreen == CardScreen::Bluetooth)
    {
        if (g_ui.bleSpinner)
        {
            if (g_state.bleConnecting)
                lv_obj_clear_flag(g_ui.bleSpinner, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(g_ui.bleSpinner, LV_OBJ_FLAG_HIDDEN);
        }
        render_ble_results();
    }
}

void ui_s3_set_gear(int gear)
{
    g_state.gear = gear;
    if (g_state.inDataView)
    {
        update_data_values();
    }
}

void ui_s3_set_shiftlight(bool active)
{
    g_state.shift = active;
    update_status_icons();
}

void ui_s3_show_logo()
{
    if (!g_ui.root)
        return;
    if (!g_ui.logoOverlay)
    {
        g_ui.logoOverlay = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.logoOverlay);
        lv_obj_set_size(g_ui.logoOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_add_style(g_ui.logoOverlay, &styleBg, 0);
        lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(g_ui.logoOverlay);
        lv_label_set_text(lbl, LV_SYMBOL_GPS);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl, color_card_accent, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -20);

        lv_obj_t *txt = lv_label_create(g_ui.logoOverlay);
        lv_label_set_text(txt, "RpmCounter");
        lv_obj_set_style_text_font(txt, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(txt, color_text, 0);
        lv_obj_align(txt, LV_ALIGN_CENTER, 0, 40);
    }
    lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_ui.logoOverlay);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&a, 2000);
    lv_anim_set_time(&a, 500);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v)
                        { lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0); });
    lv_anim_set_ready_cb(&a, [](lv_anim_t *anim)
                         {
        lv_obj_add_flag(static_cast<lv_obj_t *>(anim->var), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(anim->var), LV_OPA_COVER, 0); });
    lv_anim_start(&a);
}

void ui_s3_set_rpm(int rpm)
{
    g_state.rpm = rpm;
    if (g_state.inDataView)
    {
        update_data_values();
    }
}

void ui_s3_set_speed(int speed)
{
    g_state.speed = speed;
    if (g_state.inDataView)
    {
        update_data_values();
    }
}

void ui_s3_set_coolant(int temp)
{
    g_state.coolant = temp;
    if (g_state.inDataView)
    {
        update_data_values();
    }
}
