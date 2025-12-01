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
#include "hardware/led_bar.h"

namespace
{
    // =======================================================================
    // CONSTANTS - Timing & Throttling
    // =======================================================================
    constexpr uint32_t GESTURE_DEBOUNCE_MS = 200;
    constexpr uint32_t WIFI_SCAN_COOLDOWN_MS = 5000;
    constexpr uint32_t BLE_SCAN_COOLDOWN_MS = 5000;
    constexpr uint32_t ICON_BLINK_PERIOD_WIFI_MS = 1000;
    constexpr uint32_t ICON_BLINK_PERIOD_BLE_MS = 800;
    
    // Card visual constants - watchOS-like square widgets
    constexpr int CARD_WIDTH = 115;
    constexpr int CARD_HEIGHT = 140;
    constexpr int CARD_RADIUS = 24;
    constexpr int CARD_ICON_SIZE_ACTIVE = 36;
    constexpr int CARD_ICON_SIZE_INACTIVE = 28;
    constexpr int CARD_SHADOW_OFFSET = 4;
    
    // Carousel animation zoom levels (0-256 base = 100%)
    constexpr int ZOOM_SIDE = 230;       // ~90%
    constexpr int ZOOM_CENTER = 333;     // ~130%
    constexpr int ZOOM_NORMAL = 256;     // 100%
    
    // Data page arc gauge settings
    constexpr int ARC_THICKNESS = 8;
    constexpr int ARC_SIZE = 180;
    constexpr int MAX_RPM_GAUGE = 8000;
    constexpr int MAX_SPEED_GAUGE = 280;
    constexpr int MAX_COOLANT_GAUGE = 130;
    
    // Brightness constants
    constexpr int DEFAULT_DISPLAY_BRIGHTNESS = 128;  // 50% default
    constexpr int DEFAULT_LED_BRIGHTNESS = 80;       // LED bar default
    constexpr int DISPLAY_TO_LED_SCALE = 3;          // Display brightness / 3 = LED brightness
    constexpr int LED_PREVIEW_GREEN_SCALE = 255;     // Green channel max in preview
    constexpr int LED_PREVIEW_BLUE_SCALE = 512;      // Blue channel divisor in preview
    
    // Card indices for direct access
    constexpr size_t LED_TEST_CARD_INDEX = 4;        // Index of LED test card in CARDS array
    
    // LED test modes
    enum class LedTestMode
    {
        None,
        Sweep,
        AllGreen,
        AllRed
    };

    // =======================================================================
    // CARD DEFINITIONS - Main menu icons (including LED Test)
    // =======================================================================
    enum class CardScreen
    {
        Data,       // Vehicle data (RPM, Speed, Gear, Temp)
        Brightness,
        Wifi,
        Bluetooth,
        LedTest,    // NEW: LED test screen
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
        {CardScreen::LedTest, "LED", LV_SYMBOL_CHARGE},
        {CardScreen::Settings, "Info", LV_SYMBOL_SETTINGS},
    };

    constexpr size_t CARD_COUNT = sizeof(CARDS) / sizeof(CARDS[0]);

    // =======================================================================
    // DATA PAGE DEFINITIONS - Large number displays (extended with Oil Temp)
    // =======================================================================
    enum class DataPage
    {
        RPM,
        Speed,
        Gear,
        Coolant,
        OilTemp  // NEW: Oil temperature if available
    };

    struct DataPageDef
    {
        DataPage page;
        const char *label;
        const char *unit;
        int maxValue;  // For arc gauge
    };

    constexpr DataPageDef DATA_PAGES[] = {
        {DataPage::RPM, "RPM", "", MAX_RPM_GAUGE},
        {DataPage::Speed, "Speed", "km/h", MAX_SPEED_GAUGE},
        {DataPage::Gear, "Gear", "", 6},
        {DataPage::Coolant, "Coolant", "\xc2\xb0" "C", MAX_COOLANT_GAUGE},
        {DataPage::OilTemp, "Oil Temp", "\xc2\xb0" "C", MAX_COOLANT_GAUGE},
    };

    constexpr size_t DATA_PAGE_COUNT = sizeof(DATA_PAGES) / sizeof(DATA_PAGES[0]);

    // =======================================================================
    // UI WIDGETS
    // =======================================================================
    struct CardWidgets
    {
        lv_obj_t *container = nullptr;
        lv_obj_t *shadow = nullptr;    // NEW: soft shadow layer
        lv_obj_t *circle = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *label = nullptr;
    };

    struct DataPageWidgets
    {
        lv_obj_t *container = nullptr;
        lv_obj_t *arc = nullptr;       // NEW: progress arc gauge
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
        lv_obj_t *ledStatusIcon = nullptr;  // NEW: LED bar status
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
        lv_obj_t *brightnessSlider = nullptr;
        lv_obj_t *brightnessValue = nullptr;
        lv_obj_t *dataScreen = nullptr;
        lv_obj_t *dataCarousel = nullptr;
        lv_obj_t *dataIndicator = nullptr;
        lv_obj_t *dataWifiIcon = nullptr;
        lv_obj_t *dataBleIcon = nullptr;
        std::array<CardWidgets, CARD_COUNT> cards{};
        std::array<DataPageWidgets, DATA_PAGE_COUNT> dataPages{};
    };

    struct UiState
    {
        int cardIndex = 0;
        int dataPageIndex = 0;
        bool inDetail = false;
        bool inDataView = false;
        bool inLedTest = false;
        bool inWifiScreen = false;
        bool inBleScreen = false;
        bool tutorialVisible = true;
        bool hasInteracted = false;
        int gear = 0;
        int rpm = 0;
        int speed = 0;
        int coolant = 0;
        int oilTemp = 0;  // NEW: Oil temperature
        bool shift = false;
        bool ledBarActive = false;  // NEW: LED bar status indicator
        WifiStatus lastWifi{};
        bool bleConnected = false;
        bool bleConnecting = false;
        uint32_t lastTouchTime = 0;
        bool iconsHidden = false;
        
        // Scan throttling
        uint32_t lastWifiScanMs = 0;
        uint32_t lastBleScanMs = 0;
        bool wifiScanInProgress = false;
        bool bleScanInProgress = false;
        
        // LED test state
        LedTestMode ledTestMode = LedTestMode::None;
        uint32_t ledTestStartMs = 0;
    };

    UiRefs g_ui;
    UiState g_state;
    UiDisplayHooks g_hooks;

    // =======================================================================
    // STYLES - Clean Apple-like dark theme (iOS/watchOS style)
    // =======================================================================
    lv_style_t styleBg;
    lv_style_t styleCard;
    lv_style_t styleCardShadow;  // NEW: shadow style
    lv_style_t styleMuted;
    lv_style_t styleCircle;
    lv_style_t styleDot;
    lv_style_t styleTutorial;
    lv_style_t styleDataValue;
    lv_style_t styleDataUnit;
    lv_style_t styleDataName;
    lv_style_t styleArc;         // NEW: arc gauge style
    lv_style_t styleBtn;         // NEW: button style

    // Colors - Pure black AMOLED background with iOS accent colors
    const lv_color_t color_bg = lv_color_hex(0x000000);           // Pure black
    const lv_color_t color_card = lv_color_hex(0x1C1C1E);         // iOS dark card
    const lv_color_t color_card_hover = lv_color_hex(0x2C2C2E);   // Lighter on focus
    const lv_color_t color_card_accent = lv_color_hex(0x0A84FF);  // iOS blue
    const lv_color_t color_muted = lv_color_hex(0x8E8E93);        // iOS gray
    const lv_color_t color_ok = lv_color_hex(0x30D158);           // iOS green
    const lv_color_t color_warn = lv_color_hex(0xFF9F0A);         // iOS orange
    const lv_color_t color_error = lv_color_hex(0xFF453A);        // iOS red
    const lv_color_t color_dot = lv_color_hex(0x3A3A3C);          // Inactive dot
    const lv_color_t color_dot_active = lv_color_hex(0x0A84FF);   // Active dot
    const lv_color_t color_text = lv_color_hex(0xF2F2F7);         // Off-white (not pure white)
    const lv_color_t color_text_secondary = lv_color_hex(0x636366);
    const lv_color_t color_shadow = lv_color_hex(0x000000);       // Shadow color

