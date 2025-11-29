#include "ui_s3_main.h"

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "bluetooth/ble_obd.h"
#include "core/config.h"
#include "core/state.h"
#include "core/utils.h"
#include "core/wifi.h"

namespace
{
    // =======================================================================
    // CARD DEFINITIONS - Main menu icons
    // =======================================================================
    enum class CardScreen
    {
        Data,       // Vehicle data (RPM, Speed, Gear, Temp)
        Brightness,
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
        {CardScreen::Data, "Data", LV_SYMBOL_GPS},
        {CardScreen::Brightness, "Light", LV_SYMBOL_EYE_OPEN},
        {CardScreen::Wifi, "WiFi", LV_SYMBOL_WIFI},
        {CardScreen::Bluetooth, "BT", LV_SYMBOL_BLUETOOTH},
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
    };

    constexpr DataPageDef DATA_PAGES[] = {
        {DataPage::RPM, "RPM", ""},
        {DataPage::Speed, "Speed", "km/h"},
        {DataPage::Gear, "Gear", ""},
        {DataPage::Coolant, "Coolant", "\xc2\xb0" "C"},  // °C
    };

    constexpr size_t DATA_PAGE_COUNT = sizeof(DATA_PAGES) / sizeof(DATA_PAGES[0]);

    // =======================================================================
    // UI WIDGETS
    // =======================================================================
    struct CardWidgets
    {
        lv_obj_t *container = nullptr;
        lv_obj_t *circle = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *label = nullptr;
    };

    struct DataPageWidgets
    {
        lv_obj_t *container = nullptr;
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
        lv_obj_t *carousel = nullptr;
        lv_obj_t *pageIndicator = nullptr;
        lv_obj_t *tutorial = nullptr;
        lv_obj_t *logoOverlay = nullptr;
        lv_obj_t *detail = nullptr;
        lv_obj_t *detailContent = nullptr;
        lv_obj_t *wifiList = nullptr;
        lv_obj_t *bleList = nullptr;
        lv_obj_t *brightnessSlider = nullptr;
        lv_obj_t *brightnessValue = nullptr;
        lv_obj_t *dataScreen = nullptr;
        lv_obj_t *dataCarousel = nullptr;
        lv_obj_t *dataIndicator = nullptr;
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
    };

    UiRefs g_ui;
    UiState g_state;
    UiDisplayHooks g_hooks;

    // =======================================================================
    // STYLES - Clean Apple-like dark theme
    // =======================================================================
    lv_style_t styleBg;
    lv_style_t styleCard;
    lv_style_t styleMuted;
    lv_style_t styleCircle;
    lv_style_t styleDot;
    lv_style_t styleTutorial;
    lv_style_t styleDataValue;
    lv_style_t styleDataUnit;
    lv_style_t styleDataName;

    // Colors - Pure black AMOLED background with subtle accents
    const lv_color_t color_bg = lv_color_hex(0x000000);
    const lv_color_t color_card = lv_color_hex(0x0C0C0E);
    const lv_color_t color_card_accent = lv_color_hex(0x0A84FF); // iOS blue
    const lv_color_t color_muted = lv_color_hex(0x8E8E93);       // iOS gray
    const lv_color_t color_ok = lv_color_hex(0x30D158);          // iOS green
    const lv_color_t color_warn = lv_color_hex(0xFF9F0A);        // iOS orange
    const lv_color_t color_error = lv_color_hex(0xFF453A);       // iOS red
    const lv_color_t color_dot = lv_color_hex(0x1C1C1E);
    const lv_color_t color_dot_active = lv_color_hex(0x0A84FF);
    const lv_color_t color_text = lv_color_hex(0xFFFFFF);
    const lv_color_t color_text_secondary = lv_color_hex(0x636366);

