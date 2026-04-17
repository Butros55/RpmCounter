#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "bluetooth/ble_obd.h"
#include "core/config.h"
#include "core/wifi.h"
#include "hardware/ambient_light.h"
#include "hardware/led_bar.h"
#include "hardware/gesture_sensor.h"
#include "hardware/logo_anim.h"
#include "core/vehicle_info.h"
#include "core/state.h"
#include "hardware/display.h"
#include "core/logging.h"
#include "telemetry/telemetry_manager.h"
#include "telemetry/usb_sim_bridge.h"
#include "web_ui_pages.h"
#include <core/utils.h>

namespace
{
    WebServer server(80);

    String httpMethodName()
    {
        switch (server.method())
        {
        case HTTP_GET:
            return "GET";
        case HTTP_POST:
            return "POST";
        default:
            return "OTHER";
        }
    }

    void markHttpActivity(const char *code)
    {
        g_lastHttpMs = millis();
        LOG_DEBUG("WEB", code, String("method=") + httpMethodName() + " uri=" + server.uri());
    }

    String currentWebBaseUrl()
    {
        WifiStatus status = getWifiStatus();
        if (status.ip.length() > 0)
            return String(F("http://")) + status.ip;
        if (status.staIp.length() > 0)
            return String(F("http://")) + status.staIp;
        if (status.apIp.length() > 0)
            return String(F("http://")) + status.apIp;
        return String(F("http://192.168.4.1"));
    }

    String currentBridgeBaseUrl()
    {
        String host = g_usbBridgeHost;
        host.trim();
        if (host.isEmpty() || host == F("USB Bridge"))
            return "";
        return String(F("http://")) + host + F(":8765");
    }

    bool requestCameViaBridgeProxy()
    {
        return server.hasHeader("X-ShiftLight-Bridge-Proxy");
    }

    bool shouldRedirectToBridgeUi()
    {
        if (!usbSimBridgeOnline())
            return false;
        if (requestCameViaBridgeProxy())
            return false;
        if (server.hasArg("direct"))
            return false;
        return currentBridgeBaseUrl().length() > 0;
    }

    bool maybeRedirectToBridgeUi()
    {
        if (!shouldRedirectToBridgeUi())
            return false;

        const String target = currentBridgeBaseUrl() + server.uri();
        LOG_INFO("WEB", "WEB_REDIRECT_BRIDGE", String("target=") + target);
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate", true);
        server.sendHeader("Location", target, true);
        server.send(303, "text/plain", "Redirecting to USB bridge web UI");
        return true;
    }

    String currentIpString()
    {
        WifiStatus status = getWifiStatus();
        if (status.ip.length() > 0)
            return status.ip;
        return WiFi.softAPIP().toString();
    }

