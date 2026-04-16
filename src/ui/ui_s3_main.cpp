#include "ui_s3_main.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace
{
    enum class HomeAction : uint8_t
    {
        Display = 0,
        Source,
        Web
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

    struct UiRefs
    {
        lv_disp_t *disp = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *homeLayer = nullptr;
        lv_obj_t *statusChip = nullptr;
        lv_obj_t *statusChipLabel = nullptr;
        lv_obj_t *iconPrimary = nullptr;
        lv_obj_t *iconSecondary = nullptr;
        lv_obj_t *heroCard = nullptr;
        lv_obj_t *heroKicker = nullptr;
        lv_obj_t *heroValue = nullptr;
        lv_obj_t *heroUnit = nullptr;
        lv_obj_t *heroMeta = nullptr;
        std::array<ButtonRef, 3> heroBadges{};
        std::array<ButtonRef, 3> actionButtons{};
        lv_obj_t *homeHint = nullptr;
        lv_obj_t *focusLayer = nullptr;
        lv_obj_t *focusStatus = nullptr;
        lv_obj_t *focusValue = nullptr;
        lv_obj_t *focusUnit = nullptr;
        lv_obj_t *focusMeta = nullptr;
        lv_obj_t *focusHint = nullptr;
        lv_obj_t *pickerLayer = nullptr;
        lv_obj_t *pickerBack = nullptr;
        lv_obj_t *pickerTitle = nullptr;
        lv_obj_t *pickerSubtitle = nullptr;
        lv_obj_t *pickerPreview = nullptr;
        lv_obj_t *pickerValue = nullptr;
        lv_obj_t *pickerUnit = nullptr;
        lv_obj_t *pickerMeta = nullptr;
        std::array<ButtonRef, 3> pickerOptions{};
        lv_obj_t *pickerHint = nullptr;
        lv_obj_t *pickerWebButton = nullptr;
        lv_obj_t *webLayer = nullptr;
        lv_obj_t *webBack = nullptr;
        lv_obj_t *webTitle = nullptr;
        lv_obj_t *webSubtitle = nullptr;
        lv_obj_t *webQr = nullptr;
        lv_obj_t *webUrl = nullptr;
        lv_obj_t *webHint = nullptr;
        lv_obj_t *logoOverlay = nullptr;
        lv_obj_t *logoLabel = nullptr;
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
        uint32_t lastFocusTapMs = 0;
        uint32_t logoUntilMs = 0;
    };

    UiRefs g_ui;
    UiState g_state;

    lv_style_t styleScreen;
    lv_style_t stylePanel;
    lv_style_t stylePanelStrong;
    lv_style_t stylePill;
    lv_style_t styleButton;
    lv_style_t styleBackButton;
    lv_style_t styleMuted;
    lv_style_t styleBadge;
    lv_style_t styleLogoOverlay;

    const lv_color_t color_bg = lv_color_hex(0x070B12);
    const lv_color_t color_bg_grad = lv_color_hex(0x0C1420);
    const lv_color_t color_panel = lv_color_hex(0x0D1523);
    const lv_color_t color_panel_alt = lv_color_hex(0x121D2D);
    const lv_color_t color_border = lv_color_hex(0x25364B);
    const lv_color_t color_border_soft = lv_color_hex(0x1B2836);
    const lv_color_t color_text = lv_color_hex(0xEEF3FA);
    const lv_color_t color_muted = lv_color_hex(0x90A0B4);
    const lv_color_t color_cyan = lv_color_hex(0x4FCBFF);
    const lv_color_t color_lime = lv_color_hex(0x67F2A0);
    const lv_color_t color_warn = lv_color_hex(0xFFB648);
    const lv_color_t color_error = lv_color_hex(0xFF6B7A);
    const lv_color_t color_violet = lv_color_hex(0xA875FF);

    constexpr int kActionCount = 3;
    constexpr int kPickerOptionCount = 3;
    constexpr uint32_t kLogoDurationMs = 1400;
    constexpr uint32_t kDoubleTapWindowMs = 360;
    constexpr int kUsbBridgeWebPort = 8765;

    int wrap_index(int index, int count)
    {
        return (index % count + count) % count;
    }

    uint32_t now_ms()
    {
        return lv_tick_get();
    }

    std::string gear_text(int gear)
    {
        return gear <= 0 ? "N" : std::to_string(gear);
    }

    std::string metric_title(UiDisplayFocusMetric metric)
    {
        switch (metric)
        {
        case UiDisplayFocusMetric::Gear:
            return "Gang";
        case UiDisplayFocusMetric::Speed:
            return "Geschwindigkeit";
        case UiDisplayFocusMetric::Rpm:
        default:
            return "Drehzahl";
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
            return "km/h";
        case UiDisplayFocusMetric::Rpm:
        default:
            return "RPM";
        }
    }

    lv_color_t metric_color(UiDisplayFocusMetric metric, bool shift)
    {
        if (shift)
        {
            return color_warn;
        }
        switch (metric)
        {
        case UiDisplayFocusMetric::Gear:
            return color_violet;
        case UiDisplayFocusMetric::Speed:
            return color_lime;
        case UiDisplayFocusMetric::Rpm:
        default:
            return color_cyan;
        }
    }

    std::string telemetry_preference_text(UiTelemetryPreference preference)
    {
        switch (preference)
        {
        case UiTelemetryPreference::Obd:
            return "OBD";
        case UiTelemetryPreference::SimHub:
            return "Sim / PC";
        case UiTelemetryPreference::Auto:
        default:
            return "Auto";
        }
    }

    std::string telemetry_preference_hint(UiTelemetryPreference preference)
    {
        switch (preference)
        {
        case UiTelemetryPreference::Obd:
            return "Nur BLE / OBD aktiv";
        case UiTelemetryPreference::SimHub:
            return "Nur SimHub: Auto, USB only oder Network only";
        case UiTelemetryPreference::Auto:
        default:
            return "USB, Netzwerk und OBD werden automatisch sortiert";
        }
    }

    bool usb_transport_mode_selected()
    {
        return g_state.runtime.simTransportMode == UiSimTransportMode::UsbOnly;
    }

    bool network_transport_mode_selected()
    {
        return g_state.runtime.simTransportMode == UiSimTransportMode::NetworkOnly;
    }

    bool auto_transport_mode_selected()
    {
        return g_state.runtime.simTransportMode == UiSimTransportMode::Auto;
    }

    bool usb_transport_active()
    {
        return usb_transport_mode_selected() ||
               (auto_transport_mode_selected() && g_state.runtime.telemetrySource == UiTelemetrySource::UsbBridge);
    }

    bool network_transport_active()
    {
        return network_transport_mode_selected() ||
               (auto_transport_mode_selected() && g_state.runtime.telemetrySource == UiTelemetrySource::SimHubNetwork);
    }

    bool usb_bridge_web_selected()
    {
        return usb_transport_mode_selected() || g_state.runtime.telemetrySource == UiTelemetrySource::UsbBridge;
    }

    std::string usb_state_text(bool autoMode)
    {
        const char *prefix = autoMode ? "Auto: " : "";
        switch (g_state.runtime.usbState)
        {
        case UiUsbState::Live:
            return std::string(prefix) + "USB live";
        case UiUsbState::WaitingForBridge:
            return std::string(prefix) + "USB wartet auf Bridge";
        case UiUsbState::WaitingForData:
            return std::string(prefix) + "USB wartet auf Daten";
        case UiUsbState::Error:
            return std::string(prefix) + "USB Fehler";
        case UiUsbState::Disconnected:
            return std::string(prefix) + "USB getrennt";
        case UiUsbState::Disabled:
        default:
            return std::string(prefix) + "USB aus";
        }
    }

    std::string simhub_state_text(bool autoMode)
    {
        switch (g_state.runtime.simHubState)
        {
        case UiSimHubState::Live:
            if (autoMode && g_state.runtime.telemetryUsingFallback)
            {
                return "Auto: Netzwerk Fallback";
            }
            return autoMode ? "Auto: Netzwerk live" : "Netzwerk live";
        case UiSimHubState::WaitingForHost:
            return autoMode ? "Auto: Host fehlt" : "Host fehlt";
        case UiSimHubState::WaitingForNetwork:
            return autoMode ? "Auto: WLAN fehlt" : "WLAN fehlt";
        case UiSimHubState::WaitingForData:
            return autoMode ? "Auto: Netzwerk wartet" : "Netzwerk wartet";
        case UiSimHubState::Error:
            return autoMode ? "Auto: Netzwerk Fehler" : "Netzwerk Fehler";
        case UiSimHubState::Disabled:
        default:
            return autoMode ? "Auto: SimHub aus" : "Netzwerk aus";
        }
    }

    void set_label_text(lv_obj_t *label, const std::string &text)
    {
        if (label)
        {
            lv_label_set_text(label, text.c_str());
        }
    }

    std::string status_chip_text()
    {
        if (usb_transport_mode_selected())
        {
            return usb_state_text(false);
        }

        if (network_transport_mode_selected())
        {
            return simhub_state_text(false);
        }

        if (g_state.runtime.telemetrySource == UiTelemetrySource::UsbBridge ||
            g_state.runtime.usbConnected ||
            g_state.runtime.usbBridgeConnected)
        {
            return usb_state_text(true);
        }

        if (g_state.runtime.telemetrySource == UiTelemetrySource::SimHubNetwork || g_state.runtime.simHubConfigured)
        {
            return simhub_state_text(true);
        }

        switch (g_state.runtime.telemetrySource)
        {
        case UiTelemetrySource::Esp32Obd:
            return g_state.runtime.telemetryUsingFallback ? "Auto: OBD Fallback"
                                                          : (g_state.runtime.bleConnected ? "OBD live" : (g_state.runtime.bleConnecting ? "OBD verbindet" : "OBD bereit"));
        case UiTelemetrySource::SimHubNetwork:
            return simhub_state_text(true);
        case UiTelemetrySource::Simulator:
            return "Simulator";
        case UiTelemetrySource::UsbBridge:
        default:
            return "Bereit";
        }
    }

    std::string hero_meta_text()
    {
        std::string meta = "Quelle: ";
        meta += telemetry_preference_text(g_state.runtime.settings.telemetryPreference);
        meta += "  |  ";
        if (usb_transport_mode_selected())
        {
            meta += "USB only";
        }
        else if (network_transport_mode_selected())
        {
            meta += "Network only";
        }
        else if (g_state.runtime.telemetryUsingFallback)
        {
            if (g_state.runtime.telemetrySource == UiTelemetrySource::SimHubNetwork)
            {
                meta += "Netzwerk-Fallback";
            }
            else if (g_state.runtime.telemetrySource == UiTelemetrySource::Esp32Obd)
            {
                meta += "OBD-Fallback";
            }
            else
            {
                meta += "USB aktiv";
            }
        }
        else if (g_state.runtime.telemetrySource == UiTelemetrySource::UsbBridge)
        {
            meta += "USB aktiv";
        }
        else if (g_state.runtime.telemetrySource == UiTelemetrySource::SimHubNetwork)
        {
            meta += "Netzwerk aktiv";
        }
        else if (g_state.runtime.staConnected)
        {
            meta += g_state.runtime.currentSsid.empty() ? "WLAN online" : g_state.runtime.currentSsid;
        }
        else if (g_state.runtime.apActive)
        {
            meta += "Access Point";
        }
        else
        {
            meta += "Offline";
        }
        return meta;
    }

    std::string secondary_status_text()
    {
        if (usb_transport_active())
        {
            if (!g_state.runtime.usbError.empty())
            {
                return g_state.runtime.usbError;
            }
            if (g_state.runtime.usbBridgeConnected)
            {
                return g_state.runtime.usbHost.empty() ? "USB Bridge verbunden" : ("PC: " + g_state.runtime.usbHost);
            }
            return "USB Bridge starten, dann kommt SimHub direkt vom PC.";
        }

        if (network_transport_active() || (auto_transport_mode_selected() && g_state.runtime.simHubConfigured))
        {
            switch (g_state.runtime.simHubState)
            {
            case UiSimHubState::Live:
                return g_state.runtime.simHubEndpoint.empty() ? "SimHub direkt ueber Netzwerk aktiv"
                                                              : ("SimHub: " + g_state.runtime.simHubEndpoint);
            case UiSimHubState::WaitingForHost:
                return "SimHub Host im Web setzen.";
            case UiSimHubState::WaitingForNetwork:
                return "WLAN verbinden oder AP pruefen.";
            case UiSimHubState::WaitingForData:
                return "SimHub erreichbar, warte auf laufende Session.";
            case UiSimHubState::Error:
                return "Netzwerkpfad fehlgeschlagen, SimHub nicht erreichbar.";
            case UiSimHubState::Disabled:
            default:
                break;
            }
        }

        if (!g_state.runtime.ip.empty())
        {
            return "Web: " + g_state.runtime.ip;
        }
        if (g_state.runtime.apActive)
        {
            return "AP aktiv: " + g_state.runtime.apIp;
        }
        return "Web-Oberflaeche im Netzwerk verfuegbar";
    }

    std::string action_label(HomeAction action)
    {
        switch (action)
        {
        case HomeAction::Display:
            return "Anzeige";
        case HomeAction::Source:
            return "Quelle";
        case HomeAction::Web:
        default:
            return "QR Web";
        }
    }

    std::string picker_option_label(int index)
    {
        if (g_state.activeScreen == UiScreenId::DisplayPicker)
        {
            switch (static_cast<UiDisplayFocusMetric>(index))
            {
            case UiDisplayFocusMetric::Gear:
                return "Gang";
            case UiDisplayFocusMetric::Speed:
                return "Speed";
            case UiDisplayFocusMetric::Rpm:
            default:
                return "RPM";
            }
        }

        switch (static_cast<UiTelemetryPreference>(index))
        {
        case UiTelemetryPreference::Obd:
            return "OBD";
        case UiTelemetryPreference::SimHub:
            return "Sim";
        case UiTelemetryPreference::Auto:
        default:
            return "Auto";
        }
    }

    std::string build_web_url(WebTarget target)
    {
        std::string base;
        if (usb_bridge_web_selected())
        {
            if (!g_state.runtime.usbHost.empty())
            {
                base = "http://" + g_state.runtime.usbHost + ":" + std::to_string(kUsbBridgeWebPort);
            }
            else
            {
                base = "http://127.0.0.1:" + std::to_string(kUsbBridgeWebPort);
            }
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
        persist_settings();
    }

    void set_telemetry_preference(UiTelemetryPreference preference)
    {
        g_state.runtime.settings.telemetryPreference = preference;
        persist_settings();
    }

    void cycle_display_focus(int delta)
    {
        const int next = wrap_index(static_cast<int>(g_state.runtime.settings.displayFocus) + delta, kPickerOptionCount);
        set_display_focus(static_cast<UiDisplayFocusMetric>(next));
    }

    void cycle_telemetry_preference(int delta)
    {
        const int next = wrap_index(static_cast<int>(g_state.runtime.settings.telemetryPreference) + delta, kPickerOptionCount);
        set_telemetry_preference(static_cast<UiTelemetryPreference>(next));
    }

    void cycle_web_target(int delta)
    {
        const int next = wrap_index(static_cast<int>(g_state.webTarget) + delta, 3);
        g_state.webTarget = static_cast<WebTarget>(next);
    }

    void apply_styles()
    {
        lv_style_init(&styleScreen);
        lv_style_set_bg_color(&styleScreen, color_bg);
        lv_style_set_bg_grad_color(&styleScreen, color_bg_grad);
        lv_style_set_bg_grad_dir(&styleScreen, LV_GRAD_DIR_VER);
        lv_style_set_bg_opa(&styleScreen, LV_OPA_COVER);
        lv_style_set_pad_all(&styleScreen, 0);

        lv_style_init(&stylePanel);
        lv_style_set_bg_color(&stylePanel, color_panel);
        lv_style_set_bg_grad_color(&stylePanel, color_panel_alt);
        lv_style_set_bg_grad_dir(&stylePanel, LV_GRAD_DIR_VER);
        lv_style_set_bg_opa(&stylePanel, LV_OPA_COVER);
        lv_style_set_radius(&stylePanel, 24);
        lv_style_set_border_width(&stylePanel, 1);
        lv_style_set_border_color(&stylePanel, color_border_soft);
        lv_style_set_pad_all(&stylePanel, 14);
        lv_style_set_shadow_width(&stylePanel, 14);
        lv_style_set_shadow_color(&stylePanel, lv_color_hex(0x02060C));
        lv_style_set_shadow_opa(&stylePanel, LV_OPA_20);

        lv_style_init(&stylePanelStrong);
        lv_style_set_bg_color(&stylePanelStrong, color_panel_alt);
        lv_style_set_bg_grad_color(&stylePanelStrong, lv_color_hex(0x162438));
        lv_style_set_bg_grad_dir(&stylePanelStrong, LV_GRAD_DIR_VER);
        lv_style_set_bg_opa(&stylePanelStrong, LV_OPA_COVER);
        lv_style_set_radius(&stylePanelStrong, 26);
        lv_style_set_border_width(&stylePanelStrong, 1);
        lv_style_set_border_color(&stylePanelStrong, color_border);
        lv_style_set_pad_all(&stylePanelStrong, 16);

        lv_style_init(&stylePill);
        lv_style_set_radius(&stylePill, 18);
        lv_style_set_bg_color(&stylePill, lv_color_hex(0x101927));
        lv_style_set_bg_opa(&stylePill, LV_OPA_COVER);
        lv_style_set_border_width(&stylePill, 1);
        lv_style_set_border_color(&stylePill, color_border_soft);
        lv_style_set_pad_left(&stylePill, 12);
        lv_style_set_pad_right(&stylePill, 12);
        lv_style_set_pad_top(&stylePill, 8);
        lv_style_set_pad_bottom(&stylePill, 8);

        lv_style_init(&styleBadge);
        lv_style_set_radius(&styleBadge, 16);
        lv_style_set_bg_color(&styleBadge, lv_color_hex(0x111B28));
        lv_style_set_bg_opa(&styleBadge, LV_OPA_COVER);
        lv_style_set_border_width(&styleBadge, 1);
        lv_style_set_border_color(&styleBadge, color_border_soft);
        lv_style_set_pad_left(&styleBadge, 12);
        lv_style_set_pad_right(&styleBadge, 12);
        lv_style_set_pad_top(&styleBadge, 10);
        lv_style_set_pad_bottom(&styleBadge, 10);

        lv_style_init(&styleButton);
        lv_style_set_radius(&styleButton, 18);
        lv_style_set_bg_color(&styleButton, lv_color_hex(0x0F1824));
        lv_style_set_bg_opa(&styleButton, LV_OPA_COVER);
        lv_style_set_border_width(&styleButton, 1);
        lv_style_set_border_color(&styleButton, color_border_soft);
        lv_style_set_text_color(&styleButton, color_text);
        lv_style_set_pad_all(&styleButton, 0);

        lv_style_init(&styleBackButton);
        lv_style_set_radius(&styleBackButton, 16);
        lv_style_set_bg_color(&styleBackButton, lv_color_hex(0x101927));
        lv_style_set_bg_opa(&styleBackButton, LV_OPA_COVER);
        lv_style_set_border_width(&styleBackButton, 1);
        lv_style_set_border_color(&styleBackButton, color_border_soft);
        lv_style_set_pad_all(&styleBackButton, 0);

        lv_style_init(&styleMuted);
        lv_style_set_text_color(&styleMuted, color_muted);

        lv_style_init(&styleLogoOverlay);
        lv_style_set_bg_color(&styleLogoOverlay, color_bg);
        lv_style_set_bg_opa(&styleLogoOverlay, LV_OPA_80);
        lv_style_set_pad_all(&styleLogoOverlay, 0);
    }

    ButtonRef make_button(lv_obj_t *parent, int width, int height, const char *text, lv_event_cb_t cb = nullptr, void *userData = nullptr)
    {
        ButtonRef ref{};
        ref.button = lv_btn_create(parent);
        lv_obj_remove_style_all(ref.button);
        lv_obj_add_style(ref.button, &styleButton, 0);
        lv_obj_set_size(ref.button, width, height);
        lv_obj_clear_flag(ref.button, LV_OBJ_FLAG_SCROLLABLE);
        if (cb)
        {
            lv_obj_add_event_cb(ref.button, cb, LV_EVENT_CLICKED, userData);
        }

        ref.label = lv_label_create(ref.button);
        lv_label_set_text(ref.label, text);
        lv_obj_set_style_text_font(ref.label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ref.label, color_text, 0);
        lv_obj_center(ref.label);
        return ref;
    }

    lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int width, int height, bool strong = false)
    {
        lv_obj_t *panel = lv_obj_create(parent);
        lv_obj_remove_style_all(panel);
        lv_obj_add_style(panel, strong ? &stylePanelStrong : &stylePanel, 0);
        lv_obj_set_pos(panel, x, y);
        lv_obj_set_size(panel, width, height);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        return panel;
    }

    void update_action_button_styles()
    {
        for (int i = 0; i < kActionCount; ++i)
        {
            ButtonRef &ref = g_ui.actionButtons[static_cast<size_t>(i)];
            if (!ref.button)
            {
                continue;
            }
            const bool active = g_state.activeScreen == UiScreenId::Home && i == wrap_index(g_state.selectedAction, kActionCount);
            lv_obj_set_style_bg_color(ref.button, active ? lv_color_hex(0x162438) : lv_color_hex(0x0F1824), 0);
            lv_obj_set_style_border_color(ref.button, active ? color_cyan : color_border_soft, 0);
            lv_obj_set_style_border_width(ref.button, active ? 2 : 1, 0);
            lv_obj_set_style_text_color(ref.label, active ? color_text : color_muted, 0);
        }
    }

    void update_picker_option_styles()
    {
        int selected = g_state.activeScreen == UiScreenId::DisplayPicker ? static_cast<int>(g_state.runtime.settings.displayFocus) : static_cast<int>(g_state.runtime.settings.telemetryPreference);
        for (int i = 0; i < kPickerOptionCount; ++i)
        {
            ButtonRef &ref = g_ui.pickerOptions[static_cast<size_t>(i)];
            if (!ref.button)
            {
                continue;
            }
            const bool active = i == selected;
            lv_obj_set_style_bg_color(ref.button, active ? lv_color_hex(0x162438) : lv_color_hex(0x0F1824), 0);
            lv_obj_set_style_border_color(ref.button, active ? color_cyan : color_border_soft, 0);
            lv_obj_set_style_border_width(ref.button, active ? 2 : 1, 0);
            lv_obj_set_style_text_color(ref.label, active ? color_text : color_muted, 0);
        }
    }

    void update_status_icons()
    {
        if (usb_transport_active())
        {
            lv_label_set_text(g_ui.iconPrimary, "USB");
            lv_obj_set_style_text_color(g_ui.iconPrimary,
                                        g_state.runtime.usbState == UiUsbState::Live ? color_lime :
                                        (g_state.runtime.usbState == UiUsbState::Error ? color_error : color_warn),
                                        0);
            lv_obj_add_flag(g_ui.iconSecondary, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        if (network_transport_active())
        {
            lv_label_set_text(g_ui.iconPrimary, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(g_ui.iconPrimary,
                                        g_state.runtime.simHubState == UiSimHubState::Live ? color_lime :
                                        (g_state.runtime.simHubState == UiSimHubState::Error ? color_error : color_warn),
                                        0);
            lv_obj_add_flag(g_ui.iconSecondary, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        lv_label_set_text(g_ui.iconPrimary, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(g_ui.iconPrimary,
                                    g_state.runtime.staConnected || g_state.runtime.apActive ? color_lime :
                                    (g_state.runtime.staConnecting ? color_warn : color_error),
                                    0);
        lv_obj_clear_flag(g_ui.iconSecondary, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_ui.iconSecondary, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(g_ui.iconSecondary,
                                    g_state.runtime.bleConnected ? color_cyan :
                                    (g_state.runtime.bleConnecting ? color_warn : color_error),
                                    0);
    }

    void update_home_view()
    {
        set_label_text(g_ui.statusChipLabel, status_chip_text());
        set_label_text(g_ui.heroKicker, metric_title(g_state.runtime.settings.displayFocus));
        set_label_text(g_ui.heroValue, metric_value(g_state.runtime.settings.displayFocus));
        set_label_text(g_ui.heroUnit, metric_unit(g_state.runtime.settings.displayFocus));
        set_label_text(g_ui.heroMeta, hero_meta_text() + "\n" + secondary_status_text());
        lv_obj_set_style_text_color(g_ui.heroValue, metric_color(g_state.runtime.settings.displayFocus, g_state.runtime.shift), 0);
        lv_obj_set_style_text_color(g_ui.heroUnit, metric_color(g_state.runtime.settings.displayFocus, g_state.runtime.shift), 0);
        set_label_text(g_ui.heroBadges[0].label, "G " + gear_text(g_state.runtime.gear));
        set_label_text(g_ui.heroBadges[1].label, std::to_string(g_state.runtime.speedKmh) + " km/h");
        set_label_text(g_ui.heroBadges[2].label, g_state.runtime.shift ? "SHIFT" : "Bereit");
        lv_obj_set_style_bg_color(g_ui.heroBadges[2].button, g_state.runtime.shift ? color_warn : lv_color_hex(0x111B28), 0);
        lv_obj_set_style_text_color(g_ui.heroBadges[2].label, g_state.runtime.shift ? lv_color_black() : color_text, 0);
        update_status_icons();
        update_action_button_styles();
        if (g_state.runtime.settings.tutorialSeen)
        {
            lv_obj_add_flag(g_ui.homeHint, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(g_ui.homeHint, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void update_focus_view()
    {
        const UiDisplayFocusMetric metric = g_state.fullscreenMetric;
        set_label_text(g_ui.focusStatus, status_chip_text());
        set_label_text(g_ui.focusValue, metric_value(metric));
        set_label_text(g_ui.focusUnit, metric_unit(metric));
        set_label_text(g_ui.focusMeta, metric_title(metric) + " | G " + gear_text(g_state.runtime.gear) + " | " + std::to_string(g_state.runtime.speedKmh) + " km/h");
        lv_obj_set_style_text_color(g_ui.focusValue, metric_color(metric, g_state.runtime.shift), 0);
        lv_obj_set_style_text_color(g_ui.focusUnit, metric_color(metric, g_state.runtime.shift), 0);
    }

    void update_picker_view()
    {
        if (g_state.activeScreen == UiScreenId::DisplayPicker)
        {
            const UiDisplayFocusMetric metric = g_state.runtime.settings.displayFocus;
            set_label_text(g_ui.pickerTitle, "Anzeige waehlen");
            set_label_text(g_ui.pickerSubtitle, "Links / rechts wischen und live pruefen.");
            set_label_text(g_ui.pickerValue, metric_value(metric));
            set_label_text(g_ui.pickerUnit, metric_unit(metric));
            set_label_text(g_ui.pickerMeta, "Aktiv: " + metric_title(metric));
            lv_obj_set_style_text_color(g_ui.pickerValue, metric_color(metric, g_state.runtime.shift), 0);
            lv_obj_set_style_text_color(g_ui.pickerUnit, metric_color(metric, g_state.runtime.shift), 0);
            set_label_text(g_ui.pickerHint, "Wischen wechselt. Web fuer Details.");
        }
        else
        {
            const UiTelemetryPreference preference = g_state.runtime.settings.telemetryPreference;
            set_label_text(g_ui.pickerTitle, "Quelle waehlen");
            set_label_text(g_ui.pickerSubtitle, "Auto erkennt USB, SimHub und OBD selbst.");
            set_label_text(g_ui.pickerValue, telemetry_preference_text(preference));
            set_label_text(g_ui.pickerUnit, "MODE");
            set_label_text(g_ui.pickerMeta, telemetry_preference_hint(preference));
            lv_obj_set_style_text_color(g_ui.pickerValue, preference == UiTelemetryPreference::SimHub ? color_cyan : (preference == UiTelemetryPreference::Obd ? color_lime : color_violet), 0);
            lv_obj_set_style_text_color(g_ui.pickerUnit, color_muted, 0);
            set_label_text(g_ui.pickerHint, "Wischen wechselt Auto / OBD / Sim.");
        }

        for (int i = 0; i < kPickerOptionCount; ++i)
        {
            set_label_text(g_ui.pickerOptions[static_cast<size_t>(i)].label, picker_option_label(i));
        }
        update_picker_option_styles();
    }

    void update_web_view()
    {
        std::string title = "Web Setup";
        if (g_state.webTarget == WebTarget::Display)
        {
            title = "Anzeige im Web";
        }
        else if (g_state.webTarget == WebTarget::Telemetry)
        {
            title = "Telemetrie im Web";
        }

        const std::string url = build_web_url(g_state.webTarget);
        set_label_text(g_ui.webTitle, title);
        set_label_text(g_ui.webSubtitle, "QR scannen und direkt in die passende Web-Einstellung springen.");
        set_label_text(g_ui.webUrl, url);
#if LV_USE_QRCODE
        if (g_ui.webQr)
        {
            lv_qrcode_update(g_ui.webQr, url.c_str(), static_cast<uint32_t>(url.size()));
        }
#endif
        set_label_text(g_ui.webHint, usb_bridge_web_selected() ? "Im USB-only oder aktivem USB-Pfad ist die gleiche Seite auch lokal auf dem PC offen." : "Im WLAN oder AP direkt mit dem Handy aufrufen.");
    }

    void update_logo_overlay()
    {
        if (!g_ui.logoOverlay)
        {
            return;
        }
        if (g_state.logoUntilMs > now_ms())
        {
            lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void update_screen_visibility()
    {
        const bool homeVisible = g_state.activeScreen == UiScreenId::Home;
        const bool focusVisible = g_state.activeScreen == UiScreenId::Focus;
        const bool pickerVisible = g_state.activeScreen == UiScreenId::DisplayPicker || g_state.activeScreen == UiScreenId::SourcePicker;
        const bool webVisible = g_state.activeScreen == UiScreenId::WebLink;
        homeVisible ? lv_obj_clear_flag(g_ui.homeLayer, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(g_ui.homeLayer, LV_OBJ_FLAG_HIDDEN);
        focusVisible ? lv_obj_clear_flag(g_ui.focusLayer, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(g_ui.focusLayer, LV_OBJ_FLAG_HIDDEN);
        pickerVisible ? lv_obj_clear_flag(g_ui.pickerLayer, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(g_ui.pickerLayer, LV_OBJ_FLAG_HIDDEN);
        webVisible ? lv_obj_clear_flag(g_ui.webLayer, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(g_ui.webLayer, LV_OBJ_FLAG_HIDDEN);
    }

    void refresh_view()
    {
        update_screen_visibility();
        update_home_view();
        update_focus_view();
        update_picker_view();
        update_web_view();
        update_logo_overlay();
    }

    void show_home()
    {
        g_state.activeScreen = UiScreenId::Home;
        refresh_view();
    }

    void show_focus(UiDisplayFocusMetric metric)
    {
        g_state.fullscreenMetric = metric;
        g_state.activeScreen = UiScreenId::Focus;
        g_state.runtime.settings.tutorialSeen = true;
        persist_settings();
        refresh_view();
    }

    void show_display_picker()
    {
        g_state.activeScreen = UiScreenId::DisplayPicker;
        g_state.runtime.settings.tutorialSeen = true;
        persist_settings();
        refresh_view();
    }

    void show_source_picker()
    {
        g_state.activeScreen = UiScreenId::SourcePicker;
        g_state.runtime.settings.tutorialSeen = true;
        persist_settings();
        refresh_view();
    }

    void show_web_link(WebTarget target, UiScreenId returnScreen)
    {
        g_state.webTarget = target;
        g_state.webReturnScreen = returnScreen;
        g_state.activeScreen = UiScreenId::WebLink;
        refresh_view();
    }

    void cycle_focus_metric(int delta)
    {
        const int next = wrap_index(static_cast<int>(g_state.fullscreenMetric) + delta, kPickerOptionCount);
        g_state.fullscreenMetric = static_cast<UiDisplayFocusMetric>(next);
    }

    void handle_picker_swipe(bool next)
    {
        if (g_state.activeScreen == UiScreenId::DisplayPicker)
        {
            cycle_display_focus(next ? 1 : -1);
        }
        else if (g_state.activeScreen == UiScreenId::SourcePicker)
        {
            cycle_telemetry_preference(next ? 1 : -1);
        }
        refresh_view();
    }

    void on_home_action(lv_event_t *e)
    {
        const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        set_selected_action(index);
        switch (static_cast<HomeAction>(index))
        {
        case HomeAction::Display:
            show_display_picker();
            break;
        case HomeAction::Source:
            show_source_picker();
            break;
        case HomeAction::Web:
        default:
            show_web_link(WebTarget::Overview, UiScreenId::Home);
            break;
        }
    }

    void on_focus_badge(lv_event_t *e)
    {
        const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        const UiDisplayFocusMetric metric = index == 0 ? UiDisplayFocusMetric::Gear : UiDisplayFocusMetric::Speed;
        show_focus(metric);
    }

    void on_hero_click(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_focus(g_state.runtime.settings.displayFocus);
    }

    void on_back_home(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_home();
    }

    void on_focus_click(lv_event_t *e)
    {
        LV_UNUSED(e);
        const uint32_t now = now_ms();
        if (now - g_state.lastFocusTapMs <= kDoubleTapWindowMs)
        {
            g_state.lastFocusTapMs = 0;
            show_home();
            return;
        }
        g_state.lastFocusTapMs = now;
    }

    void on_focus_gesture(lv_event_t *e)
    {
        if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        {
            return;
        }
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev)
        {
            return;
        }
        const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT)
        {
            cycle_focus_metric(1);
        }
        else if (dir == LV_DIR_RIGHT)
        {
            cycle_focus_metric(-1);
        }
        refresh_view();
    }

    void on_picker_gesture(lv_event_t *e)
    {
        if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        {
            return;
        }
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev)
        {
            return;
        }
        const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT)
        {
            handle_picker_swipe(true);
        }
        else if (dir == LV_DIR_RIGHT)
        {
            handle_picker_swipe(false);
        }
    }

    void on_picker_option(lv_event_t *e)
    {
        const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        if (g_state.activeScreen == UiScreenId::DisplayPicker)
        {
            set_display_focus(static_cast<UiDisplayFocusMetric>(index));
        }
        else
        {
            set_telemetry_preference(static_cast<UiTelemetryPreference>(index));
        }
        refresh_view();
    }

    void on_picker_web(lv_event_t *e)
    {
        LV_UNUSED(e);
        show_web_link(g_state.activeScreen == UiScreenId::DisplayPicker ? WebTarget::Display : WebTarget::Telemetry, g_state.activeScreen);
    }

    void on_web_back(lv_event_t *e)
    {
        LV_UNUSED(e);
        if (g_state.webReturnScreen == UiScreenId::DisplayPicker)
        {
            show_display_picker();
        }
        else if (g_state.webReturnScreen == UiScreenId::SourcePicker)
        {
            show_source_picker();
        }
        else
        {
            show_home();
        }
    }

    void build_home_layer()
    {
        g_ui.homeLayer = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.homeLayer);
        lv_obj_set_size(g_ui.homeLayer, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.homeLayer, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.statusChip = lv_obj_create(g_ui.homeLayer);
        lv_obj_remove_style_all(g_ui.statusChip);
        lv_obj_add_style(g_ui.statusChip, &stylePill, 0);
        lv_obj_set_size(g_ui.statusChip, 168, 38);
        lv_obj_align(g_ui.statusChip, LV_ALIGN_TOP_LEFT, 14, 14);
        lv_obj_clear_flag(g_ui.statusChip, LV_OBJ_FLAG_SCROLLABLE);

        g_ui.statusChipLabel = lv_label_create(g_ui.statusChip);
        lv_obj_set_style_text_font(g_ui.statusChipLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(g_ui.statusChipLabel, color_text, 0);
        lv_obj_center(g_ui.statusChipLabel);

        g_ui.iconPrimary = lv_label_create(g_ui.homeLayer);
        lv_obj_set_style_text_font(g_ui.iconPrimary, &lv_font_montserrat_24, 0);
        lv_obj_align(g_ui.iconPrimary, LV_ALIGN_TOP_RIGHT, -58, 18);

        g_ui.iconSecondary = lv_label_create(g_ui.homeLayer);
        lv_obj_set_style_text_font(g_ui.iconSecondary, &lv_font_montserrat_24, 0);
        lv_obj_align(g_ui.iconSecondary, LV_ALIGN_TOP_RIGHT, -18, 18);

        g_ui.heroCard = make_panel(g_ui.homeLayer, 12, 60, 256, 212, true);
        lv_obj_add_event_cb(g_ui.heroCard, on_hero_click, LV_EVENT_CLICKED, nullptr);

        g_ui.heroKicker = lv_label_create(g_ui.heroCard);
        lv_obj_set_style_text_font(g_ui.heroKicker, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.heroKicker, &styleMuted, 0);
        lv_obj_align(g_ui.heroKicker, LV_ALIGN_TOP_LEFT, 0, 2);

        g_ui.heroValue = lv_label_create(g_ui.heroCard);
        lv_obj_set_style_text_font(g_ui.heroValue, &lv_font_montserrat_48, 0);
        lv_obj_align(g_ui.heroValue, LV_ALIGN_TOP_LEFT, 0, 48);

        g_ui.heroUnit = lv_label_create(g_ui.heroCard);
        lv_obj_set_style_text_font(g_ui.heroUnit, &lv_font_montserrat_24, 0);
        lv_obj_align(g_ui.heroUnit, LV_ALIGN_TOP_LEFT, 0, 102);

        g_ui.heroMeta = lv_label_create(g_ui.heroCard);
        lv_obj_set_width(g_ui.heroMeta, 226);
        lv_label_set_long_mode(g_ui.heroMeta, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.heroMeta, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.heroMeta, &styleMuted, 0);
        lv_obj_align(g_ui.heroMeta, LV_ALIGN_TOP_LEFT, 0, 134);

        g_ui.heroBadges[0] = make_button(g_ui.homeLayer, 74, 42, "G N", on_focus_badge, reinterpret_cast<void *>(static_cast<intptr_t>(0)));
        lv_obj_add_style(g_ui.heroBadges[0].button, &styleBadge, 0);
        lv_obj_align(g_ui.heroBadges[0].button, LV_ALIGN_TOP_LEFT, 16, 286);

        g_ui.heroBadges[1] = make_button(g_ui.homeLayer, 110, 42, "0 km/h", on_focus_badge, reinterpret_cast<void *>(static_cast<intptr_t>(1)));
        lv_obj_add_style(g_ui.heroBadges[1].button, &styleBadge, 0);
        lv_obj_align(g_ui.heroBadges[1].button, LV_ALIGN_TOP_LEFT, 96, 286);

        g_ui.heroBadges[2] = make_button(g_ui.homeLayer, 72, 42, "Bereit", nullptr, nullptr);
        lv_obj_add_style(g_ui.heroBadges[2].button, &styleBadge, 0);
        lv_obj_align(g_ui.heroBadges[2].button, LV_ALIGN_TOP_RIGHT, -16, 286);

        for (int i = 0; i < kActionCount; ++i)
        {
            g_ui.actionButtons[static_cast<size_t>(i)] = make_button(g_ui.homeLayer, 76, 48, action_label(static_cast<HomeAction>(i)).c_str(), on_home_action, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
            lv_obj_align(g_ui.actionButtons[static_cast<size_t>(i)].button, LV_ALIGN_TOP_LEFT, 16 + i * 86, 344);
        }

        g_ui.homeHint = lv_label_create(g_ui.homeLayer);
        lv_obj_set_width(g_ui.homeHint, 248);
        lv_label_set_long_mode(g_ui.homeHint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.homeHint, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.homeHint, &styleMuted, 0);
        lv_label_set_text(g_ui.homeHint, "Tippen = Vollbild | Wischen = wechseln");
        lv_obj_align(g_ui.homeHint, LV_ALIGN_BOTTOM_LEFT, 16, -14);
    }

    void build_focus_layer()
    {
        g_ui.focusLayer = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.focusLayer);
        lv_obj_set_size(g_ui.focusLayer, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.focusLayer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_ui.focusLayer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(g_ui.focusLayer, on_focus_click, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(g_ui.focusLayer, on_focus_gesture, LV_EVENT_GESTURE, nullptr);

        g_ui.focusStatus = lv_label_create(g_ui.focusLayer);
        lv_obj_set_style_text_font(g_ui.focusStatus, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.focusStatus, &styleMuted, 0);
        lv_obj_align(g_ui.focusStatus, LV_ALIGN_TOP_MID, 0, 22);

        g_ui.focusValue = lv_label_create(g_ui.focusLayer);
        lv_obj_set_style_text_font(g_ui.focusValue, &lv_font_montserrat_48, 0);
        lv_obj_align(g_ui.focusValue, LV_ALIGN_CENTER, 0, -28);

        g_ui.focusUnit = lv_label_create(g_ui.focusLayer);
        lv_obj_set_style_text_font(g_ui.focusUnit, &lv_font_montserrat_24, 0);
        lv_obj_align(g_ui.focusUnit, LV_ALIGN_CENTER, 0, 28);

        g_ui.focusMeta = lv_label_create(g_ui.focusLayer);
        lv_obj_set_width(g_ui.focusMeta, 250);
        lv_label_set_long_mode(g_ui.focusMeta, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.focusMeta, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.focusMeta, &styleMuted, 0);
        lv_obj_align(g_ui.focusMeta, LV_ALIGN_BOTTOM_MID, 0, -66);

        g_ui.focusHint = lv_label_create(g_ui.focusLayer);
        lv_obj_set_style_text_font(g_ui.focusHint, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.focusHint, &styleMuted, 0);
        lv_label_set_text(g_ui.focusHint, "Doppeltippen schliesst | Wischen wechselt");
        lv_obj_align(g_ui.focusHint, LV_ALIGN_BOTTOM_MID, 0, -20);
    }

    void build_picker_layer()
    {
        g_ui.pickerLayer = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.pickerLayer);
        lv_obj_set_size(g_ui.pickerLayer, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.pickerLayer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_ui.pickerLayer, LV_OBJ_FLAG_HIDDEN);

        g_ui.pickerBack = make_button(g_ui.pickerLayer, 78, 42, LV_SYMBOL_LEFT " Zur.", on_back_home).button;
        lv_obj_add_style(g_ui.pickerBack, &styleBackButton, 0);
        lv_obj_align(g_ui.pickerBack, LV_ALIGN_TOP_LEFT, 12, 12);

        g_ui.pickerTitle = lv_label_create(g_ui.pickerLayer);
        lv_obj_set_style_text_font(g_ui.pickerTitle, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(g_ui.pickerTitle, color_text, 0);
        lv_obj_set_width(g_ui.pickerTitle, 248);
        lv_obj_align(g_ui.pickerTitle, LV_ALIGN_TOP_LEFT, 16, 66);

        g_ui.pickerSubtitle = lv_label_create(g_ui.pickerLayer);
        lv_obj_set_width(g_ui.pickerSubtitle, 248);
        lv_label_set_long_mode(g_ui.pickerSubtitle, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.pickerSubtitle, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.pickerSubtitle, &styleMuted, 0);
        lv_obj_align(g_ui.pickerSubtitle, LV_ALIGN_TOP_LEFT, 16, 108);

        g_ui.pickerPreview = make_panel(g_ui.pickerLayer, 16, 154, 248, 148, true);
        lv_obj_add_event_cb(g_ui.pickerPreview, on_picker_gesture, LV_EVENT_GESTURE, nullptr);

        g_ui.pickerValue = lv_label_create(g_ui.pickerPreview);
        lv_obj_set_style_text_font(g_ui.pickerValue, &lv_font_montserrat_48, 0);
        lv_obj_align(g_ui.pickerValue, LV_ALIGN_TOP_MID, 0, 10);

        g_ui.pickerUnit = lv_label_create(g_ui.pickerPreview);
        lv_obj_set_style_text_font(g_ui.pickerUnit, &lv_font_montserrat_24, 0);
        lv_obj_align(g_ui.pickerUnit, LV_ALIGN_TOP_MID, 0, 58);

        g_ui.pickerMeta = lv_label_create(g_ui.pickerPreview);
        lv_obj_set_width(g_ui.pickerMeta, 214);
        lv_label_set_long_mode(g_ui.pickerMeta, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.pickerMeta, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.pickerMeta, &styleMuted, 0);
        lv_obj_align(g_ui.pickerMeta, LV_ALIGN_BOTTOM_MID, 0, -10);

        for (int i = 0; i < kPickerOptionCount; ++i)
        {
            g_ui.pickerOptions[static_cast<size_t>(i)] = make_button(g_ui.pickerLayer, 74, 44, "", on_picker_option, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
            lv_obj_align(g_ui.pickerOptions[static_cast<size_t>(i)].button, LV_ALIGN_TOP_LEFT, 16 + i * 84, 318);
        }

        g_ui.pickerHint = lv_label_create(g_ui.pickerLayer);
        lv_obj_set_width(g_ui.pickerHint, 248);
        lv_label_set_long_mode(g_ui.pickerHint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.pickerHint, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.pickerHint, &styleMuted, 0);
        lv_obj_align(g_ui.pickerHint, LV_ALIGN_TOP_LEFT, 16, 372);

        ButtonRef webButton = make_button(g_ui.pickerLayer, 164, 46, "Mehr im Web", on_picker_web);
        g_ui.pickerWebButton = webButton.button;
        lv_obj_align(g_ui.pickerWebButton, LV_ALIGN_BOTTOM_MID, 0, -14);
    }

    void build_web_layer()
    {
        g_ui.webLayer = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.webLayer);
        lv_obj_set_size(g_ui.webLayer, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.webLayer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_ui.webLayer, LV_OBJ_FLAG_HIDDEN);

        g_ui.webBack = make_button(g_ui.webLayer, 78, 42, LV_SYMBOL_LEFT " Zur.", on_web_back).button;
        lv_obj_add_style(g_ui.webBack, &styleBackButton, 0);
        lv_obj_align(g_ui.webBack, LV_ALIGN_TOP_LEFT, 12, 12);

        g_ui.webTitle = lv_label_create(g_ui.webLayer);
        lv_obj_set_style_text_font(g_ui.webTitle, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(g_ui.webTitle, color_text, 0);
        lv_obj_set_width(g_ui.webTitle, 248);
        lv_obj_align(g_ui.webTitle, LV_ALIGN_TOP_LEFT, 16, 66);

        g_ui.webSubtitle = lv_label_create(g_ui.webLayer);
        lv_obj_set_width(g_ui.webSubtitle, 248);
        lv_label_set_long_mode(g_ui.webSubtitle, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.webSubtitle, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.webSubtitle, &styleMuted, 0);
        lv_obj_align(g_ui.webSubtitle, LV_ALIGN_TOP_LEFT, 16, 108);

        lv_obj_t *qrPanel = make_panel(g_ui.webLayer, 44, 154, 192, 192, false);
#if LV_USE_QRCODE
        g_ui.webQr = lv_qrcode_create(qrPanel, 168, color_text, color_bg);
        lv_obj_center(g_ui.webQr);
#endif

        g_ui.webUrl = lv_label_create(g_ui.webLayer);
        lv_obj_set_width(g_ui.webUrl, 248);
        lv_label_set_long_mode(g_ui.webUrl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.webUrl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(g_ui.webUrl, color_cyan, 0);
        lv_obj_align(g_ui.webUrl, LV_ALIGN_TOP_LEFT, 16, 356);

        g_ui.webHint = lv_label_create(g_ui.webLayer);
        lv_obj_set_width(g_ui.webHint, 248);
        lv_label_set_long_mode(g_ui.webHint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(g_ui.webHint, &lv_font_montserrat_16, 0);
        lv_obj_add_style(g_ui.webHint, &styleMuted, 0);
        lv_obj_align(g_ui.webHint, LV_ALIGN_BOTTOM_LEFT, 16, -18);
    }

    void build_logo_overlay()
    {
        g_ui.logoOverlay = lv_obj_create(g_ui.root);
        lv_obj_remove_style_all(g_ui.logoOverlay);
        lv_obj_add_style(g_ui.logoOverlay, &styleLogoOverlay, 0);
        lv_obj_set_size(g_ui.logoOverlay, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(g_ui.logoOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_ui.logoOverlay, LV_OBJ_FLAG_HIDDEN);

        g_ui.logoLabel = lv_label_create(g_ui.logoOverlay);
        lv_label_set_text(g_ui.logoLabel, "ShiftLight");
        lv_obj_set_style_text_font(g_ui.logoLabel, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(g_ui.logoLabel, color_text, 0);
        lv_obj_center(g_ui.logoLabel);
    }

    void build_ui()
    {
        g_ui.root = lv_obj_create(nullptr);
        lv_obj_remove_style_all(g_ui.root);
        lv_obj_add_style(g_ui.root, &styleScreen, 0);
        lv_obj_set_size(g_ui.root, 280, 456);
        lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
        build_home_layer();
        build_focus_layer();
        build_picker_layer();
        build_web_layer();
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
    g_state.logoUntilMs = now_ms() + kLogoDurationMs;
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
        else if (g_state.activeScreen == UiScreenId::DisplayPicker)
        {
            cycle_display_focus(-1);
        }
        else if (g_state.activeScreen == UiScreenId::SourcePicker)
        {
            cycle_telemetry_preference(-1);
        }
        else if (g_state.activeScreen == UiScreenId::Focus)
        {
            cycle_focus_metric(-1);
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
        else if (g_state.activeScreen == UiScreenId::DisplayPicker)
        {
            cycle_display_focus(1);
        }
        else if (g_state.activeScreen == UiScreenId::SourcePicker)
        {
            cycle_telemetry_preference(1);
        }
        else if (g_state.activeScreen == UiScreenId::Focus)
        {
            cycle_focus_metric(1);
        }
        else if (g_state.activeScreen == UiScreenId::WebLink)
        {
            cycle_web_target(1);
        }
        break;
    case UiDebugAction::OpenSelectedCard:
        if (g_state.activeScreen == UiScreenId::Home)
        {
            switch (static_cast<HomeAction>(wrap_index(g_state.selectedAction, kActionCount)))
            {
            case HomeAction::Display:
                show_display_picker();
                break;
            case HomeAction::Source:
                show_source_picker();
                break;
            case HomeAction::Web:
            default:
                show_web_link(WebTarget::Overview, UiScreenId::Home);
                break;
            }
        }
        else if (g_state.activeScreen == UiScreenId::DisplayPicker)
        {
            show_web_link(WebTarget::Display, UiScreenId::DisplayPicker);
        }
        else if (g_state.activeScreen == UiScreenId::SourcePicker)
        {
            show_web_link(WebTarget::Telemetry, UiScreenId::SourcePicker);
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