    // =======================================================================
    // HELPER FUNCTIONS
    // =======================================================================
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
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, g_ui.tutorial);
            lv_anim_set_values(&a, LV_OPA_80, LV_OPA_TRANSP);
            lv_anim_set_time(&a, 300);
            lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
                lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
            });
            lv_anim_set_ready_cb(&a, [](lv_anim_t *anim) {
                lv_obj_add_flag(static_cast<lv_obj_t *>(anim->var), LV_OBJ_FLAG_HIDDEN);
            });
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
        lv_style_set_bg_opa(&styleCard, LV_OPA_90);
        lv_style_set_radius(&styleCard, 20);
        lv_style_set_pad_all(&styleCard, 8);
        lv_style_set_border_width(&styleCard, 0);

        lv_style_init(&styleCircle);
        lv_style_set_radius(&styleCircle, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleCircle, lv_color_hex(0x1C1C1E));
        lv_style_set_bg_opa(&styleCircle, LV_OPA_COVER);
        lv_style_set_border_width(&styleCircle, 0);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, color_muted);

        lv_style_init(&styleDot);
        lv_style_set_radius(&styleDot, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleDot, color_dot);
        lv_style_set_bg_opa(&styleDot, LV_OPA_COVER);
        lv_style_set_border_width(&styleDot, 0);

        lv_style_init(&styleTutorial);
        lv_style_set_radius(&styleTutorial, 14);
        lv_style_set_bg_color(&styleTutorial, lv_color_hex(0x1C1C1E));
        lv_style_set_bg_opa(&styleTutorial, LV_OPA_90);
        lv_style_set_pad_all(&styleTutorial, 12);
        lv_style_set_border_width(&styleTutorial, 0);

        // Large data value style
        lv_style_init(&styleDataValue);
        lv_style_set_text_color(&styleDataValue, color_text);

        // Data unit style
        lv_style_init(&styleDataUnit);
        lv_style_set_text_color(&styleDataUnit, color_text_secondary);

        // Data name style
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
            lv_obj_set_style_bg_color(dot, active ? color_dot_active : color_dot, 0);
            lv_obj_set_style_opa(dot, active ? LV_OPA_COVER : LV_OPA_40, 0);
            
            // Smooth width animation
            lv_coord_t targetW = active ? 18 : 6;
            lv_coord_t currentW = lv_obj_get_width(dot);
            if (currentW != targetW)
            {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, dot);
                lv_anim_set_values(&a, currentW, targetW);
                lv_anim_set_time(&a, 200);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
                    lv_obj_set_width(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v));
                });
                lv_anim_start(&a);
            }
        }
    }

    // =======================================================================
    // DATA PAGE INDICATOR UPDATE
    // =======================================================================
    void update_data_indicator()
    {
        if (!g_ui.dataIndicator)
            return;

        uint32_t childCount = lv_obj_get_child_cnt(g_ui.dataIndicator);
        for (uint32_t i = 0; i < childCount; ++i)
        {
            lv_obj_t *dot = lv_obj_get_child(g_ui.dataIndicator, i);
            bool active = static_cast<int>(i) == g_state.dataPageIndex;
            lv_obj_set_style_bg_color(dot, active ? lv_color_hex(0x0A84FF) : lv_color_hex(0x636366), 0);
            lv_obj_set_width(dot, active ? 20 : 8);
        }
    }

    // =======================================================================
    // CAROUSEL VISUAL UPDATE - Zoom/opacity animation
    // =======================================================================
    void update_carousel_visuals()
    {
        if (!g_ui.carousel)
            return;

        // Make sure all cards are visible with basic styling
        // The zoom/opacity effects are subtle to avoid visibility issues
        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            CardWidgets &cw = g_ui.cards[i];
            if (!cw.container)
                continue;

            // Ensure card is always visible
            lv_obj_set_style_opa(cw.container, LV_OPA_COVER, 0);
            lv_obj_set_style_transform_zoom(cw.container, 256, 0);
            lv_obj_clear_flag(cw.container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // =======================================================================
    // DATA CAROUSEL VISUAL UPDATE
    // =======================================================================
    void update_data_carousel_visuals()
    {
        if (!g_ui.dataCarousel)
            return;

        // Ensure all data pages are visible
        for (size_t i = 0; i < DATA_PAGE_COUNT; ++i)
        {
            DataPageWidgets &pw = g_ui.dataPages[i];
            if (!pw.container)
                continue;

            // Keep pages fully visible
            lv_obj_set_style_opa(pw.container, LV_OPA_COVER, 0);
            lv_obj_set_style_transform_zoom(pw.container, 256, 0);
            lv_obj_clear_flag(pw.container, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update page indicator based on scroll position
        if (g_ui.dataCarousel && g_ui.dataIndicator)
        {
            lv_coord_t scrollX = lv_obj_get_scroll_x(g_ui.dataCarousel);
            lv_coord_t pageWidth = lv_disp_get_hor_res(g_ui.disp);
            int newIdx = (scrollX + pageWidth / 2) / pageWidth;
            newIdx = std::max(0, std::min(newIdx, static_cast<int>(DATA_PAGE_COUNT) - 1));
            
            if (g_state.dataPageIndex != newIdx)
            {
                g_state.dataPageIndex = newIdx;
                update_data_indicator();
            }
        }
    }

    // =======================================================================
    // STATUS ICONS UPDATE
    // =======================================================================
    void update_status_icons()
    {
        if (g_ui.wifiIcon)
        {
            bool staConnected = g_state.lastWifi.staConnected;
            bool apActive = g_state.lastWifi.apActive;
            bool staConnecting = g_state.lastWifi.staConnecting;
            int apClients = g_state.lastWifi.apClients;

            lv_color_t col;
            lv_opa_t opa = LV_OPA_COVER;
            
            // Cache millis for animation calculations
            const uint32_t now = millis();

            if (staConnected)
            {
                col = color_ok;
            }
            else if (apActive)
            {
                col = color_ok;
                // Subtle pulse when waiting for clients
                if (apClients == 0)
                {
                    float phase = static_cast<float>(now % 2512) / 400.0f; // 2512 ~ 2*PI*400
                    float pulse = (sinf(phase) + 1.0f) * 0.5f;
                    opa = static_cast<lv_opa_t>(180 + static_cast<int>(75 * pulse));
                }
            }
            else if (staConnecting)
            {
                col = color_warn;
                float phase = static_cast<float>(now % 1256) / 200.0f; // 1256 ~ 2*PI*200
                float pulse = (sinf(phase) + 1.0f) * 0.5f;
                opa = static_cast<lv_opa_t>(100 + static_cast<int>(155 * pulse));
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
            lv_color_t col;
            lv_opa_t opa = LV_OPA_COVER;
            
            // Cache millis for animation calculations
            const uint32_t now = millis();
            
            if (g_state.bleConnected)
            {
                col = color_card_accent; // iOS blue
            }
            else if (g_state.bleConnecting)
            {
                col = color_warn;
                float phase = static_cast<float>(now % 1570) / 250.0f; // 1570 ~ 2*PI*250
                float pulse = (sinf(phase) + 1.0f) * 0.5f;
                opa = static_cast<lv_opa_t>(100 + static_cast<int>(155 * pulse));
            }
            else
            {
                col = color_text_secondary;
                opa = LV_OPA_60;
            }
            
            lv_obj_set_style_text_color(g_ui.bleIcon, col, 0);
            lv_obj_set_style_opa(g_ui.bleIcon, opa, 0);
        }
    }

    // =======================================================================
    // DATA PAGE VALUE UPDATES
    // =======================================================================
    void update_data_values()
    {
        // Update RPM page
        if (g_ui.dataPages[0].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[0].valueLabel, "%d", g_state.rpm);
        }
        
        // Update Speed page
        if (g_ui.dataPages[1].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[1].valueLabel, "%d", g_state.speed);
        }
        
        // Update Gear page
        if (g_ui.dataPages[2].valueLabel)
        {
            lv_label_set_text(g_ui.dataPages[2].valueLabel, 
                              g_state.gear <= 0 ? "N" : String(g_state.gear).c_str());
        }
        
        // Update Coolant page
        if (g_ui.dataPages[3].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[3].valueLabel, "%d", g_state.coolant);
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
            // Changed to swipe UP (from bottom to top) to go back
            if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
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
            if (g_ui.cards[idx].container == target || 
                g_ui.cards[idx].circle == target ||
                g_ui.cards[idx].icon == target ||
                g_ui.cards[idx].label == target)
                break;
        }

        if (idx >= CARD_COUNT)
            return;

        mark_interacted();
        
        // Always open the clicked card's menu directly
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
        lv_obj_set_size(header, LV_PCT(100), 40);
        lv_obj_set_layout(header, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Create a larger clickable back button area
        lv_obj_t *backBtn = lv_obj_create(header);
        lv_obj_remove_style_all(backBtn);
        lv_obj_set_size(backBtn, 80, 40);
        lv_obj_set_style_bg_color(backBtn, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(backBtn, 10, 0);
        lv_obj_add_flag(backBtn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(backBtn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(backBtn, on_back, LV_EVENT_CLICKED, nullptr);
        
        lv_obj_t *backLbl = lv_label_create(backBtn);
        lv_label_set_text(backLbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(backLbl, lv_color_hex(0x0A84FF), 0);
        lv_obj_center(backLbl);

        lv_obj_t *titleLbl = lv_label_create(header);
        lv_label_set_text(titleLbl, title);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(titleLbl, color_text, 0);

        lv_obj_t *body = lv_obj_create(scr);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_top(body, 16, 0);
        lv_obj_set_layout(body, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(body, 10, 0);

        return body;
    }

    CardWidgets make_icon_card(size_t idx)
    {
        const CardDef &def = CARDS[idx];
        CardWidgets w{};

        w.container = lv_obj_create(g_ui.carousel);
        lv_obj_remove_style_all(w.container);
        lv_obj_set_size(w.container, 120, 150);
        lv_obj_set_style_bg_color(w.container, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_bg_opa(w.container, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(w.container, 20, 0);
        lv_obj_set_style_border_width(w.container, 2, 0);
        lv_obj_set_style_border_color(w.container, lv_color_hex(0x3A3A3C), 0);
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_set_layout(w.container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(w.container, 8, 0);
        lv_obj_set_style_pad_top(w.container, 15, 0);
        lv_obj_add_event_cb(w.container, on_card_click, LV_EVENT_CLICKED, nullptr);

        w.circle = lv_obj_create(w.container);
        lv_obj_remove_style_all(w.circle);
        lv_obj_set_size(w.circle, 70, 70);
        lv_obj_set_style_bg_color(w.circle, lv_color_hex(0x0A84FF), 0);
        lv_obj_set_style_bg_opa(w.circle, LV_OPA_30, 0);
        lv_obj_set_style_radius(w.circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(w.circle, LV_OBJ_FLAG_SCROLLABLE);

        w.icon = lv_label_create(w.circle);
        lv_label_set_text(w.icon, def.symbol);
        lv_obj_set_style_text_font(w.icon, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(w.icon, lv_color_hex(0x0A84FF), 0);
        lv_obj_center(w.icon);

        w.label = lv_label_create(w.container);
        lv_label_set_text(w.label, def.title);
        lv_obj_set_style_text_color(w.label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(w.label, &lv_font_montserrat_16, 0);

        return w;
    }

    void build_tutorial()
    {
        g_ui.tutorial = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.tutorial);
        lv_obj_add_style(g_ui.tutorial, &styleTutorial, 0);
        lv_obj_set_width(g_ui.tutorial, LV_PCT(80));
        lv_obj_align(g_ui.tutorial, LV_ALIGN_BOTTOM_MID, 0, -50);
        lv_obj_clear_flag(g_ui.tutorial, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(g_ui.tutorial);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Swipe " LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(lbl, color_muted, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
    }

    void build_page_indicator()
    {
        g_ui.pageIndicator = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.pageIndicator);
        lv_obj_set_size(g_ui.pageIndicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g_ui.pageIndicator, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.pageIndicator, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.pageIndicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.pageIndicator, 8, 0);
        lv_obj_align(g_ui.pageIndicator, LV_ALIGN_BOTTOM_MID, 0, -20);

        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            lv_obj_t *dot = lv_obj_create(g_ui.pageIndicator);
            lv_obj_remove_style_all(dot);
            lv_obj_add_style(dot, &styleDot, 0);
            lv_obj_set_size(dot, 6, 6);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    // =======================================================================
    // BUILD HOME SCREEN - Minimalist carousel UI
    // =======================================================================
    void build_home(lv_disp_t *disp)
    {
        g_ui.disp = disp;
        g_ui.root = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.root);
        lv_obj_add_style(g_ui.root, &styleBg, 0);
        lv_obj_set_size(g_ui.root, lv_disp_get_hor_res(disp), lv_disp_get_ver_res(disp));
        lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(g_ui.root, on_gesture, LV_EVENT_GESTURE, nullptr);

        // Minimalist status bar - only icons, no brand text
        g_ui.statusBar = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.statusBar);
        lv_obj_set_size(g_ui.statusBar, LV_PCT(100), 28);
        lv_obj_set_layout(g_ui.statusBar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.statusBar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.statusBar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(g_ui.statusBar, 16, 0);
        lv_obj_set_style_pad_right(g_ui.statusBar, 16, 0);
        lv_obj_set_style_pad_top(g_ui.statusBar, 8, 0);
        lv_obj_set_style_pad_column(g_ui.statusBar, 12, 0);

        g_ui.wifiIcon = lv_label_create(g_ui.statusBar);
        lv_label_set_text(g_ui.wifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(g_ui.wifiIcon, &lv_font_montserrat_16, 0);

        g_ui.bleIcon = lv_label_create(g_ui.statusBar);
        lv_label_set_text(g_ui.bleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.bleIcon, &lv_font_montserrat_16, 0);

        // Main carousel - takes most of the screen
        g_ui.carousel = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.carousel);
        lv_obj_set_size(g_ui.carousel, LV_PCT(100), 200);
        lv_obj_align(g_ui.carousel, LV_ALIGN_CENTER, 0, 20);
        lv_obj_set_style_pad_left(g_ui.carousel, 76, 0);  // Center first card (280-128)/2 = 76
        lv_obj_set_style_pad_right(g_ui.carousel, 76, 0);
        lv_obj_set_style_pad_ver(g_ui.carousel, 10, 0);
        lv_obj_set_layout(g_ui.carousel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.carousel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.carousel, 20, 0);
        lv_obj_set_scroll_dir(g_ui.carousel, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(g_ui.carousel, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(g_ui.carousel, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(g_ui.carousel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_add_event_cb(g_ui.carousel, on_carousel_scroll, LV_EVENT_SCROLL, nullptr);

        // Create cards
        g_ui.cards.fill(CardWidgets{});
        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            g_ui.cards[i] = make_icon_card(i);
        }

        build_page_indicator();
        build_tutorial();

        update_carousel_visuals();
        update_status_icons();
        update_page_indicator();
    }

    // =======================================================================
    // SHOW HOME
    // =======================================================================
    void show_home()
    {
        if (!g_ui.root)
            return;
        g_state.inDetail = false;
        g_state.inDataView = false;
        lv_disp_load_scr(g_ui.root);
        update_carousel_visuals();
        update_status_icons();
        update_page_indicator();
    }

    // =======================================================================
    // DATA VIEW - Large number display with swipe navigation
    // =======================================================================
    void on_data_gesture(lv_event_t *e)
    {
        if (lv_event_get_code(e) == LV_EVENT_GESTURE)
        {
            // Swipe UP to go back (bottom to top)
            if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
            {
                show_home();
            }
        }
    }

    void on_data_scroll(lv_event_t *e)
    {
        LV_UNUSED(e);
        update_data_carousel_visuals();
    }

    DataPageWidgets make_data_page(size_t idx)
    {
        const DataPageDef &def = DATA_PAGES[idx];
        DataPageWidgets w{};

        // Full-screen container for each data page
        w.container = lv_obj_create(g_ui.dataCarousel);
        lv_obj_remove_style_all(w.container);
        lv_obj_set_size(w.container, lv_disp_get_hor_res(g_ui.disp) - 20, LV_PCT(100));
        lv_obj_set_style_bg_color(w.container, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_bg_opa(w.container, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(w.container, 16, 0);
        lv_obj_set_layout(w.container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_SNAPPABLE);

        // Name label at top
        w.nameLabel = lv_label_create(w.container);
        lv_label_set_text(w.nameLabel, def.label);
        lv_obj_set_style_text_color(w.nameLabel, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(w.nameLabel, &lv_font_montserrat_24, 0);

        // Large value in center
        w.valueLabel = lv_label_create(w.container);
        lv_label_set_text(w.valueLabel, "---");
        lv_obj_set_style_text_color(w.valueLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(w.valueLabel, &lv_font_montserrat_48, 0);
        lv_obj_set_style_pad_top(w.valueLabel, 20, 0);
        lv_obj_set_style_pad_bottom(w.valueLabel, 10, 0);

        // Unit label below
        w.unitLabel = lv_label_create(w.container);
        lv_label_set_text(w.unitLabel, def.unit);
        lv_obj_set_style_text_color(w.unitLabel, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(w.unitLabel, &lv_font_montserrat_16, 0);

        return w;
    }

    void open_data_view()
    {
        // Create data screen
        g_ui.dataScreen = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.dataScreen);
        lv_obj_add_style(g_ui.dataScreen, &styleBg, 0);
        lv_obj_set_size(g_ui.dataScreen, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.dataScreen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(g_ui.dataScreen, on_data_gesture, LV_EVENT_GESTURE, nullptr);

        // Larger clickable back button at top-left
        lv_obj_t *backBtn = lv_obj_create(g_ui.dataScreen);
        lv_obj_remove_style_all(backBtn);
        lv_obj_set_size(backBtn, 60, 40);
        lv_obj_set_style_bg_color(backBtn, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(backBtn, 10, 0);
        lv_obj_align(backBtn, LV_ALIGN_TOP_LEFT, 12, 8);
        lv_obj_add_flag(backBtn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(backBtn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(backBtn, on_back, LV_EVENT_CLICKED, nullptr);
        
        lv_obj_t *backLbl = lv_label_create(backBtn);
        lv_label_set_text(backLbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(backLbl, lv_color_hex(0x0A84FF), 0);
        lv_obj_center(backLbl);

        // Status icons at top-right
        lv_obj_t *statusIcons = lv_obj_create(g_ui.dataScreen);
        lv_obj_remove_style_all(statusIcons);
        lv_obj_set_size(statusIcons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(statusIcons, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(statusIcons, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(statusIcons, 12, 0);
        lv_obj_align(statusIcons, LV_ALIGN_TOP_RIGHT, -16, 12);

        lv_obj_t *wifiCopy = lv_label_create(statusIcons);
        lv_label_set_text(wifiCopy, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(wifiCopy, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(wifiCopy, color_muted, 0);

        lv_obj_t *bleCopy = lv_label_create(statusIcons);
        lv_label_set_text(bleCopy, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(bleCopy, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bleCopy, color_muted, 0);

        // Horizontal carousel for data pages
        g_ui.dataCarousel = lv_obj_create(g_ui.dataScreen);
        lv_obj_remove_style_all(g_ui.dataCarousel);
        lv_obj_set_size(g_ui.dataCarousel, LV_PCT(100), 300);
        lv_obj_align(g_ui.dataCarousel, LV_ALIGN_CENTER, 0, 20);
        lv_obj_set_layout(g_ui.dataCarousel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.dataCarousel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.dataCarousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.dataCarousel, 15, 0);
        lv_obj_set_style_pad_left(g_ui.dataCarousel, 10, 0);
        lv_obj_set_style_pad_right(g_ui.dataCarousel, 10, 0);
        lv_obj_set_scroll_dir(g_ui.dataCarousel, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(g_ui.dataCarousel, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scrollbar_mode(g_ui.dataCarousel, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(g_ui.dataCarousel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_add_event_cb(g_ui.dataCarousel, on_data_scroll, LV_EVENT_SCROLL, nullptr);

        // Create data pages
        g_ui.dataPages.fill(DataPageWidgets{});
        for (size_t i = 0; i < DATA_PAGE_COUNT; ++i)
        {
            g_ui.dataPages[i] = make_data_page(i);
        }

        // Page indicator for data view
        g_ui.dataIndicator = lv_obj_create(g_ui.dataScreen);
        lv_obj_remove_style_all(g_ui.dataIndicator);
        lv_obj_set_size(g_ui.dataIndicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g_ui.dataIndicator, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.dataIndicator, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.dataIndicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.dataIndicator, 10, 0);
        lv_obj_align(g_ui.dataIndicator, LV_ALIGN_BOTTOM_MID, 0, -30);

        for (size_t i = 0; i < DATA_PAGE_COUNT; ++i)
        {
            lv_obj_t *dot = lv_obj_create(g_ui.dataIndicator);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x636366), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }

        g_state.dataPageIndex = 0;
        update_data_indicator();
        update_data_values();
        update_data_carousel_visuals();

        lv_disp_load_scr(g_ui.dataScreen);
        g_state.inDetail = true;
        g_state.inDataView = true;
    }

    // =======================================================================
    // BRIGHTNESS SCREEN
    // =======================================================================
    void open_brightness()
    {
        g_ui.detailContent = make_detail_base("Light");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        // Current brightness percentage
        int pct = (cfg.displayBrightness * 100) / 255;
        
        lv_obj_t *pctLabel = lv_label_create(g_ui.detailContent);
        lv_label_set_text_fmt(pctLabel, "%d%%", pct);
        lv_obj_set_style_text_font(pctLabel, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(pctLabel, color_text, 0);
        lv_obj_set_style_pad_bottom(pctLabel, 20, 0);
        g_ui.brightnessValue = pctLabel;

        lv_obj_t *slider = lv_slider_create(g_ui.detailContent);
        lv_obj_set_width(slider, LV_PCT(90));
        lv_obj_set_height(slider, 8);
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
            if (g_hooks.setBrightness)
            {
                g_hooks.setBrightness(static_cast<uint8_t>(val));
            } }, LV_EVENT_VALUE_CHANGED, nullptr);

        lv_obj_add_event_cb(slider, [](lv_event_t *)
                            { saveConfig(); }, LV_EVENT_RELEASED, nullptr);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    // =======================================================================
    // WIFI SCREEN
    // =======================================================================
    void open_wifi()
    {
        g_ui.detailContent = make_detail_base("WiFi");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        // Status info
        lv_obj_t *info = lv_label_create(g_ui.detailContent);
        if (g_state.lastWifi.staConnected)
        {
            String txt = "Connected to:\n" + g_state.lastWifi.currentSsid;
            txt += "\nIP: " + g_state.lastWifi.staIp;
            lv_label_set_text(info, txt.c_str());
            lv_obj_set_style_text_color(info, color_ok, 0);
        }
        else if (g_state.lastWifi.apActive)
        {
            lv_label_set_text(info, "AP Mode Active\nConnect to: ShiftLight\nIP: 192.168.4.1");
            lv_obj_set_style_text_color(info, color_text, 0);
        }
        else
        {
            lv_label_set_text(info, "No connection");
            lv_obj_set_style_text_color(info, color_muted, 0);
        }
        lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);

        // Scan results placeholder
        g_ui.wifiList = lv_label_create(g_ui.detailContent);
        lv_obj_add_style(g_ui.wifiList, &styleMuted, 0);
        lv_label_set_text(g_ui.wifiList, "\nConfigure via WebUI");
        lv_obj_set_style_pad_top(g_ui.wifiList, 20, 0);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    // =======================================================================
    // BLUETOOTH SCREEN
    // =======================================================================
    void open_ble()
    {
        g_ui.detailContent = make_detail_base("Bluetooth");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        // Connection status
        lv_obj_t *status = lv_label_create(g_ui.detailContent);
        if (g_state.bleConnected)
        {
            lv_label_set_text(status, "OBD Connected");
            lv_obj_set_style_text_color(status, color_card_accent, 0);
        }
        else if (g_state.bleConnecting)
        {
            lv_label_set_text(status, "Connecting...");
            lv_obj_set_style_text_color(status, color_warn, 0);
        }
        else
        {
            lv_label_set_text(status, "Not connected");
            lv_obj_set_style_text_color(status, color_muted, 0);
        }
        lv_obj_set_style_text_font(status, &lv_font_montserrat_24, 0);

        g_ui.bleList = lv_label_create(g_ui.detailContent);
        lv_obj_add_style(g_ui.bleList, &styleMuted, 0);
        lv_label_set_text(g_ui.bleList, "\nPair OBD dongle via WebUI");
        lv_obj_set_style_pad_top(g_ui.bleList, 20, 0);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    // =======================================================================
    // SETTINGS/INFO SCREEN
    // =======================================================================
    void open_settings()
    {
        g_ui.detailContent = make_detail_base("Info");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        lv_obj_t *title = lv_label_create(g_ui.detailContent);
        lv_label_set_text(title, "RpmCounter");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(title, color_text, 0);

        lv_obj_t *sub = lv_label_create(g_ui.detailContent);
        lv_label_set_text(sub, "ShiftLight System");
        lv_obj_add_style(sub, &styleMuted, 0);

        lv_obj_t *ver = lv_label_create(g_ui.detailContent);
        lv_label_set_text(ver, "\nVersion: 1.0\nESP32-S3 AMOLED");
        lv_obj_add_style(ver, &styleMuted, 0);
        lv_obj_set_style_pad_top(ver, 30, 0);

        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    // =======================================================================
    // OPEN DETAIL - Route to appropriate screen
    // =======================================================================
    void open_detail(const CardDef &def)
    {
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
            open_ble();
            break;
        case CardScreen::Settings:
        default:
            open_settings();
            break;
        }
    }
} // namespace

// =======================================================================
// PUBLIC API
// =======================================================================

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

    // Note: Vehicle data (rpm, speed, gear, coolant) should be set via
    // the dedicated setter functions: ui_s3_set_rpm(), ui_s3_set_speed(),
    // ui_s3_set_gear(), ui_s3_set_coolant()

    update_status_icons();

    // Update data view if active
    if (g_state.inDataView)
    {
        update_data_values();
    }

    // Update detail screens
    if (g_state.inDetail && !g_state.inDataView)
    {
        if (g_ui.wifiList)
        {
            if (wifiStatus.scanResults.empty())
            {
                // Keep existing text
            }
            else
            {
                String lines = "\nNearby:\n";
                size_t count = std::min<size_t>(wifiStatus.scanResults.size(), 4);
                for (size_t i = 0; i < count; ++i)
                {
                    lines += wifiStatus.scanResults[i].ssid;
                    lines += " (";
                    lines += wifiStatus.scanResults[i].rssi;
                    lines += ")\n";
                }
                lv_label_set_text(g_ui.wifiList, lines.c_str());
            }
        }

        if (g_ui.bleList)
        {
            const auto &res = getBleScanResults();
            if (!res.empty())
            {
                String lines = "\nNearby:\n";
                size_t count = std::min<size_t>(res.size(), 4);
                for (size_t i = 0; i < count; ++i)
                {
                    lines += res[i].name;
                    lines += "\n";
                }
                lv_label_set_text(g_ui.bleList, lines.c_str());
            }
        }
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
    // Shift light visual feedback could flash the screen border or similar
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
    
    // Fade out animation after 2 seconds
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_ui.logoOverlay);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&a, 2000);
    lv_anim_set_time(&a, 500);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
    });
    lv_anim_set_ready_cb(&a, [](lv_anim_t *anim) {
        lv_obj_add_flag(static_cast<lv_obj_t *>(anim->var), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(anim->var), LV_OPA_COVER, 0);
    });
    lv_anim_start(&a);
}

// Additional public functions for setting vehicle data
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