    String colorToHex(const RgbColor &color)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", color.r, color.g, color.b);
        return String(buf);
    }

    RgbColor parseHexColor(const String &value, const RgbColor &fallback)
    {
        if (value.length() != 7 || value[0] != '#')
            return fallback;
        RgbColor c{};
        c.r = static_cast<uint8_t>(strtol(value.substring(1, 3).c_str(), nullptr, 16));
        c.g = static_cast<uint8_t>(strtol(value.substring(3, 5).c_str(), nullptr, 16));
        c.b = static_cast<uint8_t>(strtol(value.substring(5, 7).c_str(), nullptr, 16));
        return c;
    }

    String jsonEscape(const String &input)
    {
        String out;
        out.reserve(input.length());
        for (size_t i = 0; i < input.length(); ++i)
        {
            char c = input[i];
            switch (c)
            {
            case '\\':
            case '"':
                out += '\\';
                out += c;
                break;
            case '\n':
                out += F("\\n");
                break;
            case '\r':
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    String wifiModeToString(WifiMode mode)
    {
        switch (mode)
        {
        case STA_ONLY:
            return "STA_ONLY";
        case STA_WITH_AP_FALLBACK:
            return "STA_WITH_AP_FALLBACK";
        case AP_ONLY:
        default:
            return "AP_ONLY";
        }
    }

    String telemetryPreferenceToString(TelemetryPreference preference)
    {
        switch (preference)
        {
        case TelemetryPreference::Obd:
            return "OBD";
        case TelemetryPreference::SimHub:
            return "SIM";
        case TelemetryPreference::Auto:
        default:
            return "AUTO";
        }
    }

    String telemetryPreferenceLabel(TelemetryPreference preference)
    {
        switch (preference)
        {
        case TelemetryPreference::Obd:
            return "Nur OBD";
        case TelemetryPreference::SimHub:
            return "Nur Sim / PC";
        case TelemetryPreference::Auto:
        default:
            return "Automatisch";
        }
    }

    String simTransportPreferenceToString(SimTransportPreference preference)
    {
        switch (preference)
        {
        case SimTransportPreference::UsbSerial:
            return "USB";
        case SimTransportPreference::Network:
            return "NETWORK";
        case SimTransportPreference::Auto:
        default:
            return "AUTO";
        }
    }

    String simTransportModeToString(SimRuntimeTransportMode mode)
    {
        switch (mode)
        {
        case SimRuntimeTransportMode::UsbOnly:
            return "USB_ONLY";
        case SimRuntimeTransportMode::NetworkOnly:
            return "NETWORK_ONLY";
        case SimRuntimeTransportMode::Auto:
            return "AUTO";
        case SimRuntimeTransportMode::Disabled:
        default:
            return "DISABLED";
        }
    }

    String activeTelemetrySourceLabel(ActiveTelemetrySource source)
    {
        switch (source)
        {
        case ActiveTelemetrySource::Obd:
            return "OBD";
        case ActiveTelemetrySource::SimHubNetwork:
            return "SimHub";
        case ActiveTelemetrySource::UsbSim:
            return "USB Sim";
        case ActiveTelemetrySource::None:
        default:
            return "Keine";
        }
    }

    String usbBridgeStateLabel(UsbBridgeConnectionState state)
    {
        switch (state)
        {
        case UsbBridgeConnectionState::Disconnected:
            return "USB getrennt";
        case UsbBridgeConnectionState::WaitingForBridge:
            return "Warte auf Bridge";
        case UsbBridgeConnectionState::WaitingForData:
            return "Warte auf Telemetrie";
        case UsbBridgeConnectionState::Live:
            return "USB live";
        case UsbBridgeConnectionState::Error:
            return "USB Fehler";
        case UsbBridgeConnectionState::Disabled:
        default:
            return "USB aus";
        }
    }

    String simHubConnectionStateLabel(SimHubConnectionState state)
    {
        switch (state)
        {
        case SimHubConnectionState::WaitingForHost:
            return "Host fehlt";
        case SimHubConnectionState::WaitingForNetwork:
            return "WLAN fehlt";
        case SimHubConnectionState::WaitingForData:
            return "Warte auf SimHub-Daten";
        case SimHubConnectionState::Live:
            return "Live";
        case SimHubConnectionState::Error:
            return "Verbindung fehlgeschlagen";
        case SimHubConnectionState::Disabled:
        default:
            return "Deaktiviert";
        }
    }

    String gearSourceLabel()
    {
        switch (g_activeTelemetrySource)
        {
        case ActiveTelemetrySource::UsbSim:
        case ActiveTelemetrySource::SimHubNetwork:
            return "SimHub direkt";
        case ActiveTelemetrySource::Obd:
            return "OBD berechnet";
        case ActiveTelemetrySource::None:
        default:
            return "Keine Quelle";
        }
    }

    String argTrimmed(const char *name, const String &fallback)
    {
        String value = server.hasArg(name) ? server.arg(name) : fallback;
        value.trim();
        return value;
    }

    void handleDevObdSend()
    {
        markHttpActivity("WEB_DEV_OBD_SEND");

        if (!g_connected)
        {
            server.send(400, "text/plain", "Nicht mit OBD verbunden.");
            return;
        }

        if (!server.hasArg("cmd"))
        {
            server.send(400, "text/plain", "Parameter 'cmd' fehlt.");
            return;
        }

        String cmd = server.arg("cmd");
        cmd.trim();
        if (cmd.length() == 0)
        {
            server.send(400, "text/plain", "Befehl ist leer.");
            return;
        }

        // Vorherigen Stand merken, damit wir sehen können, ob eine neue Zeile kommt
        String prevObd = g_lastObdInfo;
        unsigned long start = millis();
        const unsigned long timeoutMs = 800;

        // Befehl ohne CR senden – sendObdCommand hängt CR dran
        sendObdCommand(cmd);

        // Kurz auf eine neue Antwort warten (nicht ewig blocken)
        while (millis() - start < timeoutMs)
        {
            if (g_lastObdInfo != prevObd)
            {
                break;
            }
            delay(10);
            yield();
        }

        // Kleine JSON-Antwort mit letztem TX/OBD zurückgeben
        String json = "{";
        json += "\"status\":\"ok\"";
        json += ",\"lastTx\":\"" + jsonEscape(g_lastTxInfo) + "\"";
        json += ",\"lastObd\":\"" + jsonEscape(g_lastObdInfo) + "\"";
        json += "}";

        server.send(200, "application/json", json);
    }

    String safeLabel(const String &value, const String &fallback)
    {
        String trimmed = value;
        trimmed.trim();
        if (trimmed.isEmpty())
            return fallback;
        return trimmed;
    }

    String htmlEscape(const String &input)
    {
        String out;
        out.reserve(input.length());
        for (size_t i = 0; i < input.length(); ++i)
        {
            char c = input[i];
            switch (c)
            {
            case '&':
                out += F("&amp;");
                break;
            case '<':
                out += F("&lt;");
                break;
            case '>':
                out += F("&gt;");
                break;
            case '\"':
                out += F("&quot;");
                break;
            case '\'':
                out += F("&#39;");
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    void enforceOrder(int &g, int &y, int &r, int &b)
    {
        g = clampInt(g, 0, 100);
        if (y < g)
            y = g;
        y = clampInt(y, 0, 100);
        if (r < y)
            r = y;
        r = clampInt(r, 0, 100);
        b = clampInt(b, 0, 100);
    }

    String htmlPage()
    {
        String color1Name = safeLabel(cfg.greenLabel, F("Farbe 1"));
        String color2Name = safeLabel(cfg.yellowLabel, F("Farbe 2"));
        String color3Name = safeLabel(cfg.redLabel, F("Farbe 3"));

        String greenHex = colorToHex(cfg.greenColor);
        String yellowHex = colorToHex(cfg.yellowColor);
        String redHex = colorToHex(cfg.redColor);

        String page;
        page.reserve(19000);

        page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
        page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
        page += F("<title>ShiftLight Setup</title>");
        page += F(
            "<style>"
            "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#111;color:#eee;padding:16px;margin:0;}"
            "h1{font-size:20px;margin:0 0 12px 0;display:flex;align-items:center;justify-content:space-between;}"
            "label{display:block;margin-top:8px;}"
            "input,select{width:100%;padding:6px;margin-top:4px;border-radius:6px;border:1px solid #444;background:#222;color:#eee;}"
            "input[type=range]{padding:0;margin-top:4px;}"
            "button{margin-top:12px;width:100%;padding:10px;border:none;border-radius:6px;background:#0af;color:#000;font-weight:bold;font-size:14px;}"
            "button:disabled{background:#555;color:#888;}"
            ".btn-danger{background:#d33;color:#fff;}"
            ".row{margin-bottom:6px;}"
            ".small{font-size:12px;color:#aaa;}"
            ".section{margin-top:12px;padding:10px 12px;border-radius:8px;background:#181818;border:1px solid #333;}"
            ".section-title{font-weight:600;margin-bottom:8px;font-size:18px;letter-spacing:0.3px;}"
            ".toggle-row{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
            ".toggle-label{font-size:14px;}"
            ".switch{position:relative;display:inline-block;width:46px;height:24px;margin-left:8px;}"
            ".switch input{opacity:0;width:0;height:0;}"
            ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#555;transition:.2s;border-radius:24px;}"
            ".slider:before{position:absolute;content:'';height:18px;width:18px;left:3px;top:3px;background:#fff;transition:.2s;border-radius:50%;}"
            ".switch input:checked + .slider{background:#0af;}"
            ".switch input:checked + .slider:before{transform:translateX(22px);}"
            ".status-line{font-size:12px;color:#ccc;margin-top:4px;}"
            ".spinner{display:inline-block;width:12px;height:12px;border-radius:50%;border:2px solid rgba(255,255,255,0.2);border-top-color:#0af;animation:spin 1s linear infinite;margin-left:6px;}"
            ".hidden{display:none;}"
            ".color-row{display:flex;justify-content:space-between;gap:12px;margin-top:10px;padding:10px;border-radius:8px;background:#151515;border:1px solid #222;}"
            ".color-swatch{flex:1;text-align:center;}"
            ".color-swatch-title{font-size:12px;color:#bbb;margin-bottom:4px;}"
            ".color-circle{padding:0;border-radius:50%;border:2px solid #444;background:#111;width:44px;height:44px;}"
            ".color-circle::-webkit-color-swatch{border:none;border-radius:50%;padding:0;}"
            ".color-circle::-moz-color-swatch{border:none;border-radius:50%;padding:0;}"
            ".color-name{margin-top:4px;font-size:11px;color:#aaa;}"
            "@keyframes spin{from{transform:rotate(0deg);}to{transform:rotate(360deg);}}"
            ".range-group{margin-top:10px;padding-top:8px;border-top:1px solid #222;}"
            ".range-group:first-child{border-top:none;padding-top:0;}"
            ".disabled-field{opacity:0.5;}"
            ".led-preview-title{margin-top:10px;margin-bottom:4px;font-size:12px;color:#aaa;}"
            ".led-preview{display:flex;flex-wrap:nowrap;align-items:center;justify-content:center;padding:6px 4px;border-radius:6px;background:#151515;border:1px solid #222;}"
            ".led-dot{border-radius:50%;background:#333;flex:0 0 auto;}"
            "</style></head><body>");

        page += "<h1><span>ShiftLight Setup</span>";
        page += "<a href=\"/settings\" style='text-decoration:none;color:#0af;font-size:20px;'>⚙️</a></h1>";

        page += F("<form id='mainForm' method='POST' action='/save'>");

        // --- Allgemein ---
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Allgemein</div>");
        page += F("<label for='modeSelect'>Mode</label><select name='mode' id='modeSelect'>");
        page += "<option value='0'";
        if (cfg.mode == 0)
            page += " selected";
        page += ">Casual</option>";
        page += "<option value='1'";
        if (cfg.mode == 1)
            page += " selected";
        page += ">F1-Style</option>";
        page += "<option value='2'";
        if (cfg.mode == 2)
            page += " selected";
        page += ">Aggressiv</option>";
        page += "<option value='3'";
        if (cfg.mode == 3)
            page += " selected";
        page += ">GT3 / Endurance</option>";
        page += "</select>";

        page += F("<label for='brightness_slider'>Brightness (0-255)</label>");
        page += "<input type='range' min='0' max='255' value='" + String(cfg.brightness) + "' id='brightness_slider'>";
        page += "<div class='small'>Wert: <span id='bval'>" + String(cfg.brightness) + "</span></div>";
        page += "<input type='hidden' name='brightness' id='brightness' value='" + String(cfg.brightness) + "'>";

        page += F("<div class='color-row'>");
        page += F("<div class='color-swatch'><div class='color-swatch-title'>Low RPM</div>");
        page += "<input type='color' class='color-circle' id='greenColorInput' name='greenColor' value='" + greenHex + "'>";
        page += "<div class='color-name' id='color1Name'>" + htmlEscape(color1Name) + "</div>";
        page += "<input type='hidden' name='greenLabel' id='greenLabelHidden' value='" + htmlEscape(color1Name) + "'>";
        page += F("</div>");

        page += F("<div class='color-swatch'><div class='color-swatch-title'>Mid RPM</div>");
        page += "<input type='color' class='color-circle' id='yellowColorInput' name='yellowColor' value='" + yellowHex + "'>";
        page += "<div class='color-name' id='color2Name'>" + htmlEscape(color2Name) + "</div>";
        page += "<input type='hidden' name='yellowLabel' id='yellowLabelHidden' value='" + htmlEscape(color2Name) + "'>";
        page += F("</div>");

        page += F("<div class='color-swatch'><div class='color-swatch-title'>Shift / Warnung</div>");
        page += "<input type='color' class='color-circle' id='redColorInput' name='redColor' value='" + redHex + "'>";
        page += "<div class='color-name' id='color3Name'>" + htmlEscape(color3Name) + "</div>";
        page += "<input type='hidden' name='redLabel' id='redLabelHidden' value='" + htmlEscape(color3Name) + "'>";
        page += F("</div></div>");
        page += F("</div>");

        // --- Drehzahl-Bereich ---
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Drehzahl-Bereich</div>");
        page += F("<div class='toggle-row'><span class='toggle-label'>Auto-Scale Max RPM (benutze max gesehene Drehzahl)</span><label class='switch'>");
        page += "<input type='checkbox' id='autoscaleToggle' name='autoscale'";
        if (cfg.autoScaleMaxRpm)
            page += " checked";
        page += "><span class='slider'></span></label></div>";

        page += F("<div class='range-group' id='fixedMaxContainer'>");
        page += F("<label for='fixedMaxRpmInput'>Fixed Max RPM</label>");
        page += "<input type='number' id='fixedMaxRpmInput' name='fixedMaxRpm' min='1000' max='8000' value='" + String(cfg.fixedMaxRpm) + "'>";
        page += F("</div>");

        page += F("<div class='range-group'>");
        page += "<label class='rpm-label' id='greenEndLabel'><span class='rpm-label-title'>Low RPM</span><span class='rpm-label-range'>End (% von Max RPM)</span></label>";
        page += "<input type='range' name='greenEndPct' min='0' max='100' value='" + String(cfg.greenEndPct) + "' id='greenEndSlider' data-display='greenEndVal'>";
        page += "<div class='small'>Wert: <span id='greenEndVal'>" + String(cfg.greenEndPct) + "%</span></div></div>";

        page += F("<div class='range-group'>");
        page += "<label class='rpm-label' id='yellowEndLabel'><span class='rpm-label-title'>Mid RPM</span><span class='rpm-label-range'>End (% von Max RPM)</span></label>";
        page += "<input type='range' name='yellowEndPct' min='0' max='100' value='" + String(cfg.yellowEndPct) + "' id='yellowEndSlider' data-display='yellowEndVal'>";
        page += "<div class='small'>Wert: <span id='yellowEndVal'>" + String(cfg.yellowEndPct) + "%</span></div></div>";

        page += "<div class='range-group' id='redEndContainer'>";
        page += "<label class='rpm-label' id='redEndLabel'><span class='rpm-label-title'>Shift / Warnung</span><span class='rpm-label-range'>Fest rot bis (% von Max RPM)</span></label>";
        page += "<input type='range' name='redEndPct' min='0' max='100' value='" + String(cfg.redEndPct) + "' id='redEndSlider' data-display='redEndVal'>";
        page += "<div class='small'>Wert: <span id='redEndVal'>" + String(cfg.redEndPct) + "%</span></div></div>";

        page += "<div class='range-group' id='blinkStartContainer'>";
        page += "<label class='rpm-label' id='blinkStartLabel'><span class='rpm-label-title'>Blink Start</span><span class='rpm-label-range'>Fruehester Blinkpunkt (% von Max RPM)</span></label>";
        page += "<input type='range' name='blinkStartPct' min='0' max='100' value='" + String(cfg.blinkStartPct) + "' id='blinkStartSlider' data-display='blinkStartVal'>";
        page += "<div class='small'>Wert: <span id='blinkStartVal'>" + String(cfg.blinkStartPct) + "%</span></div></div>";

        page += F("<div class='led-preview-title small'>LED-Vorschau</div>");
        page += "<div id='ledPreview' class='led-preview' data-led-count='" + String(NUM_LEDS) + "'></div>";
        // Test-Button jetzt im Block Drehzahl-Bereich
        page += F("<button type='button' id='btnTest'>Testlauf: RPM-Sweep</button>");
        page += F("</div>");

        // --- Coming-Home / Leaving ---
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Coming-Home / Leaving</div>");
        page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Zündung an</span><label class='switch'>");
        page += "<input type='checkbox' name='logoIgnOn'";
        if (cfg.logoOnIgnitionOn)
            page += " checked";
        page += "><span class='slider'></span></label></div>";

        page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Motorstart</span><label class='switch'>");
        page += "<input type='checkbox' name='logoEngStart'";
        if (cfg.logoOnEngineStart)
            page += " checked";
        page += "><span class='slider'></span></label></div>";

        page += F("<div class='toggle-row'><span class='toggle-label'>Leaving-Animation bei Zündung aus</span><label class='switch'>");
        page += "<input type='checkbox' name='logoIgnOff'";
        if (cfg.logoOnIgnitionOff)
            page += " checked";
        page += "><span class='slider'></span></label></div>";
        page += F("</div>");

        // --- Mein Fahrzeug ---
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Mein Fahrzeug</div>");
        page += "<div class='row small'>Fahrzeug: <strong id='vehicleModel' data-base='" + htmlEscape(g_vehicleModel) + "'>" + htmlEscape(g_vehicleModel) + "</strong></div>";
        page += "<div class='row small'>VIN: <strong id='vehicleVin' data-base='" + htmlEscape(g_vehicleVin) + "'>" + htmlEscape(g_vehicleVin) + "</strong></div>";
        page += "<div class='row small'>Diagnose: <strong id='vehicleDiag' data-base='" + htmlEscape(g_vehicleDiagStatus) + "'>" + htmlEscape(g_vehicleDiagStatus) + "</strong></div>";
        page += F("</div>");

        if (g_devMode)
        {
            // --- OBD / Verbindung ---
            page += F("<div class='section'>");
            page += F("<div class='section-title'>OBD / Verbindung</div>");
            page += F("<div class='toggle-row'><span class='toggle-label'>OBD automatisch verbinden (Reconnect)</span><label class='switch'>");
            page += "<input type='checkbox' name='autoReconnect'";
            if (g_autoReconnect)
                page += " checked";
            page += "><span class='slider'></span></label></div>";
            page += F("<div class='status-line'>BLE-Status: <span id='bleStatus'>");
            page += g_connected ? "Verbunden" : "Getrennt";
            page += g_autoReconnect ? " (Auto-Reconnect AN)" : " (Auto-Reconnect AUS)";
            page += F("</span></div>");

            // Buttons jetzt in diesem Block:
            page += "<button type='button' id='btnConnect'";
            if (g_connected)
                page += " style='display:none'";
            page += ">Jetzt mit OBD verbinden</button>";
            page += "<button type='button' class='btn-danger' id='btnDisconnect'";
            if (!g_connected)
                page += " style='display:none'";
            page += ">OBD trennen</button>";

            page += F("</div>");

            // --- Display ---
            page += F("<div class='section' id='displayStatusBlock'><div class='section-title'>Display <span id='displaySpinner' class='spinner hidden'></span></div>");
            page += F("<div class='row small'>Init versucht: <span id='dispInit'>-</span> | Ready: <span id='dispReady'>-</span></div>");
            page += F("<div class='row small'>Panel/Buffer: <span id='dispPanel'>-</span> / <span id='dispBuf'>-</span> | Touch: <span id='dispTouch'>-</span></div>");
            page += F("<div class='row small'>LVGL Tick: <span id='dispTickMode'>-</span> | Debug-UI: <span id='dispDebugUi'>-</span></div>");
            page += F("<div class='row small'>Letzte LVGL-Ausführung (ms): <span id='dispLvgl'>-</span></div>");
            page += F("<div class='row small'>Fehler: <span id='dispError'>-</span></div>");
            page += F("<button type='button' id='btnDisplayStatus'>Status aktualisieren</button>");
            page += F("<button type='button' id='btnDisplayBars'>Testbild: Farb-Balken</button>");
            page += F("<button type='button' id='btnDisplayGrid'>Testbild: Raster/Helligkeit</button>");
            page += F("<button type='button' id='btnDisplayLogo'>BMW Logo auf Display anzeigen</button>");
            page += F("<div class='small'>Zeigt Debug-Testbilder, um Panel und Treiber zu prüfen (hilft wenn nichts angezeigt wird).</div></div>");

            // --- Debug ---
            page += F("<div class='section'><div class='section-title'>Debug<span id='debugSpinner' class='spinner hidden'></span></div>");
            page += F("<div class='row small'>Letzter TX: <span id='lastTx'>");
            page += htmlEscape(g_lastTxInfo);
            page += F("</span></div>");
            page += F("<div class='row small'>Letzte OBD-Zeile: <span id='lastObd'>");
            page += htmlEscape(g_lastObdInfo);
            page += F("</span></div>");
            // RPM/Max jetzt hier unter Debug
            page += F("<div class='row small'>Aktuelle RPM: <span id='rpmVal'>");
            page += String(g_currentRpm);
            page += F("</span> / Max gesehen: <span id='rpmMaxVal'>");
            page += String(g_maxSeenRpm);
            page += F("</span></div></div>");
        }

        // Save + Reset unten
        page += F("<button type='button' id='btnSave' disabled>Speichern</button>");
        page += F("<button type='button' class='btn-danger' id='btnReset' style='display:none'>Zurücksetzen</button>");
        page += F("</form>");

        page += "<div class='small' style='text-align:center;margin-top:16px;'>IP: " + currentIpString() + "</div>";

        // --- Script ---
        page += F(
            "<script>");
        page += "const TEST_SWEEP_DURATION = " + String(TEST_SWEEP_DURATION) + ";";
        page += F("let saveDirty=false;"
                  "let initialMainState=null;"
                  "let pendingSpinner=0;"
                  "let lastSpinnerTs=0;"
                  "let statusTimer=null;"
                  "let dotIntervals={};"
                  "let ledBlinkState=false;"
                  "let lastLedBlinkTs=0;"
                  "let blinkPreviewActive=false;"
                  "let blinkPreviewEnd=0;"
                  "let testSweepActive=false;"
                  "let testSweepStart=0;"
                  "let previewTimerId=null;"

                  "function setAnimatedDots(el,loading){"
                  " if(!el) return;"
                  " const key=el.id;"
                  " if(loading){"
                  "  if(dotIntervals[key]) return;"
                  "  let step=0;"
                  "  dotIntervals[key]=setInterval(()=>{"
                  "    step=(step+1)%4;"
                  "    el.innerText=(el.dataset.base||'')+'.'.repeat(step);"
                  "  },400);"
                  " }else{"
                  "  if(dotIntervals[key]){clearInterval(dotIntervals[key]);dotIntervals[key]=null;}"
                  "  el.innerText=el.dataset.base||'';"
                  " }"
                  "}"

                  "function updateResetVisibility(){"
                  " const r=document.getElementById('btnReset');"
                  " if(r) r.style.display=saveDirty?'block':'none';"
                  "}"

                  "function captureInitialMainState(){"
                  " const form=document.getElementById('mainForm');"
                  " if(!form) return;"
                  " initialMainState={};"
                  " const elements=form.querySelectorAll('input,select,textarea');"
                  " elements.forEach(el=>{"
                  "   if(!el.name) return;"
                  "   let val;"
                  "   if(el.type==='checkbox'){"
                  "     val=el.checked?'on':'';"
                  "   }else{"
                  "     val=el.value;"
                  "   }"
                  "   initialMainState[el.name]=val;"
                  " });"
                  "}"

                  "function recomputeMainDirty(){"
                  " const form=document.getElementById('mainForm');"
                  " if(!form){saveDirty=false;return;}"
                  " if(!initialMainState) captureInitialMainState();"
                  " let changed=false;"
                  " const current={};"
                  " const elements=form.querySelectorAll('input,select,textarea');"
                  " elements.forEach(el=>{"
                  "   if(!el.name) return;"
                  "   let val;"
                  "   if(el.type==='checkbox'){"
                  "     val=el.checked?'on':'';"
                  "   }else{"
                  "     val=el.value;"
                  "   }"
                  "   current[el.name]=val;"
                  " });"
                  " for(const k in initialMainState){"
                  "   if(initialMainState[k]!==current[k]){"
                  "     changed=true;"
                  "     break;"
                  "   }"
                  " }"
                  " saveDirty=changed;"
                  " const b=document.getElementById('btnSave');"
                  " if(b) b.disabled=!changed;"
                  " updateResetVisibility();"
                  "}"

                  "function markDirty(){"
                  " recomputeMainDirty();"
                  "}"

                  "function onBrightnessChange(v){"
                  " document.getElementById('bval').innerText=v;"
                  " document.getElementById('brightness').value=v;"
                  " markDirty();"
                  " fetch('/brightness?val='+v).catch(()=>{});"
                  "}"

                  "function updateSliderDisplay(el){"
                  " const target=el.dataset.display;"
                  " const span=document.getElementById(target);"
                  " if(span) span.innerText=el.value+'%';"
                  "}"

                  "function enforceSliderOrder(changedId){"
                  " const g=document.getElementById('greenEndSlider');"
                  " const y=document.getElementById('yellowEndSlider');"
                  " const r=document.getElementById('redEndSlider');"
                  " const b=document.getElementById('blinkStartSlider');"
                  " if(!g||!y||!r||!b) return;"
                  " let gv=parseInt(g.value||'0');"
                  " let yv=parseInt(y.value||'0');"
                  " let rv=parseInt(r.value||'0');"
                  " let bv=parseInt(b.value||'0');"
                  " const sync=(el,val)=>{"
                  "   if(parseInt(el.value)!=val){"
                  "     el.value=val;"
                  "     updateSliderDisplay(el);"
                  "   }"
                  " };"
                  " if(changedId==='greenEndSlider'){"
                  "   if(yv<gv){yv=gv;sync(y,yv);}"
                  "   if(rv<yv){rv=yv;sync(r,rv);}"
                  " }else if(changedId==='yellowEndSlider'){"
                  "   if(yv<gv){yv=gv;sync(y,yv);}"
                  "   if(rv<yv){rv=yv;sync(r,rv);}"
                  " }else if(changedId==='redEndSlider'){"
                  "   if(rv<yv){rv=yv;sync(r,rv);}"
                  " }"
                  " bv=Math.max(0,Math.min(100,bv));"
                  " sync(b,bv);"
                  "}"

                  "function updateAutoscaleUi(){"
                  " const chk=document.getElementById('autoscaleToggle');"
                  " const cont=document.getElementById('fixedMaxContainer');"
                  " const inp=document.getElementById('fixedMaxRpmInput');"
                  " if(!chk||!cont||!inp) return;"
                  " const on=chk.checked;"
                  " inp.disabled=on;"
                  " cont.classList.toggle('disabled-field',on);"
                  "}"

                  "function beginRequest(){"
                  " pendingSpinner++;"
                  " lastSpinnerTs=Date.now();"
                  " updateSpinnerVisibility();"
                  " return ()=>{"
                  "   pendingSpinner=Math.max(0,pendingSpinner-1);"
                  "   updateSpinnerVisibility();"
                  " };"
                  "}"

                  "function updateSpinnerVisibility(){"
                  " const sp=document.getElementById('debugSpinner');"
                  " if(!sp) return;"
                  " const idle=(Date.now()-lastSpinnerTs)>3000;"
                  " if(pendingSpinner<=0||idle){"
                  "   sp.classList.add('hidden');"
                  " }else{"
                  "   sp.classList.remove('hidden');"
                  " }"
                  "}"

                  "function postSimple(url){"
                  " const done=beginRequest();"
                  " fetch(url,{method:'POST'}).finally(done);"
                  "}"

                  "function getThresholds(){"
                  " const gv=parseInt(document.getElementById('greenEndSlider').value||'0');"
                  " const yv=parseInt(document.getElementById('yellowEndSlider').value||'0');"
                  " const rv=parseInt(document.getElementById('redEndSlider').value||'0');"
                  " const bv=parseInt(document.getElementById('blinkStartSlider').value||'0');"
                  " let greenEnd=gv/100.0;"
                  " let yellowEnd=yv/100.0;"
                  " let redEnd=rv/100.0;"
                  " let blinkStart=bv/100.0;"
                  " if(greenEnd<0) greenEnd=0;"
                  " if(greenEnd>1) greenEnd=1;"
                  " if(yellowEnd<greenEnd) yellowEnd=greenEnd;"
                  " if(yellowEnd>1) yellowEnd=1;"
                  " if(redEnd<yellowEnd) redEnd=yellowEnd;"
                  " if(redEnd>1) redEnd=1;"
                  " if(blinkStart<0) blinkStart=0;"
                  " if(blinkStart>1) blinkStart=1;"
                  " return {greenEnd,yellowEnd,redEnd,blinkStart};"
                  "}"

                  "function computeSimFraction(t){"
                  " if(t<0) t=0;"
                  " if(t>1) t=1;"
                  " let pct=0;"
                  " if(t<0.30){"
                  "   let tt=t/0.30;"
                  "   pct=tt*tt*(3-2*tt);"
                  " }else if(t<0.60){"
                  "   let tt=(t-0.30)/0.30;"
                  "   let base=1.0-0.6*tt;"
                  "   let wobble=0.10*Math.sin(tt*Math.PI*4.0);"
                  "   pct=base+wobble;"
                  "   if(pct<0.4) pct=0.4;"
                  "   if(pct>1.0) pct=1.0;"
                  " }else if(t<0.85){"
                  "   let tt=(t-0.60)/0.25;"
                  "   let base=0.4+0.6*(tt*tt*(3-2*tt));"
                  "   let wobble=0.05*Math.sin(tt*Math.PI*2.0);"
                  "   pct=base+wobble;"
                  "   if(pct<0.4) pct=0.4;"
                  "   if(pct>1.0) pct=1.0;"
                  " }else{"
                  "   let tt=(t-0.85)/0.15;"
                  "   let base=1.0-tt;"
                  "   let wobble=0.05*Math.sin(tt*Math.PI*2.0);"
                  "   pct=base+wobble;"
                  "   if(pct<0.0) pct=0.0;"
                  "   if(pct>1.0) pct=1.0;"
                  " }"
                  " return pct;"
                  "}"

                  "function layoutLedDots(){"
                  " const cont=document.getElementById('ledPreview');"
                  " if(!cont) return;"
                  " const dots=cont.querySelectorAll('.led-dot');"
                  " if(!dots.length) return;"
                  " const w=cont.clientWidth;"
                  " const spacing=2;"
                  " const maxSize=Math.floor((w-spacing*(dots.length-1))/dots.length);"
                  " const size=Math.max(4,Math.min(14,maxSize));"
                  " dots.forEach(d=>{"
                  "   d.style.width=size+'px';"
                  "   d.style.height=size+'px';"
                  "   d.style.marginLeft=(spacing/2)+'px';"
                  "   d.style.marginRight=(spacing/2)+'px';"
                  " });"
                  "}"

                  "function initLedPreview(){"
                  " const cont=document.getElementById('ledPreview');"
                  " if(!cont) return;"
                  " const count=parseInt(cont.dataset.ledCount||'0');"
                  " cont.innerHTML='';"
                  " for(let i=0;i<count;i++){"
                  "   const d=document.createElement('div');"
                  "   d.className='led-dot';"
                  "   cont.appendChild(d);"
                  " }"
                  " layoutLedDots();"
                  " updateLedPreview();"
                  " window.addEventListener('resize',layoutLedDots);"
                  "}"

                  "function renderLedBarFraction(fraction,useBlink){"
                  " const cont=document.getElementById('ledPreview');"
                  " if(!cont) return;"
                  " const dots=cont.querySelectorAll('.led-dot');"
                  " const count=dots.length;"
                  " if(!count) return;"
                  " if(fraction<0) fraction=0;"
                  " if(fraction>1) fraction=1;"
                  " const modeVal=document.getElementById('modeSelect').value;"
                  " const mode=parseInt(modeVal||'0');"
                  " const thr=getThresholds();"
                  " const greenEnd=thr.greenEnd;"
                  " const yellowEnd=thr.yellowEnd;"
                  " const redEnd=thr.redEnd;"
                  " const blinkStart=thr.blinkStart;"
                  " const blinkTrigger=Math.max(redEnd,blinkStart);"
                  " let ledsOn=Math.round(fraction*count);"
                  " if(ledsOn<0) ledsOn=0;"
                  " if(ledsOn>count) ledsOn=count;"
                  " let shiftBlink=false;"
                  " if(useBlink && (mode===1||mode===2||mode===3) && fraction>=blinkTrigger){"
                  "   const now=Date.now();"
                  "   if(now-lastLedBlinkTs>100){"
                  "     lastLedBlinkTs=now;"
                  "     ledBlinkState=!ledBlinkState;"
                  "   }"
                  "   shiftBlink=true;"
                  " }else{"
                  "   ledBlinkState=false;"
                  " }"
                  " const mode2FullBlink=useBlink && mode===2 && fraction>=blinkTrigger;"
                  " const mode3GtBlink=useBlink && mode===3 && fraction>=blinkTrigger;"
                  " const gCol=document.getElementById('greenColorInput').value;"
                  " const yCol=document.getElementById('yellowColorInput').value;"
                  " const rCol=document.getElementById('redColorInput').value;"
                  " if(mode===3){"
                  "   const pairCount=Math.ceil(count/2);"
                  "   const pairsOn=Math.round(fraction*pairCount);"
                  "   for(let i=0;i<count;i++){"
                  "     let col='#000000';"
                  "     if(mode3GtBlink){"
                  "       col=ledBlinkState?rCol:'#000000';"
                  "     }else{"
                  "       const rank=Math.min(i,count-1-i);"
                  "       if(rank<pairsOn){"
                  "         const pos=pairCount>1 ? (rank/(pairCount-1)) : 1;"
                  "         if(pos<greenEnd){ col=gCol; }"
                  "         else if(pos<yellowEnd){ col=yCol; }"
                  "         else{ col=rCol; }"
                  "       }"
                  "     }"
                  "     dots[i].style.backgroundColor=col;"
                  "   }"
                  " }else{"
                  "   for(let i=0;i<count;i++){"
                  "     let col='#000000';"
                  "     if(i<ledsOn){"
                  "       let pos=count>1 ? (i/(count-1)) : 0;"
                  "       if(mode2FullBlink){"
                  "         col=ledBlinkState?rCol:'#000000';"
                  "       }else{"
                  "         if(pos<greenEnd){"
                  "           col=gCol;"
                  "         }else if(pos<yellowEnd){"
                  "           col=yCol;"
                  "         }else{"
                  "           if(useBlink && mode===1 && shiftBlink){"
                  "             col=ledBlinkState?rCol:'#000000';"
                  "           }else{"
                  "             col=rCol;"
                  "           }"
                  "         }"
                  "       }"
                  "     }"
                  "     dots[i].style.backgroundColor=col;"
                  "   }"
                  " }"
                  " const blinkContainer=document.getElementById('blinkStartContainer');"
                  " if(mode===0){"
                  "   blinkContainer.style.display='none';"
                  " }else{"
                  "   blinkContainer.style.display='block';"
                  " }"
                  "}"

                  "function updateLedPreview(){"
                  " if(testSweepActive){"
                  "   const elapsed=Date.now()-testSweepStart;"
                  "   let t=elapsed/TEST_SWEEP_DURATION;"
                  "   if(t>=1){"
                  "     t=1;"
                  "     testSweepActive=false;"
                  "   }"
                  "   const frac=computeSimFraction(t);"
                  "   renderLedBarFraction(frac,true);"
                  " }else if(blinkPreviewActive){"
                  "   const now=Date.now();"
                  "   if(now>=blinkPreviewEnd){"
                  "     blinkPreviewActive=false;"
                  "     renderLedBarFraction(1.0,false);"
                  "   }else{"
                  "     renderLedBarFraction(1.0,true);"
                  "   }"
                  " }else{"
                  "   renderLedBarFraction(1.0,false);"
                  " }"
                  "}"

                  "function ensurePreviewTimer(){"
                  " if(!previewTimerId){"
                  "   previewTimerId=setInterval(updateLedPreview,30);"
                  " }"
                  "}"

                  "function handleSliderChange(ev){"
                  " enforceSliderOrder(ev.target.id);"
                  " updateSliderDisplay(ev.target);"
                  " if(ev.target.id==='redEndSlider'||ev.target.id==='blinkStartSlider'){"
                  "   triggerBlinkPreview();"
                  " }else{"
                  "   updateLedPreview();"
                  " }"
                  " markDirty();"
                  "}"

                  "function triggerBlinkPreview(){"
                  " const modeVal=document.getElementById('modeSelect').value;"
                  " const mode=parseInt(modeVal||'0');"
                  " if(mode===0) return;"
                  " blinkPreviewActive=true;"
                  " blinkPreviewEnd=Date.now()+2500;"
                  " testSweepActive=false;"
                  " ensurePreviewTimer();"
                  " updateLedPreview();"
                  "}"

                  "function classifyColor(slot,value){"
                  " if(slot===1) return 'Farbe 1 – Grün';"
                  " if(slot===2) return 'Farbe 2 – Gelb';"
                  " return 'Farbe 3 – Rot';"
                  "}"

                  "function updateColorUi(){"
                  " const cfg=["
                  "   {k:'green',slot:1,labelId:'greenEndLabel',hiddenId:'greenLabelHidden',nameId:'color1Name'},"
                  "   {k:'yellow',slot:2,labelId:'yellowEndLabel',hiddenId:'yellowLabelHidden',nameId:'color2Name'},"
                  "   {k:'red',slot:3,labelId:'redEndLabel',hiddenId:'redLabelHidden',nameId:'color3Name'}"
                  " ];"
                  " cfg.forEach(c=>{"
                  "   const inp=document.getElementById(c.k+'ColorInput');"
                  "   if(!inp) return;"
                  "   const name=classifyColor(c.slot,inp.value);"
                  "   const label=name.split('–')[1].trim();"
                  "   const span=document.getElementById(c.nameId);"
                  "   if(span) span.innerText=label;"
                  "   const hid=document.getElementById(c.hiddenId);"
                  "   if(hid) hid.value=label;"
                  "   const lbl=document.getElementById(c.labelId);"
                  "   if(lbl) lbl.style.color=inp.value;"
                  " });"
                  " updateLedPreview();"
                  "}"

                  "function updateVehicleDots(loading){"
                  " ['vehicleModel','vehicleVin','vehicleDiag'].forEach(id=>{"
                  "   setAnimatedDots(document.getElementById(id),loading);"
                  " });"
                  "}"

                  "function boolText(v){return v?'ja':'nein';}"

                  "function updateDisplayStatusUi(s){"
                  " const map=["
                  "   ['dispInit',boolText(s.initAttempted)],"
                  "   ['dispReady',boolText(s.ready)],"
                  "   ['dispPanel',boolText(s.panelInitialized)],"
                  "   ['dispBuf',boolText(s.buffersAllocated)],"
                  "   ['dispTouch',boolText(s.touchReady)],"
                  "   ['dispTickMode',s.tickFallback?'loop':'timer'],"
                  "   ['dispDebugUi',boolText(s.debugSimpleUi)],"
                  "   ['dispLvgl',s.lastLvglRunMs!==undefined?s.lastLvglRunMs:'-'],"
                  "   ['dispError',s.lastError||'-']"
                  " ];"
                  " map.forEach(([id,val])=>{const el=document.getElementById(id);if(el) el.innerText=val;});"
                  "}"

                  "function toggleDisplaySpinner(active){"
                  " const sp=document.getElementById('displaySpinner');"
                  " if(!sp) return;"
                  " if(active){sp.classList.remove('hidden');}else{sp.classList.add('hidden');}"
                  "}"

                  "function fetchDisplayStatus(){"
                  " toggleDisplaySpinner(true);"
                  " const done=beginRequest();"
                  " fetch('/dev/display-status').then(r=>r.json()).then(s=>{updateDisplayStatusUi(s);}).catch(()=>{updateDisplayStatusUi({initAttempted:false,ready:false,lastError:'Request fehlgeschlagen'});}).finally(()=>{toggleDisplaySpinner(false);done();});"
                  "}"

                  "function triggerDisplayPattern(pattern){"
                  " toggleDisplaySpinner(true);"
                  " const done=beginRequest();"
                  " const body='pattern='+encodeURIComponent(pattern||'bars');"
                  " fetch('/dev/display-pattern',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body}).finally(()=>{toggleDisplaySpinner(false);done();fetchDisplayStatus();});"
                  "}"

                  "function fetchStatus(){"
                  " const done=beginRequest();"
                  " fetch('/status').then(r=>r.json()).then(s=>{"
                  "   const rpm=document.getElementById('rpmVal');"
                  "   if(rpm&&s.rpm!==undefined) rpm.innerText=s.rpm;"
                  "   const rpmMax=document.getElementById('rpmMaxVal');"
                  "   if(rpmMax&&s.maxRpm!==undefined) rpmMax.innerText=s.maxRpm;"
                  "   const lastTx=document.getElementById('lastTx');"
                  "   if(lastTx&&s.lastTx!==undefined){"
                  "     lastTx.dataset.base=s.lastTx;"
                  "     lastTx.innerText=s.lastTx;"
                  "   }"
                  "   const lastObd=document.getElementById('lastObd');"
                  "   if(lastObd&&s.lastObd!==undefined){"
                  "     lastObd.dataset.base=s.lastObd;"
                  "     lastObd.innerText=s.lastObd;"
                  "   }"
                  "   const ble=document.getElementById('bleStatus');"
                  "   if(ble&&s.bleText) ble.innerText=s.bleText;"
                  "   const vm=document.getElementById('vehicleModel');"
                  "   if(vm&&s.vehicleModel!==undefined){"
                  "     vm.dataset.base=s.vehicleModel;"
                  "     vm.innerText=s.vehicleModel;"
                  "   }"
                  "   const vv=document.getElementById('vehicleVin');"
                  "   if(vv&&s.vehicleVin!==undefined){"
                  "     vv.dataset.base=s.vehicleVin;"
                  "     vv.innerText=s.vehicleVin;"
                  "   }"
                  "   const vd=document.getElementById('vehicleDiag');"
                  "   if(vd&&s.vehicleDiag!==undefined){"
                  "     vd.dataset.base=s.vehicleDiag;"
                  "     vd.innerText=s.vehicleDiag;"
                  "   }"
                  "   updateVehicleDots(s.vehicleInfoRequestRunning);"
                  "   const btnC=document.getElementById('btnConnect');"
                  "   const btnD=document.getElementById('btnDisconnect');"
                  "   if(btnC&&btnD){"
                  "     if(s.connected){"
                  "       btnC.style.display='none';"
                  "       btnD.style.display='block';"
                  "     }else{"
                  "       btnC.style.display='block';"
                  "       btnD.style.display='none';"
                  "     }"
                  "   }"
                  " }).catch(()=>{}).finally(done);"
                  "}"

                  "function onSaveClicked(){"
                  " const done=beginRequest();"
                  " const fd=new FormData(document.getElementById('mainForm'));"
                  " fetch('/save',{method:'POST',body:fd}).then(()=>{"
                  "   captureInitialMainState();"
                  "   recomputeMainDirty();"
                  " }).finally(done);"
                  "}"

                  "function onTestClicked(){"
                  " const done=beginRequest();"
                  " const fd=new FormData(document.getElementById('mainForm'));"
                  " fetch('/test',{method:'POST',body:fd}).finally(done);"
                  " testSweepActive=true;"
                  " testSweepStart=Date.now();"
                  " blinkPreviewActive=false;"
                  " ensurePreviewTimer();"
                  " updateLedPreview();"
                  "}"

                  "function initUI(){"
                  " const form=document.getElementById('mainForm');"
                  " if(form){"
                  "   form.querySelectorAll('input,select').forEach(el=>{"
                  "     if(el.id==='brightness_slider') return;"
                  "     if(el.type==='range'){"
                  "       el.addEventListener('input',handleSliderChange);"
                  "       el.addEventListener('change',handleSliderChange);"
                  "     }else{"
                  "       el.addEventListener('change',markDirty);"
                  "       el.addEventListener('input',markDirty);"
                  "     }"
                  "   });"
                  " }"
                  " const auto=document.getElementById('autoscaleToggle');"
                  " if(auto) auto.addEventListener('change',()=>{markDirty();updateAutoscaleUi();});"
                  " updateAutoscaleUi();"
                  " ['green','yellow','red'].forEach(n=>{"
                  "   const c=document.getElementById(n+'ColorInput');"
                  "   if(c){"
                  "     c.addEventListener('input',()=>{updateColorUi();markDirty();});"
                  "     c.addEventListener('change',()=>{updateColorUi();markDirty();});"
                  "   }"
                  " });"
                  " ['greenEndSlider','yellowEndSlider','redEndSlider','blinkStartSlider'].forEach(id=>{"
                  "   const el=document.getElementById(id);"
                  "   if(el){"
                  "     updateSliderDisplay(el);"
                  "     el.addEventListener('input',handleSliderChange);"
                  "     el.addEventListener('change',handleSliderChange);"
                  "   }"
                  " });"
                  " const b=document.getElementById('brightness_slider');"
                  " if(b){"
                  "   b.addEventListener('input',e=>onBrightnessChange(e.target.value));"
                  "   b.addEventListener('change',e=>onBrightnessChange(e.target.value));"
                  " }"
                  " const ms=document.getElementById('modeSelect');"
                  " if(ms){"
                  "   ms.addEventListener('change',()=>{markDirty();updateLedPreview();});"
                  " }"
                  " const sb=document.getElementById('btnSave');"
                  " if(sb) sb.addEventListener('click',onSaveClicked);"
                  " const tb=document.getElementById('btnTest');"
                  " if(tb) tb.addEventListener('click',onTestClicked);"
                  " const bc=document.getElementById('btnConnect');"
                  " if(bc) bc.addEventListener('click',()=>postSimple('/connect'));"
                  " const bd=document.getElementById('btnDisconnect');"
                  " if(bd) bd.addEventListener('click',()=>postSimple('/disconnect'));"
                  " const br=document.getElementById('btnReset');"
                  " if(br) br.addEventListener('click',()=>window.location.reload());"
                  " const bdsp=document.getElementById('btnDisplayLogo');"
                  " if(bdsp) bdsp.addEventListener('click',()=>{toggleDisplaySpinner(true);const done=beginRequest();fetch('/dev/display-logo',{method:'POST'}).finally(()=>{toggleDisplaySpinner(false);done();fetchDisplayStatus();});});"
                  " const bds=document.getElementById('btnDisplayStatus');"
                  " if(bds) bds.addEventListener('click',fetchDisplayStatus);"
                  " const bdb=document.getElementById('btnDisplayBars');"
                  " if(bdb) bdb.addEventListener('click',()=>triggerDisplayPattern('bars'));"
                  " const bdg=document.getElementById('btnDisplayGrid');"
                  " if(bdg) bdg.addEventListener('click',()=>triggerDisplayPattern('grid'));"
                  " initLedPreview();"
                  " updateColorUi();"
                  " fetchStatus();"
                  " statusTimer=setInterval(fetchStatus,2200);"
                  " setInterval(updateSpinnerVisibility,1000);"
                  " if(document.getElementById('displayStatusBlock')) fetchDisplayStatus();"
                  " captureInitialMainState();"
                  " recomputeMainDirty();"
                  "}"
                  "document.addEventListener('DOMContentLoaded',initUI);"
                  "</script>");

        page += F("</body></html>");
        return page;
    }

    String settingsPage(bool savedNotice)
    {
        (void)savedNotice;
        String vin = readVehicleVin();
        String model = readVehicleModel();
        String diag = readVehicleDiagStatus();
        String activeTelemetry = activeTelemetrySourceLabel(g_activeTelemetrySource);
        String simHubState = simHubConnectionStateLabel(g_simHubConnectionState);

        String page;
        page.reserve(11000);

        page += F(
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>ShiftLight Einstellungen</title>"
            "<style>"
            "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
            "background:#111;color:#eee;padding:16px;margin:0;}"
            "h1{font-size:20px;margin:0 0 12px 0;display:flex;align-items:center;justify-content:space-between;}"
            "a{color:#0af;text-decoration:none;}"
            ".row{margin-bottom:6px;}"
            ".small{font-size:12px;color:#aaa;}"
            ".section{margin-top:12px;padding:10px 12px;border-radius:8px;background:#181818;border:1px solid #333;}"
            ".section-title{font-weight:600;margin-bottom:8px;font-size:18px;letter-spacing:0.3px;}"
            ".toggle-row{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
            ".toggle-label{font-size:14px;}"
            ".switch{position:relative;display:inline-block;width:46px;height:24px;margin-left:8px;}"
            ".switch input{opacity:0;width:0;height:0;}"
            ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#555;transition:.2s;border-radius:24px;}"
            ".slider:before{position:absolute;content:'';height:18px;width:18px;left:3px;top:3px;background:#fff;transition:.2s;border-radius:50%;}"
            ".switch input:checked + .slider{background:#0af;}"
            ".switch input:checked + .slider:before{transform:translateX(22px);}"
            ".btn{margin-top:12px;width:100%;padding:10px;border:none;border-radius:8px;background:#0af;color:#000;font-weight:bold;font-size:14px;display:inline-flex;align-items:center;justify-content:center;gap:8px;}"
            ".btn-label{display:inline-block;}"
            ".btn.loading{opacity:0.9;}"
            ".btn.loading .spinner{margin:0;border-top-color:#fff;border-left-color:rgba(255,255,255,0.25);border-right-color:rgba(255,255,255,0.25);}"
            ".btn-danger{background:#d33;color:#fff;}"
            ".btn:disabled{background:#555;color:#888;}"
            ".error{color:#f77;font-size:12px;margin-top:4px;}"
            ".spinner{display:inline-block;width:12px;height:12px;border-radius:50%;"
            "border:2px solid rgba(255,255,255,0.2);border-top-color:#0af;"
            "animation:spin 1s linear infinite;margin-left:6px;}"
            "@keyframes spin{from{transform:rotate(0deg);}to{transform:rotate(360deg);}}"
            ".console-box{background:#000;border-radius:6px;border:1px solid #333;"
            "padding:6px;font-family:SFMono-Regular,Menlo,monospace;font-size:11px;"
            "line-height:1.3;max-height:160px;overflow-y:auto;white-space:pre-wrap;}"
            ".console-input{flex:1;padding:6px;border-radius:6px;border:1px solid #444;"
            "background:#111;color:#eee;font-family:inherit;}"
            ".console-row{display:flex;gap:8px;margin-top:8px;}"
            ".console-footer{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
            ".console-footer-left{display:flex;align-items:center;gap:6px;font-size:11px;color:#aaa;}"
            ".console-line{margin:1px 0;}"
            ".console-line-tx{color:#8cf;}"  // gesendete Befehle
            ".console-line-rx{color:#9f9;}"  // normale Antworten
            ".console-line-err{color:#f77;}" // Fehler
            ".dev-section{"
            "  overflow:hidden;"
            "  transition:max-height 0.25s ease,opacity 0.25s ease,"
            "             margin-top 0.25s ease,padding-top 0.25s ease,"
            "             padding-bottom 0.25s ease,border-width 0.25s ease;"
            "}"
            ".dev-collapsed{"
            "  max-height:0;"
            "  opacity:0;"
            "  margin-top:0;"
            "  padding-top:0;"
            "  padding-bottom:0;"
            "  border-top-width:0;"
            "  border-bottom-width:0;"
            "}"
            ".dev-expanded{"
            "  max-height:500px;"
            "  opacity:1;"
            "}"
            ".note{background:#112233;border:1px solid #1f3b55;color:#a3c5ff;padding:10px;border-radius:8px;font-size:13px;margin-top:8px;}"
            ".console-box{"
            "  background:#000;"
            "  border-radius:6px;"
            "  border:1px solid #333;"
            "  padding:6px;"
            "  font-family:SFMono-Regular,Menlo,monospace;"
            "  font-size:11px;"
            "  line-height:1.3;"
            "  max-height:160px;"
            "  overflow-y:auto;"
            "  white-space:pre-wrap;"
            "}"
            ".console-input{"
            "  flex:1;"
            "  padding:6px;"
            "  border-radius:6px;"
            "  border:1px solid #444;"
            "  background:#111;"
            "  color:#eee;"
            "  font-family:inherit;"
            "}"
            ".status-pill{display:flex;align-items:center;gap:8px;font-size:13px;margin:6px 0;}"
            ".status-dot{width:12px;height:12px;border-radius:50%;display:inline-block;background:#d33;box-shadow:0 0 0 2px rgba(255,255,255,0.05);}"
            ".status-dot.bad{background:#d33;}"
            ".status-dot.ok{background:#4cd964;}"
            ".status-dot.warn{background:#f5a524;}"
            ".status-texts{display:flex;flex-direction:column;gap:2px;}"
            ".status-sub{font-size:11px;color:#999;line-height:1.4;}"
            ".ble-actions{display:flex;align-items:center;gap:10px;margin-top:6px;}"
            ".ble-inline{font-size:12px;color:#aaa;}"
            ".wifi-actions{display:flex;align-items:center;gap:10px;margin-top:6px;flex-wrap:wrap;}"
            ".wifi-inline{font-size:12px;color:#aaa;display:flex;align-items:center;gap:6px;}"
            ".wifi-rssi{display:flex;gap:3px;align-items:flex-end;}"
            ".wifi-bar{width:5px;background:#2a2a2a;border-radius:2px;opacity:0.6;}"
            ".wifi-bar.active{background:#0af;opacity:1;}"
            ".wifi-field{margin-top:8px;}"
            ".wifi-meta{display:flex;gap:10px;flex-wrap:wrap;font-size:12px;color:#aaa;}"
            ".wifi-note{font-size:12px;color:#aaa;margin-top:6px;}"
            ".modal{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;z-index:30;}"
            ".modal.hidden{display:none;}"
            ".modal-backdrop{position:absolute;inset:0;background:rgba(0,0,0,0.6);}"
            ".modal-card{position:relative;background:#1b1b1b;border:1px solid #2d2d2d;border-radius:12px;padding:16px;width:92%;max-width:360px;box-shadow:0 16px 40px rgba(0,0,0,0.55);z-index:2;}"
            ".modal-title{font-weight:700;font-size:17px;margin-bottom:10px;}"
            ".modal-actions{display:flex;gap:10px;margin-top:12px;}"
            ".btn-secondary{background:#333;color:#eee;}"
            ".btn-outline{background:transparent;color:#eee;border:1px solid #444;}"
            ".modal label{font-size:12px;color:#aaa;display:block;margin-bottom:4px;}"
            ".modal input[type=password]{width:100%;padding:10px;border-radius:8px;border:1px solid #444;background:#0f0f0f;color:#eee;outline:none;}"
            ".hidden{display:none;}"
            ".device-list{overflow:hidden;transition:max-height .25s ease,opacity .25s ease,margin-top .25s ease,transform .25s ease;border-top:1px solid #222;margin-top:10px;padding-top:6px;opacity:1;transform:translateY(0);}"
            ".device-list.collapsed{max-height:0;opacity:0;margin-top:0;padding-top:0;border-top-width:0;transform:translateY(-6px);}"
            ".device-item{width:100%;margin-top:8px;padding:10px;border-radius:8px;border:1px solid #333;background:#141414;color:#eee;text-align:left;display:flex;justify-content:space-between;align-items:center;}"
            ".device-meta{display:flex;flex-direction:column;gap:2px;}"
            ".device-name{font-weight:600;font-size:14px;}"
            ".device-addr{font-size:11px;color:#aaa;}"
            ".device-item.disabled{opacity:0.4;pointer-events:none;}" 
            ".device-pill{display:inline-flex;align-items:center;gap:6px;}" 
            ".pill{padding:4px 8px;border-radius:999px;font-size:11px;font-weight:600;background:#222;border:1px solid #333;}" 
            ".pill.bad{background:#331111;border-color:#442222;color:#f77;}" 
            ".pill.ok{background:#113311;border-color:#224422;color:#8f8;}" 
            ".wifi-status-card{display:flex;gap:10px;align-items:flex-start;padding:12px;border-radius:12px;background:#141414;border:1px solid #222;box-shadow:0 6px 18px rgba(0,0,0,0.25);}"
            ".wifi-status-main{font-weight:700;font-size:15px;}"
            ".wifi-status-meta{font-size:12px;color:#aaa;line-height:1.4;}"
            ".wifi-error{color:#f77;font-size:12px;margin-top:4px;}"
            ".wifi-mode-select{appearance:none;-webkit-appearance:none;-moz-appearance:none;padding:12px 14px;padding-right:34px;border-radius:10px;border:1px solid #333;background:linear-gradient(145deg,#1b1b1b,#0f0f0f);color:#eee;font-weight:700;position:relative;box-shadow:inset 0 1px 0 rgba(255,255,255,0.05);}"
            ".wifi-mode-select:focus{outline:none;border-color:#0af;}"
            ".select-wrap{position:relative;display:inline-block;width:100%;}"
            ".select-wrap:after{content:\25BE;position:absolute;right:12px;top:50%;transform:translateY(-50%);color:#888;font-size:12px;pointer-events:none;}"
            ".collapsible{overflow:hidden;transition:max-height .35s ease,opacity .35s ease,margin-top .35s ease,padding-top .35s ease,padding-bottom .35s ease;}"
            ".collapsible.expanded{max-height:900px;opacity:1;margin-top:12px;}"
            ".collapsible.collapsed{max-height:0;opacity:0;margin-top:0;padding-top:0;padding-bottom:0;}"
            ".wifi-note{font-size:12px;color:#aaa;margin-top:6px;}"
            ".wifi-scan-spinner{display:inline-flex;align-items:center;gap:6px;}"
            ".ble-device{transition:opacity .25s ease,transform .25s ease,max-height .25s ease,margin .25s ease,padding .25s ease;}"
            ".ble-device-fadeout{opacity:0;transform:translateY(-8px);max-height:0;margin-top:0;margin-bottom:0;padding-top:0;padding-bottom:0;}"
            "</style></head><body>");

        page += "<h1><a href='/'>&larr; Zurück</a><span>Einstellungen</span></h1>";
        page += F("<form id='settingsForm' method='POST' action='/settings'>");

        // --- Modus / Dev-Mode ---
        page += F("<div class='section'><div class='section-title'>Modus</div>");
        page += F("<div class='toggle-row'><span class='toggle-label'>Entwicklermodus</span><label class='switch'>");
        page += "<input type='checkbox' name='devMode' id='devModeToggle'";
        if (g_devMode)
            page += " checked";
        page += "><span class='slider'></span></label></div>";
        page += F("<div class='small'>Schaltet zusätzliche Debug- und OBD-Einstellungen frei.</div></div>");

        // --- Telemetrie ---
        page += F("<div class='section'><div class='section-title'>Telemetrie</div>");
        page += "<div class='row small'>Aktive Quelle: <strong>" + htmlEscape(activeTelemetry) + "</strong></div>";
        page += "<div class='row small'>SimHub-Status: <strong>" + htmlEscape(simHubState) + "</strong></div>";
        page += F("<label for='telemetryPreference'>Bevorzugte Quelle</label>");
        page += "<div class='select-wrap'><select name='telemetryPreference' id='telemetryPreference' class='wifi-mode-select'>";
        page += "<option value='0'";
        if (cfg.telemetryPreference == TelemetryPreference::Auto)
            page += " selected";
        page += ">Automatisch (SimHub bevorzugen, sonst OBD)</option>";
        page += "<option value='1'";
        if (cfg.telemetryPreference == TelemetryPreference::Obd)
            page += " selected";
        page += ">Nur OBD/BLE</option>";
        page += "<option value='2'";
        if (cfg.telemetryPreference == TelemetryPreference::SimHub)
            page += " selected";
        page += ">Nur SimHub</option>";
        page += "</select></div>";
        page += F("<label for='simHubHost'>SimHub Host / PC-IP</label>");
        page += "<input type='text' name='simHubHost' id='simHubHost' placeholder='z.B. 192.168.178.50' value='" + htmlEscape(cfg.simHubHost) + "'>";
        page += F("<label for='simHubPort'>SimHub Port</label>");
        page += "<input type='number' name='simHubPort' id='simHubPort' min='1' max='65535' value='" + String(cfg.simHubPort) + "'>";
        page += F("<label for='simHubPollMs'>Poll-Intervall (ms)</label>");
        page += "<input type='number' name='simHubPollMs' id='simHubPollMs' min='25' max='1000' value='" + String(cfg.simHubPollMs) + "'>";
        page += F("<div class='small'>PC und ESP32 muessen sich im selben Netzwerk sehen. Kein SimHub-Arduino-Device notwendig; der ESP32 liest SimHub direkt ueber dessen HTTP-API.</div></div>");

        // --- WLAN ---
        page += F("<div class='section'><div class='section-title'>WLAN</div>");
        page += "<div class='wifi-status-card'><span id='wifiStatusDot' class='status-dot bad'></span><div class='status-texts'><div id='wifiStatusText' class='wifi-status-main'>Keine Verbindung</div><div id='wifiStatusSub' class='status-sub'>Modus: -</div><div id='wifiStatusMeta' class='wifi-status-meta'>IP: -</div><div id='wifiStatusError' class='wifi-error'></div></div></div>";

        page += F("<label for='wifiMode'>WLAN-Modus</label>");
        page += "<div class='select-wrap'><select name='wifiMode' id='wifiMode' class='wifi-mode-select'>";
        page += "<option value='0'";
        if (cfg.wifiMode == AP_ONLY)
            page += " selected";
        page += ">Access Point (nur AP)</option>";
        page += "<option value='1'";
        if (cfg.wifiMode == STA_ONLY)
            page += " selected";
        page += ">Heim-WLAN (nur STA)</option>";
        page += "<option value='2'";
        if (cfg.wifiMode == STA_WITH_AP_FALLBACK)
            page += " selected";
        page += ">Heim-WLAN mit AP-Fallback</option>";
        page += "</select></div>";
        page += "<div class='wifi-note'>Speichern loest einen kurzen WLAN-Neustart aus. STA verbindet neu, AP bleibt in AP- oder Fallback-Modus aktiv.</div>";

        page += "<div id='wifiInteractive' class='collapsible ";
        if (cfg.wifiMode == AP_ONLY)
            page += "collapsed";
        else
            page += "expanded";
        page += "'>";
        page += "<div class='wifi-actions'>";
        page += "<button type='button' class='btn' id='wifiScanBtn'>Netzwerke suchen</button>";
        page += "<button type='button' class='btn btn-secondary' id='wifiDisconnectBtn'>Trennen</button>";
        page += "<span id='wifiScanStatus' class='wifi-inline'>Bereit</span>";
        page += "</div>";
        page += "<div id='wifiResults' class='device-list collapsed'>";
        page += "<div id='wifiResultsList'></div>";
        page += "<div id='wifiScanEmpty' class='wifi-inline'>Keine Netzwerke gefunden.</div>";
        page += "</div>";
        page += "</div>";
        page += "<div id='wifiPasswordModal' class='modal hidden'><div class='modal-backdrop'></div><div class='modal-card'><div class='modal-title'>Verbinden mit <span id='wifiModalSsid'></span></div><label for='wifiModalPassword'>Passwort</label><input type='password' id='wifiModalPassword' autocomplete='off' placeholder='Passwort eingeben'><div class='modal-actions'><button type='button' class='btn' id='wifiModalConnect'>Verbinden</button><button type='button' class='btn btn-outline' id='wifiModalCancel'>Abbrechen</button></div></div></div>";
        page += F("</div>");

        // --- Mein Fahrzeug ---
        page += F("<div class='section'><div class='section-title'>Mein Fahrzeug</div>");
        page += "<div class='row small'>Fahrzeug: <strong id='vehicleModel' data-base='" + htmlEscape(model) + "'>" + htmlEscape(model) + "</strong></div>";
        page += "<div class='row small'>VIN: <strong id='vehicleVin' data-base='" + htmlEscape(vin) + "'>" + htmlEscape(vin) + "</strong></div>";
        page += "<div class='row small'>Diagnose: <strong id='vehicleDiag' data-base='" + htmlEscape(diag) + "'>" + htmlEscape(diag) + "</strong></div>";
        page += "<div class='row small'>Status: <span id='vehicleStatus' data-base='Noch keine Daten'>Noch keine Daten</span></div>";
        page += F("<button type='button' class='btn' id='btnVehicleRefresh'>Fahrzeugdaten neu synchronisieren</button>");
        page += F("<div id='settingsError' class='error'></div></div>");

        // --- Bluetooth Verbindung ---
        page += F("<div class='section'><div class='section-title'>Bluetooth Verbindung</div>");
        page += F("<div class='status-pill'><span id='bleStatusDot' class='status-dot'></span><span id='bleStatusText'>Keine Verbindung</span></div>");
        page += "<div class='row small'>Gerät: <strong id='bleTargetName' data-base='" + htmlEscape(g_currentTargetName) + "'>" + htmlEscape(g_currentTargetName) + "</strong></div>";
        page += "<div class='row small'>MAC: <span id='bleTargetAddr' data-base='" + htmlEscape(g_currentTargetAddr) + "'>" + htmlEscape(g_currentTargetAddr) + "</span></div>";
        page += "<div class='ble-actions'><button type='button' class='btn' id='bleScanBtn'>Geräte suchen</button><span id='bleScanStatus' class='ble-inline'>Bereit</span></div>";
        page += F("<div id='bleError' class='error'></div>");
        page += F("<div id='bleResults' class='device-list collapsed'>");
        page += F("<div id='bleResultsList'></div>");
        page += F("<div id='bleScanEmpty' class='ble-inline'>Keine Geräte gefunden.</div></div></div>");

        // --- Dev-OBD-Konsole: ausblendbar über Entwicklermodus ---
        page += "<div id='devObdSection' class='section dev-section ";
        if (g_devMode)
            page += "dev-expanded";
        else
            page += "dev-collapsed";
        page += "'>";

        page += F("<div class='section-title'>OBD-Konsole</div>");
        page += F("<div class='small'>"
                  "Direkte AT-/OBD-Befehle senden (z.&nbsp;B. <code>010C</code>, <code>010D</code>, <code>ATZ</code>). "
                  "Antworten erscheinen unten."
                  "</div>");
        page += F("<div id='obdConsole' class='console-box'></div>");
        page += F("<div class='console-row'>");
        page += F("<input type='text' id='obdCmdInput' class='console-input' placeholder='Befehl, z.B. 010C'>");
        page += F("<button type='button' class='btn' id='obdSendBtn'>Senden</button>");
        page += F("</div>");
        page += F("<div class='console-footer'>");
        page += F("<div class='console-footer-left'>");
        page += F("<span class='toggle-label'>Auto-Log</span>");
        page += F("<label class='switch'>");
        page += F("<input type='checkbox' id='obdAutoLog'><span class='slider'></span>");
        page += F("</label>");
        page += F("</div>");

        page += F("<button type='button' class='btn btn-danger' id='obdClearBtn' style='width:auto;padding:6px 10px;'>Clear</button>");
        page += F("</div>");
        page += F("</div>");

        // --- Buttons ---
        page += F("<button type='submit' class='btn' id='settingsSave' disabled>Speichern</button>");
        page += F("<button type='button' class='btn btn-danger' id='settingsReset' style='display:none'>Zurücksetzen</button>");
        page += F("</form>");

        // --- Script ---
        page += F("<script>");
        page += "const MANUAL_CONNECT_RETRY_COUNT=" + String(MANUAL_CONNECT_RETRY_COUNT) + ";";
        page += F(
            "let settingsDirty=false;"
            "let refreshActive=false;"
            "let refreshStart=0;"
            "let dotIntervals={};"
            "let consoleLastTx='';"
            "let consoleLastObd='';"
            "let initialSettingsState=null;"
            "let lastWifiScanStart=0;"
            "let wifiInitialScanDone=false;"
            "let wifiStatusCache=null;"
            "let wifiConnectInFlight=false;"
            "let uiModeOverride=false;"
            "let currentUiMode=null;"
            "let lastWifiStatus=null;"
            "let wifiLastScanResults=[];"
            "let bleDeviceMap=new Map();"
            "const WIFI_SCAN_COOLDOWN_MS=6000;"
            "const STATUS_POLL_MS=2500;"
            "const WIFI_POLL_MS=3200;"
            "const BLE_POLL_MS=3400;"
            "function setAnimatedDots(el,loading){"
            "  if(!el) return;"
            "  const key=el.id;"
            "  if(loading){"
            "    if(dotIntervals[key]) return;"
            "    let step=0;"
            "    dotIntervals[key]=setInterval(()=>{"
            "      step=(step+1)%4;"
            "      el.innerText=(el.dataset.base||'')+'.'.repeat(step);"
            "    },400);"
            "  }else{"
            "    if(dotIntervals[key]){clearInterval(dotIntervals[key]);dotIntervals[key]=null;}"
            "    el.innerText=el.dataset.base||'';"
            "  }"
            "}"
            "function appendConsole(line){" // alte, einfache Version – wird später überschrieben"
            "  const box=document.getElementById('obdConsole');"
            "  if(!box || !line) return;"
            "  box.textContent += (box.textContent ? '\\n' : '') + line;"
            "  box.scrollTop = box.scrollHeight;"
            "}"
            "function formatObdLine(lastTx, resp){" // bleibt, wird im neuen Script wiederverwendet"
            "  if(!resp) return null;"
            "  const cleanResp=resp.trim();"
            "  const parts=cleanResp.split(/\\s+/);"
            "  if(parts.length<2) return null;"
            "  const mode=parts[0].toUpperCase();"
            "  const pid=parts[1].toUpperCase();"
            "  if(mode!=='41') return null;"
            "  if(pid==='0C' && parts.length>=4){"
            "    const A=parseInt(parts[2],16);"
            "    const B=parseInt(parts[3],16);"
            "    if(isNaN(A)||isNaN(B)) return null;"
            "    const rpm=((A*256)+B)/4;"
            "    return '< '+cleanResp+'   (RPM ≈ '+rpm+')';"
            "  }"
            "  if(pid==='0D' && parts.length>=3){"
            "    const A=parseInt(parts[2],16);"
            "    if(isNaN(A)) return null;"
            "    return '< '+cleanResp+'   (Speed ≈ '+A+' km/h)';"
            "  }"
            "  return '< '+cleanResp;"
            "}"

            "function captureInitialSettingsState(){"
            "  const form=document.getElementById('settingsForm');"
            "  if(!form) return;"
            "  initialSettingsState={};"
            "  const elements=form.querySelectorAll('input,select,textarea');"
            "  elements.forEach(el=>{"
            "    if(!el.name) return;"
            "    let val;"
            "    if(el.type==='checkbox'){"
            "      val=el.checked?'on':'';"
            "    }else{"
            "      val=el.value;"
            "    }"
            "    initialSettingsState[el.name]=val;"
            "  });"
            "}"

            "function recomputeSettingsDirty(){"
            "  const form=document.getElementById('settingsForm');"
            "  if(!form || !initialSettingsState) return;"
            "  let changed=false;"
            "  const elements=form.querySelectorAll('input,select,textarea');"
            "  const current={};"
            "  elements.forEach(el=>{"
            "    if(!el.name) return;"
            "    let val;"
            "    if(el.type==='checkbox'){"
            "      val=el.checked?'on':'';"
            "    }else{"
            "      val=el.value;"
            "    }"
            "    current[el.name]=val;"
            "  });"
            "  for(const k in initialSettingsState){"
            "    if(initialSettingsState[k]!==current[k]){"
            "      changed=true;"
            "      break;"
            "    }"
            "  }"
            "  settingsDirty=changed;"
            "  const s=document.getElementById('settingsSave');"
            "  const r=document.getElementById('settingsReset');"
            "  if(s) s.disabled=!changed;"
            "  if(r) r.style.display=changed?'block':'none';"
            "}"

            "function markSettingsDirty(){"
            "  recomputeSettingsDirty();"
            "}"

            "function setStatus(text,loading){"
            "  const el=document.getElementById('vehicleStatus');"
            "  if(el){el.dataset.base=text;setAnimatedDots(el,loading);}"
            "  const sp=document.getElementById('statusSpinner');"
            "  if(sp){if(loading) sp.classList.remove('hidden'); else sp.classList.add('hidden');}"
            "}"

            "function setRefreshActive(on){"
            "  refreshActive=on;"
            "  const btn=document.getElementById('btnVehicleRefresh');"
            "  if(btn) btn.disabled=on;"
            "  const err=document.getElementById('settingsError');"
            "  if(err && !on) err.innerText='';"
            "}"

            "function showError(msg){"
            "  const err=document.getElementById('settingsError');"
            "  if(err) err.innerText=msg;"
            "}"

            "function wifiModeValue(){const sel=document.getElementById('wifiMode');const v=sel?sel.value:'0';if(v==='1') return 'STA_ONLY';if(v==='2') return 'STA_WITH_AP_FALLBACK';return 'AP_ONLY';}"
            "function wifiModeOption(mode){switch(mode){case 'STA_ONLY': return '1';case 'STA_WITH_AP_FALLBACK': return '2';default: return '0';}}"
            "function wifiModeLabel(mode){"
            "  switch(mode){"
            "    case 'STA_ONLY': return 'Heim-WLAN (nur STA)';"
            "    case 'STA_WITH_AP_FALLBACK': return 'Heim-WLAN mit AP-Fallback';"
            "    default: return 'Access Point (nur AP)';"
            "  }"
            "}"
            "function isApOnlyMode(mode){const val=mode||wifiModeValue();return val==='0' || val==='AP_ONLY';}"
            "function wifiBars(rssi){"
            "  const level=parseInt(rssi,10);"
            "  const steps=[-90,-75,-60];"
            "  const heights=[8,12,16];"
            "  let html='';"
            "  for(let i=0;i<steps.length;i++){const active=level>=steps[i];const h=heights[i]||12;html+=`<span class=\\\"wifi-bar${active?' active':''}\\\" style=\\\"height:${h}px\\\"></span>`;}"
            "  return html;"
            "}"
            "function updateWifiScanUi(running,label){"
            "  const scanBtn=document.getElementById('wifiScanBtn');"
            "  const scanStatus=document.getElementById('wifiScanStatus');"
            "  const text=label||'Suche...';"
            "  if(scanBtn){"
            "    scanBtn.disabled=!!running;"
            "    scanBtn.classList.toggle('loading',!!running);"
            "    if(running){scanBtn.innerHTML='<span class=\"spinner\"></span><span class=\"btn-label\">'+text+'</span>'; }"
            "    else{scanBtn.innerHTML='<span class=\"btn-label\">Netzwerke suchen</span>'; }"
            "  }"
            "  if(scanStatus){"
            "    if(running){scanStatus.innerHTML='<span class=\"spinner\"></span><span>'+text+'</span>'; }"
            "    else{scanStatus.textContent=label||'Bereit';}"
            "  }"
            "}"
            "function renderWifiResults(data){"
            "  const list=document.getElementById('wifiResultsList');"
            "  const empty=document.getElementById('wifiScanEmpty');"
            "  const wrapper=document.getElementById('wifiResults');"
            "  if(!list||!wrapper) return;"
            "  const scanning=!!(data&&data.scanRunning);"
            "  const resultsRaw=(data&&data.scanResults)||[];"
            "  if(!scanning){wifiLastScanResults=Array.isArray(resultsRaw)?resultsRaw.slice():[];}"
            "  const results=scanning?(wifiLastScanResults||[]):resultsRaw;"
            "  list.textContent='';"
            "  const busySsid=(data&&data.staConnecting)?(data.currentSsid||''):'';"
            "  const connectedSsid=(data&&data.staConnected)?(data.currentSsid||''):'';"
            "  results.forEach(res=>{"
            "    const item=document.createElement('button');"
            "    item.type='button';"
            "    item.className='device-item';"
            "    const meta=document.createElement('div');"
            "    meta.className='device-meta';"
            "    const name=document.createElement('span');"
            "    name.className='device-name';"
            "    name.textContent=res.ssid||'(unbekannt)';"
            "    const rssi=document.createElement('div');"
            "    rssi.className='wifi-rssi';"
            "    rssi.innerHTML=wifiBars(res.rssi);"
            "    meta.appendChild(name);"
            "    meta.appendChild(rssi);"
            "    const isBusy=busySsid && res.ssid===busySsid;"
            "    const isConnected=connectedSsid && res.ssid===connectedSsid;"
            "    const pill=document.createElement('span');"
            "    pill.className='device-pill';"
            "    if(isConnected){pill.innerHTML='<span class=\"pill ok\">Verbunden</span>';item.classList.add('disabled');}"
            "    else if(isBusy){pill.innerHTML='<span class=\"spinner\"></span><span>Verbinde...</span>'; }"
            "    else{pill.innerHTML='<span class=\"pill\">Verbinden</span>'; }"
            "    if((busySsid && !isBusy) || isConnected){item.classList.add('disabled');}"
            "    item.appendChild(meta);"
            "    item.appendChild(pill);"
            "    if(!isBusy){"
            "      item.addEventListener('click',()=>openWifiPasswordModal(res.ssid||''));"
            "    }"
            "    list.appendChild(item);"
            "  });"
            "  if(empty){empty.style.display=results.length===0&&!data.scanRunning?'block':'none';}"
            "  if(wrapper){"
            "    const showList=!isApOnlyMode(data.mode) && (results.length>0 || data.scanRunning || busySsid);"
            "    wrapper.classList.toggle('collapsed',!showList);"
            "  }"
            "}"
            "function setWifiStatusUi(data,opts){"
            "  data=data||{};"
            "  const preview=!!(opts&&opts.preview);"
            "  const dot=document.getElementById('wifiStatusDot');"
            "  if(!preview){wifiStatusCache=data;}"
            "  const backendMode=(preview && wifiStatusCache?wifiStatusCache.mode:data.mode)||wifiModeValue();"
            "  if(!currentUiMode){currentUiMode=backendMode;}"
            "  const prev=lastWifiStatus||{};"
            "  const modeChanged=!preview && prev.mode && data.mode && prev.mode!==data.mode;"
            "  const connectSuccess=!preview && data.staConnected && !prev.staConnected;"
            "  const disconnectEvent=!preview && prev.staConnected && !data.staConnected && !data.staConnecting;"
            "  const connectError=!preview && data.staLastError && !data.staConnecting;"
            "  if(connectSuccess||connectError||modeChanged||disconnectEvent){uiModeOverride=false;currentUiMode=backendMode;}"
            "  if(!uiModeOverride && !preview){currentUiMode=backendMode;}"
            "  const effectiveMode=currentUiMode||backendMode||'AP_ONLY';"
            "  const statusMode=data.mode||backendMode||effectiveMode;"
            "  const uiApMode=isApOnlyMode(effectiveMode);"
            "  if(dot){dot.classList.remove('ok','warn','bad');}"
            "  const ssidLabel=data.currentSsid||'(unbekannt)';"
            "  const apActive=!!data.apActive;"
            "  const staConnected=!!data.staConnected;"
            "  const connecting=!!data.staConnecting;"
            "  const scanning=!!data.scanRunning;"
            "  const clients=Number(data.apClients||0);"
            "  let statusText='Keine Verbindung';"
            "  let dotClass='bad';"
            "  if(isApOnlyMode(statusMode)){statusText=apActive?'Access Point aktiv':'Access Point inaktiv';dotClass=apActive?'ok':'bad';}"
            "  else if(staConnected){statusText='STA verbunden mit '+ssidLabel;dotClass='ok';}"
            "  else if(connecting||scanning){statusText=connecting?('Verbindung wird aufgebaut... '+ssidLabel):'Suche nach Netzwerken...';dotClass='warn';}"
            "  else if(apActive){statusText='AP aktiv, STA nicht verbunden';dotClass='ok';}"
            "  if(dot && dotClass){dot.classList.add(dotClass);}"
            "  const text=document.getElementById('wifiStatusText');"
            "  if(text){text.textContent=statusText;}"
            "  const sub=document.getElementById('wifiStatusSub');"
            "  if(sub){sub.textContent='Modus: '+wifiModeLabel(statusMode);}"
            "  const meta=document.getElementById('wifiStatusMeta');"
            "  if(meta){const parts=[];if(data.staIp){parts.push('STA-IP: '+data.staIp);}if(apActive && data.apIp){parts.push('AP-IP: '+data.apIp);}if(apActive){parts.push('AP-Clients: '+clients);}meta.textContent=parts.length?parts.join(' • '):'Keine IP verfügbar';}"
            "  const err=document.getElementById('wifiStatusError');"
            "  if(err){err.textContent=data.staLastError||'';err.style.display=data.staLastError?'block':'none';}"
            "  const disconnect=document.getElementById('wifiDisconnectBtn');"
            "  if(disconnect){const show=!isApOnlyMode(statusMode)&&staConnected;disconnect.style.display=show?'inline-block':'none';disconnect.disabled=!staConnected;disconnect.style.opacity=disconnect.disabled?'0.7':'1';}"
            "  const scanLabel=connecting?'Verbinde...':(scanning?'Suche...':(uiApMode?'AP-Modus aktiv':'Netzwerke suchen'));"
            "  updateWifiScanUi(scanning||connecting,scanLabel);"
            "  renderWifiResults(Object.assign({},data,{mode:effectiveMode}));"
            "  const interactive=document.getElementById('wifiInteractive');"
            "  if(interactive){interactive.classList.toggle('expanded',!uiApMode);interactive.classList.toggle('collapsed',uiApMode);}"
            "  const modeSel=document.getElementById('wifiMode');"
            "  if(modeSel){const val=wifiModeOption(effectiveMode);if(modeSel.value!==val){modeSel.value=val;}}"
            "  if(!wifiInitialScanDone && !data.scanRunning && !data.staConnecting && !uiApMode){wifiInitialScanDone=true;if(!data.scanResults || data.scanResults.length===0){startWifiScan(true);}}"
            "  if(!preview){lastWifiStatus=data;}"
            "}"
            "function startWifiScan(force){"
            "  const now=Date.now();"
            "  const mode=currentUiMode || (wifiStatusCache?wifiStatusCache.mode:null) || wifiModeValue();"
            "  if(isApOnlyMode(mode)){updateWifiScanUi(false,'AP-Modus aktiv');return;}"
            "  if(!force && now-lastWifiScanStart<WIFI_SCAN_COOLDOWN_MS){return;}"
            "  if(wifiStatusCache && (wifiStatusCache.staConnecting || wifiStatusCache.scanRunning)){"
            "    lastWifiScanStart=now;"
            "    updateWifiScanUi(true,wifiStatusCache.staConnecting?'Verbinde...':'Suche...');"
            "    return;"
            "  }"
            "  lastWifiScanStart=now;"
            "  updateWifiScanUi(true,'Suche...');"
            "  fetch('/wifi/scan',{method:'POST'})"
            "    .then(r=>r.json())"
            "    .then(d=>{if(d && d.status==='busy'){updateWifiScanUi(true,'Scan laeuft...');}})"
            "    .catch(()=>{updateWifiScanUi(false,'Bereit');});"
            "}"
            "function fetchWifiStatus(){"
            "  fetch('/wifi/status')"
            "    .then(r=>r.json())"
            "    .then(setWifiStatusUi)"
            "    .catch(()=>{});"
            "}"
            "function closeWifiPasswordModal(){"
            "  const modal=document.getElementById('wifiPasswordModal');"
            "  if(modal) modal.classList.add('hidden');"
            "  const pw=document.getElementById('wifiModalPassword');"
            "  if(pw) pw.value='';"
            "}"
            "function openWifiPasswordModal(ssid){"
            "  const modal=document.getElementById('wifiPasswordModal');"
            "  const label=document.getElementById('wifiModalSsid');"
            "  if(label) label.textContent=ssid||'';"
            "  const pwd=document.getElementById('wifiModalPassword');"
            "  if(pwd){pwd.value='';pwd.focus();}"
            "  if(modal) modal.classList.remove('hidden');"
            "}"
            "function submitWifiPassword(){"
            "  const ssidEl=document.getElementById('wifiModalSsid');"
            "  const passEl=document.getElementById('wifiModalPassword');"
            "  if(!ssidEl || !passEl) return;"
            "  const ssid=ssidEl.textContent||'';"
            "  const password=passEl.value||'';"
            "  const modeEl=document.getElementById('wifiMode');"
            "  const modeVal=modeEl?modeEl.value:'0';"
            "  const params=new URLSearchParams();"
            "  params.append('ssid',ssid);"
            "  params.append('password',password);"
            "  params.append('mode',modeVal);"
            "  if(wifiConnectInFlight) return;"
            "  wifiConnectInFlight=true;"
            "  updateWifiScanUi(true,'Verbinde...');"
            "  const btn=document.getElementById('wifiModalConnect');"
            "  if(btn) btn.disabled=true;"
            "  fetch('/wifi/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()})"
            "    .catch(()=>{})"
            "    .finally(()=>{wifiConnectInFlight=false;if(btn) btn.disabled=false;closeWifiPasswordModal();fetchWifiStatus();});"
            "}"
            "function disconnectWifi(){"
            "  uiModeOverride=false;"
            "  currentUiMode=null;"
            "  const btn=document.getElementById('wifiDisconnectBtn');"
            "  if(btn) btn.disabled=true;"
            "  updateWifiScanUi(true,'Trenne...');"
            "  fetch('/wifi/disconnect',{method:'POST'}).finally(()=>{updateWifiScanUi(false,'Bereit');fetchWifiStatus();});"
            "}"
            "function setBleError(msg){const el=document.getElementById('bleError');if(el) el.innerText=msg||'';}"
            "function setBleStatusUi(data){"
            "  data=data||{};"
            "  const dot=document.getElementById('bleStatusDot');"
            "  if(dot){dot.classList.remove('ok','warn');if(data.connected){dot.classList.add('ok');}else if(data.connectInProgress){dot.classList.add('warn');}}"
            "  const txt=document.getElementById('bleStatusText');"
            "  if(txt){"
            "    let t='Keine Verbindung';"
            "    if(data.connectError){t=data.connectError;}"
            "    else if(data.manualFailed){t='Verbindung fehlgeschlagen';}"
            "    else if(data.connectInProgress){t='Verbindung wird aufgebaut...';}"
            "    else if(data.connected){t='Verbunden';}"
            "    txt.textContent=t;"
            "  }"
            "  const tgt=document.getElementById('bleTargetName');"
            "  if(tgt && data.targetName!==undefined){tgt.textContent=data.targetName||data.targetAddr||'(unbekannt)';}"
            "  const addr=document.getElementById('bleTargetAddr');"
            "  if(addr && data.targetAddr!==undefined){addr.textContent=data.targetAddr||'–';}"
            "  const scanStatus=document.getElementById('bleScanStatus');"
            "  if(scanStatus){"
            "    if(data.scanRunning){scanStatus.innerHTML='<span class=\"spinner\"></span><span>Suche Geräte...</span>'; }"
            "    else if(data.connectInProgress){scanStatus.textContent='Verbinden...';}"
            "    else if(data.manualFailed){scanStatus.textContent='Keine Verbindung';}"
            "    else if(data.scanAge>=0){scanStatus.textContent='Letzter Scan: '+data.scanAge+'s';}"
            "    else{scanStatus.textContent='Bereit';}"
            "  }"
            "  const scanBtn=document.getElementById('bleScanBtn');"
            "  if(scanBtn){scanBtn.disabled=!!data.scanRunning||!!data.connectInProgress;scanBtn.classList.toggle('loading',!!data.scanRunning);scanBtn.innerHTML=data.scanRunning?'<span class=\"spinner\"></span><span class=\"btn-label\">Suche Geräte...</span>':'<span class=\"btn-label\">Geräte suchen</span>'; }"
            "  renderBleResults(data);"
            "}"
            "function renderBleResults(data){"
            "  const list=document.getElementById('bleResultsList');"
            "  const empty=document.getElementById('bleScanEmpty');"
            "  const wrapper=document.getElementById('bleResults');"
            "  const results=(data&&data.results)||[];"
            "  const busyAddr=(data&&data.connectTargetAddr)||'';"
            "  const busy=data&&data.connectInProgress;"
            "  const scanning=!!(data&&data.scanRunning);"
            "  if(empty){const count=results.length||bleDeviceMap.size;empty.style.display=count===0&&!scanning?'block':'none';}"
            "  if(wrapper){const showList=scanning||!data.connected||data.manualActive||data.manualFailed||results.length>0||bleDeviceMap.size>0; if(showList){wrapper.classList.remove('collapsed');wrapper.style.maxHeight='800px';}else{wrapper.classList.add('collapsed');wrapper.style.maxHeight='0px';}}"
            "  if(!list) return;"
            "  const seen=new Set();"
            "  results.forEach(dev=>{"
            "    const key=dev.addr||dev.name||('unknown-'+Math.random().toString(16).slice(2));"
            "    seen.add(key);"
            "    let btn=bleDeviceMap.get(key);"
            "    if(!btn){btn=document.createElement('button');btn.className='device-item ble-device';btn.dataset.key=key;btn.dataset.addr=dev.addr||'';btn.dataset.name=dev.name||'';list.appendChild(btn);bleDeviceMap.set(key,btn);}"
            "    btn.classList.remove('ble-device-fadeout','disabled');"
            "    btn.dataset.addr=dev.addr||'';btn.dataset.name=dev.name||'';"
            "    const isBusy=busy && !!dev.addr && dev.addr===busyAddr;"
            "    if(busy && !isBusy){btn.classList.add('disabled');}"
            "    const pill=isBusy?'<span class=\"device-pill\"><span class=\"spinner\"></span><span>Verbinde...</span></span>':'<span class=\"pill\">Verbinden</span>';"
            "    btn.innerHTML=`<span class=\"device-meta\"><span class=\"device-name\">${dev.name||'(unbekannt)'}</span><span class=\"device-addr\">${dev.addr||''}</span></span>${pill}`;"
            "    if(!busy || isBusy){btn.onclick=()=>{requestBleConnect(dev.addr||'',dev.name||'');};}else{btn.onclick=null;}"
            "  });"
            "  if(!scanning){"
            "    bleDeviceMap.forEach((el,key)=>{"
            "      if(!seen.has(key)){"
            "        el.classList.add('ble-device-fadeout');"
            "        const remove=()=>{el.removeEventListener('transitionend',remove);if(el.parentElement){el.parentElement.removeChild(el);} };"
            "        el.addEventListener('transitionend',remove);"
            "        setTimeout(remove,300);"
            "        bleDeviceMap.delete(key);"
            "      }"
            "    });"
            "  }"
            "}"
            "function requestBleConnect(addr,name){"
            "  const btn=document.getElementById('bleScanBtn');"
            "  if(btn) btn.disabled=true;"
            "  setBleError('');"
            "  const list=document.getElementById('bleResultsList');"
            "  if(list){"
            "    list.querySelectorAll('.device-item').forEach(el=>{"
            "      const isTarget=(el.dataset&&el.dataset.addr)===addr;"
            "      if(isTarget){"
            "        const meta=el.querySelector('.device-meta');"
            "        const metaHtml=meta?meta.innerHTML:'';"
            "        el.classList.remove('disabled');"
            "        el.innerHTML='<span class=\\\"device-meta\\\">'+metaHtml+'</span><span class=\\\"device-pill\\\"><span class=\\\"spinner\\\"></span><span>Verbinde...</span></span>';"
            "      }else{"
            "        el.classList.add('disabled');"
            "      }"
            "    });"
            "  }"
            "  fetch('/ble/connect-device',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`address=${encodeURIComponent(addr)}&name=${encodeURIComponent(name||'')}&attempts=${MANUAL_CONNECT_RETRY_COUNT}`})"
            "    .then(r=>{if(!r.ok) throw new Error(); return r.json ? r.json() : null;})"
            "    .catch(()=>{setBleError('Verbindung konnte nicht gestartet werden.');});"
            "}"
            "function startBleScan(){"
            "  setBleError('');"
            "  const btn=document.getElementById('bleScanBtn');"
            "  if(btn){btn.disabled=true;btn.classList.add('loading');btn.innerHTML='<span class=\"spinner\"></span><span class=\"btn-label\">Suche Geräte...</span>'; }"
            "  fetch('/ble/scan',{method:'POST'})"
            "    .then(r=>r.json())"
            "    .then(d=>{if(d&&d.status==='busy'){setBleError('Scan läuft bereits oder Verbindung wird aufgebaut.');}})"
            "    .catch(()=>{setBleError('Scan konnte nicht gestartet werden.');});"
            "}"
            "function fetchBleStatus(){"
            "  fetch('/ble/status')"
            "    .then(r=>r.json())"
            "    .then(d=>{setBleStatusUi(d||{}); if(!d.connected && !d.scanRunning && (!d.results || d.results.length===0)){startBleScan();}})"
            "    .catch(()=>{});"
            "}"

            "function updateVehicleInfo(data){"
            "  ['vehicleModel','vehicleVin','vehicleDiag'].forEach(id=>{"
            "    const el=document.getElementById(id);"
            "    if(el && data[id]!==undefined){"
            "      el.dataset.base=data[id];"
            "      if(!data.vehicleInfoRequestRunning) el.innerText=data[id];"
            "    }"
            "  });"
            "  let statusText='Noch keine Daten';"
            "  let loading=false;"
            "  if(data.vehicleInfoRequestRunning){"
            "    statusText='Abruf läuft';"
            "    loading=true;"
            "  }else if(data.vehicleInfoReady){"
            "    statusText=data.vehicleInfoAge<=1"
            "      ?'Gerade aktualisiert (0s)'"
            "      :'Letztes Update vor '+data.vehicleInfoAge+'s';"
            "  }"
            "  setStatus(statusText,loading);"
            "  setAnimatedDots(document.getElementById('vehicleModel'),loading);"
            "  setAnimatedDots(document.getElementById('vehicleVin'),loading);"
            "  setAnimatedDots(document.getElementById('vehicleDiag'),loading);"
            "  if(refreshActive && !data.vehicleInfoRequestRunning){"
            "    if(data.vehicleInfoReady && data.vehicleInfoAge<=2){"
            "      setRefreshActive(false);"
            "      setStatus('Gerade aktualisiert (0s)',false);"
            "      showError('');"
            "    }else if(Date.now()-refreshStart>7000){"
            "      setRefreshActive(false);"
            "      showError('Keine Antwort vom Fahrzeug.');"
            "    }"
            "  }"
            "  const autoLog=document.getElementById('obdAutoLog');"
            "  const allowLog=!autoLog || autoLog.checked;"
            "  if(allowLog && data.lastTx!==undefined && data.lastTx!==consoleLastTx){"
            "    consoleLastTx=data.lastTx;"
            "    appendConsole('> '+data.lastTx);"
            "  }"
            "  if(allowLog && data.lastObd!==undefined && data.lastObd!==consoleLastObd){"
            "    consoleLastObd=data.lastObd;"
            "    const pretty=formatObdLine(consoleLastTx,data.lastObd);"
            "    appendConsole(pretty || ('< '+data.lastObd));"
            "  }"
            "}"

            "function poll(){"
            "  fetch('/status')"
            "    .then(r=>r.json())"
            "    .then(updateVehicleInfo)"
            "    .catch(()=>{});"
            "}"

            "function initObdAutoLog(){"
            "  const chk=document.getElementById('obdAutoLog');"
            "  if(!chk) return;"
            "  const stored=localStorage.getItem('obdAutoLog');"
            "  if(stored===null){"
            "    chk.checked=true;"
            "  }else{"
            "    chk.checked=(stored==='1');"
            "  }"
            "  chk.addEventListener('change',()=>{"
            "    localStorage.setItem('obdAutoLog',chk.checked?'1':'0');"
            "  });"
            "}"

            "document.getElementById('settingsForm').addEventListener('change',markSettingsDirty);"
            "document.getElementById('settingsReset').addEventListener('click',()=>window.location.reload());"
            "const wifiModeSel=document.getElementById('wifiMode');"
            "if(wifiModeSel){wifiModeSel.addEventListener('change',()=>{wifiInitialScanDone=false;uiModeOverride=true;currentUiMode=wifiModeValue();markSettingsDirty();setWifiStatusUi(Object.assign({},wifiStatusCache||{}, {mode:currentUiMode}),{preview:true});});setWifiStatusUi(wifiStatusCache||{mode:wifiModeValue()},{preview:true});}"
            "const wifiScan=document.getElementById('wifiScanBtn');"
            "if(wifiScan){wifiScan.addEventListener('click',()=>startWifiScan(true));}"
            "const wifiDisconnect=document.getElementById('wifiDisconnectBtn');"
            "if(wifiDisconnect){wifiDisconnect.addEventListener('click',disconnectWifi);}" 
            "const wifiModalCancel=document.getElementById('wifiModalCancel');"
            "if(wifiModalCancel){wifiModalCancel.addEventListener('click',closeWifiPasswordModal);}" 
            "const wifiModalConnect=document.getElementById('wifiModalConnect');"
            "if(wifiModalConnect){wifiModalConnect.addEventListener('click',submitWifiPassword);}" 

            "document.getElementById('btnVehicleRefresh').addEventListener('click',()=>{"
            "  setRefreshActive(true);"
            "  refreshStart=Date.now();"
            "  setStatus('Abruf läuft',true);"
            "  fetch('/settings/vehicle-refresh',{method:'POST'})"
            "    .then(r=>r.json())"
            "    .then(d=>{"
            "      if(!d || d.status!=='started'){"
            "        setRefreshActive(false);"
            "        if(d && d.reason==='no-connection'){"
            "          showError('Keine OBD-Verbindung vorhanden.');"
            "          setStatus('Sync nicht möglich',false);"
            "        }else{"
            "          showError('Sync konnte nicht gestartet werden.');"
            "        }"
            "      }"
            "    })"
            "    .catch(()=>{setRefreshActive(false);showError('Sync fehlgeschlagen.');});"
            "});"

            "const bleBtn=document.getElementById('bleScanBtn');"
            "if(bleBtn){bleBtn.addEventListener('click',()=>{startBleScan();});}"

            "document.getElementById('settingsForm').addEventListener('submit',function(ev){"
            "  ev.preventDefault();"
            "  const fd=new FormData(this);"
            "  const params=new URLSearchParams();"
            "  fd.forEach((v,k)=>{params.append(k,v);});"
            "  fetch('/settings',{" 
            "    method:'POST'," 
            "    headers:{'Content-Type':'application/x-www-form-urlencoded'}," 
            "    body:params.toString()" 
            "  }).then(()=>{captureInitialSettingsState();recomputeSettingsDirty();}).catch(()=>{});" 
            "});" 

            "poll();"
            "fetchWifiStatus();"
            "fetchBleStatus();"
            "setInterval(poll,STATUS_POLL_MS);"
            "setInterval(fetchWifiStatus,WIFI_POLL_MS);"
            "setInterval(fetchBleStatus,BLE_POLL_MS);"
            "initObdAutoLog();"
            "captureInitialSettingsState();"
            "recomputeSettingsDirty();"

            "const devToggle=document.getElementById('devModeToggle');"
            "const devSection=document.getElementById('devObdSection');"
            "function updateDevSection(){"
            "  if(!devSection) return;"
            "  if(devToggle && devToggle.checked){"
            "    devSection.classList.remove('dev-collapsed');"
            "    devSection.classList.add('dev-expanded');"
            "  }else{"
            "    devSection.classList.remove('dev-expanded');"
            "    devSection.classList.add('dev-collapsed');"
            "  }"
            "}"
            "if(devToggle){"
            "  devToggle.addEventListener('change',()=>{"
            "    markSettingsDirty();"
            "    updateDevSection();"
            "  });"
            "  updateDevSection();"
            "}"

            "const obdBtn=document.getElementById('obdSendBtn');"
            "const obdInput=document.getElementById('obdCmdInput');"
            "const obdClear=document.getElementById('obdClearBtn');"
            "if(obdClear){"
            "  obdClear.addEventListener('click',()=>{"
            "    const box=document.getElementById('obdConsole');"
            "    if(box) box.textContent='';"
            "  });"
            "}"
            "if(obdBtn && obdInput){"
            "  obdBtn.addEventListener('click',()=>{"
            "    const cmd=obdInput.value.trim();"
            "    if(!cmd) return;"
            "    appendConsole('> '+cmd);"
            "    fetch('/dev/obd-send',{"
            "      method:'POST',"
            "      headers:{'Content-Type':'application/x-www-form-urlencoded'},"
            "      body:'cmd='+encodeURIComponent(cmd)"
            "    }).catch(()=>appendConsole('! Fehler beim Senden'));"
            "  });"
            "  obdInput.addEventListener('keydown',e=>{"
            "    if(e.key==='Enter'){e.preventDefault();obdBtn.click();}"
            "  });"
            "}"
            "</script>");

        page += F(
            "<script>"
            "(function(){"
            "  function overrideAppendConsole(){"
            "    window.appendConsole=function(line){"
            "      var box=document.getElementById('obdConsole');"
            "      if(!box||!line) return;"
            "      var div=document.createElement('div');"
            "      var txt=String(line).trim();"
            "      var cls='console-line';"
            "      if(txt.charAt(0)==='>'){"
            "        cls+=' console-line-tx';"
            "      }else if(txt.charAt(0)==='!'){"
            "        cls+=' console-line-err';"
            "      }else if(/NO DATA|ERROR|UNABLE TO CONNECT|STOPPED|BUS INIT/i.test(txt)){"
            "        cls+=' console-line-err';"
            "      }else{"
            "        cls+=' console-line-rx';"
            "      }"
            "      div.className=cls;"
            "      div.textContent=txt;"
            "      box.appendChild(div);"
            "      box.scrollTop=box.scrollHeight;"
            "    };"
            "  }"
            "  function enhancedObdConsoleInit(){"
            "    var box=document.getElementById('obdConsole');"
            "    var btn=document.getElementById('obdSendBtn');"
            "    var input=document.getElementById('obdCmdInput');"
            "    if(!box||!btn||!input) return;"
            "    // alten Button + Input klonen, um alte Event-Listener loszuwerden"
            "    var newBtn=btn.cloneNode(true);"
            "    btn.parentNode.replaceChild(newBtn,btn);"
            "    var newInput=input.cloneNode(true);"
            "    input.parentNode.replaceChild(newInput,input);"
            "    var sending=false;"
            "    function setSending(on){"
            "      sending=on;"
            "      newBtn.disabled=on;"
            "      newInput.disabled=on;"
            "      if(on){"
            "        if(!newBtn.dataset.label){newBtn.dataset.label=newBtn.textContent;}"
            "        newBtn.innerHTML='<span class=\"spinner\"></span>';"
            "      }else{"
            "        newBtn.textContent=newBtn.dataset.label||'Senden';"
            "      }"
            "    }"
            "    function doSend(){"
            "      var cmd=newInput.value.trim();"
            "      if(!cmd||sending) return;"
            "      setSending(true);"
            "      if(window.appendConsole){appendConsole('> '+cmd);}"
            "      fetch('/dev/obd-send',{"
            "        method:'POST',"
            "        headers:{'Content-Type':'application/x-www-form-urlencoded'},"
            "        body:'cmd='+encodeURIComponent(cmd)"
            "      }).then(function(resp){"
            "        if(!resp.ok){"
            "          return resp.text().then(function(t){"
            "            if(window.appendConsole){appendConsole('! '+(t||'Fehler beim Senden'));}"
            "          });"
            "        }"
            "        return resp.json().then(function(data){"
            "          if(data&&data.lastObd){"
            "            var txt;"
            "            if(typeof formatObdLine==='function'){"
            "              txt=formatObdLine(data.lastTx||'',data.lastObd)||('< '+data.lastObd);"
            "            }else{"
            "              txt='< '+data.lastObd;"
            "            }"
            "            if(window.appendConsole){appendConsole(txt);}"
            "            if(typeof consoleLastTx!=='undefined' && data.lastTx!==undefined){consoleLastTx=data.lastTx;}"
            "            if(typeof consoleLastObd!=='undefined' && data.lastObd!==undefined){consoleLastObd=data.lastObd;}"
            "          }"
            "        });"
            "      }).catch(function(){"
            "        if(window.appendConsole){appendConsole('! Fehler beim Senden');}"
            "      }).finally(function(){"
            "        setSending(false);"
            "      });"
            "    }"
            "    newBtn.addEventListener('click',doSend);"
            "    newInput.addEventListener('keydown',function(e){"
            "      if(e.key==='Enter'){e.preventDefault();doSend();}"
            "    });"
            "  }"
            "  function init(){"
            "    overrideAppendConsole();"
            "    enhancedObdConsoleInit();"
            "  }"
            "  if(document.readyState==='loading'){"
            "    document.addEventListener('DOMContentLoaded',init);"
            "  }else{"
            "    init();"
            "  }"
            "})();"
            "</script>"
            "</body></html>");

        return page;
    }

    void handleRoot()
    {
        markHttpActivity("WEB_ROOT");
        if (maybeRedirectToBridgeUi())
        {
            return;
        }
        server.send(200, "text/html", buildDashboardPage());
    }

    void handleBrightness()
    {
        markHttpActivity("WEB_BRIGHTNESS");
        if (server.hasArg("val"))
        {
            int v = clampInt(server.arg("val").toInt(), 0, 255);
            cfg.brightness = v;
            if (cfg.autoBrightnessMin > cfg.brightness)
            {
                cfg.autoBrightnessMin = cfg.brightness;
            }
            ambientLightOnConfigChanged();
            ledBarRefreshBrightness();
            g_lastBrightnessChangeMs = millis();
            ledBarRequestBrightnessPreview();
        }
        server.send(200, "text/plain", "OK");
    }

    void applyConfigFromRequest(bool allowAutoReconnect)
    {
        if (server.hasArg("mode"))
        {
            int m = server.arg("mode").toInt();
            if (m < 0 || m > 3)
                m = 0;
            cfg.mode = m;
        }
        if (server.hasArg("brightness"))
        {
            cfg.brightness = clampInt(server.arg("brightness").toInt(), 0, 255);
        }

        cfg.autoBrightnessEnabled = server.hasArg("autoBrightnessEnabled");
        if (server.hasArg("ambientLightSdaPin"))
        {
            cfg.ambientLightSdaPin = clampInt(server.arg("ambientLightSdaPin").toInt(), 0, 48);
        }
        if (server.hasArg("ambientLightSclPin"))
        {
            cfg.ambientLightSclPin = clampInt(server.arg("ambientLightSclPin").toInt(), 0, 48);
        }
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        if (cfg.ambientLightSdaPin == 21 && cfg.ambientLightSclPin == 22)
        {
            cfg.ambientLightSdaPin = AMBIENT_LIGHT_DEFAULT_SDA;
            cfg.ambientLightSclPin = AMBIENT_LIGHT_DEFAULT_SCL;
        }
#endif
        if (server.hasArg("autoBrightnessStrengthPct"))
        {
            cfg.autoBrightnessStrengthPct = clampInt(server.arg("autoBrightnessStrengthPct").toInt(), 25, 200);
        }
        if (server.hasArg("autoBrightnessMin"))
        {
            cfg.autoBrightnessMin = clampInt(server.arg("autoBrightnessMin").toInt(), 0, 255);
        }
        if (server.hasArg("autoBrightnessResponsePct"))
        {
            cfg.autoBrightnessResponsePct = clampInt(server.arg("autoBrightnessResponsePct").toInt(), 1, 100);
        }
        if (server.hasArg("autoBrightnessLuxMin"))
        {
            cfg.autoBrightnessLuxMin = clampInt(server.arg("autoBrightnessLuxMin").toInt(), 0, 120000);
        }
        if (server.hasArg("autoBrightnessLuxMax"))
        {
            cfg.autoBrightnessLuxMax =
                clampInt(server.arg("autoBrightnessLuxMax").toInt(), cfg.autoBrightnessLuxMin + 1, 120000);
        }
        if (cfg.autoBrightnessMin > cfg.brightness)
        {
            cfg.autoBrightnessMin = cfg.brightness;
        }

        cfg.autoScaleMaxRpm = server.hasArg("autoscale");
        if (server.hasArg("fixedMaxRpm"))
            cfg.fixedMaxRpm = clampInt(server.arg("fixedMaxRpm").toInt(), 1000, 8000);
        if (server.hasArg("rpmStartRpm"))
            cfg.rpmStartRpm = clampInt(server.arg("rpmStartRpm").toInt(), 0, 12000);
        if (server.hasArg("activeLedCount"))
            cfg.activeLedCount = clampInt(server.arg("activeLedCount").toInt(), 1, NUM_LEDS);

        int gPct = cfg.greenEndPct;
        int yPct = cfg.yellowEndPct;
        int rPct = cfg.redEndPct;
        int bPct = cfg.blinkStartPct;
        int blinkSpeedPct = cfg.blinkSpeedPct;
        if (server.hasArg("greenEndPct"))
            gPct = server.arg("greenEndPct").toInt();
        if (server.hasArg("yellowEndPct"))
            yPct = server.arg("yellowEndPct").toInt();
        if (server.hasArg("redEndPct"))
            rPct = server.arg("redEndPct").toInt();
        if (server.hasArg("blinkStartPct"))
            bPct = server.arg("blinkStartPct").toInt();
        if (server.hasArg("blinkSpeedPct"))
            blinkSpeedPct = clampInt(server.arg("blinkSpeedPct").toInt(), 0, 100);
        enforceOrder(gPct, yPct, rPct, bPct);
        cfg.greenEndPct = gPct;
        cfg.yellowEndPct = yPct;
        cfg.redEndPct = rPct;
        cfg.blinkStartPct = bPct;
        cfg.blinkSpeedPct = blinkSpeedPct;

        if (server.hasArg("greenColor"))
            cfg.greenColor = parseHexColor(server.arg("greenColor"), cfg.greenColor);
        if (server.hasArg("yellowColor"))
            cfg.yellowColor = parseHexColor(server.arg("yellowColor"), cfg.yellowColor);
        if (server.hasArg("redColor"))
            cfg.redColor = parseHexColor(server.arg("redColor"), cfg.redColor);

        cfg.greenLabel = safeLabel(server.arg("greenLabel"), "Farbe 1");
        cfg.yellowLabel = safeLabel(server.arg("yellowLabel"), "Farbe 2");
        cfg.redLabel = safeLabel(server.arg("redLabel"), "Farbe 3");

        cfg.logoOnIgnitionOn = server.hasArg("logoIgnOn");
        cfg.logoOnEngineStart = server.hasArg("logoEngStart");
        cfg.logoOnIgnitionOff = server.hasArg("logoIgnOff");
        cfg.simSessionLedEffectsEnabled = server.hasArg("simFxLed");
        cfg.gestureControlEnabled = server.hasArg("gestureControlEnabled");
        ambientLightOnConfigChanged();
        gestureSensorOnConfigChanged();
        ledBarRefreshBrightness();

        if (allowAutoReconnect)
        {
            if (g_devMode)
            {
                bool prevReconnect = g_autoReconnect;
                g_autoReconnect = server.hasArg("autoReconnect");
                if (g_autoReconnect && !prevReconnect)
                {
                    g_forceImmediateReconnect = true;
                    g_lastBleRetryMs = 0;
                }
            }
            else
            {
                if (!g_autoReconnect)
                {
                    g_autoReconnect = true;
                    g_forceImmediateReconnect = true;
                    g_lastBleRetryMs = 0;
                }
            }
        }
    }

    void applyWifiConfigFromRequest()
    {
        WifiMode mode = cfg.wifiMode;
        if (server.hasArg("wifiMode"))
        {
            mode = static_cast<WifiMode>(clampInt(server.arg("wifiMode").toInt(), 0, 2));
        }

        String staSsid = argTrimmed("staSsid", cfg.staSsid);
        String staPass = argTrimmed("staPassword", cfg.staPassword);
        String apSsid = argTrimmed("apSsid", cfg.apSsid);
        String apPass = argTrimmed("apPassword", cfg.apPassword);

        if (apSsid.isEmpty())
            apSsid = AP_SSID;
        if (apPass.length() < 8)
            apPass = AP_PASS;

        cfg.wifiMode = mode;
        cfg.staSsid = staSsid;
        cfg.staPassword = staPass;
        cfg.apSsid = apSsid;
        cfg.apPassword = apPass;
    }

    void applyTelemetryConfigFromRequest()
    {
        if (server.hasArg("telemetryPreference"))
        {
            const int rawPreference = clampInt(server.arg("telemetryPreference").toInt(), 0, 2);
            cfg.telemetryPreference = static_cast<TelemetryPreference>(rawPreference);
        }

        if (server.hasArg("simTransport"))
        {
            const int rawTransport = clampInt(server.arg("simTransport").toInt(), 0, 2);
            cfg.simTransportPreference = static_cast<SimTransportPreference>(rawTransport);
        }

        if (server.hasArg("displayFocus"))
        {
            const int rawFocus = clampInt(server.arg("displayFocus").toInt(), 0, 2);
            cfg.uiDisplayFocus = static_cast<DisplayFocusMetric>(rawFocus);
        }

        cfg.simHubHost = argTrimmed("simHubHost", cfg.simHubHost);
        cfg.simHubPort = static_cast<uint16_t>(clampInt(server.arg("simHubPort").toInt(), 1, 65535));
        if (!server.hasArg("simHubPort"))
        {
            cfg.simHubPort = 8888;
        }

        cfg.simHubPollMs = static_cast<uint16_t>(clampInt(server.arg("simHubPollMs").toInt(), 25, 1000));
        if (!server.hasArg("simHubPollMs"))
        {
            cfg.simHubPollMs = 75;
        }
    }

    void handleSave()
    {
        markHttpActivity("WEB_SAVE");
        applyConfigFromRequest(true);
        saveConfig();
        server.send(200, "text/plain", "OK");
    }

    void handleTest()
    {
        markHttpActivity("WEB_TEST");
        if (server.method() == HTTP_POST)
        {
            applyConfigFromRequest(true);
            int maxRpm = 4000;
            if (cfg.autoScaleMaxRpm)
                maxRpm = (g_maxSeenRpm > 0) ? g_maxSeenRpm : 4000;
            else
                maxRpm = (cfg.fixedMaxRpm > 0) ? cfg.fixedMaxRpm : 4000;
            ledBarStartTestSweep(LedTestSweepMode::Expressive, maxRpm);
            server.send(200, "text/plain", "OK");
            return;
        }
        server.send(405, "text/plain", "Method Not Allowed");
    }

    void handleTestDiagnostic()
    {
        markHttpActivity("WEB_TEST_DIAG");
        if (server.method() == HTTP_POST)
        {
            applyConfigFromRequest(true);
            int maxRpm = 4000;
            if (cfg.autoScaleMaxRpm)
                maxRpm = (g_maxSeenRpm > 0) ? g_maxSeenRpm : 4000;
            else
                maxRpm = (cfg.fixedMaxRpm > 0) ? cfg.fixedMaxRpm : 4000;
            ledBarStartTestSweep(LedTestSweepMode::Deterministic, maxRpm);
            server.send(200, "text/plain", "OK");
            return;
        }
        server.send(405, "text/plain", "Method Not Allowed");
    }

    void handleConnect()
    {
        markHttpActivity("WEB_CONNECT");
        if (!telemetryShouldAllowObd())
        {
            server.send(409, "text/plain", "OBD ist im Sim / USB Modus deaktiviert.");
            return;
        }
        g_autoReconnect = true;
        g_forceImmediateReconnect = true;
        g_lastBleRetryMs = 0;
        server.send(200, "text/plain", "OK");
    }

    void handleDisconnect()
    {
        markHttpActivity("WEB_DISCONNECT");
        if (g_client != nullptr)
            g_client->disconnect();
        server.send(200, "text/plain", "OK");
    }

    void handleStatus()
    {
        markHttpActivity("WEB_STATUS");
        unsigned long now = millis();
        int vehicleAge = 0;
        bool ready = g_vehicleInfoAvailable;
        const AmbientLightDebugInfo ambientInfo = ambientLightGetDebugInfo();
        const GestureSensorDebugInfo gestureInfo = gestureSensorGetDebugInfo();
        const TelemetryDebugInfo telemetryInfo = telemetryGetDebugInfo();
        const LedRenderHistoryInfo ledHistory = ledBarGetRenderHistoryInfo();
        const TelemetryRenderSnapshot &telemetrySnapshot = telemetryInfo.snapshot;
        if (g_vehicleInfoAvailable && g_vehicleInfoLastUpdate > 0)
            vehicleAge = static_cast<int>((now - g_vehicleInfoLastUpdate) / 1000UL);

        String json = "{";
        json += "\"rpm\":" + String(telemetrySnapshot.rpm);
        json += ",\"maxRpm\":" + String(telemetrySnapshot.maxSeenRpm);
        json += ",\"speed\":" + String(telemetrySnapshot.speedKmh);
        json += ",\"gear\":" + String(telemetrySnapshot.gear);
        json += ",\"gearSource\":\"" + jsonEscape(gearSourceLabel()) + "\"";
        json += ",\"pitLimiter\":" + String(telemetrySnapshot.pitLimiter ? "true" : "false");
        json += ",\"lastTx\":\"" + jsonEscape(g_lastTxInfo) + "\"";
        json += ",\"lastObd\":\"" + jsonEscape(g_lastObdInfo) + "\"";
        json += ",\"connected\":" + String(g_connected ? "true" : "false");
        json += ",\"autoReconnect\":" + String(g_autoReconnect ? "true" : "false");
        json += ",\"devMode\":" + String(g_devMode ? "true" : "false");
        String bleText = g_connected ? "Verbunden" : "Getrennt";
        bleText += g_autoReconnect ? " (Auto-Reconnect AN)" : " (Auto-Reconnect AUS)";
        json += ",\"bleText\":\"" + jsonEscape(bleText) + "\"";
        json += ",\"vehicleVin\":\"" + jsonEscape(g_vehicleVin) + "\"";
        json += ",\"vehicleModel\":\"" + jsonEscape(g_vehicleModel) + "\"";
        json += ",\"vehicleDiag\":\"" + jsonEscape(g_vehicleDiagStatus) + "\"";
        json += ",\"vehicleInfoRequestRunning\":" + String(g_vehicleInfoRequestRunning ? "true" : "false");
        json += ",\"vehicleInfoReady\":" + String(ready ? "true" : "false");
        json += ",\"vehicleInfoAge\":" + String(vehicleAge);
        json += ",\"telemetryPreference\":\"" + telemetryPreferenceToString(cfg.telemetryPreference) + "\"";
        json += ",\"simTransport\":\"" + simTransportPreferenceToString(cfg.simTransportPreference) + "\"";
        json += ",\"simTransportMode\":\"" + simTransportModeToString(resolveSimRuntimeTransportMode(cfg.telemetryPreference, cfg.simTransportPreference)) + "\"";
        json += ",\"telemetryFallback\":" + String(telemetrySourceIsFallback(g_activeTelemetrySource, cfg.telemetryPreference, cfg.simTransportPreference) ? "true" : "false");
        json += ",\"activeTelemetry\":\"" + activeTelemetrySourceLabel(g_activeTelemetrySource) + "\"";
        json += ",\"telemetrySnapshotVersion\":" + String(telemetrySnapshot.version);
        json += ",\"telemetrySnapshotAgeMs\":" + String(telemetrySnapshot.sampleTimestampMs > 0 ? (now - telemetrySnapshot.sampleTimestampMs) : 0);
        json += ",\"telemetrySnapshotPublishAgeMs\":" + String(telemetryInfo.lastSnapshotPublishMs > 0 ? (now - telemetryInfo.lastSnapshotPublishMs) : 0);
        json += ",\"telemetrySnapshotPublishCount\":" + String(telemetryInfo.snapshotPublishCount);
        json += ",\"telemetrySnapshotSource\":\"" + jsonEscape(String(telemetrySourceName(telemetrySnapshot.source))) + "\"";
        json += ",\"telemetrySnapshotFresh\":" + String(telemetrySnapshot.telemetryFresh ? "true" : "false");
        json += ",\"telemetryTaskRunning\":" + String(telemetryInfo.taskRunning ? "true" : "false");
        json += ",\"telemetryTaskIntervalMs\":" + String(telemetryInfo.taskIntervalMs);
        json += ",\"simSessionState\":\"" + jsonEscape(String(simSessionStateName(telemetrySnapshot.simSessionState))) + "\"";
        json += ",\"telemetrySourceTransitionCount\":" + String(telemetryInfo.sourceTransitionCount);
        json += ",\"telemetryLastSourceTransition\":\"" + jsonEscape(String(telemetrySourceName(telemetryInfo.lastSourceTransition.fromSource)) + " -> " + telemetrySourceName(telemetryInfo.lastSourceTransition.toSource)) + "\"";
        json += ",\"telemetryLastSourceTransitionHold\":" + String(telemetryInfo.lastSourceTransition.holdApplied ? "true" : "false");
        json += ",\"telemetryLastSourceTransitionFallback\":" + String(telemetryInfo.lastSourceTransition.fallbackActive ? "true" : "false");
        json += ",\"simSessionTransitionCount\":" + String(telemetryInfo.simSessionTransitionCount);
        json += ",\"simSessionSuppressedCount\":" + String(telemetryInfo.simSessionSuppressedCount);
        json += ",\"simSessionLastTransition\":\"" + jsonEscape(String(simSessionTransitionTypeName(telemetryInfo.lastSimSessionTransition.transition))) + "\"";
        json += ",\"simHubState\":\"" + jsonEscape(simHubConnectionStateLabel(g_simHubConnectionState)) + "\"";
        json += ",\"usbState\":\"" + jsonEscape(usbBridgeStateLabel(g_usbBridgeConnectionState)) + "\"";
        json += ",\"usbConnected\":" + String(g_usbSerialConnected ? "true" : "false");
        json += ",\"usbBridgeConnected\":" + String(g_usbBridgeConnected ? "true" : "false");
        json += ",\"usbBridgeWebActive\":" + String(g_usbBridgeWebActive ? "true" : "false");
        json += ",\"usbHost\":\"" + jsonEscape(g_usbBridgeHost) + "\"";
        json += ",\"bridgeWebUrl\":\"" + jsonEscape(currentBridgeBaseUrl()) + "\"";
        json += ",\"localWebUrl\":\"" + jsonEscape(currentWebBaseUrl()) + "\"";
        json += ",\"preferredWebUrl\":\"" + jsonEscape(shouldRedirectToBridgeUi() ? currentBridgeBaseUrl() : currentWebBaseUrl()) + "\"";
        json += ",\"usbError\":\"" + jsonEscape(g_usbBridgeLastError) + "\"";
        json += ",\"wifiSuspended\":" + String(isWifiSuspendedForUsb() ? "true" : "false");
        json += ",\"obdAllowed\":" + String(telemetryShouldAllowObd() ? "true" : "false");
        json += ",\"usbTelemetryAgeMs\":" + String(g_usbTelemetryDebug.lastFrameMs > 0 ? (now - g_usbTelemetryDebug.lastFrameMs) : 0);
        json += ",\"usbTelemetryFrames\":" + String(g_usbTelemetryDebug.framesReceived);
        json += ",\"usbTelemetryParseErrors\":" + String(g_usbTelemetryDebug.parseErrors);
        json += ",\"usbTelemetryGlitchRejects\":" + String(g_usbTelemetryDebug.glitchRejects);
        json += ",\"usbTelemetryGlitchRejectUps\":" + String(g_usbTelemetryDebug.glitchRejectUpCount);
        json += ",\"usbTelemetryGlitchRejectDowns\":" + String(g_usbTelemetryDebug.glitchRejectDownCount);
        json += ",\"usbTelemetryGlitchWindowMs\":" + String(g_usbTelemetryDebug.glitchWindowMs);
        json += ",\"usbTelemetryGlitchDeltaRpm\":" + String(g_usbTelemetryDebug.glitchDeltaRpm);
        json += ",\"usbTelemetryGapEvents\":" + String(g_usbTelemetryDebug.gapEvents);
        json += ",\"usbTelemetryLastGapMs\":" + String(g_usbTelemetryDebug.lastGapMs);
        json += ",\"usbTelemetryMaxGapMs\":" + String(g_usbTelemetryDebug.maxGapMs);
        json += ",\"usbTelemetrySeq\":" + String(g_usbTelemetryDebug.lastSeq);
        json += ",\"usbTelemetrySeqGapEvents\":" + String(g_usbTelemetryDebug.seqGapEvents);
        json += ",\"usbTelemetrySeqGapFrames\":" + String(g_usbTelemetryDebug.seqGapFrames);
        json += ",\"usbTelemetrySeqDuplicates\":" + String(g_usbTelemetryDebug.seqDuplicates);
        json += ",\"usbTelemetryLineOverflows\":" + String(g_usbTelemetryDebug.lineOverflows);
        json += ",\"usbTelemetryLastRawRpm\":" + String(g_usbTelemetryDebug.lastRawRpm);
        json += ",\"usbTelemetryLastAcceptedRpm\":" + String(g_usbTelemetryDebug.lastAcceptedRpm);
        json += ",\"usbTelemetryLastRejectedRpm\":" + String(g_usbTelemetryDebug.lastRejectedRpm);
        json += ",\"simHubPollOk\":" + String(g_simHubDebug.pollSuccessCount);
        json += ",\"simHubPollErr\":" + String(g_simHubDebug.pollErrorCount);
        json += ",\"simHubSuppressedWhileUsb\":" + String(g_simHubDebug.suppressedWhileUsbCount);
        json += ",\"simHubLastOkAgeMs\":" + String(g_simHubDebug.lastSuccessMs > 0 ? (now - g_simHubDebug.lastSuccessMs) : 0);
        json += ",\"simHubLastErrAgeMs\":" + String(g_simHubDebug.lastErrorMs > 0 ? (now - g_simHubDebug.lastErrorMs) : 0);
        json += ",\"simHubLastError\":\"" + jsonEscape(g_simHubDebug.lastError) + "\"";
        json += ",\"ledRawRpm\":" + String(g_ledRenderDebug.lastRawRpm);
        json += ",\"ledFilteredRpm\":" + String(g_ledRenderDebug.lastFilteredRpm);
        json += ",\"ledStartRpm\":" + String(g_ledRenderDebug.lastStartRpm);
        json += ",\"ledDisplayedLeds\":" + String(g_ledRenderDebug.lastDisplayedLeds);
        json += ",\"ledDesiredLevel\":" + String(g_ledRenderDebug.lastDesiredLevel);
        json += ",\"ledDisplayedLevel\":" + String(g_ledRenderDebug.lastDisplayedLevel);
        json += ",\"ledLevelCount\":" + String(g_ledRenderDebug.lastLevelCount);
        json += ",\"ledFilterAdjusts\":" + String(g_ledRenderDebug.filterAdjustCount);
        json += ",\"ledRenderCalls\":" + String(g_ledRenderDebug.renderCalls);
        json += ",\"ledFrameShows\":" + String(g_ledRenderDebug.frameShowCount);
        json += ",\"ledFrameSkips\":" + String(g_ledRenderDebug.frameSkipCount);
        json += ",\"ledBrightnessUpdates\":" + String(g_ledRenderDebug.brightnessUpdateCount);
        json += ",\"ledShiftBlink\":" + String(g_ledRenderDebug.lastShiftBlink ? "true" : "false");
        json += ",\"ledPitLimiterOnly\":" + String(g_ledRenderDebug.pitLimiterOnly ? "true" : "false");
        json += ",\"ledFastResponseActive\":" + String(g_ledRenderDebug.fastResponseActive ? "true" : "false");
        json += ",\"redColorFallbackActive\":" + String(g_ledRenderDebug.redFallbackActive ? "true" : "false");
        json += ",\"ledDiagnosticMode\":\"" + jsonEscape(String(ledBarGetDiagnosticModeName())) + "\"";
        json += ",\"ledRenderMode\":\"" + jsonEscape(String(ledBarGetLastRenderModeName())) + "\"";
        json += ",\"ledLastWriter\":\"" + jsonEscape(String(ledBarGetLastWriterName())) + "\"";
        json += ",\"ledLastShowAgeMs\":" + String(g_ledRenderDebug.lastShowMs > 0 ? (now - g_ledRenderDebug.lastShowMs) : 0);
        json += ",\"ledLastFrameHash\":" + String(ledHistory.lastEvent.frameHash);
        json += ",\"ledExternalWriteAttempts\":" + String(ledHistory.externalWriteAttempts);
        json += ",\"ledSnapshotChangedDuringRender\":" + String(ledHistory.snapshotChangedDuringRender);
        json += ",\"ledDeterministicSweepActive\":" + String(ledHistory.deterministicSweepActive ? "true" : "false");
        json += ",\"ledSessionEffectsEnabled\":" + String(cfg.simSessionLedEffectsEnabled ? "true" : "false");
        json += ",\"ledActiveEffect\":\"" + jsonEscape(String(ledBarEffectNameById(ledHistory.activeEffect))) + "\"";
        json += ",\"ledQueuedEffect\":\"" + jsonEscape(String(ledBarEffectNameById(ledHistory.queuedEffect))) + "\"";
        json += ",\"ledLastQueuedEffect\":\"" + jsonEscape(String(ledBarEffectNameById(ledHistory.lastQueuedEffect))) + "\"";
        json += ",\"ledSessionEffectRequests\":" + String(ledHistory.sessionEffectRequests);
        json += ",\"ledSessionEffectSuppressions\":" + String(ledHistory.sessionEffectSuppressions);
        json += ",\"rpmStartRpm\":" + String(cfg.rpmStartRpm);
        json += ",\"ledManualBrightness\":" + String(cfg.brightness);
        json += ",\"ledConfiguredCount\":" + String(cfg.activeLedCount);
        json += ",\"ledAppliedBrightness\":" + String(ledBarGetAppliedBrightness());
        json += ",\"autoBrightnessEnabled\":" + String(cfg.autoBrightnessEnabled ? "true" : "false");
        json += ",\"ambientLightDetected\":" + String(ambientInfo.sensorDetected ? "true" : "false");
        json += ",\"ambientLightActive\":" + String(ambientInfo.sensorActive ? "true" : "false");
        json += ",\"ambientBusInitialized\":" + String(ambientInfo.busInitialized ? "true" : "false");
        json += ",\"ambientDeviceResponding\":" + String(ambientInfo.deviceResponding ? "true" : "false");
        json += ",\"ambientUsingSharedBus\":" + String(ambientInfo.usingSharedBus ? "true" : "false");
        json += ",\"ambientLightSdaPin\":" + String(cfg.ambientLightSdaPin);
        json += ",\"ambientLightSclPin\":" + String(cfg.ambientLightSclPin);
        json += ",\"ambientLux\":" + String(ambientInfo.filteredLux, 1);
        json += ",\"ambientRawLux\":" + String(ambientInfo.rawLux, 1);
        json += ",\"ambientRawAls\":" + String(ambientInfo.rawAls);
        json += ",\"ambientRawWhite\":" + String(ambientInfo.rawWhite);
        json += ",\"ambientConfigReg\":" + String(ambientInfo.configReg);
        json += ",\"ambientTargetBrightness\":" + String(ambientInfo.targetBrightness);
        json += ",\"ambientDesiredBrightness\":" + String(ambientInfo.desiredBrightness);
        json += ",\"ambientInitAttempts\":" + String(ambientInfo.initAttempts);
        json += ",\"ambientInitSuccess\":" + String(ambientInfo.initSuccessCount);
        json += ",\"ambientReadCount\":" + String(ambientInfo.readCount);
        json += ",\"ambientReadErrors\":" + String(ambientInfo.readErrorCount);
        json += ",\"ambientLastInitAgeMs\":" + String(ambientInfo.lastInitMs > 0 ? (now - ambientInfo.lastInitMs) : 0);
        json += ",\"ambientLastReadAgeMs\":" + String(ambientInfo.lastReadMs > 0 ? (now - ambientInfo.lastReadMs) : 0);
        json += ",\"ambientLastApplyAgeMs\":" + String(ambientInfo.lastApplyMs > 0 ? (now - ambientInfo.lastApplyMs) : 0);
        json += ",\"ambientLastError\":\"" + jsonEscape(ambientInfo.lastError) + "\"";
        json += ",\"gestureControlEnabled\":" + String(cfg.gestureControlEnabled ? "true" : "false");
        json += ",\"gestureSensorDetected\":" + String(gestureInfo.sensorDetected ? "true" : "false");
        json += ",\"gestureSensorActive\":" + String(gestureInfo.sensorActive ? "true" : "false");
        json += ",\"gestureBusInitialized\":" + String(gestureInfo.busInitialized ? "true" : "false");
        json += ",\"gestureDeviceResponding\":" + String(gestureInfo.deviceResponding ? "true" : "false");
        json += ",\"gestureAckResponding\":" + String(gestureInfo.ackResponding ? "true" : "false");
        json += ",\"gestureIdReadOk\":" + String(gestureInfo.idReadOk ? "true" : "false");
        json += ",\"gestureConfigApplied\":" + String(gestureInfo.configApplied ? "true" : "false");
        json += ",\"gestureUsingSharedBus\":" + String(gestureInfo.usingSharedBus ? "true" : "false");
        json += ",\"gestureSdaPin\":" + String(gestureInfo.sdaPin);
        json += ",\"gestureSclPin\":" + String(gestureInfo.sclPin);
        json += ",\"gestureIntPin\":" + String(gestureInfo.intPin);
        json += ",\"gestureIntConfigured\":" + String(gestureInfo.intConfigured ? "true" : "false");
        json += ",\"gestureIntEnabled\":" + String(gestureInfo.intEnabled ? "true" : "false");
        json += ",\"gestureIntPending\":" + String(gestureInfo.intPending ? "true" : "false");
        json += ",\"gestureIntLineLow\":" + String(gestureInfo.intLineLow ? "true" : "false");
        json += ",\"gestureIntTriggerCount\":" + String(gestureInfo.intTriggerCount);
        json += ",\"gestureDeviceId\":" + String(gestureInfo.deviceId);
        json += ",\"gestureLastStatusReg\":" + String(gestureInfo.lastStatusReg);
        json += ",\"gestureLastFifoLevel\":" + String(gestureInfo.lastFifoLevel);
        json += ",\"gestureInitAttempts\":" + String(gestureInfo.initAttempts);
        json += ",\"gestureInitSuccess\":" + String(gestureInfo.initSuccessCount);
        json += ",\"gestureProbeCount\":" + String(gestureInfo.probeCount);
        json += ",\"gesturePollCount\":" + String(gestureInfo.pollCount);
        json += ",\"gestureReadErrors\":" + String(gestureInfo.readErrorCount);
        json += ",\"gestureLastIntAgeMs\":" + String(gestureInfo.lastIntMs > 0 ? (now - gestureInfo.lastIntMs) : 0);
        json += ",\"gestureLastProbeAgeMs\":" + String(gestureInfo.lastProbeMs > 0 ? (now - gestureInfo.lastProbeMs) : 0);
        json += ",\"gestureLastReadAgeMs\":" + String(gestureInfo.lastReadMs > 0 ? (now - gestureInfo.lastReadMs) : 0);
        json += ",\"gestureLastError\":\"" + jsonEscape(gestureInfo.lastError) + "\"";
        json += ",\"gestureLastDirection\":\"" + jsonEscape(String(gestureSensorDirectionName(gestureInfo.lastGesture))) + "\"";
        json += ",\"gestureLastGestureAgeMs\":" + String(gestureInfo.lastGestureMs > 0 ? (now - gestureInfo.lastGestureMs) : 0);
        json += ",\"gestureCount\":" + String(gestureInfo.gestureCount);
        json += ",\"gestureModeSwitchCount\":" + String(gestureInfo.modeSwitchCount);
        json += ",\"displayFocus\":" + String(static_cast<int>(cfg.uiDisplayFocus));
        json += ",\"simHubConfigured\":" + String(cfg.simHubHost.length() > 0 ? "true" : "false");
        json += ",\"bleConnectInProgress\":" + String(g_bleConnectInProgress ? "true" : "false");
        json += ",\"bleConnectTargetAddr\":\"" + jsonEscape(g_bleConnectTargetAddr) + "\"";
        json += ",\"bleConnectTargetName\":\"" + jsonEscape(g_bleConnectTargetName) + "\"";
        json += ",\"bleConnectError\":\"" + jsonEscape(g_bleConnectLastError) + "\"";
        json += "}";
        server.send(200, "application/json", json);
    }

    void handleBleStatus()
    {
        markHttpActivity("WEB_BLE_STATUS");
        unsigned long now = millis();

        String json = "{";
        json += "\"connected\":" + String(g_connected ? "true" : "false");
        json += ",\"targetName\":\"" + jsonEscape(g_currentTargetName) + "\"";
        json += ",\"targetAddr\":\"" + jsonEscape(g_currentTargetAddr) + "\"";
        json += ",\"autoReconnect\":" + String(g_autoReconnect ? "true" : "false");
        json += ",\"manualActive\":" + String(g_manualConnectActive ? "true" : "false");
        json += ",\"manualFailed\":" + String(g_manualConnectFailed ? "true" : "false");
        json += ",\"manualAttempts\":" + String(g_manualConnectAttempts);
        json += ",\"autoAttempts\":" + String(g_autoReconnectAttempts);
        json += ",\"connectBusy\":" + String(g_connectTaskRunning ? "true" : "false");
        json += ",\"connectManual\":" + String(g_connectTaskWasManual ? "true" : "false");
        json += ",\"lastConnectOk\":" + String(g_connectTaskResult ? "true" : "false");
        json += ",\"connectInProgress\":" + String(g_bleConnectInProgress ? "true" : "false");
        json += ",\"connectTargetAddr\":\"" + jsonEscape(g_bleConnectTargetAddr) + "\"";
        json += ",\"connectTargetName\":\"" + jsonEscape(g_bleConnectTargetName) + "\"";
        json += ",\"connectError\":\"" + jsonEscape(g_bleConnectLastError) + "\"";
        long scanAge = (g_bleScanFinishedMs > 0) ? static_cast<long>((now - g_bleScanFinishedMs) / 1000UL) : -1;
        long connectAge = g_connectTaskRunning ? static_cast<long>((now - g_connectTaskStartMs) / 1000UL) : -1;
        json += ",\"scanRunning\":" + String(g_bleScanRunning ? "true" : "false");
        json += ",\"scanAge\":" + String(scanAge);
        json += ",\"connectAge\":" + String(connectAge);
        json += ",\"results\":[";
        const auto &res = getBleScanResults();
        for (size_t i = 0; i < res.size(); ++i)
        {
            if (i > 0)
                json += ",";
            json += "{\"name\":\"" + jsonEscape(res[i].name) + "\",\"addr\":\"" + jsonEscape(res[i].address) + "\"}";
        }
        json += "]";
        json += "}";
        server.send(200, "application/json", json);
    }

    void handleBleScan()
    {
        markHttpActivity("WEB_BLE_SCAN");
        if (g_bleConnectInProgress || g_connectTaskRunning || g_manualConnectActive)
        {
            server.send(200, "application/json", "{\"status\":\"busy\",\"reason\":\"connecting\"}");
            return;
        }
        bool started = startBleScan();
        if (started)
            server.send(200, "application/json", "{\"status\":\"started\"}");
        else
            server.send(200, "application/json", "{\"status\":\"busy\"}");
    }

    void handleBleConnectDevice()
    {
        markHttpActivity("WEB_BLE_CONNECT_DEVICE");
        if (!server.hasArg("address"))
        {
            server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"missing-address\"}");
            return;
        }

        String address = server.arg("address");
        String name = server.hasArg("name") ? server.arg("name") : "";
        int attempts = server.hasArg("attempts") ? server.arg("attempts").toInt() : MANUAL_CONNECT_RETRY_COUNT;

        requestManualConnect(address, name, attempts);
        server.send(200, "application/json", "{\"status\":\"queued\"}");
    }

    void handleWifiStatus()
    {
        markHttpActivity("WEB_WIFI_STATUS");
        WifiStatus st = getWifiStatus();
        String json = "{";
        json += "\"mode\":\"" + wifiModeToString(st.mode) + "\"";
        json += ",\"apActive\":" + String(st.apActive ? "true" : "false");
        json += ",\"apClients\":" + String(st.apClients);
        json += ",\"apIp\":\"" + jsonEscape(st.apIp) + "\"";
        json += ",\"staConnected\":" + String(st.staConnected ? "true" : "false");
        json += ",\"staConnecting\":" + String(st.staConnecting ? "true" : "false");
        json += ",\"staLastError\":\"" + jsonEscape(st.staLastError) + "\"";
        json += ",\"currentSsid\":\"" + jsonEscape(st.currentSsid) + "\"";
        json += ",\"staIp\":\"" + jsonEscape(st.staIp) + "\"";
        json += ",\"ip\":\"" + jsonEscape(st.ip) + "\"";
        json += ",\"scanRunning\":" + String(st.scanRunning ? "true" : "false");
        json += ",\"scanResults\":[";
        for (size_t i = 0; i < st.scanResults.size(); ++i)
        {
            if (i > 0)
                json += ",";
            json += "{\"ssid\":\"" + jsonEscape(st.scanResults[i].ssid) + "\",\"rssi\":" + String(st.scanResults[i].rssi) + "}";
        }
        json += "]";
        json += "}";
        server.send(200, "application/json", json);
    }

    void handleWifiScan()
    {
        markHttpActivity("WEB_WIFI_SCAN");
        bool started = startWifiScan();
        if (started)
            server.send(200, "application/json", "{\"status\":\"started\"}");
        else
            server.send(200, "application/json", "{\"status\":\"busy\"}");
    }

    void handleWifiConnect()
    {
        markHttpActivity("WEB_WIFI_CONNECT");
        if (!server.hasArg("ssid"))
        {
            server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"missing-ssid\"}");
            return;
        }

        String ssid = argTrimmed("ssid", "");
        String password = argTrimmed("password", "");
        WifiMode mode = cfg.wifiMode;
        WifiStatus st = getWifiStatus();
        if (st.staConnecting)
        {
            server.send(200, "application/json", "{\"status\":\"busy\",\"reason\":\"connecting\"}");
            return;
        }
        if (st.scanRunning)
        {
            server.send(200, "application/json", "{\"status\":\"busy\",\"reason\":\"scan-running\"}");
            return;
        }
        if (server.hasArg("mode"))
        {
            mode = static_cast<WifiMode>(clampInt(server.arg("mode").toInt(), 0, 2));
        }

        bool started = requestWifiConnect(ssid, password, mode);
        if (started)
        {
            server.send(200, "application/json", "{\"status\":\"started\"}");
        }
        else
        {
            server.send(200, "application/json", "{\"status\":\"error\",\"reason\":\"connect-not-started\"}");
        }
    }

    void handleWifiDisconnect()
    {
        markHttpActivity("WEB_WIFI_DISCONNECT");
        requestWifiDisconnect();
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    }

    void handleWifiSave()
    {
        markHttpActivity("WEB_WIFI_SAVE");
        applyWifiConfigFromRequest();
        saveConfig();
        setupWifiFromConfig(cfg);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    }

    void handleSettingsGet()
    {
        markHttpActivity("WEB_SETTINGS_GET");
        if (maybeRedirectToBridgeUi())
        {
            return;
        }
        bool saved = server.hasArg("saved");
        server.send(200, "text/html", buildSettingsPage(saved));
    }

    void handleSettingsSave()
    {
        markHttpActivity("WEB_SETTINGS_SAVE");
        bool newDev = server.hasArg("devMode");
        g_devMode = newDev;
        if (!g_devMode)
        {
            g_autoReconnect = true;
            g_forceImmediateReconnect = true;
            g_lastBleRetryMs = 0;
        }

        applyTelemetryConfigFromRequest();
        applyWifiConfigFromRequest();
        saveConfig();
        setupWifiFromConfig(cfg);

        server.sendHeader("Location", "/settings?saved=1", true);
        server.send(303, "text/plain", "Redirect");
    }

    void handleSettingsVehicleRefresh()
    {
        markHttpActivity("WEB_SETTINGS_VEHICLE_REFRESH");
        if (!g_connected)
        {
            server.send(200, "application/json", "{\"status\":\"error\",\"reason\":\"no-connection\"}");
            return;
        }
        requestVehicleInfo(true);
        server.send(200, "application/json", "{\"status\":\"started\"}");
    }

    void handleDevDisplayLogo()
    {
        markHttpActivity("WEB_DEV_DISPLAY_LOGO");
        displayShowTestLogo();
        server.send(200, "text/plain", "OK");
    }

    void handleDevDisplayStatus()
    {
        markHttpActivity("WEB_DEV_DISPLAY_STATUS");
        DisplayDebugInfo info = displayGetDebugInfo();

        String json = "{";
        json += "\"initAttempted\":" + String(info.initAttempted ? "true" : "false");
        json += ",\"ready\":" + String(info.ready ? "true" : "false");
        json += ",\"buffersAllocated\":" + String(info.buffersAllocated ? "true" : "false");
        json += ",\"panelInitialized\":" + String(info.panelInitialized ? "true" : "false");
        json += ",\"touchReady\":" + String(info.touchReady ? "true" : "false");
        json += ",\"tickFallback\":" + String(info.tickFallback ? "true" : "false");
        json += ",\"debugSimpleUi\":" + String(info.debugSimpleUi ? "true" : "false");
        json += ",\"lastLvglRunMs\":" + String(info.lastLvglRunMs);
        json += ",\"lastError\":\"" + jsonEscape(info.lastError) + "\"";
        json += "}";

        server.send(200, "application/json", json);
    }

    void handleDevDisplayPattern()
    {
        markHttpActivity("WEB_DEV_DISPLAY_PATTERN");
        if (server.method() != HTTP_POST)
        {
            server.send(405, "text/plain", "Method Not Allowed");
            return;
        }

        DisplayDebugPattern pattern = DisplayDebugPattern::ColorBars;
        if (server.hasArg("pattern"))
        {
            String p = server.arg("pattern");
            p.toLowerCase();
            if (p == "grid")
                pattern = DisplayDebugPattern::Grid;
            else if (p == "ui")
                pattern = DisplayDebugPattern::UiLabel;
            else
                pattern = DisplayDebugPattern::ColorBars;
        }

        displayShowDebugPattern(pattern);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    }

    void handleDevLedMode()
    {
        markHttpActivity("WEB_DEV_LED_MODE");
        if (server.method() != HTTP_POST)
        {
            server.send(405, "text/plain", "Method Not Allowed");
            return;
        }

        LedDiagnosticMode mode = LedDiagnosticMode::Live;
        if (server.hasArg("mode"))
        {
            String value = server.arg("mode");
            value.trim();
            value.toLowerCase();
            if (value == "off")
            {
                mode = LedDiagnosticMode::Off;
            }
            else if (value == "static-green")
            {
                mode = LedDiagnosticMode::StaticGreen;
            }
            else if (value == "static-white")
            {
                mode = LedDiagnosticMode::StaticWhite;
            }
            else if (value == "pit" || value == "pit-markers")
            {
                mode = LedDiagnosticMode::PitMarkers;
            }
        }

        ledBarSetDiagnosticMode(mode);
        String json = "{\"status\":\"ok\",\"mode\":\"";
        json += ledBarGetDiagnosticModeName();
        json += "\"}";
        server.send(200, "application/json", json);
    }

    void handleDevAmbientProbe()
    {
        markHttpActivity("WEB_DEV_AMBIENT_PROBE");
        if (server.method() != HTTP_POST)
        {
            server.send(405, "text/plain", "Method Not Allowed");
            return;
        }

        ambientLightForceProbe();
        const AmbientLightDebugInfo ambientInfo = ambientLightGetDebugInfo();

        String json = "{\"status\":\"ok\"";
        json += ",\"sensorDetected\":" + String(ambientInfo.sensorDetected ? "true" : "false");
        json += ",\"deviceResponding\":" + String(ambientInfo.deviceResponding ? "true" : "false");
        json += ",\"lastError\":\"" + jsonEscape(ambientInfo.lastError) + "\"";
        json += "}";
        server.send(200, "application/json", json);
    }

    void handleDevGestureProbe()
    {
        markHttpActivity("WEB_DEV_GESTURE_PROBE");
        if (server.method() != HTTP_POST)
        {
            server.send(405, "text/plain", "Method Not Allowed");
            return;
        }

        gestureSensorForceProbe();
        const GestureSensorDebugInfo gestureInfo = gestureSensorGetDebugInfo();

        String json = "{\"status\":\"ok\"";
        json += ",\"sensorDetected\":" + String(gestureInfo.sensorDetected ? "true" : "false");
        json += ",\"busInitialized\":" + String(gestureInfo.busInitialized ? "true" : "false");
        json += ",\"ackResponding\":" + String(gestureInfo.ackResponding ? "true" : "false");
        json += ",\"idReadOk\":" + String(gestureInfo.idReadOk ? "true" : "false");
        json += ",\"configApplied\":" + String(gestureInfo.configApplied ? "true" : "false");
        json += ",\"deviceResponding\":" + String(gestureInfo.deviceResponding ? "true" : "false");
        json += ",\"deviceId\":" + String(gestureInfo.deviceId);
        json += ",\"lastError\":\"" + jsonEscape(gestureInfo.lastError) + "\"";
        json += "}";
        server.send(200, "application/json", json);
    }

    void handleNotFound()
    {
        markHttpActivity("WEB_NOT_FOUND");
        server.send(404, "text/plain", "Not found");
    }
}

void initWebUi()
{
    static const char *kCollectedHeaders[] = {"X-ShiftLight-Bridge-Proxy"};
    server.collectHeaders(kCollectedHeaders, 1);
    server.on("/", HTTP_GET, handleRoot);
    server.on("/brightness", HTTP_GET, handleBrightness);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/test", HTTP_POST, handleTest);
    server.on("/test-diagnostic", HTTP_POST, handleTestDiagnostic);
    server.on("/connect", HTTP_POST, handleConnect);
    server.on("/disconnect", HTTP_POST, handleDisconnect);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/ble/status", HTTP_GET, handleBleStatus);
    server.on("/ble/scan", HTTP_POST, handleBleScan);
    server.on("/ble/connect-device", HTTP_POST, handleBleConnectDevice);
    server.on("/wifi/status", HTTP_GET, handleWifiStatus);
    server.on("/wifi/scan", HTTP_POST, handleWifiScan);
    server.on("/wifi/connect", HTTP_POST, handleWifiConnect);
    server.on("/wifi/disconnect", HTTP_POST, handleWifiDisconnect);
    server.on("/wifi/save", HTTP_POST, handleWifiSave);
    server.on("/settings", HTTP_GET, handleSettingsGet);
    server.on("/settings/", HTTP_GET, handleSettingsGet);
    server.on("/settings", HTTP_POST, handleSettingsSave);
    server.on("/settings/", HTTP_POST, handleSettingsSave);
    server.on("/settings/vehicle-refresh", HTTP_POST, handleSettingsVehicleRefresh);
    server.on("/dev/display-logo", HTTP_POST, handleDevDisplayLogo);
    server.on("/dev/display-status", HTTP_GET, handleDevDisplayStatus);
    server.on("/dev/display-pattern", HTTP_POST, handleDevDisplayPattern);
    server.on("/dev/led-mode", HTTP_POST, handleDevLedMode);
    server.on("/dev/ambient-probe", HTTP_POST, handleDevAmbientProbe);
    server.on("/dev/gesture-probe", HTTP_POST, handleDevGestureProbe);
    server.on("/dev/obd-send", HTTP_POST, handleDevObdSend);
    server.onNotFound(handleNotFound);

    LOG_INFO("WEB", "WEB_INIT", "Web UI routes registered");
    server.begin();
}

void webUiLoop()
{
    server.handleClient();
}
