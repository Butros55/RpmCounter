#include "ui_s3_main.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
    enum class HomeAction : uint8_t
    {
        Display = 0,
        Source,
        Session
    };

    enum class WebTarget : uint8_t
    {
        Overview = 0,
        Display,
        Telemetry
    };

    struct ButtonRef
    {
        lv_obj_t *button = nullptr;
        lv_obj_t *label = nullptr;
    };

    struct ValueTile
    {
        lv_obj_t *panel = nullptr;
        lv_obj_t *title = nullptr;
        lv_obj_t *value = nullptr;
        lv_obj_t *unit = nullptr;
    };

    struct StatRef
    {
        lv_obj_t *value = nullptr;
        lv_obj_t *caption = nullptr;
    };

    struct UiRefs
    {
        lv_disp_t *disp = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *homeLayer = nullptr;
        int width = 0;
        int height = 0;
        bool compact = false;

        std::array<lv_obj_t *, 16> shiftSegments{};
        std::array<lv_obj_t *, SIDE_LED_MAX_COUNT_PER_SIDE> sideLeftSegments{};
        std::array<lv_obj_t *, SIDE_LED_MAX_COUNT_PER_SIDE> sideRightSegments{};

        lv_obj_t *sourcePanel = nullptr;
        lv_obj_t *sourceTitle = nullptr;
        lv_obj_t *sourceValue = nullptr;
        lv_obj_t *sourceMeta = nullptr;

        lv_obj_t *deltaPanel = nullptr;
        lv_obj_t *deltaValue = nullptr;
        lv_obj_t *deltaCaption = nullptr;

        lv_obj_t *sessionPanel = nullptr;
        std::array<StatRef, 3> sessionStats{};

        lv_obj_t *sensorPanel = nullptr;
        std::array<ValueTile, 6> sensorTiles{};

        lv_obj_t *centerPanel = nullptr;
        lv_obj_t *centerStatus = nullptr;
        lv_obj_t *centerGear = nullptr;
        lv_obj_t *centerRpm = nullptr;
        lv_obj_t *centerSpeed = nullptr;
        lv_obj_t *centerBarTrack = nullptr;
        lv_obj_t *centerBarFill = nullptr;

        lv_obj_t *lapsPanel = nullptr;
        lv_obj_t *lapsTitle = nullptr;
        lv_obj_t *lapsPredicted = nullptr;
        lv_obj_t *lapsPredictedCaption = nullptr;
        lv_obj_t *lapsLast = nullptr;
        lv_obj_t *lapsLastCaption = nullptr;
        lv_obj_t *lapsBest = nullptr;
        lv_obj_t *lapsBestCaption = nullptr;

        std::array<ValueTile, 5> controlTiles{};

        lv_obj_t *fuelPanel = nullptr;
        lv_obj_t *fuelTitle = nullptr;
        std::array<StatRef, 3> fuelStats{};

        lv_obj_t *compactStatusPanel = nullptr;
        lv_obj_t *compactStatusTitle = nullptr;
        lv_obj_t *compactStatusValue = nullptr;
        lv_obj_t *compactSessionPanel = nullptr;
        lv_obj_t *compactSessionTitle = nullptr;
        lv_obj_t *compactSessionValue = nullptr;
        lv_obj_t *compactHero = nullptr;
        lv_obj_t *compactHeroTitle = nullptr;
        lv_obj_t *compactHeroValue = nullptr;
        lv_obj_t *compactHeroUnit = nullptr;
        lv_obj_t *compactHeroMeta = nullptr;
        std::array<ValueTile, 3> compactTiles{};
        std::array<ButtonRef, 3> actionButtons{};

        lv_obj_t *footerHint = nullptr;
        std::array<lv_obj_t *, 3> homeActionTargets{};

        lv_obj_t *focusOverlay = nullptr;
        lv_obj_t *focusCard = nullptr;
        lv_obj_t *focusStatus = nullptr;
        lv_obj_t *focusValue = nullptr;
        lv_obj_t *focusUnit = nullptr;
        lv_obj_t *focusMeta = nullptr;
        lv_obj_t *focusHint = nullptr;

        lv_obj_t *sourceOverlay = nullptr;
        lv_obj_t *sourceCard = nullptr;
        lv_obj_t *sourceBack = nullptr;
        lv_obj_t *sourcePickerTitle = nullptr;
        lv_obj_t *sourcePickerSubtitle = nullptr;
        std::array<ButtonRef, 3> sourceOptions{};
        lv_obj_t *sourceHint = nullptr;

        lv_obj_t *webOverlay = nullptr;
        lv_obj_t *webCard = nullptr;
        lv_obj_t *webBack = nullptr;
        lv_obj_t *webTitle = nullptr;
        lv_obj_t *webSubtitle = nullptr;
        lv_obj_t *webQr = nullptr;
        lv_obj_t *webUrl = nullptr;
        lv_obj_t *webHint = nullptr;

        lv_obj_t *logoOverlay = nullptr;
        lv_obj_t *logoCard = nullptr;
        lv_obj_t *logoLabel = nullptr;
        lv_obj_t *logoSubtitle = nullptr;
    };

    struct UiState
    {
        UiRuntimeState runtime{};
        UiDisplayHooks hooks{};
        int selectedAction = 0;
        UiScreenId activeScreen = UiScreenId::Home;
        UiDisplayFocusMetric fullscreenMetric = UiDisplayFocusMetric::Rpm;
        WebTarget webTarget = WebTarget::Overview;
        UiScreenId webReturnScreen = UiScreenId::Home;
        uint32_t logoUntilMs = 0;
        std::string overlayTitle = "ShiftLight";
        std::string overlaySubtitle;
        uint32_t overlayAccent = 0x4FCBFF;
    };

    UiRefs g_ui;
    UiState g_state;

    lv_style_t styleScreen;
    lv_style_t stylePanel;
    lv_style_t styleOverlay;
    lv_style_t styleOverlayCard;
    lv_style_t styleHintLabel;
    bool g_stylesInitialized = false;

    constexpr int kActionCount = 3;
    constexpr int kShiftSegmentCount = 16;
    constexpr int kSideLedCount = static_cast<int>(SIDE_LED_MAX_COUNT_PER_SIDE);
    constexpr uint32_t kLogoDurationMs = 1400;
    constexpr uint32_t kUsbBridgeWebPort = 8765;

    const lv_color_t colorBg = lv_color_hex(0x030405);
    const lv_color_t colorBgSoft = lv_color_hex(0x090C10);
    const lv_color_t colorPanel = lv_color_hex(0x050709);
    const lv_color_t colorPanelAlt = lv_color_hex(0x0C1017);
    const lv_color_t colorText = lv_color_hex(0xF5F7FB);
    const lv_color_t colorMuted = lv_color_hex(0x96A0AF);
    const lv_color_t colorWhiteLine = lv_color_hex(0xEDF1F7);
    const lv_color_t colorGreen = lv_color_hex(0x4AF07B);
    const lv_color_t colorYellow = lv_color_hex(0xFFD74A);
    const lv_color_t colorOrange = lv_color_hex(0xFF8C38);
    const lv_color_t colorRed = lv_color_hex(0xFF5A48);
    const lv_color_t colorBlue = lv_color_hex(0x5AAEFF);
    const lv_color_t colorPurple = lv_color_hex(0xD477FF);

    void reset_style(lv_style_t &style)
    {
        if (g_stylesInitialized)
        {
            lv_style_reset(&style);
        }
        lv_style_init(&style);
    }

    int wrap_index(int index, int count)
    {
        return (index % count + count) % count;
    }

    uint32_t now_ms()
    {
        return lv_tick_get();
    }

    bool compact_mode()
    {
        return g_ui.compact;
    }

    bool show_shift_strip_enabled()
    {
        return g_state.runtime.settings.showShiftStrip;
    }

    std::string gear_text(int gear)
    {
        return gear <= 0 ? "N" : std::to_string(gear);
    }

    std::string telemetry_preference_text(UiTelemetryPreference preference)
    {
        switch (preference)
        {
        case UiTelemetryPreference::Obd:
            return "OBD";
        case UiTelemetryPreference::SimHub:
            return "SIM / PC";
        case UiTelemetryPreference::Auto:
        default:
            return "AUTO";
        }
    }

    std::string telemetry_source_text(UiTelemetrySource source)
    {
        switch (source)
        {
        case UiTelemetrySource::Esp32Obd:
            return "OBD";
        case UiTelemetrySource::Simulator:
            return "SIM";
        case UiTelemetrySource::SimHubNetwork:
            return "SIMHUB";
        case UiTelemetrySource::UsbBridge:
        default:
            return "USB";
        }
    }

    std::string metric_title(UiDisplayFocusMetric metric)
    {
        switch (metric)
        {
        case UiDisplayFocusMetric::Gear:
            return "GEAR";
        case UiDisplayFocusMetric::Speed:
            return "SPEED";
        case UiDisplayFocusMetric::Rpm:
        default:
            return "RPM";
        }
    }

    std::string metric_value(UiDisplayFocusMetric metric)
    {
        switch (metric)
        {
        case UiDisplayFocusMetric::Gear:
            return gear_text(g_state.runtime.gear);
        case UiDisplayFocusMetric::Speed:
            return std::to_string(g_state.runtime.speedKmh);
        case UiDisplayFocusMetric::Rpm:
        default:
            return std::to_string(g_state.runtime.rpm);
        }
    }

    std::string metric_unit(UiDisplayFocusMetric metric)
    {
        switch (metric)
        {
        case UiDisplayFocusMetric::Gear:
            return "GEAR";
        case UiDisplayFocusMetric::Speed:
            return "KM/H";
        case UiDisplayFocusMetric::Rpm:
        default:
            return "RPM";
        }
    }

    lv_color_t metric_color(UiDisplayFocusMetric metric, bool shift)
    {
        if (shift)
        {
            return colorRed;
        }

        switch (metric)
        {
        case UiDisplayFocusMetric::Gear:
            return colorText;
        case UiDisplayFocusMetric::Speed:
            return colorBlue;
        case UiDisplayFocusMetric::Rpm:
        default:
            return colorYellow;
        }
    }

    lv_color_t source_color(UiTelemetrySource source, bool stale)
    {
        if (stale)
        {
            return colorRed;
        }

        switch (source)
        {
        case UiTelemetrySource::Esp32Obd:
            return colorGreen;
        case UiTelemetrySource::Simulator:
            return colorYellow;
        case UiTelemetrySource::SimHubNetwork:
            return colorPurple;
        case UiTelemetrySource::UsbBridge:
        default:
            return colorBlue;
        }
    }

    uint32_t source_accent_rgb(UiTelemetrySource source, bool stale)
    {
        if (stale)
        {
            return 0xFF5A48;
        }

        switch (source)
        {
        case UiTelemetrySource::Esp32Obd:
            return 0x4AF07B;
        case UiTelemetrySource::Simulator:
            return 0xFFD74A;
        case UiTelemetrySource::SimHubNetwork:
            return 0xD477FF;
        case UiTelemetrySource::UsbBridge:
        default:
            return 0x5AAEFF;
        }
    }

    std::string wifi_state_short()
    {
        if (g_state.runtime.staConnected)
        {
            return g_state.runtime.currentSsid.empty() ? "LIVE" : g_state.runtime.currentSsid;
        }
        if (g_state.runtime.staConnecting)
        {
            return "CONNECT";
        }
        if (g_state.runtime.apActive)
        {
            return g_state.runtime.apClients > 0 ? "AP LIVE" : "AP READY";
        }
        return "OFF";
    }

    std::string ble_state_short()
    {
        if (g_state.runtime.bleConnected)
        {
            return "LIVE";
        }
        if (g_state.runtime.bleConnecting)
        {
            return "SCAN";
        }
        return "OFF";
    }

    std::string usb_state_short()
    {
        switch (g_state.runtime.usbState)
        {
        case UiUsbState::Live:
            return "LIVE";
        case UiUsbState::WaitingForBridge:
        case UiUsbState::WaitingForData:
            return "WAIT";
        case UiUsbState::Error:
            return "ERROR";
        case UiUsbState::Disconnected:
            return "OFF";
        case UiUsbState::Disabled:
        default:
            return "DISABLED";
        }
    }

    std::string simhub_state_short()
    {
        switch (g_state.runtime.simHubState)
        {
        case UiSimHubState::Live:
            return "LIVE";
        case UiSimHubState::WaitingForHost:
        case UiSimHubState::WaitingForNetwork:
        case UiSimHubState::WaitingForData:
            return "WAIT";
        case UiSimHubState::Error:
            return "ERROR";
        case UiSimHubState::Disabled:
        default:
            return "DISABLED";
        }
    }

    std::string source_status_value()
    {
        std::string text = telemetry_source_text(g_state.runtime.telemetrySource);
        text += g_state.runtime.telemetryStale ? " WAIT" : " LIVE";
        if (g_state.runtime.telemetryUsingFallback)
        {
            text += " FB";
        }
        return text;
    }

    std::string source_status_meta()
    {
        std::string meta = g_state.runtime.currentSsid.empty() ? wifi_state_short() : g_state.runtime.currentSsid;
        meta += "  |  ";
        meta += ble_state_short();
        return meta;
    }

    std::string secondary_status_text()
    {
        if (g_state.runtime.telemetryStale)
        {
            if (!g_state.runtime.usbError.empty())
            {
                return g_state.runtime.usbError;
            }
            if (!g_state.runtime.staLastError.empty())
            {
                return g_state.runtime.staLastError;
            }
            return "Waiting for live telemetry";
        }

        if (g_state.runtime.shift)
        {
            return "SHIFT";
        }
        if (!g_state.runtime.simHubEndpoint.empty() && g_state.runtime.telemetrySource == UiTelemetrySource::SimHubNetwork)
        {
            return "SIM LIVE";
        }
        if (!g_state.runtime.usbHost.empty() && g_state.runtime.telemetrySource == UiTelemetrySource::UsbBridge)
        {
            return "USB LIVE";
        }
        if (g_state.runtime.telemetrySource == UiTelemetrySource::Esp32Obd)
        {
            return "OBD LIVE";
        }
        return "TRACK READY";
    }

    std::string action_label(HomeAction action)
    {
        switch (action)
        {
        case HomeAction::Display:
            return "Display";
        case HomeAction::Source:
            return "Source";
        case HomeAction::Session:
        default:
            return "Session";
        }
    }

    float active_led_ratio()
    {
        const int startRpm = std::max(0, g_state.runtime.ledStartRpm);
        const int maxRpm = std::max(startRpm + 1, g_state.runtime.ledMaxRpm);
        return std::clamp((static_cast<float>(g_state.runtime.rpm) - static_cast<float>(startRpm)) /
                              static_cast<float>(std::max(1, maxRpm - startRpm)),
                          0.0f,
                          1.0f);
    }

    void set_object_y(lv_obj_t *obj, int y)
    {
        if (obj)
        {
            lv_obj_set_y(obj, y);
        }
    }

    void set_object_pos(lv_obj_t *obj, int x, int y)
    {
        if (obj)
        {
            lv_obj_set_pos(obj, x, y);
        }
    }

    int active_side_led_count()
    {
        const uint8_t configured = g_state.runtime.sideLedConfig.ledCountPerSide;
        return std::clamp(static_cast<int>(configured),
                          static_cast<int>(SIDE_LED_MIN_COUNT_PER_SIDE),
                          static_cast<int>(SIDE_LED_MAX_COUNT_PER_SIDE));
    }

    int side_led_width()
    {
        return compact_mode() ? 8 : 10;
    }

    int side_led_height()
    {
        const int count = active_side_led_count();
        if (count >= 14)
        {
            return compact_mode() ? 10 : 12;
        }
        if (count >= 10)
        {
            return compact_mode() ? 12 : 14;
        }
        if (count >= 8)
        {
            return compact_mode() ? 14 : 17;
        }
        return compact_mode() ? 16 : 20;
    }

    int side_led_gap()
    {
        const int count = active_side_led_count();
        if (count >= 14)
        {
            return compact_mode() ? 3 : 4;
        }
        if (count >= 10)
        {
            return compact_mode() ? 4 : 6;
        }
        if (count >= 8)
        {
            return compact_mode() ? 5 : 8;
        }
        return compact_mode() ? 8 : 12;
    }

    std::string format_clock(uint32_t milliseconds)
    {
        if (milliseconds == 0)
        {
            return "--:--";
        }

        const uint32_t totalSeconds = milliseconds / 1000U;
        const uint32_t minutes = totalSeconds / 60U;
        const uint32_t seconds = totalSeconds % 60U;

        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%02lu:%02lu",
                      static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(seconds));
        return std::string(buffer);
    }

    std::string format_lap_time(uint32_t milliseconds)
    {
        if (milliseconds == 0)
        {
            return "--:--.---";
        }

        const uint32_t minutes = milliseconds / 60000U;
        const uint32_t seconds = (milliseconds / 1000U) % 60U;
        const uint32_t millis = milliseconds % 1000U;

        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%02lu:%02lu.%03lu",
                      static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(seconds),
                      static_cast<unsigned long>(millis));
        return std::string(buffer);
    }

    std::string format_signed_delta(float seconds)
    {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%+.3f", static_cast<double>(seconds));
        return std::string(buffer);
    }

    std::string format_one_decimal(float value)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(value));
        return std::string(buffer);
    }

    std::string format_two_decimals(float value)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
        return std::string(buffer);
    }

    UiSessionData build_derived_session_data()
    {
        UiSessionData data{};
        if (g_state.runtime.telemetryStale && g_state.runtime.rpm <= 0 && g_state.runtime.speedKmh <= 0)
        {
            return data;
        }

        const float rpmRatio = std::clamp((static_cast<float>(g_state.runtime.rpm) - 900.0f) / 6300.0f, 0.0f, 1.0f);
        const float speedRatio = std::clamp(static_cast<float>(g_state.runtime.speedKmh) / 220.0f, 0.0f, 1.0f);
        const float throttle = std::clamp(g_state.runtime.throttle, 0.0f, 1.0f);
        const float timeS = static_cast<float>(now_ms()) / 1000.0f;

        data.hasAnyData = true;
        data.hasDelta = true;
        data.deltaSeconds = 0.18f * std::sin(timeS * 0.9f) + (0.45f - speedRatio) * 0.32f;
        data.hasPredictedLap = true;
        data.predictedLapMs = static_cast<uint32_t>(std::clamp(90500.0f - speedRatio * 12000.0f - throttle * 2600.0f, 76000.0f, 120000.0f));
        data.hasLastLap = true;
        data.lastLapMs = static_cast<uint32_t>(std::max(60000.0f, data.predictedLapMs + 1150.0f + 650.0f * std::sin(timeS * 0.55f)));
        data.hasBestLap = true;
        data.bestLapMs = static_cast<uint32_t>(std::max(60000.0f, data.predictedLapMs - 420.0f - 260.0f * std::cos(timeS * 0.45f)));
        data.hasSessionClock = true;
        data.sessionClockMs = 17U * 60U * 1000U + (now_ms() % (47U * 60U * 1000U));
        data.hasPosition = true;
        data.position = std::max(1, 5 - static_cast<int>(std::round(speedRatio * 2.0f)));
        data.hasTotalPositions = true;
        data.totalPositions = 21;
        data.hasLap = true;
        data.lap = 1 + static_cast<int>((timeS / std::max(1.0f, static_cast<float>(data.predictedLapMs) / 1000.0f))) % 12;
        data.hasTotalLaps = true;
        data.totalLaps = 12;
        data.hasFuelLiters = true;
        data.fuelLiters = std::max(2.4f, 12.0f - timeS * 0.015f);
        data.hasFuelAvgPerLap = true;
        data.fuelAvgPerLap = 2.85f;
        data.hasFuelLapsRemaining = true;
        data.fuelLapsRemaining = data.fuelLiters / data.fuelAvgPerLap;
        data.hasOilTemp = true;
        data.oilTempC = 77.0f + throttle * 10.0f + speedRatio * 3.5f;
        data.hasOilPressure = true;
        data.oilPressureBar = 2.1f + throttle * 2.3f + rpmRatio * 1.0f;
        data.hasOilLevel = true;
        data.oilLevel = 5.0f;
        data.hasFuelPressure = true;
        data.fuelPressureBar = 2.3f + throttle * 0.9f;
        data.hasWaterTemp = true;
        data.waterTempC = 77.0f + throttle * 8.0f + speedRatio * 2.5f;
        data.hasBatteryVolts = true;
        data.batteryVolts = 14.0f + (g_state.runtime.usbConnected ? 0.2f : 0.0f) + (g_state.runtime.staConnected ? 0.1f : 0.0f);
        data.hasTractionControl = true;
        data.tractionControl = 3;
        data.hasTractionCut = true;
        data.tractionCut = g_state.runtime.shift ? 1 : 0;
        data.hasAbs = true;
        data.absLevel = 4;
        data.hasBrakeBias = true;
        data.brakeBias = 53.0f + 0.2f * std::sin(timeS * 0.4f);
        data.hasEngineMap = true;
        data.engineMap = g_state.runtime.telemetrySource == UiTelemetrySource::SimHubNetwork ? 1 : 2;
        return data;
    }

    UiSessionData build_session_display_data()
    {
        if (g_state.runtime.session.hasAnyData)
        {
            return g_state.runtime.session;
        }

        if (g_state.runtime.telemetrySource == UiTelemetrySource::SimHubNetwork && !g_state.runtime.telemetryUsingFallback)
        {
            return UiSessionData{};
        }

        return build_derived_session_data();
    }

    std::string format_position_text(const UiSessionData &data)
    {
        if (data.hasPosition && data.hasTotalPositions)
        {
            return std::to_string(data.position) + "/" + std::to_string(data.totalPositions);
        }
        if (data.hasPosition)
        {
            return std::to_string(data.position);
        }
        return "--";
    }

    std::string format_lap_text(const UiSessionData &data)
    {
        if (data.hasLap && data.hasTotalLaps)
        {
            return std::to_string(data.lap) + "/" + std::to_string(data.totalLaps);
        }
        if (data.hasLap)
        {
            return std::to_string(data.lap);
        }
        return "--";
    }

    void set_label_text(lv_obj_t *label, const std::string &text)
    {
        if (label)
        {
            lv_label_set_text(label, text.c_str());
        }
    }

    void set_panel_accent(lv_obj_t *obj, uint32_t accentRgb)
    {
        if (!obj)
        {
            return;
        }

        const lv_color_t accent = lv_color_hex(accentRgb);
        lv_obj_set_style_border_color(obj, accent, 0);
        lv_obj_set_style_shadow_color(obj, accent, 0);
        lv_obj_set_style_shadow_width(obj, 14, 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
    }

    void set_selected_outline(lv_obj_t *obj, bool selected)
    {
        if (!obj)
        {
            return;
        }

        lv_obj_set_style_outline_width(obj, selected ? 2 : 0, 0);
        lv_obj_set_style_outline_pad(obj, 3, 0);
        lv_obj_set_style_outline_color(obj, colorText, 0);
        lv_obj_set_style_outline_opa(obj, selected ? LV_OPA_70 : LV_OPA_0, 0);
    }

    lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h)
    {
        lv_obj_t *panel = lv_obj_create(parent);
        lv_obj_remove_style_all(panel);
        lv_obj_add_style(panel, &stylePanel, 0);
        lv_obj_set_pos(panel, x, y);
        lv_obj_set_size(panel, w, h);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        return panel;
    }

    lv_obj_t *make_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
    {
        lv_obj_t *label = lv_label_create(parent);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, color, 0);
        lv_obj_set_style_text_align(label, align, 0);
        return label;
    }

    void set_single_line_width(lv_obj_t *label, int width, lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
    {
        if (!label)
        {
            return;
        }

        lv_obj_set_width(label, width);
        lv_label_set_long_mode(label, mode);
    }

    ValueTile make_value_tile(lv_obj_t *parent, int x, int y, int w, int h, const char *title, uint32_t accentRgb, const lv_font_t *valueFont)
    {
        ValueTile tile{};
        tile.panel = make_panel(parent, x, y, w, h);
        set_panel_accent(tile.panel, accentRgb);

        if (h <= 54)
        {
            tile.title = make_text(tile.panel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_LEFT);
            set_single_line_width(tile.title, w - 12, LV_LABEL_LONG_DOT);
            lv_label_set_text(tile.title, title);
            lv_obj_set_pos(tile.title, 2, -2);

            tile.value = make_text(tile.panel, valueFont, colorText, LV_TEXT_ALIGN_LEFT);
            set_single_line_width(tile.value, w - 52);
            lv_obj_set_pos(tile.value, 2, 18);

            tile.unit = make_text(tile.panel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_RIGHT);
            set_single_line_width(tile.unit, 34);
            lv_obj_set_pos(tile.unit, w - 46, 22);
        }
        else if (h <= 72)
        {
            tile.title = make_text(tile.panel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
            set_single_line_width(tile.title, w - 10, LV_LABEL_LONG_DOT);
            lv_label_set_text(tile.title, title);
            lv_obj_align(tile.title, LV_ALIGN_TOP_MID, 0, 0);

            tile.value = make_text(tile.panel, valueFont, colorText, LV_TEXT_ALIGN_CENTER);
            set_single_line_width(tile.value, w - 10);
            lv_obj_align(tile.value, LV_ALIGN_CENTER, 0, 8);

            tile.unit = make_text(tile.panel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
            set_single_line_width(tile.unit, w - 10);
            lv_obj_align(tile.unit, LV_ALIGN_BOTTOM_MID, 0, -2);
        }
        else
        {
            tile.title = make_text(tile.panel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
            set_single_line_width(tile.title, w - 12, LV_LABEL_LONG_DOT);
            lv_label_set_text(tile.title, title);
            lv_obj_align(tile.title, LV_ALIGN_TOP_MID, 0, 4);

            tile.value = make_text(tile.panel, valueFont, colorText, LV_TEXT_ALIGN_CENTER);
            set_single_line_width(tile.value, w - 12);
            lv_obj_align(tile.value, LV_ALIGN_CENTER, 0, -4);

            tile.unit = make_text(tile.panel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
            set_single_line_width(tile.unit, w - 12, LV_LABEL_LONG_DOT);
            lv_obj_align(tile.unit, LV_ALIGN_BOTTOM_MID, 0, -4);
        }

        return tile;
    }

    ButtonRef make_button(lv_obj_t *parent, int x, int y, int w, int h, const char *labelText, lv_event_cb_t callback, void *userData)
    {
        ButtonRef ref{};
        ref.button = make_panel(parent, x, y, w, h);
        lv_obj_add_flag(ref.button, LV_OBJ_FLAG_CLICKABLE);
        if (callback)
        {
            lv_obj_add_event_cb(ref.button, callback, LV_EVENT_CLICKED, userData);
        }
        ref.label = make_text(ref.button, &lv_font_montserrat_16, colorText, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(ref.label, w - 8);
        lv_label_set_text(ref.label, labelText);
        lv_obj_center(ref.label);
        return ref;
    }

    void apply_styles()
    {
        reset_style(styleScreen);
        lv_style_set_bg_color(&styleScreen, colorBg);
        lv_style_set_bg_grad_color(&styleScreen, colorBgSoft);
        lv_style_set_bg_grad_dir(&styleScreen, LV_GRAD_DIR_VER);
        lv_style_set_bg_opa(&styleScreen, LV_OPA_COVER);
        lv_style_set_pad_all(&styleScreen, 0);

        reset_style(stylePanel);
        lv_style_set_bg_color(&stylePanel, colorPanel);
        lv_style_set_bg_grad_color(&stylePanel, colorPanelAlt);
        lv_style_set_bg_grad_dir(&stylePanel, LV_GRAD_DIR_VER);
        lv_style_set_bg_opa(&stylePanel, LV_OPA_COVER);
        lv_style_set_radius(&stylePanel, 18);
        lv_style_set_border_width(&stylePanel, 2);
        lv_style_set_border_color(&stylePanel, colorWhiteLine);
        lv_style_set_border_opa(&stylePanel, LV_OPA_70);
        lv_style_set_pad_all(&stylePanel, 8);
        lv_style_set_shadow_width(&stylePanel, 12);
        lv_style_set_shadow_color(&stylePanel, colorWhiteLine);
        lv_style_set_shadow_opa(&stylePanel, LV_OPA_10);

        reset_style(styleOverlay);
        lv_style_set_bg_color(&styleOverlay, lv_color_black());
        lv_style_set_bg_opa(&styleOverlay, LV_OPA_70);
        lv_style_set_pad_all(&styleOverlay, 0);

        reset_style(styleOverlayCard);
        lv_style_set_bg_color(&styleOverlayCard, colorPanelAlt);
        lv_style_set_bg_opa(&styleOverlayCard, LV_OPA_COVER);
        lv_style_set_radius(&styleOverlayCard, 20);
        lv_style_set_border_width(&styleOverlayCard, 2);
        lv_style_set_border_color(&styleOverlayCard, colorWhiteLine);
        lv_style_set_border_opa(&styleOverlayCard, LV_OPA_80);
        lv_style_set_pad_all(&styleOverlayCard, 14);
        lv_style_set_shadow_width(&styleOverlayCard, 20);
        lv_style_set_shadow_color(&styleOverlayCard, colorWhiteLine);
        lv_style_set_shadow_opa(&styleOverlayCard, LV_OPA_10);

        reset_style(styleHintLabel);
        lv_style_set_text_color(&styleHintLabel, colorMuted);
        lv_style_set_text_font(&styleHintLabel, &lv_font_montserrat_16);

        g_stylesInitialized = true;
    }

    void sync_runtime_settings()
    {
        g_state.runtime.settings.lastMenuIndex = wrap_index(g_state.selectedAction, kActionCount);
    }

    void persist_settings()
    {
        sync_runtime_settings();
        g_state.runtime.settings.tutorialSeen = true;
        if (g_state.hooks.saveSettings)
        {
            g_state.hooks.saveSettings(g_state.runtime.settings, g_state.hooks.userData);
        }
    }

    void set_selected_action(int index)
    {
        g_state.selectedAction = wrap_index(index, kActionCount);
        sync_runtime_settings();
    }

    void set_display_focus(UiDisplayFocusMetric metric)
    {
        g_state.runtime.settings.displayFocus = metric;
        g_state.fullscreenMetric = metric;
        persist_settings();
    }

    void set_telemetry_preference(UiTelemetryPreference preference)
    {
        g_state.runtime.settings.telemetryPreference = preference;
        persist_settings();
    }

    void cycle_display_focus(int delta)
    {
        const int next = wrap_index(static_cast<int>(g_state.runtime.settings.displayFocus) + delta, 3);
        set_display_focus(static_cast<UiDisplayFocusMetric>(next));
    }

    void cycle_telemetry_preference(int delta)
    {
        const int next = wrap_index(static_cast<int>(g_state.runtime.settings.telemetryPreference) + delta, 3);
        set_telemetry_preference(static_cast<UiTelemetryPreference>(next));
    }

    void cycle_web_target(int delta)
    {
        const int next = wrap_index(static_cast<int>(g_state.webTarget) + delta, 3);
        g_state.webTarget = static_cast<WebTarget>(next);
    }

    std::string build_web_url(WebTarget target)
    {
        std::string base;
        if (!g_state.runtime.usbHost.empty() &&
            (g_state.runtime.usbBridgeConnected || g_state.runtime.telemetrySource == UiTelemetrySource::UsbBridge))
        {
            base = "http://" + g_state.runtime.usbHost + ":" + std::to_string(kUsbBridgeWebPort);
        }
        else if (!g_state.runtime.ip.empty())
        {
            base = "http://" + g_state.runtime.ip;
        }
        else if (!g_state.runtime.staIp.empty())
        {
            base = "http://" + g_state.runtime.staIp;
        }
        else if (!g_state.runtime.apIp.empty())
        {
            base = "http://" + g_state.runtime.apIp;
        }
        else
        {
            base = "http://192.168.4.1";
        }

        switch (target)
        {
        case WebTarget::Display:
            return base + "/settings#display-settings";
        case WebTarget::Telemetry:
            return base + "/settings#telemetry-settings";
        case WebTarget::Overview:
        default:
            return base + "/settings";
        }
    }

    std::string web_title(WebTarget target)
    {
        switch (target)
        {
        case WebTarget::Display:
            return "Display Settings";
        case WebTarget::Telemetry:
            return "Telemetry Settings";
        case WebTarget::Overview:
        default:
            return "Web Dashboard";
        }
    }

    std::string web_subtitle(WebTarget target)
    {
        switch (target)
        {
        case WebTarget::Display:
            return "Brightness, colors and shift-light tuning";
        case WebTarget::Telemetry:
            return "Source, USB bridge and network setup";
        case WebTarget::Overview:
        default:
            return "Scan or open the dashboard on the host PC or ESP";
        }
    }

    void show_home()
    {
        g_state.activeScreen = UiScreenId::Home;
    }

    void show_focus()
    {
        g_state.fullscreenMetric = g_state.runtime.settings.displayFocus;
        g_state.activeScreen = UiScreenId::Focus;
    }

    void show_source_picker()
    {
        g_state.activeScreen = UiScreenId::SourcePicker;
    }

    void show_web_link(WebTarget target, UiScreenId returnScreen)
    {
        g_state.webTarget = target;
        g_state.webReturnScreen = returnScreen;
        g_state.activeScreen = UiScreenId::WebLink;
    }

    void open_selected_action()
    {
        switch (static_cast<HomeAction>(wrap_index(g_state.selectedAction, kActionCount)))
        {
        case HomeAction::Display:
            show_focus();
            break;
        case HomeAction::Source:
            show_source_picker();
            break;
        case HomeAction::Session:
        default:
            show_focus();
            break;
        }
    }

    int gesture_delta()
    {
        lv_indev_t *active = lv_indev_get_act();
        if (!active)
        {
            return 0;
        }

        switch (lv_indev_get_gesture_dir(active))
        {
        case LV_DIR_LEFT:
            return 1;
        case LV_DIR_RIGHT:
            return -1;
        default:
            return 0;
        }
    }

    void refresh_shift_strip()
    {
        if (!g_ui.homeLayer)
        {
            return;
        }

        const bool visible = show_shift_strip_enabled();
        const float ratio = active_led_ratio();
        const int activeSegments = std::clamp(static_cast<int>(std::round(ratio * static_cast<float>(kShiftSegmentCount))), 0, kShiftSegmentCount);
        const bool blinkOn = !g_state.runtime.shiftWindowActive || g_state.runtime.shift;

        static const std::array<uint32_t, kShiftSegmentCount> palette = {
            0xF5F7FB, 0xF5F7FB,
            0x4AF07B, 0x4AF07B, 0x56F28C, 0x56F28C,
            0x8CFF66, 0xC9FF5A,
            0xFFD74A, 0xFFCA44, 0xFFB13E, 0xFF9D39,
            0xFF6A48, 0xFF5947,
            0x63AFFF, 0x78B9FF};

        for (int i = 0; i < kShiftSegmentCount; ++i)
        {
            lv_obj_t *segment = g_ui.shiftSegments[static_cast<size_t>(i)];
            if (!segment)
            {
                continue;
            }

            if (!visible)
            {
                lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            lv_obj_clear_flag(segment, LV_OBJ_FLAG_HIDDEN);

            const bool lit = i < activeSegments && blinkOn;
            lv_obj_set_style_bg_color(segment, lit ? lv_color_hex(palette[static_cast<size_t>(i)]) : lv_color_hex(0x1B1F27), 0);
            lv_obj_set_style_bg_opa(segment, lit ? LV_OPA_COVER : LV_OPA_50, 0);
            lv_obj_set_style_border_opa(segment, lit ? LV_OPA_40 : LV_OPA_10, 0);
        }
    }

    void update_side_led_layout()
    {
        const int ledWidth = side_led_width();
        const int ledHeight = side_led_height();
        const int gap = side_led_gap();
        const int visibleCount = active_side_led_count();
        const int totalHeight = visibleCount * ledHeight + (visibleCount - 1) * gap;
        const int startY = std::max(18, (g_ui.height - totalHeight) / 2);
        const int leftX = compact_mode() ? 2 : 4;
        const int rightX = g_ui.width - ledWidth - (compact_mode() ? 2 : 4);

        for (int i = 0; i < kSideLedCount; ++i)
        {
            const lv_coord_t sizeW = static_cast<lv_coord_t>(ledWidth);
            const lv_coord_t sizeH = static_cast<lv_coord_t>(ledHeight);
            lv_obj_set_size(g_ui.sideLeftSegments[static_cast<size_t>(i)], sizeW, sizeH);
            lv_obj_set_size(g_ui.sideRightSegments[static_cast<size_t>(i)], sizeW, sizeH);
            if (i >= visibleCount)
            {
                lv_obj_add_flag(g_ui.sideLeftSegments[static_cast<size_t>(i)], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(g_ui.sideRightSegments[static_cast<size_t>(i)], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(g_ui.sideLeftSegments[static_cast<size_t>(i)], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_ui.sideRightSegments[static_cast<size_t>(i)], LV_OBJ_FLAG_HIDDEN);
            const int y = startY + i * (ledHeight + gap);
            set_object_pos(g_ui.sideLeftSegments[static_cast<size_t>(i)], leftX, y);
            set_object_pos(g_ui.sideRightSegments[static_cast<size_t>(i)], rightX, y);
        }
    }

    void refresh_side_leds()
    {
        const bool enabled = g_state.runtime.sideLedConfig.enabled;
        const uint32_t dimColor = g_state.runtime.sideLedConfig.colors.dim;
        const int visibleCount = active_side_led_count();

        for (int i = 0; i < kSideLedCount; ++i)
        {
            lv_obj_t *left = g_ui.sideLeftSegments[static_cast<size_t>(i)];
            lv_obj_t *right = g_ui.sideRightSegments[static_cast<size_t>(i)];
            if (!left || !right)
            {
                continue;
            }

            if (!enabled || i >= visibleCount)
            {
                lv_obj_add_flag(left, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(right, LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            lv_obj_clear_flag(left, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right, LV_OBJ_FLAG_HIDDEN);

            const uint32_t leftColor = g_state.runtime.sideLedFrame.left[static_cast<size_t>(i)];
            const uint32_t rightColor = g_state.runtime.sideLedFrame.right[static_cast<size_t>(i)];
            const bool leftLit = leftColor != 0;
            const bool rightLit = rightColor != 0;

            lv_obj_set_style_bg_color(left, lv_color_hex(leftLit ? leftColor : dimColor), 0);
            lv_obj_set_style_bg_opa(left, leftLit ? LV_OPA_COVER : LV_OPA_30, 0);
            lv_obj_set_style_border_opa(left, leftLit ? LV_OPA_50 : LV_OPA_20, 0);
            lv_obj_set_style_shadow_width(left, leftLit ? 10 : 0, 0);
            lv_obj_set_style_shadow_color(left, lv_color_hex(leftLit ? leftColor : dimColor), 0);
            lv_obj_set_style_shadow_opa(left, leftLit ? LV_OPA_50 : LV_OPA_0, 0);

            lv_obj_set_style_bg_color(right, lv_color_hex(rightLit ? rightColor : dimColor), 0);
            lv_obj_set_style_bg_opa(right, rightLit ? LV_OPA_COVER : LV_OPA_30, 0);
            lv_obj_set_style_border_opa(right, rightLit ? LV_OPA_50 : LV_OPA_20, 0);
            lv_obj_set_style_shadow_width(right, rightLit ? 10 : 0, 0);
            lv_obj_set_style_shadow_color(right, lv_color_hex(rightLit ? rightColor : dimColor), 0);
            lv_obj_set_style_shadow_opa(right, rightLit ? LV_OPA_50 : LV_OPA_0, 0);
        }
    }

    void update_home_layout()
    {
        const int shiftOffset = show_shift_strip_enabled() ? (compact_mode() ? 28 : 32) : 0;
        update_side_led_layout();

        if (compact_mode())
        {
            set_object_pos(g_ui.compactStatusPanel, 12, 12 + shiftOffset);
            set_object_pos(g_ui.compactSessionPanel, 12, 64 + shiftOffset);
            set_object_pos(g_ui.compactHero, 12, 112 + shiftOffset);
            const int tilesY = 292 + shiftOffset;
            const int tileWidth = (g_ui.width - 32) / 3;
            set_object_pos(g_ui.compactTiles[0].panel, 12, tilesY);
            set_object_pos(g_ui.compactTiles[1].panel, 16 + tileWidth, tilesY);
            set_object_pos(g_ui.compactTiles[2].panel, 20 + 2 * tileWidth, tilesY);
            const int buttonY = g_ui.height - 72;
            const int buttonWidth = (g_ui.width - 40) / 3;
            for (int i = 0; i < kActionCount; ++i)
            {
                set_object_pos(g_ui.actionButtons[static_cast<size_t>(i)].button, 12 + i * (buttonWidth + 8), buttonY);
            }
            if (g_ui.footerHint)
            {
                lv_obj_align(g_ui.footerHint, LV_ALIGN_BOTTOM_MID, 0, -8);
            }
            return;
        }

        const int topY = 14 + shiftOffset;
        const int dataY = 104 + shiftOffset;
        const int bottomY = 292 + shiftOffset;
        set_object_pos(g_ui.sourcePanel, 16, topY);
        set_object_pos(g_ui.deltaPanel, (g_ui.width - 168) / 2, topY + 4);
        set_object_pos(g_ui.sessionPanel, g_ui.width - 336, topY);
        set_object_pos(g_ui.sensorPanel, 16, dataY);
        set_object_pos(g_ui.centerPanel, 280, dataY);
        set_object_pos(g_ui.lapsPanel, 464, dataY);
        for (int i = 0; i < 5; ++i)
        {
            set_object_pos(g_ui.controlTiles[static_cast<size_t>(i)].panel, 16 + i * 88, bottomY);
        }
        set_object_pos(g_ui.fuelPanel, 464, bottomY);
        if (g_ui.footerHint)
        {
            lv_obj_align(g_ui.footerHint, LV_ALIGN_BOTTOM_MID, 0, -8);
        }
    }

    void refresh_home_selection()
    {
        if (compact_mode())
        {
            for (int i = 0; i < kActionCount; ++i)
            {
                ButtonRef &button = g_ui.actionButtons[static_cast<size_t>(i)];
                if (!button.button)
                {
                    continue;
                }

                const bool selected = i == wrap_index(g_state.selectedAction, kActionCount);
                const uint32_t accent = i == 0 ? 0xFFD74A : (i == 1 ? 0x56F28C : 0xFF8C38);
                set_panel_accent(button.button, selected ? accent : 0x394252);
                set_selected_outline(button.button, selected);
                lv_obj_set_style_text_color(button.label, selected ? lv_color_hex(accent) : colorText, 0);
            }
            return;
        }

        for (int i = 0; i < kActionCount; ++i)
        {
            set_selected_outline(g_ui.homeActionTargets[static_cast<size_t>(i)],
                                 i == wrap_index(g_state.selectedAction, kActionCount));
        }
    }

    void refresh_home_compact()
    {
        if (!g_ui.compactHero)
        {
            return;
        }

        set_label_text(g_ui.compactStatusValue, source_status_value());
        const UiSessionData session = build_session_display_data();
        set_label_text(g_ui.compactSessionValue, session.hasSessionClock ? format_clock(session.sessionClockMs) + "  |  POS " + format_position_text(session) : secondary_status_text());
        set_panel_accent(g_ui.compactStatusPanel, source_accent_rgb(g_state.runtime.telemetrySource, g_state.runtime.telemetryStale));
        set_panel_accent(g_ui.compactSessionPanel, 0xF5F7FB);

        const UiDisplayFocusMetric focusMetric = g_state.runtime.settings.displayFocus;
        set_label_text(g_ui.compactHeroTitle, metric_title(focusMetric));
        set_label_text(g_ui.compactHeroValue, metric_value(focusMetric));
        set_label_text(g_ui.compactHeroUnit, metric_unit(focusMetric));
        set_label_text(g_ui.compactHeroMeta, g_state.runtime.shift ? "SHIFT" : source_status_value());
        lv_obj_set_style_text_color(g_ui.compactHeroValue, metric_color(focusMetric, g_state.runtime.shift), 0);
        set_panel_accent(g_ui.compactHero, g_state.runtime.shift ? 0xFF5A48 : 0xF5F7FB);

        lv_label_set_text(g_ui.compactTiles[0].title, "GEAR");
        set_label_text(g_ui.compactTiles[0].value, gear_text(g_state.runtime.gear));
        set_label_text(g_ui.compactTiles[0].unit, "LIVE");
        lv_obj_set_style_text_color(g_ui.compactTiles[0].value, colorText, 0);

        lv_label_set_text(g_ui.compactTiles[1].title, "SPEED");
        set_label_text(g_ui.compactTiles[1].value, std::to_string(g_state.runtime.speedKmh));
        set_label_text(g_ui.compactTiles[1].unit, "KM/H");
        lv_obj_set_style_text_color(g_ui.compactTiles[1].value, colorBlue, 0);

        lv_label_set_text(g_ui.compactTiles[2].title, "LAP");
        set_label_text(g_ui.compactTiles[2].value, format_lap_text(session));
        set_label_text(g_ui.compactTiles[2].unit, "SESSION");
        lv_obj_set_style_text_color(g_ui.compactTiles[2].value, colorPurple, 0);

        set_label_text(g_ui.footerHint, "Focus / Source / Session");
        refresh_home_selection();
    }

    void refresh_sensor_tile(ValueTile &tile, const char *title, const std::string &value, const char *unit, uint32_t accentRgb)
    {
        if (!tile.panel)
        {
            return;
        }
        lv_label_set_text(tile.title, title);
        set_label_text(tile.value, value);
        lv_label_set_text(tile.unit, unit);
        set_panel_accent(tile.panel, accentRgb);
        lv_obj_set_style_text_color(tile.value, lv_color_hex(accentRgb), 0);
    }

    void refresh_control_tile(ValueTile &tile, const char *title, const std::string &value, uint32_t accentRgb)
    {
        if (!tile.panel)
        {
            return;
        }
        lv_label_set_text(tile.title, title);
        set_label_text(tile.value, value);
        lv_label_set_text(tile.unit, "");
        set_panel_accent(tile.panel, accentRgb);
        lv_obj_set_style_text_color(tile.value, lv_color_hex(accentRgb), 0);
    }

    void refresh_home_landscape()
    {
        if (!g_ui.centerPanel)
        {
            return;
        }

        const UiSessionData session = build_session_display_data();
        const uint32_t sourceAccent = source_accent_rgb(g_state.runtime.telemetrySource, g_state.runtime.telemetryStale);

        set_panel_accent(g_ui.sourcePanel, sourceAccent);
        set_label_text(g_ui.sourceValue, source_status_value());
        set_label_text(g_ui.sourceMeta, source_status_meta());

        set_panel_accent(g_ui.deltaPanel, 0xFFD74A);
        set_label_text(g_ui.deltaValue, session.hasDelta ? format_signed_delta(session.deltaSeconds) : "+0.000");

        set_panel_accent(g_ui.sessionPanel, 0xF5F7FB);
        set_label_text(g_ui.sessionStats[0].value, session.hasSessionClock ? format_clock(session.sessionClockMs) : "--:--");
        set_label_text(g_ui.sessionStats[1].value, format_position_text(session));
        set_label_text(g_ui.sessionStats[2].value, format_lap_text(session));

        refresh_sensor_tile(g_ui.sensorTiles[0], "OIL TEMP", session.hasOilTemp ? format_one_decimal(session.oilTempC) : "--.-", "C", 0xF5F7FB);
        refresh_sensor_tile(g_ui.sensorTiles[1], "OIL PRES", session.hasOilPressure ? format_one_decimal(session.oilPressureBar) : "--.-", "B", 0xF5F7FB);
        refresh_sensor_tile(g_ui.sensorTiles[2], "OIL LVL", session.hasOilLevel ? format_one_decimal(session.oilLevel) : "--.-", "L", 0x56F28C);
        refresh_sensor_tile(g_ui.sensorTiles[3], "FUEL P", session.hasFuelPressure ? format_one_decimal(session.fuelPressureBar) : "--.-", "B", 0x5AAEFF);
        refresh_sensor_tile(g_ui.sensorTiles[4], "WATER T", session.hasWaterTemp ? format_one_decimal(session.waterTempC) : "--.-", "C", 0x5AAEFF);
        refresh_sensor_tile(g_ui.sensorTiles[5], "BATT", session.hasBatteryVolts ? format_one_decimal(session.batteryVolts) : "--.-", "V", 0xF5F7FB);

        set_panel_accent(g_ui.centerPanel, g_state.runtime.shift ? 0xFF5A48 : 0xF5F7FB);
        set_label_text(g_ui.centerStatus, g_state.runtime.shift ? "SHIFT" : source_status_value());
        set_label_text(g_ui.centerGear, gear_text(g_state.runtime.gear));
        set_label_text(g_ui.centerRpm, std::to_string(g_state.runtime.rpm));
        set_label_text(g_ui.centerSpeed, std::to_string(g_state.runtime.speedKmh) + " KM/H");
        lv_obj_set_style_text_color(g_ui.centerGear, metric_color(UiDisplayFocusMetric::Gear, g_state.runtime.shift), 0);
        lv_obj_set_style_text_color(g_ui.centerRpm, metric_color(UiDisplayFocusMetric::Rpm, g_state.runtime.shift), 0);
        lv_obj_set_style_text_color(g_ui.centerSpeed, colorText, 0);

        const int trackWidth = lv_obj_get_width(g_ui.centerBarTrack);
        const float rpmRatio = active_led_ratio();
        const int fillWidth = std::max(12, static_cast<int>(std::round(rpmRatio * static_cast<float>(trackWidth))));
        lv_obj_set_width(g_ui.centerBarFill, fillWidth);
        lv_obj_set_style_bg_color(g_ui.centerBarFill, metric_color(UiDisplayFocusMetric::Rpm, g_state.runtime.shift), 0);

        set_panel_accent(g_ui.lapsPanel, 0xF5F7FB);
        set_label_text(g_ui.lapsPredicted, session.hasPredictedLap ? format_lap_time(session.predictedLapMs) : "--:--.---");
        set_label_text(g_ui.lapsLast, session.hasLastLap ? format_lap_time(session.lastLapMs) : "--:--.---");
        set_label_text(g_ui.lapsBest, session.hasBestLap ? format_lap_time(session.bestLapMs) : "--:--.---");

        refresh_control_tile(g_ui.controlTiles[0], "TC", session.hasTractionControl ? std::to_string(session.tractionControl) : "0", 0x5AAEFF);
        refresh_control_tile(g_ui.controlTiles[1], "TC CUT", session.hasTractionCut ? std::to_string(session.tractionCut) : "0", 0x33B5FF);
        refresh_control_tile(g_ui.controlTiles[2], "ABS", session.hasAbs ? std::to_string(session.absLevel) : "0", 0xFFD74A);
        refresh_control_tile(g_ui.controlTiles[3], "BB", session.hasBrakeBias ? format_one_decimal(session.brakeBias) : "0.0", 0xFF6A38);
        refresh_control_tile(g_ui.controlTiles[4], "MAP", session.hasEngineMap ? std::to_string(session.engineMap) : "0", 0x7BFF4C);

        set_panel_accent(g_ui.fuelPanel, 0xF5F7FB);
        set_label_text(g_ui.fuelStats[0].value, session.hasFuelLiters ? format_one_decimal(session.fuelLiters) : "0.0");
        set_label_text(g_ui.fuelStats[1].value, session.hasFuelAvgPerLap ? format_two_decimals(session.fuelAvgPerLap) : "0.00");
        set_label_text(g_ui.fuelStats[2].value, session.hasFuelLapsRemaining ? format_one_decimal(session.fuelLapsRemaining) : "0.0");

        set_label_text(g_ui.footerHint, "Center = Focus   |   Left = Source   |   Right = Session");
        refresh_home_selection();
    }

    void refresh_focus_overlay()
    {
        const bool visible = g_state.activeScreen == UiScreenId::Focus;
        if (!g_ui.focusOverlay)
        {
            return;
        }

        if (visible)
        {
            lv_obj_clear_flag(g_ui.focusOverlay, LV_OBJ_FLAG_HIDDEN);
            const UiDisplayFocusMetric metric = g_state.runtime.settings.displayFocus;
            set_label_text(g_ui.focusStatus, source_status_value());
            set_label_text(g_ui.focusValue, metric_value(metric));
            set_label_text(g_ui.focusUnit, metric_unit(metric));
            set_label_text(g_ui.focusMeta, secondary_status_text());
            lv_obj_set_style_text_color(g_ui.focusValue, metric_color(metric, g_state.runtime.shift), 0);
        }
        else
        {
            lv_obj_add_flag(g_ui.focusOverlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void refresh_source_overlay()
    {
        const bool visible = g_state.activeScreen == UiScreenId::SourcePicker;
        if (!g_ui.sourceOverlay)
        {
            return;
        }

        if (!visible)
        {
            lv_obj_add_flag(g_ui.sourceOverlay, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        lv_obj_clear_flag(g_ui.sourceOverlay, LV_OBJ_FLAG_HIDDEN);
        set_label_text(g_ui.sourcePickerSubtitle, source_status_meta());
        set_label_text(g_ui.sourceHint, "Left / Right cycles  |  Tap a tile to save");

        for (int i = 0; i < 3; ++i)
        {
            ButtonRef &option = g_ui.sourceOptions[static_cast<size_t>(i)];
            const bool selected = i == static_cast<int>(g_state.runtime.settings.telemetryPreference);
            lv_label_set_text(option.label, telemetry_preference_text(static_cast<UiTelemetryPreference>(i)).c_str());
            const uint32_t accent = i == 0 ? 0xF5F7FB : (i == 1 ? 0x56F28C : 0xD477FF);
            set_panel_accent(option.button, selected ? accent : 0x394252);
            set_selected_outline(option.button, selected);
            lv_obj_set_style_text_color(option.label, selected ? lv_color_hex(accent) : colorText, 0);
        }
    }

    void refresh_web_overlay()
    {
        const bool visible = g_state.activeScreen == UiScreenId::WebLink;
        if (!g_ui.webOverlay)
        {
            return;
        }

        if (!visible)
        {
            lv_obj_add_flag(g_ui.webOverlay, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        lv_obj_clear_flag(g_ui.webOverlay, LV_OBJ_FLAG_HIDDEN);
        const std::string url = build_web_url(g_state.webTarget);
        set_label_text(g_ui.webTitle, web_title(g_state.webTarget));
        set_label_text(g_ui.webSubtitle, web_subtitle(g_state.webTarget));
        set_label_text(g_ui.webUrl, url);
        set_label_text(g_ui.webHint, "Left / Right switches target  |  Back returns home");
#if LV_USE_QRCODE
        if (g_ui.webQr)
        {
            lv_qrcode_update(g_ui.webQr, url.c_str(), url.size());
        }
#endif
    }

    void refresh_logo_overlay()
    {
        if (!g_ui.logoOverlay)
        {
            return;
        }

        if (g_state.logoUntilMs > now_ms())
        {
            lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);
            set_panel_accent(g_ui.logoCard, g_state.overlayAccent);
            set_label_text(g_ui.logoLabel, g_state.overlayTitle);
            set_label_text(g_ui.logoSubtitle, g_state.overlaySubtitle);
            if (g_state.overlaySubtitle.empty())
            {
                lv_obj_add_flag(g_ui.logoSubtitle, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_clear_flag(g_ui.logoSubtitle, LV_OBJ_FLAG_HIDDEN);
            }
        }
        else
        {
            lv_obj_add_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void refresh_view()
    {
        refresh_shift_strip();
        refresh_side_leds();
        update_home_layout();
        if (compact_mode())
        {
            refresh_home_compact();
        }
        else
        {
            refresh_home_landscape();
        }
        refresh_focus_overlay();
        refresh_source_overlay();
        refresh_web_overlay();
        refresh_logo_overlay();
    }

    void on_home_action(lv_event_t *e)
    {
        const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        set_selected_action(index);
        open_selected_action();
        refresh_view();
    }

    void on_focus_click(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_home();
        refresh_view();
    }

    void on_focus_gesture(lv_event_t *e)
    {
        LV_UNUSED(e);
        const int delta = gesture_delta();
        if (delta != 0)
        {
            cycle_display_focus(delta);
            refresh_view();
        }
    }

    void on_source_back(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_home();
        refresh_view();
    }

    void on_source_gesture(lv_event_t *e)
    {
        LV_UNUSED(e);
        const int delta = gesture_delta();
        if (delta != 0)
        {
            cycle_telemetry_preference(delta);
            refresh_view();
        }
    }

    void on_source_option(lv_event_t *e)
    {
        const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        set_telemetry_preference(static_cast<UiTelemetryPreference>(index));
        show_home();
        refresh_view();
    }

    void on_web_back(lv_event_t *e)
    {
        LV_UNUSED(e);
        if (g_state.webReturnScreen == UiScreenId::SourcePicker)
        {
            show_source_picker();
        }
        else
        {
            show_home();
        }
        refresh_view();
    }

    void on_web_gesture(lv_event_t *e)
    {
        LV_UNUSED(e);
        const int delta = gesture_delta();
        if (delta != 0)
        {
            cycle_web_target(delta);
            refresh_view();
        }
    }

    void build_shift_strip()
    {
        const int margin = compact_mode() ? 14 : 28;
        const int gap = compact_mode() ? 4 : 8;
        const int segmentHeight = compact_mode() ? 10 : 12;
        const int availableWidth = g_ui.width - margin * 2 - gap * (kShiftSegmentCount - 1);
        const int segmentWidth = std::max(10, availableWidth / kShiftSegmentCount);
        const int y = compact_mode() ? 10 : 12;

        for (int i = 0; i < kShiftSegmentCount; ++i)
        {
            lv_obj_t *segment = lv_obj_create(g_ui.homeLayer);
            lv_obj_remove_style_all(segment);
            lv_obj_set_size(segment, segmentWidth, segmentHeight);
            lv_obj_set_pos(segment, margin + i * (segmentWidth + gap), y);
            lv_obj_set_style_bg_color(segment, lv_color_hex(0x1B1F27), 0);
            lv_obj_set_style_bg_opa(segment, LV_OPA_50, 0);
            lv_obj_set_style_radius(segment, 3, 0);
            lv_obj_set_style_border_width(segment, 1, 0);
            lv_obj_set_style_border_color(segment, colorWhiteLine, 0);
            lv_obj_set_style_border_opa(segment, LV_OPA_10, 0);
            lv_obj_clear_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
            g_ui.shiftSegments[static_cast<size_t>(i)] = segment;
        }
    }

    void build_side_leds()
    {
        const int ledWidth = side_led_width();
        const int ledHeight = side_led_height();
        const int radius = compact_mode() ? 4 : 5;

        for (int i = 0; i < kSideLedCount; ++i)
        {
            lv_obj_t *left = lv_obj_create(g_ui.homeLayer);
            lv_obj_remove_style_all(left);
            lv_obj_set_size(left, ledWidth, ledHeight);
            lv_obj_set_style_radius(left, radius, 0);
            lv_obj_set_style_bg_color(left, lv_color_hex(0x10202A), 0);
            lv_obj_set_style_bg_opa(left, LV_OPA_30, 0);
            lv_obj_set_style_border_width(left, 1, 0);
            lv_obj_set_style_border_color(left, colorWhiteLine, 0);
            lv_obj_set_style_border_opa(left, LV_OPA_20, 0);
            lv_obj_set_style_shadow_width(left, 0, 0);
            lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
            g_ui.sideLeftSegments[static_cast<size_t>(i)] = left;

            lv_obj_t *right = lv_obj_create(g_ui.homeLayer);
            lv_obj_remove_style_all(right);
            lv_obj_set_size(right, ledWidth, ledHeight);
            lv_obj_set_style_radius(right, radius, 0);
            lv_obj_set_style_bg_color(right, lv_color_hex(0x10202A), 0);
            lv_obj_set_style_bg_opa(right, LV_OPA_30, 0);
            lv_obj_set_style_border_width(right, 1, 0);
            lv_obj_set_style_border_color(right, colorWhiteLine, 0);
            lv_obj_set_style_border_opa(right, LV_OPA_20, 0);
            lv_obj_set_style_shadow_width(right, 0, 0);
            lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
            g_ui.sideRightSegments[static_cast<size_t>(i)] = right;
        }
        update_side_led_layout();
    }

    void build_landscape_home()
    {
        g_ui.sourcePanel = make_panel(g_ui.homeLayer, 16, 14, 248, 64);
        lv_obj_add_event_cb(g_ui.sourcePanel, on_home_action, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(1)));
        lv_obj_add_flag(g_ui.sourcePanel, LV_OBJ_FLAG_CLICKABLE);
        g_ui.sourceTitle = make_text(g_ui.sourcePanel, &lv_font_montserrat_16, colorMuted);
        lv_label_set_text(g_ui.sourceTitle, "");
        lv_obj_align(g_ui.sourceTitle, LV_ALIGN_TOP_RIGHT, 0, 0);
        g_ui.sourceValue = make_text(g_ui.sourcePanel, &lv_font_montserrat_24, colorText);
        lv_obj_set_width(g_ui.sourceValue, 224);
        lv_label_set_long_mode(g_ui.sourceValue, LV_LABEL_LONG_DOT);
        lv_obj_align(g_ui.sourceValue, LV_ALIGN_BOTTOM_LEFT, 0, 2);
        g_ui.sourceMeta = make_text(g_ui.sourcePanel, &lv_font_montserrat_16, colorMuted);
        lv_obj_set_width(g_ui.sourceMeta, 224);
        lv_label_set_long_mode(g_ui.sourceMeta, LV_LABEL_LONG_DOT);
        lv_obj_align(g_ui.sourceMeta, LV_ALIGN_TOP_LEFT, 0, 0);

        g_ui.deltaPanel = make_panel(g_ui.homeLayer, (g_ui.width - 168) / 2, 18, 168, 52);
        g_ui.deltaValue = make_text(g_ui.deltaPanel, &lv_font_montserrat_24, colorBg, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.deltaValue, 152);
        lv_obj_align(g_ui.deltaValue, LV_ALIGN_TOP_MID, 0, -2);
        g_ui.deltaCaption = make_text(g_ui.deltaPanel, &lv_font_montserrat_16, colorBg, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.deltaCaption, 152);
        lv_label_set_text(g_ui.deltaCaption, "");
        lv_obj_align(g_ui.deltaCaption, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_set_style_bg_color(g_ui.deltaPanel, colorYellow, 0);
        lv_obj_set_style_bg_grad_color(g_ui.deltaPanel, lv_color_hex(0xFFEE88), 0);

        g_ui.sessionPanel = make_panel(g_ui.homeLayer, g_ui.width - 336, 14, 320, 64);
        lv_obj_add_event_cb(g_ui.sessionPanel, on_home_action, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(2)));
        lv_obj_add_flag(g_ui.sessionPanel, LV_OBJ_FLAG_CLICKABLE);
        for (int i = 0; i < 3; ++i)
        {
            const int x = 10 + i * 100;
            g_ui.sessionStats[static_cast<size_t>(i)].value = make_text(g_ui.sessionPanel, &lv_font_montserrat_24, colorText, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_width(g_ui.sessionStats[static_cast<size_t>(i)].value, 88);
            lv_obj_set_pos(g_ui.sessionStats[static_cast<size_t>(i)].value, x, 4);
            g_ui.sessionStats[static_cast<size_t>(i)].caption = make_text(g_ui.sessionPanel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_width(g_ui.sessionStats[static_cast<size_t>(i)].caption, 88);
            lv_obj_set_pos(g_ui.sessionStats[static_cast<size_t>(i)].caption, x, 30);
        }
        lv_label_set_text(g_ui.sessionStats[0].caption, "TIME");
        lv_label_set_text(g_ui.sessionStats[1].caption, "POS");
        lv_label_set_text(g_ui.sessionStats[2].caption, "LAP");

        g_ui.sensorPanel = make_panel(g_ui.homeLayer, 16, 104, 248, 186);
        int tileIndex = 0;
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 2; ++col)
            {
                const int x = 8 + col * 114;
                const int y = 8 + row * 58;
                g_ui.sensorTiles[static_cast<size_t>(tileIndex++)] = make_value_tile(g_ui.sensorPanel, x, y, 106, 50, "", 0xF5F7FB, &lv_font_montserrat_24);
            }
        }

        g_ui.centerPanel = make_panel(g_ui.homeLayer, 280, 104, 168, 186);
        lv_obj_add_event_cb(g_ui.centerPanel, on_home_action, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(0)));
        lv_obj_add_flag(g_ui.centerPanel, LV_OBJ_FLAG_CLICKABLE);
        g_ui.homeActionTargets[0] = g_ui.centerPanel;
        g_ui.homeActionTargets[1] = g_ui.sourcePanel;
        g_ui.homeActionTargets[2] = g_ui.sessionPanel;

        g_ui.centerStatus = make_text(g_ui.centerPanel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.centerStatus, 152);
        lv_obj_align(g_ui.centerStatus, LV_ALIGN_TOP_MID, 0, 4);

        g_ui.centerGear = make_text(g_ui.centerPanel, &lv_font_montserrat_48, colorText, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.centerGear, 152);
        lv_obj_align(g_ui.centerGear, LV_ALIGN_TOP_MID, 0, 28);

        g_ui.centerRpm = make_text(g_ui.centerPanel, &lv_font_montserrat_32, colorYellow, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.centerRpm, 152);
        lv_obj_align(g_ui.centerRpm, LV_ALIGN_TOP_MID, 0, 92);

        g_ui.centerSpeed = make_text(g_ui.centerPanel, &lv_font_montserrat_24, colorText, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.centerSpeed, 152);
        lv_obj_align(g_ui.centerSpeed, LV_ALIGN_TOP_MID, 0, 126);

        g_ui.centerBarTrack = make_panel(g_ui.centerPanel, 12, 156, 144, 16);
        lv_obj_set_style_bg_color(g_ui.centerBarTrack, lv_color_hex(0x1A2029), 0);
        lv_obj_set_style_bg_grad_color(g_ui.centerBarTrack, lv_color_hex(0x1A2029), 0);
        lv_obj_set_style_border_opa(g_ui.centerBarTrack, LV_OPA_20, 0);
        lv_obj_set_style_shadow_width(g_ui.centerBarTrack, 0, 0);
        g_ui.centerBarFill = lv_obj_create(g_ui.centerBarTrack);
        lv_obj_remove_style_all(g_ui.centerBarFill);
        lv_obj_set_size(g_ui.centerBarFill, 12, 12);
        lv_obj_align(g_ui.centerBarFill, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_set_style_bg_color(g_ui.centerBarFill, colorYellow, 0);
        lv_obj_set_style_bg_opa(g_ui.centerBarFill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(g_ui.centerBarFill, 6, 0);

        g_ui.lapsPanel = make_panel(g_ui.homeLayer, 464, 104, 320, 186);
        g_ui.lapsTitle = make_text(g_ui.lapsPanel, &lv_font_montserrat_16, colorMuted);
        lv_label_set_text(g_ui.lapsTitle, "LAP TIMES");
        lv_obj_align(g_ui.lapsTitle, LV_ALIGN_TOP_LEFT, 0, 0);
        g_ui.lapsPredicted = make_text(g_ui.lapsPanel, &lv_font_montserrat_48, colorPurple, LV_TEXT_ALIGN_RIGHT);
        set_single_line_width(g_ui.lapsPredicted, 300);
        lv_obj_align(g_ui.lapsPredicted, LV_ALIGN_TOP_RIGHT, 0, 12);
        g_ui.lapsPredictedCaption = make_text(g_ui.lapsPanel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_RIGHT);
        set_single_line_width(g_ui.lapsPredictedCaption, 300);
        lv_label_set_text(g_ui.lapsPredictedCaption, "PREDICTED LAP");
        lv_obj_align(g_ui.lapsPredictedCaption, LV_ALIGN_TOP_RIGHT, 0, 60);
        g_ui.lapsLast = make_text(g_ui.lapsPanel, &lv_font_montserrat_32, lv_color_hex(0x7C8594), LV_TEXT_ALIGN_RIGHT);
        set_single_line_width(g_ui.lapsLast, 300);
        lv_obj_align(g_ui.lapsLast, LV_ALIGN_TOP_RIGHT, 0, 78);
        g_ui.lapsLastCaption = make_text(g_ui.lapsPanel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_RIGHT);
        set_single_line_width(g_ui.lapsLastCaption, 300);
        lv_label_set_text(g_ui.lapsLastCaption, "LAST LAP");
        lv_obj_align(g_ui.lapsLastCaption, LV_ALIGN_TOP_RIGHT, 0, 106);
        g_ui.lapsBest = make_text(g_ui.lapsPanel, &lv_font_montserrat_32, lv_color_hex(0x7C8594), LV_TEXT_ALIGN_RIGHT);
        set_single_line_width(g_ui.lapsBest, 300);
        lv_obj_align(g_ui.lapsBest, LV_ALIGN_TOP_RIGHT, 0, 124);
        g_ui.lapsBestCaption = make_text(g_ui.lapsPanel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_RIGHT);
        set_single_line_width(g_ui.lapsBestCaption, 300);
        lv_label_set_text(g_ui.lapsBestCaption, "BEST LAP");
        lv_obj_align(g_ui.lapsBestCaption, LV_ALIGN_TOP_RIGHT, 0, 152);

        const int bottomY = 300;
        for (int i = 0; i < 5; ++i)
        {
            g_ui.controlTiles[static_cast<size_t>(i)] = make_value_tile(g_ui.homeLayer, 16 + i * 88, bottomY, 80, 68, "", 0xF5F7FB, &lv_font_montserrat_24);
        }

        g_ui.fuelPanel = make_panel(g_ui.homeLayer, 464, bottomY, 320, 68);
        g_ui.fuelTitle = make_text(g_ui.fuelPanel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.fuelTitle, 300);
        lv_label_set_text(g_ui.fuelTitle, "FUEL");
        lv_obj_align(g_ui.fuelTitle, LV_ALIGN_TOP_MID, 0, 0);
        for (int i = 0; i < 3; ++i)
        {
            const int x = 8 + i * 102;
            g_ui.fuelStats[static_cast<size_t>(i)].value = make_text(g_ui.fuelPanel, &lv_font_montserrat_24, colorText, LV_TEXT_ALIGN_CENTER);
            set_single_line_width(g_ui.fuelStats[static_cast<size_t>(i)].value, 96);
            lv_obj_set_pos(g_ui.fuelStats[static_cast<size_t>(i)].value, x, 18);
            g_ui.fuelStats[static_cast<size_t>(i)].caption = make_text(g_ui.fuelPanel, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
            set_single_line_width(g_ui.fuelStats[static_cast<size_t>(i)].caption, 96);
            lv_obj_set_pos(g_ui.fuelStats[static_cast<size_t>(i)].caption, x, 42);
        }
        lv_label_set_text(g_ui.fuelStats[0].caption, "LITERS");
        lv_label_set_text(g_ui.fuelStats[1].caption, "AVG");
        lv_label_set_text(g_ui.fuelStats[2].caption, "LAPS");

        g_ui.footerHint = make_text(g_ui.homeLayer, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.footerHint, g_ui.width - 32);
        lv_obj_align(g_ui.footerHint, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    void build_compact_home()
    {
        g_ui.compactStatusPanel = make_panel(g_ui.homeLayer, 12, 32, g_ui.width - 24, 46);
        g_ui.compactStatusTitle = make_text(g_ui.compactStatusPanel, &lv_font_montserrat_16, colorMuted);
        lv_label_set_text(g_ui.compactStatusTitle, "SOURCE");
        lv_obj_align(g_ui.compactStatusTitle, LV_ALIGN_TOP_LEFT, 0, 0);
        g_ui.compactStatusValue = make_text(g_ui.compactStatusPanel, &lv_font_montserrat_24, colorText);
        lv_obj_align(g_ui.compactStatusValue, LV_ALIGN_TOP_LEFT, 0, 16);

        g_ui.compactSessionPanel = make_panel(g_ui.homeLayer, 12, 84, g_ui.width - 24, 42);
        g_ui.compactSessionTitle = make_text(g_ui.compactSessionPanel, &lv_font_montserrat_16, colorMuted);
        lv_label_set_text(g_ui.compactSessionTitle, "STATUS");
        lv_obj_align(g_ui.compactSessionTitle, LV_ALIGN_TOP_LEFT, 0, 0);
        g_ui.compactSessionValue = make_text(g_ui.compactSessionPanel, &lv_font_montserrat_16, colorText);
        lv_obj_set_width(g_ui.compactSessionValue, g_ui.width - 48);
        lv_label_set_long_mode(g_ui.compactSessionValue, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.compactSessionValue, LV_ALIGN_TOP_LEFT, 0, 16);

        g_ui.compactHero = make_panel(g_ui.homeLayer, 12, 132, g_ui.width - 24, 172);
        lv_obj_add_event_cb(g_ui.compactHero, on_home_action, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(0)));
        lv_obj_add_flag(g_ui.compactHero, LV_OBJ_FLAG_CLICKABLE);
        g_ui.compactHeroTitle = make_text(g_ui.compactHero, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.compactHeroTitle, g_ui.width - 48);
        lv_obj_align(g_ui.compactHeroTitle, LV_ALIGN_TOP_MID, 0, 2);
        g_ui.compactHeroValue = make_text(g_ui.compactHero, &lv_font_montserrat_48, colorYellow, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.compactHeroValue, g_ui.width - 48);
        lv_obj_align(g_ui.compactHeroValue, LV_ALIGN_TOP_MID, 0, 34);
        g_ui.compactHeroUnit = make_text(g_ui.compactHero, &lv_font_montserrat_24, colorText, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.compactHeroUnit, g_ui.width - 48);
        lv_obj_align(g_ui.compactHeroUnit, LV_ALIGN_TOP_MID, 0, 96);
        g_ui.compactHeroMeta = make_text(g_ui.compactHero, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.compactHeroMeta, g_ui.width - 48);
        lv_label_set_long_mode(g_ui.compactHeroMeta, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.compactHeroMeta, LV_ALIGN_BOTTOM_MID, 0, -6);

        g_ui.compactTiles[0] = make_value_tile(g_ui.homeLayer, 12, 312, (g_ui.width - 32) / 3, 62, "", 0xF5F7FB, &lv_font_montserrat_32);
        g_ui.compactTiles[1] = make_value_tile(g_ui.homeLayer, 16 + (g_ui.width - 32) / 3, 312, (g_ui.width - 32) / 3, 62, "", 0x5AAEFF, &lv_font_montserrat_32);
        g_ui.compactTiles[2] = make_value_tile(g_ui.homeLayer, 20 + 2 * ((g_ui.width - 32) / 3), 312, (g_ui.width - 32) / 3, 62, "", 0x56F28C, &lv_font_montserrat_32);

        const int buttonY = g_ui.height - 72;
        const int buttonWidth = (g_ui.width - 40) / 3;
        for (int i = 0; i < kActionCount; ++i)
        {
            g_ui.actionButtons[static_cast<size_t>(i)] =
                make_button(g_ui.homeLayer,
                            12 + i * (buttonWidth + 8),
                            buttonY,
                            buttonWidth,
                            44,
                            action_label(static_cast<HomeAction>(i)).c_str(),
                            on_home_action,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        }

        g_ui.footerHint = make_text(g_ui.homeLayer, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.footerHint, g_ui.width - 24);
        lv_obj_align(g_ui.footerHint, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    void build_focus_overlay()
    {
        g_ui.focusOverlay = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.focusOverlay);
        lv_obj_add_style(g_ui.focusOverlay, &styleOverlay, 0);
        lv_obj_set_size(g_ui.focusOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.focusOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_ui.focusOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(g_ui.focusOverlay, on_focus_click, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(g_ui.focusOverlay, on_focus_gesture, LV_EVENT_GESTURE, nullptr);

        const int cardW = compact_mode() ? g_ui.width - 28 : std::min(500, g_ui.width - 120);
        const int cardH = compact_mode() ? std::min(224, g_ui.height - 40) : std::min(220, g_ui.height - 110);
        g_ui.focusCard = lv_obj_create(g_ui.focusOverlay);
        lv_obj_remove_style_all(g_ui.focusCard);
        lv_obj_add_style(g_ui.focusCard, &styleOverlayCard, 0);
        lv_obj_set_size(g_ui.focusCard, cardW, cardH);
        lv_obj_center(g_ui.focusCard);

        g_ui.focusStatus = make_text(g_ui.focusCard, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.focusStatus, cardW - 28);
        lv_obj_align(g_ui.focusStatus, LV_ALIGN_TOP_MID, 0, 8);

        g_ui.focusValue = make_text(g_ui.focusCard, &lv_font_montserrat_48, colorYellow, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.focusValue, cardW - 28);
        lv_obj_align(g_ui.focusValue, LV_ALIGN_CENTER, 0, -24);

        g_ui.focusUnit = make_text(g_ui.focusCard, &lv_font_montserrat_24, colorText, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.focusUnit, cardW - 28);
        lv_obj_align(g_ui.focusUnit, LV_ALIGN_CENTER, 0, 28);

        g_ui.focusMeta = make_text(g_ui.focusCard, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.focusMeta, cardW - 28);
        lv_label_set_long_mode(g_ui.focusMeta, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.focusMeta, LV_ALIGN_BOTTOM_MID, 0, -34);

        g_ui.focusHint = make_text(g_ui.focusCard, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.focusHint, cardW - 28);
        lv_label_set_text(g_ui.focusHint, "Swipe left / right to change metric  |  Tap to close");
        lv_obj_align(g_ui.focusHint, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    void build_source_overlay()
    {
        g_ui.sourceOverlay = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.sourceOverlay);
        lv_obj_add_style(g_ui.sourceOverlay, &styleOverlay, 0);
        lv_obj_set_size(g_ui.sourceOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.sourceOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_ui.sourceOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(g_ui.sourceOverlay, on_source_gesture, LV_EVENT_GESTURE, nullptr);

        const int cardW = compact_mode() ? g_ui.width - 28 : std::min(500, g_ui.width - 140);
        const int cardH = compact_mode() ? 248 : 270;
        g_ui.sourceCard = lv_obj_create(g_ui.sourceOverlay);
        lv_obj_remove_style_all(g_ui.sourceCard);
        lv_obj_add_style(g_ui.sourceCard, &styleOverlayCard, 0);
        lv_obj_set_size(g_ui.sourceCard, cardW, cardH);
        lv_obj_center(g_ui.sourceCard);

        g_ui.sourceBack = make_button(g_ui.sourceCard, 10, 10, 72, 34, "< Back", on_source_back, nullptr).button;
        set_panel_accent(g_ui.sourceBack, 0x5AAEFF);

        g_ui.sourcePickerTitle = make_text(g_ui.sourceCard, &lv_font_montserrat_24, colorText);
        lv_label_set_text(g_ui.sourcePickerTitle, "Telemetry Source");
        lv_obj_align(g_ui.sourcePickerTitle, LV_ALIGN_TOP_LEFT, 0, 44);

        g_ui.sourcePickerSubtitle = make_text(g_ui.sourceCard, &lv_font_montserrat_16, colorMuted);
        lv_obj_set_width(g_ui.sourcePickerSubtitle, cardW - 28);
        lv_label_set_long_mode(g_ui.sourcePickerSubtitle, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.sourcePickerSubtitle, LV_ALIGN_TOP_LEFT, 0, 76);

        for (int i = 0; i < 3; ++i)
        {
            g_ui.sourceOptions[static_cast<size_t>(i)] =
                make_button(g_ui.sourceCard, 16, 116 + i * 42, cardW - 32, 36, "", on_source_option, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        }

        g_ui.sourceHint = make_text(g_ui.sourceCard, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.sourceHint, cardW - 28);
        lv_obj_align(g_ui.sourceHint, LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    void build_web_overlay()
    {
        g_ui.webOverlay = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.webOverlay);
        lv_obj_add_style(g_ui.webOverlay, &styleOverlay, 0);
        lv_obj_set_size(g_ui.webOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.webOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_ui.webOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(g_ui.webOverlay, on_web_gesture, LV_EVENT_GESTURE, nullptr);

        const int cardW = compact_mode() ? g_ui.width - 24 : std::min(580, g_ui.width - 140);
        const int cardH = compact_mode() ? g_ui.height - 32 : std::min(340, g_ui.height - 80);
        g_ui.webCard = lv_obj_create(g_ui.webOverlay);
        lv_obj_remove_style_all(g_ui.webCard);
        lv_obj_add_style(g_ui.webCard, &styleOverlayCard, 0);
        lv_obj_set_size(g_ui.webCard, cardW, cardH);
        lv_obj_center(g_ui.webCard);

        g_ui.webBack = make_button(g_ui.webCard, 10, 10, 72, 34, "< Back", on_web_back, nullptr).button;
        set_panel_accent(g_ui.webBack, 0x5AAEFF);

        g_ui.webTitle = make_text(g_ui.webCard, &lv_font_montserrat_24, colorText);
        lv_obj_align(g_ui.webTitle, LV_ALIGN_TOP_LEFT, 0, 50);

        g_ui.webSubtitle = make_text(g_ui.webCard, &lv_font_montserrat_16, colorMuted);
        lv_obj_set_width(g_ui.webSubtitle, cardW - 28);
        lv_label_set_long_mode(g_ui.webSubtitle, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.webSubtitle, LV_ALIGN_TOP_LEFT, 0, 84);

        const int qrPanelSize = compact_mode() ? 160 : 180;
        lv_obj_t *qrPanel = make_panel(g_ui.webCard, (cardW - qrPanelSize) / 2, 130, qrPanelSize, qrPanelSize);
        set_panel_accent(qrPanel, 0xF5F7FB);
#if LV_USE_QRCODE
        g_ui.webQr = lv_qrcode_create(qrPanel, qrPanelSize - 16, colorText, colorBg);
        lv_obj_center(g_ui.webQr);
#endif

        g_ui.webUrl = make_text(g_ui.webCard, &lv_font_montserrat_16, colorBlue, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.webUrl, cardW - 28);
        lv_label_set_long_mode(g_ui.webUrl, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.webUrl, LV_ALIGN_BOTTOM_MID, 0, compact_mode() ? -52 : -42);

        g_ui.webHint = make_text(g_ui.webCard, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.webHint, cardW - 28);
        lv_obj_align(g_ui.webHint, LV_ALIGN_BOTTOM_MID, 0, -14);
    }

    void build_logo_overlay()
    {
        g_ui.logoOverlay = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.logoOverlay);
        lv_obj_add_style(g_ui.logoOverlay, &styleOverlay, 0);
        lv_obj_set_size(g_ui.logoOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);

        const int cardW = compact_mode() ? g_ui.width - 36 : 420;
        const int cardH = compact_mode() ? 160 : 180;
        g_ui.logoCard = lv_obj_create(g_ui.logoOverlay);
        lv_obj_remove_style_all(g_ui.logoCard);
        lv_obj_add_style(g_ui.logoCard, &styleOverlayCard, 0);
        lv_obj_set_size(g_ui.logoCard, cardW, cardH);
        lv_obj_center(g_ui.logoCard);

        g_ui.logoLabel = make_text(g_ui.logoCard, &lv_font_montserrat_48, colorText, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.logoLabel, cardW - 28);
        lv_obj_align(g_ui.logoLabel, LV_ALIGN_CENTER, 0, -26);

        g_ui.logoSubtitle = make_text(g_ui.logoCard, &lv_font_montserrat_16, colorMuted, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(g_ui.logoSubtitle, cardW - 28);
        lv_label_set_long_mode(g_ui.logoSubtitle, LV_LABEL_LONG_WRAP);
        lv_obj_align(g_ui.logoSubtitle, LV_ALIGN_CENTER, 0, 34);
    }

    void build_ui()
    {
        g_ui.root = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.root);
        lv_obj_add_style(g_ui.root, &styleScreen, 0);
        lv_obj_set_size(g_ui.root, g_ui.width, g_ui.height);
        lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.homeLayer = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.homeLayer);
        lv_obj_set_size(g_ui.homeLayer, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.homeLayer, LV_OBJ_FLAG_SCROLLABLE);

        build_shift_strip();
        build_side_leds();
        if (compact_mode())
        {
            build_compact_home();
        }
        else
        {
            build_landscape_home();
        }
        build_focus_overlay();
        build_source_overlay();
        build_web_overlay();
        build_logo_overlay();
    }
}

void ui_s3_init(lv_disp_t *disp, const UiDisplayHooks &hooks, const UiRuntimeState &initialState)
{
    g_ui = UiRefs{};
    g_state = UiState{};
    g_ui.disp = disp;
    g_state.hooks = hooks;
    g_state.runtime = initialState;
    g_state.selectedAction = wrap_index(initialState.settings.lastMenuIndex, kActionCount);
    g_state.fullscreenMetric = initialState.settings.displayFocus;

    g_ui.width = disp ? lv_disp_get_hor_res(disp) : 800;
    g_ui.height = disp ? lv_disp_get_ver_res(disp) : 400;
    g_ui.compact = g_ui.width < 600 || g_ui.height > g_ui.width;

    apply_styles();
    build_ui();
    refresh_view();
    lv_scr_load(g_ui.root);
}

void ui_s3_loop(const UiRuntimeState &state)
{
    g_state.runtime = state;
    sync_runtime_settings();
    refresh_view();
}

void ui_s3_set_gear(int gear)
{
    g_state.runtime.gear = gear;
    refresh_view();
}

void ui_s3_set_shiftlight(bool active)
{
    g_state.runtime.shift = active;
    refresh_view();
}

void ui_s3_show_logo()
{
    g_state.overlayTitle = "ShiftLight";
    g_state.overlaySubtitle.clear();
    g_state.overlayAccent = 0x5AAEFF;
    g_state.logoUntilMs = now_ms() + kLogoDurationMs;
    refresh_view();
}

void ui_s3_show_transient_message(const char *title, const char *subtitle, uint32_t durationMs, uint32_t accentColorRgb)
{
    g_state.overlayTitle = title != nullptr ? title : "";
    g_state.overlaySubtitle = subtitle != nullptr ? subtitle : "";
    g_state.overlayAccent = accentColorRgb;
    g_state.logoUntilMs = now_ms() + std::max<uint32_t>(durationMs, 400U);
    refresh_view();
}

void ui_s3_debug_dispatch(UiDebugAction action)
{
    switch (action)
    {
    case UiDebugAction::PreviousCard:
        if (g_state.activeScreen == UiScreenId::Home)
        {
            set_selected_action(g_state.selectedAction - 1);
        }
        else if (g_state.activeScreen == UiScreenId::Focus)
        {
            cycle_display_focus(-1);
        }
        else if (g_state.activeScreen == UiScreenId::SourcePicker)
        {
            cycle_telemetry_preference(-1);
        }
        else if (g_state.activeScreen == UiScreenId::WebLink)
        {
            cycle_web_target(-1);
        }
        break;
    case UiDebugAction::NextCard:
        if (g_state.activeScreen == UiScreenId::Home)
        {
            set_selected_action(g_state.selectedAction + 1);
        }
        else if (g_state.activeScreen == UiScreenId::Focus)
        {
            cycle_display_focus(1);
        }
        else if (g_state.activeScreen == UiScreenId::SourcePicker)
        {
            cycle_telemetry_preference(1);
        }
        else if (g_state.activeScreen == UiScreenId::WebLink)
        {
            cycle_web_target(1);
        }
        break;
    case UiDebugAction::OpenSelectedCard:
        if (g_state.activeScreen == UiScreenId::Home)
        {
            open_selected_action();
        }
        else
        {
            show_home();
        }
        break;
    case UiDebugAction::GoHome:
        show_home();
        break;
    case UiDebugAction::ShowLogo:
        ui_s3_show_logo();
        break;
    }
    refresh_view();
}

UiDebugSnapshot ui_s3_debug_snapshot()
{
    UiDebugSnapshot snapshot{};
    snapshot.activeScreen = g_state.activeScreen;
    snapshot.selectedCardIndex = wrap_index(g_state.selectedAction, kActionCount);
    snapshot.inDetail = g_state.activeScreen != UiScreenId::Home;
    snapshot.displayBrightness = g_state.runtime.settings.displayBrightness;
    snapshot.gear = g_state.runtime.gear;
    snapshot.rpm = g_state.runtime.rpm;
    snapshot.speedKmh = g_state.runtime.speedKmh;
    snapshot.shift = g_state.runtime.shift;
    snapshot.bleConnected = g_state.runtime.bleConnected;
    snapshot.staConnected = g_state.runtime.staConnected;
    snapshot.telemetrySource = g_state.runtime.telemetrySource;
    snapshot.telemetryPreference = g_state.runtime.settings.telemetryPreference;
    snapshot.telemetryStale = g_state.runtime.telemetryStale;
    snapshot.telemetryUsingFallback = g_state.runtime.telemetryUsingFallback;
    return snapshot;
}