    // =======================================================================
    // HELPER FUNCTIONS
    // =======================================================================
    const CardDef &current_card()
    {
        const int count = static_cast<int>(CARD_COUNT);
        g_state.cardIndex = (g_state.cardIndex % count + count) % count;
        return CARDS[g_state.cardIndex];
    }

    void show_status_icons();
    void update_page_indicator();
    void stop_wifi_scan();
    void stop_ble_scan();

    // Debounce helper - returns true if enough time has passed since last touch
    bool is_gesture_debounced()
    {
        uint32_t now = millis();
        return (now - g_state.lastTouchTime) >= GESTURE_DEBOUNCE_MS;
    }

    void mark_interacted()
    {
        if (!is_gesture_debounced())
            return;
            
        g_state.hasInteracted = true;
        g_state.lastTouchTime = millis();
        
        // Show status icons if they were hidden
        if (g_state.iconsHidden)
        {
            g_state.iconsHidden = false;
            show_status_icons();
        }
        
        // Store tutorial seen flag in config and persist
        if (g_state.tutorialVisible && g_ui.tutorial)
        {
            g_state.tutorialVisible = false;
            cfg.uiTutorialSeen = true;
            saveConfig();  // Persist to NVS so tutorial doesn't show again
            
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
    // STYLE INITIALIZATION - Enhanced Apple/watchOS dark theme
    // =======================================================================
    void apply_styles()
    {
        // Pure black background for AMOLED
        lv_style_init(&styleBg);
        lv_style_set_bg_color(&styleBg, color_bg);
        lv_style_set_bg_opa(&styleBg, LV_OPA_COVER);
        lv_style_set_pad_all(&styleBg, 0);

        // Card style with subtle gradient effect via border
        lv_style_init(&styleCard);
        lv_style_set_bg_color(&styleCard, color_card);
        lv_style_set_bg_opa(&styleCard, LV_OPA_COVER);
        lv_style_set_radius(&styleCard, CARD_RADIUS);
        lv_style_set_pad_all(&styleCard, 10);
        lv_style_set_border_width(&styleCard, 1);
        lv_style_set_border_color(&styleCard, lv_color_hex(0x3A3A3C));
        lv_style_set_border_opa(&styleCard, LV_OPA_50);
        
        // Card shadow style
        lv_style_init(&styleCardShadow);
        lv_style_set_shadow_width(&styleCardShadow, 20);
        lv_style_set_shadow_color(&styleCardShadow, color_shadow);
        lv_style_set_shadow_opa(&styleCardShadow, LV_OPA_30);
        lv_style_set_shadow_ofs_x(&styleCardShadow, 0);
        lv_style_set_shadow_ofs_y(&styleCardShadow, CARD_SHADOW_OFFSET);

        lv_style_init(&styleCircle);
        lv_style_set_radius(&styleCircle, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&styleCircle, lv_color_hex(0x2C2C2E));
        lv_style_set_bg_opa(&styleCircle, LV_OPA_COVER);
        lv_style_set_border_width(&styleCircle, 0);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, color_muted);

        // Pill-shaped page indicator dots
        lv_style_init(&styleDot);
        lv_style_set_radius(&styleDot, 4);  // Pill shape
        lv_style_set_bg_color(&styleDot, color_dot);
        lv_style_set_bg_opa(&styleDot, LV_OPA_COVER);
        lv_style_set_border_width(&styleDot, 0);

        lv_style_init(&styleTutorial);
        lv_style_set_radius(&styleTutorial, 16);
        lv_style_set_bg_color(&styleTutorial, lv_color_hex(0x1C1C1E));
        lv_style_set_bg_opa(&styleTutorial, LV_OPA_90);
        lv_style_set_pad_all(&styleTutorial, 14);
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
        
        // Arc gauge style for data pages
        lv_style_init(&styleArc);
        lv_style_set_arc_width(&styleArc, ARC_THICKNESS);
        lv_style_set_arc_color(&styleArc, lv_color_hex(0x2C2C2E));
        
        // Button style
        lv_style_init(&styleBtn);
        lv_style_set_bg_color(&styleBtn, color_card_accent);
        lv_style_set_bg_opa(&styleBtn, LV_OPA_COVER);
        lv_style_set_radius(&styleBtn, 12);
        lv_style_set_text_color(&styleBtn, color_text);
        lv_style_set_pad_ver(&styleBtn, 10);
        lv_style_set_pad_hor(&styleBtn, 20);
    }

    // =======================================================================
    // PAGE INDICATOR UPDATE - Pill-shaped with animated brightness/width
    // =======================================================================
    void update_page_indicator()
    {
        if (!g_ui.pageIndicator)
            return;

        uint32_t childCount = lv_obj_get_child_cnt(g_ui.pageIndicator);
        for (uint32_t i = 0; i < childCount; ++i)
        {
            lv_obj_t *dot = lv_obj_get_child(g_ui.pageIndicator, i);
            if (!dot) continue;
            
            bool active = static_cast<int>(i) == g_state.cardIndex;
            
            // Animate color and opacity
            lv_color_t targetColor = active ? color_dot_active : color_dot;
            lv_opa_t targetOpa = active ? LV_OPA_COVER : LV_OPA_60;
            lv_obj_set_style_bg_color(dot, targetColor, 0);
            lv_obj_set_style_opa(dot, targetOpa, 0);
            
            // Animate width (pill expands when active)
            lv_coord_t targetW = active ? 24 : 8;
            lv_coord_t currentW = lv_obj_get_width(dot);
            if (currentW != targetW)
            {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, dot);
                lv_anim_set_values(&a, currentW, targetW);
                lv_anim_set_time(&a, 250);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
                    lv_obj_set_width(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v));
                });
                lv_anim_start(&a);
            }
        }
    }

    // =======================================================================
    // DATA PAGE INDICATOR UPDATE - With animation
    // =======================================================================
    void update_data_indicator()
    {
        if (!g_ui.dataIndicator)
            return;

        uint32_t childCount = lv_obj_get_child_cnt(g_ui.dataIndicator);
        for (uint32_t i = 0; i < childCount; ++i)
        {
            lv_obj_t *dot = lv_obj_get_child(g_ui.dataIndicator, i);
            if (!dot) continue;
            
            bool active = static_cast<int>(i) == g_state.dataPageIndex;
            lv_obj_set_style_bg_color(dot, active ? color_dot_active : lv_color_hex(0x48484A), 0);
            lv_obj_set_style_opa(dot, active ? LV_OPA_COVER : LV_OPA_60, 0);
            
            // Animate width
            lv_coord_t targetW = active ? 24 : 8;
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
    // CAROUSEL VISUAL UPDATE - Smooth zoom animation (90% to 130%)
    // =======================================================================
    void update_carousel_visuals()
    {
        if (!g_ui.carousel)
            return;
        
        // Calculate which card is closest to center for focus effect
        lv_coord_t scrollX = lv_obj_get_scroll_x(g_ui.carousel);
        lv_coord_t carouselW = lv_obj_get_width(g_ui.carousel);
        lv_coord_t centerX = scrollX + carouselW / 2;
        
        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            CardWidgets &cw = g_ui.cards[i];
            if (!cw.container)
                continue;
            
            // Calculate distance from center
            lv_coord_t cardX = lv_obj_get_x(cw.container);
            lv_coord_t cardW = lv_obj_get_width(cw.container);
            lv_coord_t cardCenterX = cardX + cardW / 2 - scrollX;
            lv_coord_t dist = abs(cardCenterX - carouselW / 2);
            
            // Calculate zoom based on distance (center = 130%, edges = 90%)
            float normalizedDist = static_cast<float>(dist) / static_cast<float>(carouselW / 2);
            normalizedDist = std::min(1.0f, normalizedDist);
            
            // Smooth interpolation: center (0) -> ZOOM_CENTER, edge (1) -> ZOOM_SIDE
            int targetZoom = ZOOM_CENTER - static_cast<int>((ZOOM_CENTER - ZOOM_SIDE) * normalizedDist);
            
            // Calculate opacity (center = 100%, edges = 70%)
            lv_opa_t targetOpa = static_cast<lv_opa_t>(LV_OPA_COVER - (LV_OPA_30 * normalizedDist));
            
            // Apply zoom and opacity with smooth animation
            int currentZoom = lv_obj_get_style_transform_zoom(cw.container, 0);
            if (abs(currentZoom - targetZoom) > 5)
            {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, cw.container);
                lv_anim_set_values(&a, currentZoom, targetZoom);
                lv_anim_set_time(&a, 150);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
                    lv_obj_set_style_transform_zoom(static_cast<lv_obj_t *>(obj), static_cast<uint16_t>(v), 0);
                });
                lv_anim_start(&a);
            }
            
            // Apply opacity
            lv_obj_set_style_opa(cw.container, targetOpa, 0);
            
            // Update icon size based on focus (active card gets larger icon)
            bool isActive = (static_cast<int>(i) == g_state.cardIndex);
            if (cw.icon)
            {
                lv_obj_set_style_text_font(cw.icon, 
                    isActive ? &lv_font_montserrat_32 : &lv_font_montserrat_24, 0);
            }
            
            // Subtle background color animation for active card
            lv_color_t bgColor = isActive ? color_card_hover : color_card;
            lv_obj_set_style_bg_color(cw.container, bgColor, 0);
            
            lv_obj_clear_flag(cw.container, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update current card index based on scroll position
        int newIdx = 0;
        lv_coord_t minDist = 9999;
        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            CardWidgets &cw = g_ui.cards[i];
            if (!cw.container) continue;
            
            lv_coord_t cardX = lv_obj_get_x(cw.container);
            lv_coord_t cardW = lv_obj_get_width(cw.container);
            lv_coord_t cardCenterX = cardX + cardW / 2 - scrollX;
            lv_coord_t dist = abs(cardCenterX - carouselW / 2);
            
            if (dist < minDist)
            {
                minDist = dist;
                newIdx = static_cast<int>(i);
            }
        }
        
        if (g_state.cardIndex != newIdx)
        {
            g_state.cardIndex = newIdx;
            update_page_indicator();
        }
    }

    // =======================================================================
    // DATA CAROUSEL VISUAL UPDATE - With zoom animation
    // =======================================================================
    void update_data_carousel_visuals()
    {
        if (!g_ui.dataCarousel)
            return;
        
        lv_coord_t scrollX = lv_obj_get_scroll_x(g_ui.dataCarousel);
        lv_coord_t carouselW = lv_obj_get_width(g_ui.dataCarousel);

        for (size_t i = 0; i < DATA_PAGE_COUNT; ++i)
        {
            DataPageWidgets &pw = g_ui.dataPages[i];
            if (!pw.container)
                continue;
            
            // Calculate distance from center for zoom effect
            lv_coord_t pageX = lv_obj_get_x(pw.container);
            lv_coord_t pageW = lv_obj_get_width(pw.container);
            lv_coord_t pageCenterX = pageX + pageW / 2 - scrollX;
            lv_coord_t dist = abs(pageCenterX - carouselW / 2);
            
            float normalizedDist = static_cast<float>(dist) / static_cast<float>(carouselW / 2);
            normalizedDist = std::min(1.0f, normalizedDist);
            
            // Subtle zoom (center = 105%, edges = 95%)
            int targetZoom = 269 - static_cast<int>(26 * normalizedDist);  // ~105% to ~95%
            lv_opa_t targetOpa = static_cast<lv_opa_t>(LV_OPA_COVER - (LV_OPA_20 * normalizedDist));
            
            lv_obj_set_style_transform_zoom(pw.container, static_cast<uint16_t>(targetZoom), 0);
            lv_obj_set_style_opa(pw.container, targetOpa, 0);
            lv_obj_clear_flag(pw.container, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update page indicator based on scroll position
        if (g_ui.dataCarousel && g_ui.dataIndicator)
        {
            lv_coord_t pageWidth = lv_disp_get_hor_res(g_ui.disp) - 20;
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
    
    // Helper to get WiFi icon color and opacity based on state (with blinking)
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
            col = color_ok; // Green = connected
            isConnected = true;
        }
        else if (apActive && apClients > 0)
        {
            col = color_ok; // Green = AP with clients
            isConnected = true;
        }
        else if (apActive || staConnecting)
        {
            // Blinking orange during AP waiting or STA connecting
            col = color_warn;
            float phase = static_cast<float>(now % 1000) / 500.0f; // 1Hz blink
            opa = (phase < 1.0f) ? LV_OPA_COVER : LV_OPA_30;
        }
        else
        {
            col = color_error; // Red = error/no connection
        }
    }
    
    // Helper to get BLE icon color and opacity based on state (with blinking)
    void get_ble_icon_style(lv_color_t &col, lv_opa_t &opa, bool &isConnected)
    {
        const uint32_t now = millis();
        opa = LV_OPA_COVER;
        isConnected = false;
        
        if (g_state.bleConnected)
        {
            col = color_card_accent; // Blue = connected
            isConnected = true;
        }
        else if (g_state.bleConnecting)
        {
            // Blinking blue during connection attempt
            col = color_card_accent;
            float phase = static_cast<float>(now % 800) / 400.0f; // 1.25Hz blink
            opa = (phase < 1.0f) ? LV_OPA_COVER : LV_OPA_30;
        }
        else
        {
            col = color_error; // Red = not connected / failed
            opa = LV_OPA_80;
        }
    }
    
    void apply_icon_style(lv_obj_t *icon, lv_color_t col, lv_opa_t opa)
    {
        if (!icon) return;
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
        
        // Update main screen icons
        apply_icon_style(g_ui.wifiIcon, wifiCol, wifiOpa);
        apply_icon_style(g_ui.bleIcon, bleCol, bleOpa);
        
        // Update LED status icon (green = active, red = flashing/shift)
        if (g_ui.ledStatusIcon)
        {
            lv_color_t ledCol = g_state.ledBarActive ? color_ok : color_muted;
            if (g_state.shift)
            {
                // Blink red during shift light
                uint32_t now = millis();
                float phase = static_cast<float>(now % 200) / 100.0f;
                ledCol = (phase < 1.0f) ? color_error : color_muted;
            }
            lv_obj_set_style_text_color(g_ui.ledStatusIcon, ledCol, 0);
        }
        
        // Update detail screen icons
        apply_icon_style(g_ui.detailWifiIcon, wifiCol, wifiOpa);
        apply_icon_style(g_ui.detailBleIcon, bleCol, bleOpa);
        
        // Update data screen icons
        apply_icon_style(g_ui.dataWifiIcon, wifiCol, wifiOpa);
        apply_icon_style(g_ui.dataBleIcon, bleCol, bleOpa);
    }
    
    void show_status_icons()
    {
        // Show main screen icons
        if (g_ui.statusBar)
            lv_obj_set_style_opa(g_ui.statusBar, LV_OPA_COVER, 0);
    }
    
    // =======================================================================
    // WIFI/BLE SCAN CONTROL - Throttled and auto-stop
    // =======================================================================
    void start_wifi_scan()
    {
        uint32_t now = millis();
        if (now - g_state.lastWifiScanMs < WIFI_SCAN_COOLDOWN_MS)
            return;  // Throttle scans
        
        if (g_state.wifiScanInProgress)
            return;
        
        g_state.wifiScanInProgress = true;
        g_state.lastWifiScanMs = now;
        startWifiScan();  // From core/wifi.h
    }
    
    void stop_wifi_scan()
    {
        g_state.wifiScanInProgress = false;
        // WiFi scan stops automatically when results are ready
    }
    
    void start_ble_scan()
    {
        uint32_t now = millis();
        if (now - g_state.lastBleScanMs < BLE_SCAN_COOLDOWN_MS)
            return;  // Throttle scans
        
        if (g_state.bleScanInProgress)
            return;
        
        g_state.bleScanInProgress = true;
        g_state.lastBleScanMs = now;
        startBleScan(4);  // From ble_obd.h, 4 second scan
    }
    
    void stop_ble_scan()
    {
        g_state.bleScanInProgress = false;
        // BLE scan stops automatically after duration
    }

    // =======================================================================
    // DATA PAGE VALUE UPDATES - With arc gauge animation
    // =======================================================================
    void update_arc_value(lv_obj_t *arc, int value, int maxValue)
    {
        if (!arc) return;
        int pct = (value * 100) / std::max(1, maxValue);
        pct = std::max(0, std::min(100, pct));
        
        int16_t currentAngle = lv_arc_get_angle_end(arc);
        int16_t targetAngle = static_cast<int16_t>((pct * 270) / 100);  // 0-270 degree arc
        
        // Smooth animation only if change is significant
        if (abs(currentAngle - targetAngle) > 2)
        {
            lv_arc_set_value(arc, pct);
        }
    }
    
    void update_data_values()
    {
        // Update RPM page
        if (g_ui.dataPages[0].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[0].valueLabel, "%d", g_state.rpm);
        }
        if (g_ui.dataPages[0].arc)
        {
            update_arc_value(g_ui.dataPages[0].arc, g_state.rpm, MAX_RPM_GAUGE);
        }
        
        // Update Speed page
        if (g_ui.dataPages[1].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[1].valueLabel, "%d", g_state.speed);
        }
        if (g_ui.dataPages[1].arc)
        {
            update_arc_value(g_ui.dataPages[1].arc, g_state.speed, MAX_SPEED_GAUGE);
        }
        
        // Update Gear page
        if (g_ui.dataPages[2].valueLabel)
        {
            lv_label_set_text(g_ui.dataPages[2].valueLabel, 
                              g_state.gear <= 0 ? "N" : String(g_state.gear).c_str());
        }
        // Gear doesn't need arc (discrete values)
        
        // Update Coolant page
        if (g_ui.dataPages[3].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[3].valueLabel, "%d", g_state.coolant);
        }
        if (g_ui.dataPages[3].arc)
        {
            update_arc_value(g_ui.dataPages[3].arc, g_state.coolant, MAX_COOLANT_GAUGE);
        }
        
        // Update Oil Temp page
        if (DATA_PAGE_COUNT > 4 && g_ui.dataPages[4].valueLabel)
        {
            lv_label_set_text_fmt(g_ui.dataPages[4].valueLabel, "%d", g_state.oilTemp);
        }
        if (DATA_PAGE_COUNT > 4 && g_ui.dataPages[4].arc)
        {
            update_arc_value(g_ui.dataPages[4].arc, g_state.oilTemp, MAX_COOLANT_GAUGE);
        }
    }

    void show_home();

    void on_back(lv_event_t *e)
    {
        LV_UNUSED(e);
        
        // Stop any ongoing scans when leaving screens
        if (g_state.inWifiScreen)
        {
            stop_wifi_scan();
            g_state.inWifiScreen = false;
        }
        if (g_state.inBleScreen)
        {
            stop_ble_scan();
            g_state.inBleScreen = false;
        }
        g_state.inLedTest = false;
        g_state.ledTestMode = LedTestMode::None;
        
        show_home();
    }

    void on_detail_gesture(lv_event_t *e)
    {
        if (!is_gesture_debounced())
            return;
            
        if (lv_event_get_code(e) == LV_EVENT_GESTURE)
        {
            // Changed to swipe UP (from bottom to top) to go back
            if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
            {
                on_back(e);
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
        g_state.cardIndex = idx;  // Update state
        lv_obj_t *target = g_ui.cards[static_cast<size_t>(idx)].container;
        if (target)
        {
            lv_obj_scroll_to_view(target, anim);
        }
        update_page_indicator();  // Update dots
        update_carousel_visuals();
    }

    void open_detail(const CardDef &def);

    void on_card_click(lv_event_t *e)
    {
        if (!is_gesture_debounced())
            return;
            
        lv_obj_t *target = lv_event_get_target(e);
        size_t idx = 0;
        for (; idx < CARD_COUNT; ++idx)
        {
            if (g_ui.cards[idx].container == target || 
                g_ui.cards[idx].circle == target ||
                g_ui.cards[idx].icon == target ||
                g_ui.cards[idx].label == target ||
                g_ui.cards[idx].shadow == target)
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
        if (is_gesture_debounced())
            mark_interacted();
        update_carousel_visuals();
    }

    void on_gesture(lv_event_t *e)
    {
        if (!is_gesture_debounced())
            return;
            
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
        // Use flex column layout so body can take remaining space
        lv_obj_set_layout(scr, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

        lv_obj_add_event_cb(scr, on_detail_gesture, LV_EVENT_GESTURE, nullptr);

        // Header row with back button, title, and status icons
        lv_obj_t *header = lv_obj_create(scr);
        lv_obj_remove_style_all(header);
        lv_obj_set_width(header, LV_PCT(100));
        lv_obj_set_height(header, 44);
        lv_obj_set_layout(header, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        // Larger back button with padding for easier tap
        lv_obj_t *backBtn = lv_obj_create(header);
        lv_obj_remove_style_all(backBtn);
        lv_obj_set_size(backBtn, 44, 44);
        lv_obj_add_flag(backBtn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(backBtn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(backBtn, on_back, LV_EVENT_CLICKED, nullptr);
        
        lv_obj_t *backLbl = lv_label_create(backBtn);
        lv_label_set_text(backLbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(backLbl, lv_color_hex(0x0A84FF), 0);
        lv_obj_center(backLbl);

        // Title in center
        lv_obj_t *titleLbl = lv_label_create(header);
        lv_label_set_text(titleLbl, title);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(titleLbl, color_text, 0);

        // Status icons container (top-right)
        lv_obj_t *statusIcons = lv_obj_create(header);
        lv_obj_remove_style_all(statusIcons);
        lv_obj_set_size(statusIcons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(statusIcons, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(statusIcons, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(statusIcons, 8, 0);
        lv_obj_clear_flag(statusIcons, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.detailWifiIcon = lv_label_create(statusIcons);
        lv_label_set_text(g_ui.detailWifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(g_ui.detailWifiIcon, &lv_font_montserrat_16, 0);

        g_ui.detailBleIcon = lv_label_create(statusIcons);
        lv_label_set_text(g_ui.detailBleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.detailBleIcon, &lv_font_montserrat_16, 0);

        // Body takes remaining space after header
        lv_obj_t *body = lv_obj_create(scr);
        lv_obj_remove_style_all(body);
        lv_obj_set_width(body, LV_PCT(100));
        lv_obj_set_flex_grow(body, 1);  // Take remaining vertical space
        lv_obj_set_style_pad_top(body, 20, 0);
        lv_obj_set_layout(body, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(body, 12, 0);
        lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLL_ONE);
        lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

        return body;
    }

    // =======================================================================
    // CARD CREATION - watchOS-style widget with shadow and depth
    // =======================================================================
    CardWidgets make_icon_card(size_t idx)
    {
        const CardDef &def = CARDS[idx];
        CardWidgets w{};

        // Main container with shadow effect
        w.container = lv_obj_create(g_ui.carousel);
        lv_obj_remove_style_all(w.container);
        lv_obj_set_size(w.container, CARD_WIDTH, CARD_HEIGHT);
        lv_obj_set_style_bg_color(w.container, color_card, 0);
        lv_obj_set_style_bg_opa(w.container, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(w.container, CARD_RADIUS, 0);
        // Subtle border for depth effect
        lv_obj_set_style_border_width(w.container, 1, 0);
        lv_obj_set_style_border_color(w.container, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_border_opa(w.container, LV_OPA_50, 0);
        // Add soft shadow
        lv_obj_set_style_shadow_width(w.container, 16, 0);
        lv_obj_set_style_shadow_color(w.container, color_shadow, 0);
        lv_obj_set_style_shadow_opa(w.container, LV_OPA_40, 0);
        lv_obj_set_style_shadow_ofs_y(w.container, CARD_SHADOW_OFFSET, 0);
        
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_set_layout(w.container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(w.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(w.container, 10, 0);
        lv_obj_set_style_pad_top(w.container, 18, 0);
        lv_obj_add_event_cb(w.container, on_card_click, LV_EVENT_CLICKED, nullptr);

        // Circular icon background with gradient-like effect
        w.circle = lv_obj_create(w.container);
        lv_obj_remove_style_all(w.circle);
        lv_obj_set_size(w.circle, 64, 64);
        lv_obj_set_style_bg_color(w.circle, color_card_accent, 0);
        lv_obj_set_style_bg_opa(w.circle, LV_OPA_20, 0);
        lv_obj_set_style_radius(w.circle, LV_RADIUS_CIRCLE, 0);
        // Inner ring for depth
        lv_obj_set_style_border_width(w.circle, 2, 0);
        lv_obj_set_style_border_color(w.circle, color_card_accent, 0);
        lv_obj_set_style_border_opa(w.circle, LV_OPA_40, 0);
        lv_obj_clear_flag(w.circle, LV_OBJ_FLAG_SCROLLABLE);

        // Icon with variable size (will be adjusted based on focus)
        w.icon = lv_label_create(w.circle);
        lv_label_set_text(w.icon, def.symbol);
        lv_obj_set_style_text_font(w.icon, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(w.icon, color_card_accent, 0);
        lv_obj_center(w.icon);

        // Title label
        w.label = lv_label_create(w.container);
        lv_label_set_text(w.label, def.title);
        lv_obj_set_style_text_color(w.label, color_muted, 0);
        lv_obj_set_style_text_font(w.label, &lv_font_montserrat_16, 0);

        return w;
    }

    // =======================================================================
    // TUTORIAL OVERLAY - Informative with icons
    // =======================================================================
    void build_tutorial()
    {
        // Skip if already seen (stored in config)
        if (cfg.uiTutorialSeen)
        {
            g_state.tutorialVisible = false;
            return;
        }
        
        g_ui.tutorial = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.tutorial);
        lv_obj_add_style(g_ui.tutorial, &styleTutorial, 0);
        lv_obj_set_width(g_ui.tutorial, LV_PCT(85));
        lv_obj_align(g_ui.tutorial, LV_ALIGN_BOTTOM_MID, 0, -55);
        lv_obj_clear_flag(g_ui.tutorial, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(g_ui.tutorial, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.tutorial, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(g_ui.tutorial, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(g_ui.tutorial, 6, 0);

        // Swipe hint with icons
        lv_obj_t *swipeHint = lv_label_create(g_ui.tutorial);
        lv_label_set_text(swipeHint, LV_SYMBOL_LEFT "  Swipe to navigate  " LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(swipeHint, color_text, 0);
        lv_obj_set_style_text_font(swipeHint, &lv_font_montserrat_16, 0);
        
        // Tap hint
        lv_obj_t *tapHint = lv_label_create(g_ui.tutorial);
        lv_label_set_text(tapHint, "Tap card to open");
        lv_obj_set_style_text_color(tapHint, color_muted, 0);
        lv_obj_set_style_text_font(tapHint, &lv_font_montserrat_16, 0);
    }

    // =======================================================================
    // PAGE INDICATOR - Pill-shaped dots
    // =======================================================================
    void build_page_indicator()
    {
        g_ui.pageIndicator = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.pageIndicator);
        lv_obj_set_size(g_ui.pageIndicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(g_ui.pageIndicator, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.pageIndicator, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.pageIndicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.pageIndicator, 6, 0);
        lv_obj_align(g_ui.pageIndicator, LV_ALIGN_BOTTOM_MID, 0, -18);

        for (size_t i = 0; i < CARD_COUNT; ++i)
        {
            lv_obj_t *dot = lv_obj_create(g_ui.pageIndicator);
            lv_obj_remove_style_all(dot);
            // Pill shape (height < width when active)
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_bg_color(dot, color_dot, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_60, 0);
            lv_obj_set_style_radius(dot, 4, 0);  // Pill-shaped
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    // =======================================================================
    // BUILD HOME SCREEN - Minimalist carousel with status bar
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

        // Status bar with WiFi, BLE, and LED status icons
        g_ui.statusBar = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.statusBar);
        lv_obj_set_size(g_ui.statusBar, LV_PCT(100), 32);
        lv_obj_set_layout(g_ui.statusBar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.statusBar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.statusBar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(g_ui.statusBar, 14, 0);
        lv_obj_set_style_pad_right(g_ui.statusBar, 14, 0);
        lv_obj_set_style_pad_top(g_ui.statusBar, 8, 0);
        lv_obj_set_style_pad_column(g_ui.statusBar, 10, 0);

        // LED bar status icon (optional, shows if LED bar is active)
        g_ui.ledStatusIcon = lv_label_create(g_ui.statusBar);
        lv_label_set_text(g_ui.ledStatusIcon, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_font(g_ui.ledStatusIcon, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(g_ui.ledStatusIcon, color_muted, 0);

        g_ui.wifiIcon = lv_label_create(g_ui.statusBar);
        lv_label_set_text(g_ui.wifiIcon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(g_ui.wifiIcon, &lv_font_montserrat_16, 0);

        g_ui.bleIcon = lv_label_create(g_ui.statusBar);
        lv_label_set_text(g_ui.bleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.bleIcon, &lv_font_montserrat_16, 0);

        // Main carousel - centered on screen
        g_ui.carousel = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.carousel);
        lv_obj_set_size(g_ui.carousel, LV_PCT(100), 200);
        lv_obj_align(g_ui.carousel, LV_ALIGN_CENTER, 0, 15);
        // Center padding to align first/last cards in center
        lv_coord_t sidePad = (lv_disp_get_hor_res(disp) - CARD_WIDTH) / 2;
        lv_obj_set_style_pad_left(g_ui.carousel, sidePad, 0);
        lv_obj_set_style_pad_right(g_ui.carousel, sidePad, 0);
        lv_obj_set_style_pad_ver(g_ui.carousel, 15, 0);
        lv_obj_set_layout(g_ui.carousel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(g_ui.carousel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(g_ui.carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(g_ui.carousel, 16, 0);
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
    // SHOW HOME - Clean up and return to home screen
    // =======================================================================
    void show_home()
    {
        if (!g_ui.root)
            return;
        
        // Stop any active scans
        if (g_state.inWifiScreen)
        {
            stop_wifi_scan();
            g_state.inWifiScreen = false;
        }
        if (g_state.inBleScreen)
        {
            stop_ble_scan();
            g_state.inBleScreen = false;
        }
        
        // Reset state flags
        g_state.inDetail = false;
        g_state.inDataView = false;
        g_state.inLedTest = false;
        g_state.ledTestMode = LedTestMode::None;
        
        // Clean up detail screen by deleting it if it exists
        if (g_ui.detail)
        {
            lv_obj_del(g_ui.detail);
            g_ui.detail = nullptr;
        }
        if (g_ui.dataScreen)
        {
            lv_obj_del(g_ui.dataScreen);
            g_ui.dataScreen = nullptr;
        }
        
        // Clear detail screen widget references
        g_ui.detailContent = nullptr;
        g_ui.detailWifiIcon = nullptr;
        g_ui.detailBleIcon = nullptr;
        g_ui.dataWifiIcon = nullptr;
        g_ui.dataBleIcon = nullptr;
        g_ui.wifiList = nullptr;
        g_ui.bleList = nullptr;
        g_ui.brightnessSlider = nullptr;
        g_ui.brightnessValue = nullptr;
        g_ui.dataCarousel = nullptr;
        g_ui.dataIndicator = nullptr;
        
        // Clear data page widgets
        for (size_t i = 0; i < DATA_PAGE_COUNT; ++i)
        {
            g_ui.dataPages[i] = DataPageWidgets{};
        }
        
        lv_disp_load_scr(g_ui.root);
        update_carousel_visuals();
        update_status_icons();
        update_page_indicator();
    }

    // =======================================================================
    // DATA VIEW - Large number display with arc gauges
    // =======================================================================
    void on_data_gesture(lv_event_t *e)
    {
        if (!is_gesture_debounced())
            return;
            
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
        lv_obj_set_size(w.container, lv_disp_get_hor_res(g_ui.disp) - 24, 280);
        lv_obj_set_style_bg_color(w.container, color_card, 0);
        lv_obj_set_style_bg_opa(w.container, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(w.container, 20, 0);
        lv_obj_set_style_border_width(w.container, 1, 0);
        lv_obj_set_style_border_color(w.container, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_border_opa(w.container, LV_OPA_40, 0);
        lv_obj_clear_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(w.container, LV_OBJ_FLAG_SNAPPABLE);

        // Name label at top
        w.nameLabel = lv_label_create(w.container);
        lv_label_set_text(w.nameLabel, def.label);
        lv_obj_set_style_text_color(w.nameLabel, color_muted, 0);
        lv_obj_set_style_text_font(w.nameLabel, &lv_font_montserrat_20, 0);
        lv_obj_align(w.nameLabel, LV_ALIGN_TOP_MID, 0, 16);

        // Mini arc gauge behind the value (for RPM, Speed, Coolant, OilTemp - not Gear)
        if (def.page != DataPage::Gear)
        {
            w.arc = lv_arc_create(w.container);
            lv_obj_set_size(w.arc, ARC_SIZE, ARC_SIZE);
            lv_obj_align(w.arc, LV_ALIGN_CENTER, 0, 10);
            lv_arc_set_rotation(w.arc, 135);  // Start from bottom-left
            lv_arc_set_bg_angles(w.arc, 0, 270);
            lv_arc_set_range(w.arc, 0, 100);
            lv_arc_set_value(w.arc, 0);
            lv_obj_remove_style(w.arc, NULL, LV_PART_KNOB);
            lv_obj_clear_flag(w.arc, LV_OBJ_FLAG_CLICKABLE);
            
            // Background arc color
            lv_obj_set_style_arc_color(w.arc, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
            lv_obj_set_style_arc_width(w.arc, ARC_THICKNESS, LV_PART_MAIN);
            
            // Indicator arc color based on page type
            lv_color_t arcColor = color_card_accent;
            if (def.page == DataPage::RPM)
            {
                arcColor = color_ok;  // Green for RPM
            }
            else if (def.page == DataPage::Coolant || def.page == DataPage::OilTemp)
            {
                arcColor = color_warn;  // Orange for temperature
            }
            lv_obj_set_style_arc_color(w.arc, arcColor, LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(w.arc, ARC_THICKNESS, LV_PART_INDICATOR);
        }

        // Large value in center (overlays the arc)
        w.valueLabel = lv_label_create(w.container);
        lv_label_set_text(w.valueLabel, "---");
        lv_obj_set_style_text_color(w.valueLabel, color_text, 0);
        lv_obj_set_style_text_font(w.valueLabel, &lv_font_montserrat_48, 0);
        lv_obj_align(w.valueLabel, LV_ALIGN_CENTER, 0, 10);

        // Unit label below value
        w.unitLabel = lv_label_create(w.container);
        lv_label_set_text(w.unitLabel, def.unit);
        lv_obj_set_style_text_color(w.unitLabel, color_text_secondary, 0);
        lv_obj_set_style_text_font(w.unitLabel, &lv_font_montserrat_16, 0);
        lv_obj_align(w.unitLabel, LV_ALIGN_CENTER, 0, 50);

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

        // Simple back arrow at top-left (no grey box)
        lv_obj_t *backLbl = lv_label_create(g_ui.dataScreen);
        lv_label_set_text(backLbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(backLbl, lv_color_hex(0x0A84FF), 0);
        lv_obj_align(backLbl, LV_ALIGN_TOP_LEFT, 12, 12);
        lv_obj_add_flag(backLbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(backLbl, on_back, LV_EVENT_CLICKED, nullptr);

        // Status icons at top-right (tracked for updates)
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
        lv_obj_set_style_text_font(g_ui.dataWifiIcon, &lv_font_montserrat_16, 0);

        g_ui.dataBleIcon = lv_label_create(statusIcons);
        lv_label_set_text(g_ui.dataBleIcon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(g_ui.dataBleIcon, &lv_font_montserrat_16, 0);

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
        update_status_icons();

        lv_disp_load_scr(g_ui.dataScreen);
        g_state.inDetail = true;
        g_state.inDataView = true;
    }

    // =======================================================================
    // BRIGHTNESS SCREEN - With LED bar preview
    // =======================================================================
    void open_brightness()
    {
        g_ui.detailContent = make_detail_base("Light");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);

        // Current brightness percentage (large display)
        int pct = (cfg.displayBrightness * 100) / 255;
        
        lv_obj_t *pctLabel = lv_label_create(g_ui.detailContent);
        lv_label_set_text_fmt(pctLabel, "%d%%", pct);
        lv_obj_set_style_text_font(pctLabel, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(pctLabel, color_text, 0);
        lv_obj_set_style_pad_bottom(pctLabel, 16, 0);
        g_ui.brightnessValue = pctLabel;

        // Brightness slider
        lv_obj_t *slider = lv_slider_create(g_ui.detailContent);
        lv_obj_set_width(slider, LV_PCT(85));
        lv_obj_set_height(slider, 10);
        lv_slider_set_range(slider, 10, 255);
        lv_slider_set_value(slider, cfg.displayBrightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, color_card_accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, color_text, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, -2, LV_PART_KNOB);
        lv_obj_set_style_radius(slider, 5, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 5, LV_PART_INDICATOR);
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
            }
            // Also update LED bar brightness preview
            cfg.brightness = val / DISPLAY_TO_LED_SCALE;
            g_brightnessPreviewActive = true;
            g_lastBrightnessChangeMs = millis();
            strip.setBrightness(cfg.brightness);
            rememberPreviewPixels();
            // Show preview gradient on LED bar
            for (int i = 0; i < NUM_LEDS; i++)
            {
                int intensity = (i * LED_PREVIEW_GREEN_SCALE) / NUM_LEDS;
                int green = intensity * val / LED_PREVIEW_GREEN_SCALE;
                int blue = intensity * val / LED_PREVIEW_BLUE_SCALE;
                strip.setPixelColor(i, strip.Color(0, green, blue));
            }
            strip.show(); }, LV_EVENT_VALUE_CHANGED, nullptr);

        lv_obj_add_event_cb(slider, [](lv_event_t *)
                            { saveConfig(); }, LV_EVENT_RELEASED, nullptr);

        // LED bar preview label
        lv_obj_t *previewLabel = lv_label_create(g_ui.detailContent);
        lv_label_set_text(previewLabel, "LED Bar Preview");
        lv_obj_set_style_text_color(previewLabel, color_muted, 0);
        lv_obj_set_style_text_font(previewLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_top(previewLabel, 24, 0);

        // Preview bar (visual representation of LED bar brightness)
        lv_obj_t *previewBar = lv_bar_create(g_ui.detailContent);
        lv_obj_set_width(previewBar, LV_PCT(80));
        lv_obj_set_height(previewBar, 12);
        lv_bar_set_range(previewBar, 0, 100);
        lv_bar_set_value(previewBar, pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(previewBar, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
        lv_obj_set_style_bg_color(previewBar, color_ok, LV_PART_INDICATOR);
        lv_obj_set_style_radius(previewBar, 6, LV_PART_MAIN);
        lv_obj_set_style_radius(previewBar, 6, LV_PART_INDICATOR);

        // Reset button
        lv_obj_t *resetBtn = lv_btn_create(g_ui.detailContent);
        lv_obj_set_size(resetBtn, 120, 40);
        lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_radius(resetBtn, 10, 0);
        lv_obj_set_style_pad_top(resetBtn, 20, 0);
        lv_obj_t *resetLbl = lv_label_create(resetBtn);
        lv_label_set_text(resetLbl, "Reset");
        lv_obj_set_style_text_color(resetLbl, color_text, 0);
        lv_obj_center(resetLbl);
        lv_obj_add_event_cb(resetBtn, [](lv_event_t *)
                            {
            cfg.displayBrightness = DEFAULT_DISPLAY_BRIGHTNESS;
            cfg.brightness = DEFAULT_LED_BRIGHTNESS;
            if (g_ui.brightnessSlider)
                lv_slider_set_value(g_ui.brightnessSlider, DEFAULT_DISPLAY_BRIGHTNESS, LV_ANIM_ON);
            if (g_ui.brightnessValue)
                lv_label_set_text(g_ui.brightnessValue, "50%");
            if (g_hooks.setBrightness)
                g_hooks.setBrightness(DEFAULT_DISPLAY_BRIGHTNESS);
            strip.setBrightness(DEFAULT_LED_BRIGHTNESS);
            strip.clear();
            strip.show();
            saveConfig(); }, LV_EVENT_CLICKED, nullptr);

        update_status_icons();
        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    // =======================================================================
    // WIFI SCREEN - With scan functionality
    // =======================================================================
    void on_wifi_rescan(lv_event_t *e)
    {
        LV_UNUSED(e);
        if (g_ui.wifiList)
        {
            lv_label_set_text(g_ui.wifiList, "\n" LV_SYMBOL_REFRESH " Scanning...");
        }
        start_wifi_scan();
    }
    
    void open_wifi()
    {
        g_ui.detailContent = make_detail_base("WiFi");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);
        g_state.inWifiScreen = true;

        // Connection status at top
        lv_obj_t *info = lv_label_create(g_ui.detailContent);
        if (g_state.lastWifi.staConnected)
        {
            String txt = LV_SYMBOL_OK " Connected to:\n" + g_state.lastWifi.currentSsid;
            txt += "\nIP: " + g_state.lastWifi.staIp;
            lv_label_set_text(info, txt.c_str());
            lv_obj_set_style_text_color(info, color_ok, 0);
        }
        else if (g_state.lastWifi.apActive)
        {
            lv_label_set_text(info, LV_SYMBOL_WIFI " AP Mode Active\nSSID: ShiftLight\nIP: 192.168.4.1");
            lv_obj_set_style_text_color(info, color_text, 0);
        }
        else
        {
            lv_label_set_text(info, LV_SYMBOL_CLOSE " No connection");
            lv_obj_set_style_text_color(info, color_error, 0);
        }
        lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);

        // Scan results list
        g_ui.wifiList = lv_label_create(g_ui.detailContent);
        lv_obj_add_style(g_ui.wifiList, &styleMuted, 0);
        lv_label_set_text(g_ui.wifiList, "\n" LV_SYMBOL_REFRESH " Scanning...");
        lv_obj_set_style_pad_top(g_ui.wifiList, 16, 0);
        lv_obj_set_style_text_align(g_ui.wifiList, LV_TEXT_ALIGN_CENTER, 0);

        // Rescan button
        lv_obj_t *rescanBtn = lv_btn_create(g_ui.detailContent);
        lv_obj_set_size(rescanBtn, 120, 40);
        lv_obj_set_style_bg_color(rescanBtn, color_card_accent, 0);
        lv_obj_set_style_radius(rescanBtn, 10, 0);
        lv_obj_set_style_pad_top(rescanBtn, 16, 0);
        lv_obj_t *rescanLbl = lv_label_create(rescanBtn);
        lv_label_set_text(rescanLbl, LV_SYMBOL_REFRESH " Rescan");
        lv_obj_set_style_text_color(rescanLbl, color_text, 0);
        lv_obj_center(rescanLbl);
        lv_obj_add_event_cb(rescanBtn, on_wifi_rescan, LV_EVENT_CLICKED, nullptr);

        // Start initial scan when screen opens
        start_wifi_scan();

        update_status_icons();
        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    // =======================================================================
    // BLUETOOTH SCREEN - With scan functionality
    // =======================================================================
    void on_ble_rescan(lv_event_t *e)
    {
        LV_UNUSED(e);
        if (g_ui.bleList)
        {
            lv_label_set_text(g_ui.bleList, "\n" LV_SYMBOL_REFRESH " Scanning...");
        }
        start_ble_scan();
    }
    
    void open_ble()
    {
        g_ui.detailContent = make_detail_base("Bluetooth");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);
        g_state.inBleScreen = true;

        // Connection status
        lv_obj_t *status = lv_label_create(g_ui.detailContent);
        if (g_state.bleConnected)
        {
            lv_label_set_text(status, LV_SYMBOL_OK " OBD Connected");
            lv_obj_set_style_text_color(status, color_card_accent, 0);
        }
        else if (g_state.bleConnecting)
        {
            lv_label_set_text(status, LV_SYMBOL_REFRESH " Connecting...");
            lv_obj_set_style_text_color(status, color_warn, 0);
        }
        else
        {
            lv_label_set_text(status, LV_SYMBOL_CLOSE " Not connected");
            lv_obj_set_style_text_color(status, color_muted, 0);
        }
        lv_obj_set_style_text_font(status, &lv_font_montserrat_24, 0);

        // Device list
        g_ui.bleList = lv_label_create(g_ui.detailContent);
        lv_obj_add_style(g_ui.bleList, &styleMuted, 0);
        
        // If already connected, don't auto-scan
        if (g_state.bleConnected)
        {
            lv_label_set_text(g_ui.bleList, "\nOBD dongle active\nTap Rescan to find others");
        }
        else
        {
            lv_label_set_text(g_ui.bleList, "\n" LV_SYMBOL_REFRESH " Scanning...");
            start_ble_scan();
        }
        lv_obj_set_style_pad_top(g_ui.bleList, 16, 0);
        lv_obj_set_style_text_align(g_ui.bleList, LV_TEXT_ALIGN_CENTER, 0);

        // Rescan button
        lv_obj_t *rescanBtn = lv_btn_create(g_ui.detailContent);
        lv_obj_set_size(rescanBtn, 120, 40);
        lv_obj_set_style_bg_color(rescanBtn, color_card_accent, 0);
        lv_obj_set_style_radius(rescanBtn, 10, 0);
        lv_obj_set_style_pad_top(rescanBtn, 16, 0);
        lv_obj_t *rescanLbl = lv_label_create(rescanBtn);
        lv_label_set_text(rescanLbl, LV_SYMBOL_REFRESH " Rescan");
        lv_obj_set_style_text_color(rescanLbl, color_text, 0);
        lv_obj_center(rescanLbl);
        lv_obj_add_event_cb(rescanBtn, on_ble_rescan, LV_EVENT_CLICKED, nullptr);

        update_status_icons();
        lv_disp_load_scr(g_ui.detail);
        g_state.inDetail = true;
    }

    // =======================================================================
    // LED TEST SCREEN - Run LED animations
    // =======================================================================
    void on_led_test_sweep(lv_event_t *e)
    {
        LV_UNUSED(e);
        g_state.ledTestMode = LedTestMode::Sweep;
        g_state.ledTestStartMs = millis();
        g_testActive = true;
        g_testStartMs = millis();
        g_testMaxRpm = 8000;
    }
    
    void on_led_test_green(lv_event_t *e)
    {
        LV_UNUSED(e);
        g_state.ledTestMode = LedTestMode::AllGreen;
        for (int i = 0; i < NUM_LEDS; i++)
        {
            strip.setPixelColor(i, strip.Color(0, 255, 0));
        }
        strip.show();
    }
    
    void on_led_test_red(lv_event_t *e)
    {
        LV_UNUSED(e);
        g_state.ledTestMode = LedTestMode::AllRed;
        for (int i = 0; i < NUM_LEDS; i++)
        {
            strip.setPixelColor(i, strip.Color(255, 0, 0));
        }
        strip.show();
    }
    
    void on_led_restore(lv_event_t *e)
    {
        LV_UNUSED(e);
        g_state.ledTestMode = LedTestMode::None;
        g_testActive = false;
        strip.clear();
        strip.show();
        updateRpmBar(g_currentRpm);
    }
    
    void open_led_test()
    {
        g_ui.detailContent = make_detail_base("LED Test");
        g_ui.detail = lv_obj_get_parent(g_ui.detailContent);
        g_state.inLedTest = true;

        // Info text
        lv_obj_t *info = lv_label_create(g_ui.detailContent);
        lv_label_set_text(info, "Test LED Bar Animations");
        lv_obj_set_style_text_color(info, color_text, 0);
        lv_obj_set_style_text_font(info, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_bottom(info, 20, 0);

        // Sweep Test button
        lv_obj_t *sweepBtn = lv_btn_create(g_ui.detailContent);
        lv_obj_set_size(sweepBtn, 180, 44);
        lv_obj_set_style_bg_color(sweepBtn, color_card_accent, 0);
        lv_obj_set_style_radius(sweepBtn, 12, 0);
        lv_obj_t *sweepLbl = lv_label_create(sweepBtn);
        lv_label_set_text(sweepLbl, LV_SYMBOL_PLAY " Sweep Test");
        lv_obj_set_style_text_color(sweepLbl, color_text, 0);
        lv_obj_center(sweepLbl);
        lv_obj_add_event_cb(sweepBtn, on_led_test_sweep, LV_EVENT_CLICKED, nullptr);

        // All Green button
        lv_obj_t *greenBtn = lv_btn_create(g_ui.detailContent);
        lv_obj_set_size(greenBtn, 180, 44);
        lv_obj_set_style_bg_color(greenBtn, color_ok, 0);
        lv_obj_set_style_radius(greenBtn, 12, 0);
        lv_obj_t *greenLbl = lv_label_create(greenBtn);
        lv_label_set_text(greenLbl, "All Green");
        lv_obj_set_style_text_color(greenLbl, lv_color_hex(0x000000), 0);
        lv_obj_center(greenLbl);
        lv_obj_add_event_cb(greenBtn, on_led_test_green, LV_EVENT_CLICKED, nullptr);

        // All Red button
        lv_obj_t *redBtn = lv_btn_create(g_ui.detailContent);
        lv_obj_set_size(redBtn, 180, 44);
        lv_obj_set_style_bg_color(redBtn, color_error, 0);
        lv_obj_set_style_radius(redBtn, 12, 0);
        lv_obj_t *redLbl = lv_label_create(redBtn);
        lv_label_set_text(redLbl, "All Red");
        lv_obj_set_style_text_color(redLbl, color_text, 0);
        lv_obj_center(redLbl);
        lv_obj_add_event_cb(redBtn, on_led_test_red, LV_EVENT_CLICKED, nullptr);

        // Restore Defaults button
        lv_obj_t *restoreBtn = lv_btn_create(g_ui.detailContent);
        lv_obj_set_size(restoreBtn, 180, 44);
        lv_obj_set_style_bg_color(restoreBtn, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_radius(restoreBtn, 12, 0);
        lv_obj_t *restoreLbl = lv_label_create(restoreBtn);
        lv_label_set_text(restoreLbl, LV_SYMBOL_HOME " Restore");
        lv_obj_set_style_text_color(restoreLbl, color_text, 0);
        lv_obj_center(restoreLbl);
        lv_obj_add_event_cb(restoreBtn, on_led_restore, LV_EVENT_CLICKED, nullptr);

        update_status_icons();
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

        update_status_icons();
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
        case CardScreen::LedTest:
            open_led_test();
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
    
    // Check if tutorial was already seen
    g_state.tutorialVisible = !cfg.uiTutorialSeen;
    
    apply_styles();
    build_home(disp);
    lv_disp_load_scr(g_ui.root);
}

void ui_s3_loop(const WifiStatus &wifiStatus, bool bleConnected, bool bleConnecting)
{
    g_state.lastWifi = wifiStatus;
    g_state.bleConnected = bleConnected;
    g_state.bleConnecting = bleConnecting;
    
    // Update LED bar active status (true if RPM > 0 or test active)
    g_state.ledBarActive = (g_state.rpm > 0) || g_testActive;

    update_status_icons();

    // Update data view if active
    if (g_state.inDataView)
    {
        update_data_values();
    }

    // Update WiFi scan results on WiFi screen
    if (g_state.inWifiScreen && g_ui.wifiList && !g_state.wifiScanInProgress)
    {
        if (!wifiStatus.scanResults.empty())
        {
            String lines = "\n" LV_SYMBOL_WIFI " Nearby Networks:\n";
            size_t count = std::min<size_t>(wifiStatus.scanResults.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                lines += wifiStatus.scanResults[i].ssid;
                lines += " (";
                lines += wifiStatus.scanResults[i].rssi;
                lines += " dBm)\n";
            }
            lv_label_set_text(g_ui.wifiList, lines.c_str());
            g_state.wifiScanInProgress = false;
        }
        else if (!wifiStatus.scanRunning)
        {
            lv_label_set_text(g_ui.wifiList, "\nNo networks found\nTap Rescan to try again");
        }
    }

    // Update BLE scan results on BLE screen
    if (g_state.inBleScreen && g_ui.bleList && !g_state.bleScanInProgress)
    {
        const auto &res = getBleScanResults();
        if (!res.empty())
        {
            String lines = "\n" LV_SYMBOL_BLUETOOTH " Nearby Devices:\n";
            size_t count = std::min<size_t>(res.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                lines += res[i].name.length() > 0 ? res[i].name : "(unknown)";
                lines += "\n";
            }
            lv_label_set_text(g_ui.bleList, lines.c_str());
            g_state.bleScanInProgress = false;
        }
        else if (!isBleScanRunning())
        {
            lv_label_set_text(g_ui.bleList, "\nNo devices found\nTap Rescan to try again");
        }
    }
    
    // Check if scans have completed
    if (g_state.wifiScanInProgress && !wifiStatus.scanRunning)
    {
        g_state.wifiScanInProgress = false;
    }
    if (g_state.bleScanInProgress && !isBleScanRunning())
    {
        g_state.bleScanInProgress = false;
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

void ui_s3_set_oil_temp(int temp)
{
    g_state.oilTemp = temp;
    if (g_state.inDataView)
    {
        update_data_values();
    }
}

void ui_s3_show_led_test()
{
    // Navigate to LED test screen using defined constant
    open_detail(CARDS[LED_TEST_CARD_INDEX]);
}
