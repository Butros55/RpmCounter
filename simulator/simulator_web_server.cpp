#include "simulator_web_server.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#include "simulator_app.h"
#include "virtual_led_bar.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
    constexpr intptr_t kInvalidSocketHandle = static_cast<intptr_t>(INVALID_SOCKET);
#else
    constexpr intptr_t kInvalidSocketHandle = -1;
#endif

    struct HttpRequest
    {
        std::string method;
        std::string path;
        std::string query;
        std::string body;
    };

    std::string trim_copy(std::string value)
    {
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch)
                                                { return !std::isspace(ch); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch)
                                 { return !std::isspace(ch); })
                        .base(),
                    value.end());
        return value;
    }

    std::string html_escape(const std::string &value)
    {
        std::string output;
        output.reserve(value.size());
        for (char ch : value)
        {
            switch (ch)
            {
            case '&':
                output += "&amp;";
                break;
            case '<':
                output += "&lt;";
                break;
            case '>':
                output += "&gt;";
                break;
            case '"':
                output += "&quot;";
                break;
            case '\'':
                output += "&#39;";
                break;
            default:
                output += ch;
                break;
            }
        }
        return output;
    }

    std::string json_escape(const std::string &value)
    {
        std::string output;
        output.reserve(value.size() + 8);
        for (char ch : value)
        {
            switch (ch)
            {
            case '\\':
            case '"':
                output += '\\';
                output += ch;
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                break;
            default:
                output += ch;
                break;
            }
        }
        return output;
    }

    std::string url_decode(const std::string &value)
    {
        std::string decoded;
        decoded.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] == '+')
            {
                decoded += ' ';
            }
            else if (value[i] == '%' && i + 2 < value.size())
            {
                const char hex[3] = {value[i + 1], value[i + 2], '\0'};
                decoded += static_cast<char>(std::strtol(hex, nullptr, 16));
                i += 2;
            }
            else
            {
                decoded += value[i];
            }
        }
        return decoded;
    }

    std::map<std::string, std::string> parse_form_urlencoded(const std::string &body)
    {
        std::map<std::string, std::string> values;
        std::stringstream stream(body);
        std::string pair;
        while (std::getline(stream, pair, '&'))
        {
            const size_t eq = pair.find('=');
            const std::string key = url_decode(pair.substr(0, eq));
            const std::string value = eq == std::string::npos ? std::string() : url_decode(pair.substr(eq + 1));
            values[key] = value;
        }
        return values;
    }

    int parse_int_or(const std::map<std::string, std::string> &values, const char *key, int fallback)
    {
        const auto it = values.find(key);
        if (it == values.end() || it->second.empty())
        {
            return fallback;
        }
        try
        {
            return std::stoi(it->second);
        }
        catch (...)
        {
            return fallback;
        }
    }

    bool parse_flag(const std::map<std::string, std::string> &values, const char *key, bool fallback)
    {
        const auto it = values.find(key);
        if (it == values.end())
        {
            return fallback;
        }

        const std::string normalized = trim_copy(it->second);
        return normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes";
    }

    std::string parse_string_or(const std::map<std::string, std::string> &values, const char *key, const std::string &fallback)
    {
        const auto it = values.find(key);
        if (it == values.end())
        {
            return fallback;
        }
        return trim_copy(it->second);
    }

    std::string html_color_hex(uint32_t color)
    {
        char buffer[8];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "#%02X%02X%02X",
                      static_cast<unsigned int>((color >> 16) & 0xFFu),
                      static_cast<unsigned int>((color >> 8) & 0xFFu),
                      static_cast<unsigned int>(color & 0xFFu));
        return std::string(buffer);
    }

    uint32_t parse_hex_color(const std::map<std::string, std::string> &values, const char *key, uint32_t fallback)
    {
        const auto it = values.find(key);
        if (it == values.end())
        {
            return fallback;
        }

        const std::string value = trim_copy(it->second);
        if (value.size() != 7 || value.front() != '#')
        {
            return fallback;
        }

        try
        {
            return static_cast<uint32_t>(std::stoul(value.substr(1), nullptr, 16));
        }
        catch (...)
        {
            return fallback;
        }
    }

    std::string telemetry_mode_name(const TelemetryServiceConfig &config)
    {
        if (config.mode == TelemetryInputMode::SimHub && config.allowSimulatorFallback)
        {
            return "auto";
        }
        return config.mode == TelemetryInputMode::SimHub ? "simhub" : "simulator";
    }

    std::string telemetry_mode_label(const TelemetryServiceConfig &config)
    {
        if (config.mode == TelemetryInputMode::SimHub && config.allowSimulatorFallback)
        {
            return "Auto";
        }
        return config.mode == TelemetryInputMode::SimHub ? "SimHub" : "Simulator";
    }

    std::string transport_name(SimHubTransport transport)
    {
        return transport == SimHubTransport::JsonUdp ? "udp" : "http";
    }

    std::string transport_label(SimHubTransport transport)
    {
        return transport == SimHubTransport::JsonUdp ? "UDP JSON" : "HTTP API";
    }

    std::string display_focus_label(UiDisplayFocusMetric focus)
    {
        switch (focus)
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

    UiDisplayFocusMetric parse_display_focus_value(const std::string &value, UiDisplayFocusMetric fallback)
    {
        std::string normalized = trim_copy(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (normalized == "gear" || normalized == "1")
        {
            return UiDisplayFocusMetric::Gear;
        }
        if (normalized == "speed" || normalized == "2")
        {
            return UiDisplayFocusMetric::Speed;
        }
        if (normalized == "rpm" || normalized == "0")
        {
            return UiDisplayFocusMetric::Rpm;
        }
        return fallback;
    }

    UiWifiMode parse_wifi_mode_value(const std::string &value, UiWifiMode fallback)
    {
        std::string normalized = trim_copy(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (normalized == "ap" || normalized == "aponly" || normalized == "0")
        {
            return UiWifiMode::ApOnly;
        }
        if (normalized == "sta" || normalized == "staonly" || normalized == "1")
        {
            return UiWifiMode::StaOnly;
        }
        if (normalized == "fallback" || normalized == "stawithapfallback" || normalized == "2")
        {
            return UiWifiMode::StaWithApFallback;
        }
        return fallback;
    }

    std::string wifi_mode_name(UiWifiMode mode)
    {
        switch (mode)
        {
        case UiWifiMode::ApOnly:
            return "ap";
        case UiWifiMode::StaOnly:
            return "sta";
        case UiWifiMode::StaWithApFallback:
        default:
            return "fallback";
        }
    }

    std::string ui_screen_name(UiScreenId screen)
    {
        switch (screen)
        {
        case UiScreenId::Brightness:
            return "brightness";
        case UiScreenId::Vehicle:
            return "vehicle";
        case UiScreenId::Wifi:
            return "wifi";
        case UiScreenId::Bluetooth:
            return "bluetooth";
        case UiScreenId::Settings:
            return "settings";
        case UiScreenId::Focus:
            return "focus";
        case UiScreenId::DisplayPicker:
            return "display";
        case UiScreenId::SourcePicker:
            return "source";
        case UiScreenId::WebLink:
            return "web";
        case UiScreenId::Home:
        default:
            return "home";
        }
    }

    std::string telemetry_source_label(UiTelemetrySource source, bool usingFallback)
    {
        if (usingFallback && source == UiTelemetrySource::Simulator)
        {
            return "Simulator Fallback";
        }

        switch (source)
        {
        case UiTelemetrySource::SimHubNetwork:
            return "SimHub";
        case UiTelemetrySource::UsbBridge:
            return "USB";
        case UiTelemetrySource::Simulator:
            return "Simulator";
        case UiTelemetrySource::Esp32Obd:
        default:
            return "OBD";
        }
    }

    SimulatorLedMode parse_led_mode_value(const std::string &value, SimulatorLedMode fallback)
    {
        std::string normalized = trim_copy(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });

        if (normalized == "casual" || normalized == "0")
        {
            return SimulatorLedMode::Casual;
        }
        if (normalized == "aggressive" || normalized == "aggressiv" || normalized == "2")
        {
            return SimulatorLedMode::Aggressive;
        }
        if (normalized == "gt3" || normalized == "endurance" || normalized == "3")
        {
            return SimulatorLedMode::Gt3;
        }
        if (normalized == "f1" || normalized == "f1-style" || normalized == "1")
        {
            return SimulatorLedMode::F1;
        }

        return fallback;
    }

    std::string wifi_mode_label(UiWifiMode mode)
    {
        switch (mode)
        {
        case UiWifiMode::StaOnly:
            return "Heim-WLAN";
        case UiWifiMode::StaWithApFallback:
            return "WLAN + AP";
        case UiWifiMode::ApOnly:
        default:
            return "Access Point";
        }
    }

    int clamp_int(int value, int minValue, int maxValue)
    {
        return std::max(minValue, std::min(maxValue, value));
    }

    void close_socket_handle(intptr_t handle)
    {
#ifdef _WIN32
        if (handle != static_cast<intptr_t>(INVALID_SOCKET))
        {
            closesocket(static_cast<SOCKET>(handle));
        }
#else
        if (handle >= 0)
        {
            close(static_cast<int>(handle));
        }
#endif
    }

    std::string http_response(const char *status, const char *contentType, const std::string &body)
    {
        return std::string("HTTP/1.1 ") + status +
               "\r\nContent-Type: " + contentType +
               "\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) +
               "\r\n\r\n" + body;
    }

    std::string redirect_response(const std::string &target)
    {
        return std::string("HTTP/1.1 303 See Other\r\nLocation: ") + target +
               "\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    }

    HttpRequest parse_request(const std::string &raw)
    {
        HttpRequest request{};
        const size_t firstLineEnd = raw.find("\r\n");
        if (firstLineEnd == std::string::npos)
        {
            return request;
        }

        std::stringstream parser(raw.substr(0, firstLineEnd));
        std::string rawPath;
        parser >> request.method >> rawPath;
        const size_t queryPos = rawPath.find('?');
        if (queryPos != std::string::npos)
        {
            request.path = rawPath.substr(0, queryPos);
            request.query = rawPath.substr(queryPos + 1);
        }
        else
        {
            request.path = rawPath;
        }

        const size_t bodyPos = raw.find("\r\n\r\n");
        if (bodyPos != std::string::npos)
        {
            request.body = raw.substr(bodyPos + 4);
        }
        return request;
    }

    std::string query_param(const HttpRequest &request, const char *key)
    {
        const auto values = parse_form_urlencoded(request.query);
        const auto it = values.find(key);
        return it == values.end() ? std::string() : it->second;
    }

    std::string build_status_json(const SimulatorStatusSnapshot &snapshot)
    {
        const VirtualLedBarFrame ledFrame =
            build_virtual_led_bar_frame(snapshot.runtime, snapshot.ledBar, snapshot.runtime.telemetryTimestampMs);

        std::string json = "{";
        json += "\"rpm\":" + std::to_string(snapshot.runtime.rpm);
        json += ",\"speedKmh\":" + std::to_string(snapshot.runtime.speedKmh);
        json += ",\"gear\":" + std::to_string(snapshot.runtime.gear);
        json += ",\"shift\":" + std::string(snapshot.runtime.shift ? "true" : "false");
        json += ",\"telemetrySource\":\"" + json_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "\"";
        json += ",\"telemetryMode\":\"" + telemetry_mode_name(snapshot.telemetry) + "\"";
        json += ",\"telemetryModeLabel\":\"" + json_escape(telemetry_mode_label(snapshot.telemetry)) + "\"";
        json += ",\"telemetryTransport\":\"" + transport_name(snapshot.telemetry.simHubTransport) + "\"";
        json += ",\"telemetryTransportLabel\":\"" + json_escape(transport_label(snapshot.telemetry.simHubTransport)) + "\"";
        json += ",\"telemetryStale\":" + std::string(snapshot.runtime.telemetryStale ? "true" : "false");
        json += ",\"telemetryUsingFallback\":" + std::string(snapshot.runtime.telemetryUsingFallback ? "true" : "false");
        json += ",\"wifiMode\":\"" + json_escape(wifi_mode_label(snapshot.runtime.wifiMode)) + "\"";
        json += ",\"wifiName\":\"" + json_escape(snapshot.runtime.currentSsid) + "\"";
        json += ",\"wifiConnected\":" + std::string(snapshot.runtime.staConnected ? "true" : "false");
        json += ",\"bleConnected\":" + std::string(snapshot.runtime.bleConnected ? "true" : "false");
        json += ",\"webBaseUrl\":\"" + json_escape(snapshot.webBaseUrl) + "\"";
        json += ",\"uiScreen\":\"" + json_escape(ui_screen_name(snapshot.ui.activeScreen)) + "\"";
        json += ",\"displayBrightness\":" + std::to_string(snapshot.runtime.settings.displayBrightness);
        json += ",\"nightMode\":" + std::string(snapshot.runtime.settings.nightMode ? "true" : "false");
        json += ",\"displayFocus\":\"" + json_escape(display_focus_label(snapshot.runtime.settings.displayFocus)) + "\"";
        json += ",\"useMph\":" + std::string(snapshot.device.useMph ? "true" : "false");
        json += ",\"ledBar\":{";
        json += "\"mode\":\"" + json_escape(simulator_led_mode_name(snapshot.ledBar.mode)) + "\"";
        json += ",\"modeLabel\":\"" + json_escape(simulator_led_mode_label(snapshot.ledBar.mode)) + "\"";
        json += ",\"autoScaleMaxRpm\":" + std::string(snapshot.ledBar.autoScaleMaxRpm ? "true" : "false");
        json += ",\"fixedMaxRpm\":" + std::to_string(snapshot.ledBar.fixedMaxRpm);
        json += ",\"effectiveMaxRpm\":" + std::to_string(snapshot.ledBar.effectiveMaxRpm);
        json += ",\"count\":" + std::to_string(snapshot.ledBar.activeLedCount);
        json += ",\"brightness\":" + std::to_string(snapshot.ledBar.brightness);
        json += ",\"litCount\":" + std::to_string(ledFrame.litCount);
        json += ",\"blinkActive\":" + std::string(ledFrame.blinkActive ? "true" : "false");
        json += ",\"greenColor\":\"" + json_escape(html_color_hex(snapshot.ledBar.greenColor)) + "\"";
        json += ",\"yellowColor\":\"" + json_escape(html_color_hex(snapshot.ledBar.yellowColor)) + "\"";
        json += ",\"redColor\":\"" + json_escape(html_color_hex(snapshot.ledBar.redColor)) + "\"";
        json += ",\"greenLabel\":\"" + json_escape(snapshot.ledBar.greenLabel) + "\"";
        json += ",\"yellowLabel\":\"" + json_escape(snapshot.ledBar.yellowLabel) + "\"";
        json += ",\"redLabel\":\"" + json_escape(snapshot.ledBar.redLabel) + "\"";
        json += ",\"colors\":[";
        for (size_t i = 0; i < ledFrame.leds.size(); ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += "\"" + virtual_led_color_hex(ledFrame.leds[i]) + "\"";
        }
        json += "]";
        json += "}";
        json += ",\"device\":{";
        json += "\"autoBrightnessEnabled\":" + std::string(snapshot.device.autoBrightnessEnabled ? "true" : "false");
        json += ",\"ambientLightSdaPin\":" + std::to_string(snapshot.device.ambientLightSdaPin);
        json += ",\"ambientLightSclPin\":" + std::to_string(snapshot.device.ambientLightSclPin);
        json += ",\"autoBrightnessStrengthPct\":" + std::to_string(snapshot.device.autoBrightnessStrengthPct);
        json += ",\"autoBrightnessMin\":" + std::to_string(snapshot.device.autoBrightnessMin);
        json += ",\"autoBrightnessResponsePct\":" + std::to_string(snapshot.device.autoBrightnessResponsePct);
        json += ",\"autoBrightnessLuxMin\":" + std::to_string(snapshot.device.autoBrightnessLuxMin);
        json += ",\"autoBrightnessLuxMax\":" + std::to_string(snapshot.device.autoBrightnessLuxMax);
        json += ",\"logoOnIgnitionOn\":" + std::string(snapshot.device.logoOnIgnitionOn ? "true" : "false");
        json += ",\"logoOnEngineStart\":" + std::string(snapshot.device.logoOnEngineStart ? "true" : "false");
        json += ",\"logoOnIgnitionOff\":" + std::string(snapshot.device.logoOnIgnitionOff ? "true" : "false");
        json += ",\"simSessionLedEffectsEnabled\":" + std::string(snapshot.device.simSessionLedEffectsEnabled ? "true" : "false");
        json += ",\"gestureControlEnabled\":" + std::string(snapshot.device.gestureControlEnabled ? "true" : "false");
        json += ",\"autoReconnect\":" + std::string(snapshot.device.autoReconnect ? "true" : "false");
        json += ",\"wifiModePreference\":\"" + json_escape(wifi_mode_name(snapshot.device.wifiModePreference)) + "\"";
        json += ",\"staSsid\":\"" + json_escape(snapshot.device.staSsid) + "\"";
        json += ",\"apSsid\":\"" + json_escape(snapshot.device.apSsid) + "\"";
        json += "}}";
        return json;
    }

    std::string build_wifi_status_json(const SimulatorStatusSnapshot &snapshot)
    {
        std::string json = "{";
        json += "\"mode\":\"" + json_escape(wifi_mode_label(snapshot.runtime.wifiMode)) + "\"";
        json += ",\"currentSsid\":\"" + json_escape(snapshot.runtime.currentSsid) + "\"";
        json += ",\"connected\":" + std::string(snapshot.runtime.staConnected ? "true" : "false");
        json += ",\"connecting\":" + std::string(snapshot.runtime.staConnecting ? "true" : "false");
        json += ",\"ip\":\"" + json_escape(snapshot.runtime.ip) + "\"";
        json += "}";
        return json;
    }

    std::string build_ble_status_json(const SimulatorStatusSnapshot &snapshot)
    {
        std::string json = "{";
        json += "\"connected\":" + std::string(snapshot.runtime.bleConnected ? "true" : "false");
        json += ",\"connecting\":" + std::string(snapshot.runtime.bleConnecting ? "true" : "false");
        json += ",\"source\":\"" + json_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "\"";
        json += "}";
        return json;
    }

    void append_shell_head(std::string &page, const char *title, bool settingsActive)
    {
        page += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>";
        page += "<meta name='theme-color' content='#08111b'><title>";
        page += title;
        page += "</title><style>";
        page += R"CSS(
:root{--bg:#08111b;--panel:#101a27;--panel-2:#132131;--text:#eef5ff;--muted:#8da2ba;--accent:#4bb7ff;--success:#40d39c;--warn:#ffb84d;--danger:#ff6f81;--border:#24364a;--radius:18px}
*{box-sizing:border-box}body{margin:0;min-height:100vh;font:500 15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;background:radial-gradient(circle at top right,rgba(56,118,255,.18),transparent 28%),radial-gradient(circle at top left,rgba(43,208,255,.1),transparent 24%),linear-gradient(180deg,#060c14 0%,#0a121d 100%);color:var(--text)}
a{color:inherit;text-decoration:none}.app{width:min(1180px,100%);margin:0 auto;padding:18px 16px 28px}.topbar{display:flex;flex-direction:column;gap:14px;margin-bottom:18px}.topbar-row{display:flex;align-items:flex-start;justify-content:space-between;gap:14px;flex-wrap:wrap}
.brand{display:flex;flex-direction:column;gap:4px}.eyebrow{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}.brand h1{margin:0;font-size:clamp(28px,8vw,42px);line-height:1;letter-spacing:-.04em}.brand p{margin:0;color:var(--muted);font-size:14px}
.tabs{display:inline-flex;gap:6px;padding:6px;border-radius:999px;background:rgba(12,21,32,.86);border:1px solid var(--border)}.tab{min-height:42px;padding:10px 16px;border-radius:999px;color:var(--muted);font-weight:700}.tab.active{background:linear-gradient(180deg,#18293d,#142435);color:var(--text);box-shadow:inset 0 0 0 1px rgba(99,165,255,.18)}
.hero{display:grid;gap:14px;margin-bottom:18px}.hero-card,.panel{border:1px solid var(--border);background:linear-gradient(180deg,rgba(16,26,39,.95),rgba(12,20,31,.95));border-radius:22px;padding:18px}.hero-card--accent{background:linear-gradient(180deg,rgba(20,31,48,.98),rgba(14,23,35,.98)),linear-gradient(135deg,rgba(75,183,255,.18),transparent 46%)}
.hero-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}.hero-kicker{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}.hero-title{font-size:20px;font-weight:800;line-height:1.1}.hero-sub{margin-top:6px;color:var(--muted);font-size:13px}
.pill-list{display:flex;gap:8px;flex-wrap:wrap}.pill{display:inline-flex;align-items:center;gap:8px;min-height:34px;padding:8px 12px;border-radius:999px;border:1px solid var(--border);background:rgba(8,14,22,.55);color:var(--text);font-size:12px;font-weight:700}.pill::before{content:"";width:8px;height:8px;border-radius:50%;background:#64768b}.pill.ok::before{background:var(--success)}.pill.warn::before{background:var(--warn)}.pill.bad::before{background:var(--danger)}.pill.neutral::before{background:var(--accent)}
.metric-grid{display:grid;gap:10px;grid-template-columns:repeat(2,minmax(0,1fr))}.metric{padding:12px;border-radius:16px;background:rgba(8,14,22,.56);border:1px solid rgba(255,255,255,.05)}.metric-label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}.metric-value{margin-top:6px;font-size:clamp(22px,7vw,34px);line-height:1;font-weight:800;letter-spacing:-.04em}
.layout,.two-col,.field-grid,.button-row{display:grid;gap:16px}.panel-title{margin:0;font-size:18px;line-height:1.1;letter-spacing:-.02em}.panel-copy{margin-top:4px;color:var(--muted);font-size:13px}.panel-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}
.led-preview{display:grid;grid-template-columns:repeat(auto-fit,minmax(18px,1fr));gap:6px;padding:14px;border-radius:16px;border:1px solid var(--border);background:#0b121b}.led-dot{width:100%;aspect-ratio:1/1;border-radius:999px;background:#26384d}
.button-row{grid-template-columns:repeat(auto-fit,minmax(120px,1fr))}.btn{appearance:none;border:none;min-height:48px;padding:12px 16px;border-radius:14px;font-weight:800;letter-spacing:-.01em;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;gap:8px}.btn-primary{background:linear-gradient(180deg,#46b0ff,#2bd0ff);color:#06111c}.btn-secondary{background:#1b2938;color:var(--text);border:1px solid var(--border)}
.field-grid{grid-template-columns:repeat(1,minmax(0,1fr))}.field label{display:block;margin-bottom:8px;font-size:12px;font-weight:700;color:var(--muted);letter-spacing:.06em;text-transform:uppercase}.field input,.field select{width:100%;min-height:48px;padding:12px 14px;border-radius:14px;border:1px solid var(--border);background:#0b121b;color:var(--text);outline:none}.field input:focus,.field select:focus{border-color:rgba(75,183,255,.72);box-shadow:0 0 0 1px rgba(75,183,255,.2)}
.callout,.toast{padding:14px;border-radius:16px;font-size:13px}.callout{border:1px solid rgba(75,183,255,.28);background:rgba(37,74,110,.14);color:#bfd9ff}.toast{border:1px solid rgba(64,211,156,.28);background:rgba(16,59,48,.36);color:#d9fff0;margin-top:12px}.hidden{display:none!important}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
@media(min-width:760px){.hero{grid-template-columns:1.2fr .8fr}.layout{grid-template-columns:1.1fr .9fr}.two-col,.field-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}
@media(min-width:980px){.field-grid.three{grid-template-columns:repeat(3,minmax(0,1fr))}}
)CSS";
        page += "</style></head><body><div class='app'><header class='topbar'><div class='topbar-row'><div class='brand'><span class='eyebrow'>RPMCounter / ShiftLight</span><h1>ShiftLight</h1><p>Lokale Dashboard- und Verbindungsansicht fuer den Desktop-Simulator mit gemeinsamer DDU-, LED- und SimHub-Steuerung.</p></div><nav class='tabs'>";
        page += settingsActive ? "<a class='tab' href='/'>Dashboard</a><a class='tab active' href='/settings'>Verbindung</a>"
                               : "<a class='tab active' href='/'>Dashboard</a><a class='tab' href='/settings'>Verbindung</a>";
        page += "</nav></div></header>";
    }

    void append_shell_footer(std::string &page)
    {
        page += "</div></body></html>";
    }

    void append_led_preview(std::string &page, const VirtualLedBarFrame &frame)
    {
        page += "<div class='led-preview' id='ledPreview'>";
        for (uint32_t color : frame.leds)
        {
            page += "<span class='led-dot' style='background:";
            page += virtual_led_color_hex(color);
            page += "'></span>";
        }
        page += "</div>";
    }

    std::string build_dashboard_page(const SimulatorStatusSnapshot &snapshot)
    {
        const VirtualLedBarFrame ledFrame =
            build_virtual_led_bar_frame(snapshot.runtime, snapshot.ledBar, snapshot.runtime.telemetryTimestampMs);

        std::string page;
        page.reserve(18000);
        append_shell_head(page, "ShiftLight Dashboard", false);

        page += "<section class='hero'><div class='hero-card hero-card--accent'><div class='hero-head'><div><div class='hero-kicker'>Dashboard</div><div class='hero-title'>Live-Anzeige, LED-Bar und Simulatorsteuerung</div><div class='hero-sub'>Der Desktop-Simulator bedient jetzt dieselbe Dashboard-/Verbindungsstruktur wie das eigentliche ShiftLight-Webfrontend.</div></div><div class='pill-list'>";
        page += "<span class='pill neutral' id='telemetryPill'>" + html_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "</span>";
        page += "<span class='pill " + std::string(snapshot.runtime.telemetryStale ? "warn" : "ok") + "' id='simhubPill'>" + html_escape(telemetry_mode_label(snapshot.telemetry)) + "</span>";
        page += "<span class='pill " + std::string(snapshot.runtime.staConnected ? "ok" : "warn") + "' id='wifiPill'>" + html_escape(snapshot.runtime.currentSsid.empty() ? wifi_mode_label(snapshot.runtime.wifiMode) : snapshot.runtime.currentSsid) + "</span>";
        page += "</div></div><div class='metric-grid'>";
        page += "<div class='metric'><div class='metric-label'>RPM</div><div class='metric-value' id='rpmValue'>" + std::to_string(snapshot.runtime.rpm) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>Gang</div><div class='metric-value' id='gearValue'>" + std::to_string(snapshot.runtime.gear) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>Speed</div><div class='metric-value' id='speedValue'>" + std::to_string(snapshot.runtime.speedKmh) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>Display</div><div class='metric-value' id='screenValue'>" + html_escape(ui_screen_name(snapshot.ui.activeScreen)) + "</div></div>";
        page += "</div></div>";

        page += "<div class='hero-card'><div class='hero-head'><div><div class='hero-kicker'>Zugriff</div><div class='hero-title'>Web- und SimHub-Pfade</div><div class='hero-sub'>Dashboard und Simulatorsteuerung laufen gemeinsam unter derselben Root-URL.</div></div></div>";
        page += "<div class='callout'>Dashboard: <strong class='mono'>" + html_escape(snapshot.webBaseUrl) + "</strong><br>SimHub Transport: <strong>" + html_escape(transport_label(snapshot.telemetry.simHubTransport)) + "</strong> auf Port <strong>" + std::to_string(snapshot.telemetry.simHubTransport == SimHubTransport::HttpApi ? snapshot.telemetry.httpPort : snapshot.telemetry.udpPort) + "</strong></div>";
        page += "</div></section>";

        page += "<div class='layout'><section class='panel'><div class='panel-head'><div><h2 class='panel-title'>Virtuelle externe LED-Bar</h2><div class='panel-copy'>Die LED-Bar bleibt ausserhalb des DDU-Displays und folgt denselben lokalen Einstellungen wie im Dashboard.</div></div></div>";
        append_led_preview(page, ledFrame);
        page += "<div class='panel-copy' style='margin-top:10px'>Modus: <span id='ledModeValue'>" + html_escape(simulator_led_mode_label(snapshot.ledBar.mode)) + "</span> | Aktive LEDs: <span id='ledCountValue'>" + std::to_string(snapshot.ledBar.activeLedCount) + "</span> | Brightness: <span id='ledBrightnessValue'>" + std::to_string(snapshot.ledBar.brightness) + "</span> | Leuchtet: <span id='ledLitValue'>" + std::to_string(ledFrame.litCount) + "</span></div>";
        page += "<div class='two-col' style='margin-top:14px'><div class='field'><label for='ledModeQuick'>LED Modus</label><select id='ledModeQuick' onchange='applyLedMode(this.value)'><option value='casual'" + std::string(snapshot.ledBar.mode == SimulatorLedMode::Casual ? " selected" : "") + ">Casual</option><option value='f1'" + std::string(snapshot.ledBar.mode == SimulatorLedMode::F1 ? " selected" : "") + ">F1-Style</option><option value='aggressive'" + std::string(snapshot.ledBar.mode == SimulatorLedMode::Aggressive ? " selected" : "") + ">Aggressiv</option><option value='gt3'" + std::string(snapshot.ledBar.mode == SimulatorLedMode::Gt3 ? " selected" : "") + ">GT3 / Endurance</option></select></div><div class='callout'>Den LED-Modus kannst du jetzt direkt hier im Dashboard umschalten, ohne auf eine Extra-Seite zu wechseln.</div></div>";
        page += "<div class='button-row' style='margin-top:16px'><button class='btn btn-secondary' type='button' onclick=\"sendCommand('rpm_down')\">RPM -</button><button class='btn btn-primary' type='button' onclick=\"sendCommand('rpm_up')\">RPM +</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('toggle_anim')\">Animation</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('toggle_shift')\">Shift</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('toggle_wifi')\">WiFi</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('toggle_ble')\">BLE</button></div>";
        page += "<div class='button-row' style='margin-top:12px'><button class='btn btn-secondary' type='button' onclick=\"sendCommand('ui_prev')\">UI Prev</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('ui_next')\">UI Next</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('ui_open')\">UI Open</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('ui_home')\">UI Home</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('ui_logo')\">Logo</button><button class='btn btn-secondary' type='button' onclick=\"sendCommand('reset')\">Reset</button></div></section>";

        page += "<section class='panel'><div class='panel-head'><div><h2 class='panel-title'>Status & Schnellzugriff</h2><div class='panel-copy'>Dashboard-Status wie auf dem eigentlichen Geraet, plus direkter Sprung in die Verbindungsseite.</div></div></div><div class='metric-grid'>";
        page += "<div class='metric'><div class='metric-label'>Quelle</div><div class='metric-value' id='sourceValue'>" + html_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>Modus</div><div class='metric-value' id='modeValue'>" + html_escape(telemetry_mode_label(snapshot.telemetry)) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>WLAN</div><div class='metric-value' id='wifiValue'>" + html_escape(snapshot.runtime.currentSsid) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>Display</div><div class='metric-value' id='displayBrightnessValue'>" + std::to_string(snapshot.runtime.settings.displayBrightness) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>LED Modus</div><div class='metric-value' id='ledModeStatusValue'>" + html_escape(simulator_led_mode_label(snapshot.ledBar.mode)) + "</div></div>";
        page += "</div><div class='callout' style='margin-top:16px'>Wenn du SimHub, LED-Anzahl, Polling oder Brightness aendern willst, nutze die <strong>Verbindung</strong>-Seite oben. Das Dashboard bleibt die Hauptansicht und aktualisiert sich live.</div><div class='button-row' style='margin-top:16px'><a class='btn btn-primary' href='/settings'>Zur Verbindung / Konfiguration</a></div></section></div>";

        page += R"HTML(<script>
async function fetchStatus(){ const res = await fetch('/status',{cache:'no-store'}); return await res.json(); }
function renderLedPreview(colors){
  const host=document.getElementById('ledPreview');
  host.innerHTML='';
  colors.forEach(color=>{ const dot=document.createElement('span'); dot.className='led-dot'; dot.style.background=color; host.appendChild(dot); });
}
async function applyLedMode(mode){
  await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ledMode='+encodeURIComponent(mode)});
  setTimeout(refreshDashboard,80);
}
async function refreshDashboard(){
  try{
    const data = await fetchStatus();
    document.getElementById('rpmValue').textContent = data.rpm;
    document.getElementById('gearValue').textContent = data.gear;
    document.getElementById('speedValue').textContent = data.speedKmh;
    document.getElementById('screenValue').textContent = data.uiScreen;
    document.getElementById('telemetryPill').textContent = data.telemetrySource;
    document.getElementById('simhubPill').textContent = data.telemetryModeLabel || (data.telemetryMode === 'auto' ? 'Auto' : (data.telemetryMode === 'simhub' ? 'SimHub' : 'Simulator'));
    document.getElementById('wifiPill').textContent = data.wifiName || data.wifiMode;
    document.getElementById('sourceValue').textContent = data.telemetrySource;
    document.getElementById('modeValue').textContent = data.telemetryModeLabel || (data.telemetryMode === 'auto' ? 'Auto' : (data.telemetryMode === 'simhub' ? 'SimHub' : 'Simulator'));
    document.getElementById('wifiValue').textContent = data.wifiName || data.wifiMode;
    document.getElementById('displayBrightnessValue').textContent = data.displayBrightness;
    document.getElementById('ledModeValue').textContent = data.ledBar.modeLabel;
    document.getElementById('ledModeStatusValue').textContent = data.ledBar.modeLabel;
    document.getElementById('ledModeQuick').value = data.ledBar.mode;
    document.getElementById('ledCountValue').textContent = data.ledBar.count;
    document.getElementById('ledBrightnessValue').textContent = data.ledBar.brightness;
    document.getElementById('ledLitValue').textContent = data.ledBar.litCount;
    renderLedPreview(data.ledBar.colors || []);
  }catch(e){}
}
async function sendCommand(action){
  await fetch('/command',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action='+encodeURIComponent(action)});
  setTimeout(refreshDashboard,80);
}
setInterval(refreshDashboard,500);
refreshDashboard();
</script>)HTML";

        append_shell_footer(page);
        return page;
    }

    std::string build_settings_page(const SimulatorStatusSnapshot &snapshot, bool savedNotice)
    {
        std::string page;
        page.reserve(28000);
        append_shell_head(page, "ShiftLight Verbindung", true);

        page += "<section class='hero'><div class='hero-card hero-card--accent'><div class='hero-head'><div><div class='hero-kicker'>Verbindung & Telemetrie</div><div class='hero-title'>SimHub, LED-Bar und Desktop-Simulator gemeinsam konfigurieren</div><div class='hero-sub'>Diese Seite ersetzt die separate Simulator-Website. Alles Wichtige bleibt im bekannten Dashboard-/Verbindungsfluss.</div></div><div class='pill-list'><span class='pill neutral'>" + html_escape(telemetry_mode_label(snapshot.telemetry)) + "</span><span class='pill " + std::string(snapshot.runtime.telemetryStale ? "warn" : "ok") + "'>" + html_escape(transport_label(snapshot.telemetry.simHubTransport)) + "</span><span class='pill " + std::string(snapshot.runtime.staConnected ? "ok" : "warn") + "'>" + html_escape(snapshot.runtime.currentSsid) + "</span></div></div><div class='callout'>Root-Dashboard: <strong class='mono'>" + html_escape(snapshot.webBaseUrl) + "</strong> | API: <strong class='mono'>/status</strong>, <strong class='mono'>/wifi/status</strong>, <strong class='mono'>/ble/status</strong></div></div>";
        page += "<div class='hero-card'><div class='hero-head'><div><div class='hero-kicker'>Aktueller Zustand</div><div class='hero-title'>Live-Werte aus dem laufenden Simulator</div><div class='hero-sub'>Beim Speichern werden LED-Bar, Display und Telemetrie sofort im laufenden Simulator uebernommen.</div></div></div><div class='metric-grid'><div class='metric'><div class='metric-label'>RPM</div><div class='metric-value'>" + std::to_string(snapshot.runtime.rpm) + "</div></div><div class='metric'><div class='metric-label'>Gear</div><div class='metric-value'>" + std::to_string(snapshot.runtime.gear) + "</div></div><div class='metric'><div class='metric-label'>Speed</div><div class='metric-value'>" + std::to_string(snapshot.runtime.speedKmh) + "</div></div><div class='metric'><div class='metric-label'>UI</div><div class='metric-value'>" + html_escape(ui_screen_name(snapshot.ui.activeScreen)) + "</div></div></div></div></section>";

        page += "<form method='POST' action='/settings'>";
        page += "<div class='layout'><section class='panel'><div class='panel-head'><div><h2 class='panel-title'>LED-Bar & Anzeige</h2><div class='panel-copy'>Hier liegen jetzt auch Auto-RPM, Farben, Labels und Anzeigeoptionen wie im normalen Setup.</div></div></div><div class='field-grid three'>";
        page += "<div class='field'><label for='ledMode'>LED Modus</label><select id='ledMode' name='ledMode'><option value='casual'" + std::string(snapshot.ledBar.mode == SimulatorLedMode::Casual ? " selected" : "") + ">Casual</option><option value='f1'" + std::string(snapshot.ledBar.mode == SimulatorLedMode::F1 ? " selected" : "") + ">F1-Style</option><option value='aggressive'" + std::string(snapshot.ledBar.mode == SimulatorLedMode::Aggressive ? " selected" : "") + ">Aggressiv</option><option value='gt3'" + std::string(snapshot.ledBar.mode == SimulatorLedMode::Gt3 ? " selected" : "") + ">GT3 / Endurance</option></select></div>";
        page += "<div class='field'><label for='ledCount'>LED Anzahl</label><input id='ledCount' name='ledCount' type='number' min='1' max='60' value='" + std::to_string(snapshot.ledBar.activeLedCount) + "'></div>";
        page += "<div class='field'><label for='ledBrightness'>LED Brightness</label><input id='ledBrightness' name='ledBrightness' type='number' min='0' max='255' value='" + std::to_string(snapshot.ledBar.brightness) + "'></div>";
        page += "<div class='field'><label for='autoScaleMaxRpm'>Auto Max RPM</label><select id='autoScaleMaxRpm' name='autoScaleMaxRpm'><option value='1'" + std::string(snapshot.ledBar.autoScaleMaxRpm ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.ledBar.autoScaleMaxRpm ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='fixedMaxRpm'>Fixed Max RPM</label><input id='fixedMaxRpm' name='fixedMaxRpm' type='number' min='1000' max='14000' value='" + std::to_string(snapshot.ledBar.fixedMaxRpm) + "'></div>";
        page += "<div class='field'><label for='startRpm'>Start RPM</label><input id='startRpm' name='startRpm' type='number' min='0' max='12000' value='" + std::to_string(snapshot.ledBar.startRpm) + "'></div>";
        page += "<div class='field'><label for='displayBrightness'>Display Brightness</label><input id='displayBrightness' name='displayBrightness' type='number' min='10' max='255' value='" + std::to_string(snapshot.runtime.settings.displayBrightness) + "'></div>";
        page += "<div class='field'><label for='nightMode'>Night Mode</label><select id='nightMode' name='nightMode'><option value='1'" + std::string(snapshot.runtime.settings.nightMode ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.runtime.settings.nightMode ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='displayFocus'>Display Fokus</label><select id='displayFocus' name='displayFocus'><option value='rpm'" + std::string(snapshot.runtime.settings.displayFocus == UiDisplayFocusMetric::Rpm ? " selected" : "") + ">RPM</option><option value='gear'" + std::string(snapshot.runtime.settings.displayFocus == UiDisplayFocusMetric::Gear ? " selected" : "") + ">Gang</option><option value='speed'" + std::string(snapshot.runtime.settings.displayFocus == UiDisplayFocusMetric::Speed ? " selected" : "") + ">Speed</option></select></div>";
        page += "<div class='field'><label for='useMph'>Einheit</label><select id='useMph' name='useMph'><option value='0'" + std::string(snapshot.device.useMph ? "" : " selected") + ">km/h</option><option value='1'" + std::string(snapshot.device.useMph ? " selected" : "") + ">mph</option></select></div>";
        page += "<div class='field'><label for='greenEndPct'>Gruen Ende %</label><input id='greenEndPct' name='greenEndPct' type='number' min='0' max='100' value='" + std::to_string(snapshot.ledBar.greenEndPct) + "'></div>";
        page += "<div class='field'><label for='yellowEndPct'>Gelb Ende %</label><input id='yellowEndPct' name='yellowEndPct' type='number' min='0' max='100' value='" + std::to_string(snapshot.ledBar.yellowEndPct) + "'></div>";
        page += "<div class='field'><label for='redEndPct'>Rot Ende %</label><input id='redEndPct' name='redEndPct' type='number' min='0' max='100' value='" + std::to_string(snapshot.ledBar.redEndPct) + "'></div>";
        page += "<div class='field'><label for='blinkStartPct'>Blink Start %</label><input id='blinkStartPct' name='blinkStartPct' type='number' min='0' max='100' value='" + std::to_string(snapshot.ledBar.blinkStartPct) + "'></div>";
        page += "<div class='field'><label for='blinkSpeedPct'>Blink Speed %</label><input id='blinkSpeedPct' name='blinkSpeedPct' type='number' min='0' max='100' value='" + std::to_string(snapshot.ledBar.blinkSpeedPct) + "'></div>";
        page += "<div class='field'><label for='effectiveMaxRpmView'>Aktive Max RPM</label><input id='effectiveMaxRpmView' type='number' value='" + std::to_string(snapshot.ledBar.effectiveMaxRpm) + "' readonly></div>";
        page += "<div class='field'><label for='greenColor'>Gruen Farbe</label><input id='greenColor' name='greenColor' type='color' value='" + html_color_hex(snapshot.ledBar.greenColor) + "'></div>";
        page += "<div class='field'><label for='yellowColor'>Gelb Farbe</label><input id='yellowColor' name='yellowColor' type='color' value='" + html_color_hex(snapshot.ledBar.yellowColor) + "'></div>";
        page += "<div class='field'><label for='redColor'>Rot Farbe</label><input id='redColor' name='redColor' type='color' value='" + html_color_hex(snapshot.ledBar.redColor) + "'></div>";
        page += "<div class='field'><label for='greenLabel'>Gruen Label</label><input id='greenLabel' name='greenLabel' type='text' value='" + html_escape(snapshot.ledBar.greenLabel) + "'></div>";
        page += "<div class='field'><label for='yellowLabel'>Gelb Label</label><input id='yellowLabel' name='yellowLabel' type='text' value='" + html_escape(snapshot.ledBar.yellowLabel) + "'></div>";
        page += "<div class='field'><label for='redLabel'>Rot Label</label><input id='redLabel' name='redLabel' type='text' value='" + html_escape(snapshot.ledBar.redLabel) + "'></div>";
        page += "</div></section>";

        page += "<section class='panel'><div class='panel-head'><div><h2 class='panel-title'>Auto-Brightness, Effekte & Netzwerk</h2><div class='panel-copy'>Das sind die restlichen Setup-Felder aus dem normalen Bereich, damit du im Simulator nicht dauernd zwischen Oberflaechen springen musst.</div></div></div><div class='field-grid three'>";
        page += "<div class='field'><label for='autoBrightnessEnabled'>Auto Brightness</label><select id='autoBrightnessEnabled' name='autoBrightnessEnabled'><option value='1'" + std::string(snapshot.device.autoBrightnessEnabled ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.device.autoBrightnessEnabled ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='ambientLightSdaPin'>Ambient SDA</label><input id='ambientLightSdaPin' name='ambientLightSdaPin' type='number' min='0' max='48' value='" + std::to_string(snapshot.device.ambientLightSdaPin) + "'></div>";
        page += "<div class='field'><label for='ambientLightSclPin'>Ambient SCL</label><input id='ambientLightSclPin' name='ambientLightSclPin' type='number' min='0' max='48' value='" + std::to_string(snapshot.device.ambientLightSclPin) + "'></div>";
        page += "<div class='field'><label for='autoBrightnessStrengthPct'>Auto Brightness %</label><input id='autoBrightnessStrengthPct' name='autoBrightnessStrengthPct' type='number' min='25' max='200' value='" + std::to_string(snapshot.device.autoBrightnessStrengthPct) + "'></div>";
        page += "<div class='field'><label for='autoBrightnessMin'>Auto Brightness Min</label><input id='autoBrightnessMin' name='autoBrightnessMin' type='number' min='0' max='255' value='" + std::to_string(snapshot.device.autoBrightnessMin) + "'></div>";
        page += "<div class='field'><label for='autoBrightnessResponsePct'>Auto Response %</label><input id='autoBrightnessResponsePct' name='autoBrightnessResponsePct' type='number' min='1' max='100' value='" + std::to_string(snapshot.device.autoBrightnessResponsePct) + "'></div>";
        page += "<div class='field'><label for='autoBrightnessLuxMin'>Auto Lux Min</label><input id='autoBrightnessLuxMin' name='autoBrightnessLuxMin' type='number' min='0' max='120000' value='" + std::to_string(snapshot.device.autoBrightnessLuxMin) + "'></div>";
        page += "<div class='field'><label for='autoBrightnessLuxMax'>Auto Lux Max</label><input id='autoBrightnessLuxMax' name='autoBrightnessLuxMax' type='number' min='1' max='120000' value='" + std::to_string(snapshot.device.autoBrightnessLuxMax) + "'></div>";
        page += "<div class='field'><label for='logoIgnOn'>Logo bei Zuendung an</label><select id='logoIgnOn' name='logoIgnOn'><option value='1'" + std::string(snapshot.device.logoOnIgnitionOn ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.device.logoOnIgnitionOn ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='logoEngStart'>Logo bei Motorstart</label><select id='logoEngStart' name='logoEngStart'><option value='1'" + std::string(snapshot.device.logoOnEngineStart ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.device.logoOnEngineStart ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='logoIgnOff'>Logo bei Zuendung aus</label><select id='logoIgnOff' name='logoIgnOff'><option value='1'" + std::string(snapshot.device.logoOnIgnitionOff ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.device.logoOnIgnitionOff ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='simFxLed'>Sim Session LED Effekte</label><select id='simFxLed' name='simFxLed'><option value='1'" + std::string(snapshot.device.simSessionLedEffectsEnabled ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.device.simSessionLedEffectsEnabled ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='gestureControlEnabled'>Gestensteuerung</label><select id='gestureControlEnabled' name='gestureControlEnabled'><option value='1'" + std::string(snapshot.device.gestureControlEnabled ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.device.gestureControlEnabled ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='autoReconnect'>Auto Reconnect</label><select id='autoReconnect' name='autoReconnect'><option value='1'" + std::string(snapshot.device.autoReconnect ? " selected" : "") + ">An</option><option value='0'" + std::string(snapshot.device.autoReconnect ? "" : " selected") + ">Aus</option></select></div>";
        page += "<div class='field'><label for='wifiModePreference'>WiFi Modus</label><select id='wifiModePreference' name='wifiModePreference'><option value='fallback'" + std::string(snapshot.device.wifiModePreference == UiWifiMode::StaWithApFallback ? " selected" : "") + ">WLAN + AP Fallback</option><option value='sta'" + std::string(snapshot.device.wifiModePreference == UiWifiMode::StaOnly ? " selected" : "") + ">Nur STA</option><option value='ap'" + std::string(snapshot.device.wifiModePreference == UiWifiMode::ApOnly ? " selected" : "") + ">Nur AP</option></select></div>";
        page += "<div class='field'><label for='staSsid'>STA SSID</label><input id='staSsid' name='staSsid' type='text' value='" + html_escape(snapshot.device.staSsid) + "'></div>";
        page += "<div class='field'><label for='staPassword'>STA Passwort</label><input id='staPassword' name='staPassword' type='text' value='" + html_escape(snapshot.device.staPassword) + "'></div>";
        page += "<div class='field'><label for='apSsid'>AP SSID</label><input id='apSsid' name='apSsid' type='text' value='" + html_escape(snapshot.device.apSsid) + "'></div>";
        page += "<div class='field'><label for='apPassword'>AP Passwort</label><input id='apPassword' name='apPassword' type='text' value='" + html_escape(snapshot.device.apPassword) + "'></div>";
        page += "</div></section></div>";

        page += "<div class='layout' style='margin-top:16px'><section class='panel'><div class='panel-head'><div><h2 class='panel-title'>Telemetrie & Transport</h2><div class='panel-copy'>Standard ist Auto. Dazu kommen die restlichen SimHub-/Transport-Felder im selben Flow.</div></div></div><div class='field-grid three'>";
        page += "<div class='field'><label for='telemetryMode'>Telemetry Mode</label><select id='telemetryMode' name='telemetryMode'><option value='auto'" + std::string(snapshot.telemetry.mode == TelemetryInputMode::SimHub && snapshot.telemetry.allowSimulatorFallback ? " selected" : "") + ">Auto (SimHub bevorzugt)</option><option value='simhub'" + std::string(snapshot.telemetry.mode == TelemetryInputMode::SimHub && !snapshot.telemetry.allowSimulatorFallback ? " selected" : "") + ">Nur SimHub</option><option value='simulator'" + std::string(snapshot.telemetry.mode == TelemetryInputMode::Simulator ? " selected" : "") + ">Nur Simulator</option></select></div>";
        page += "<div class='field'><label for='simhubTransport'>SimHub Transport</label><select id='simhubTransport' name='simhubTransport'><option value='http'" + std::string(snapshot.telemetry.simHubTransport == SimHubTransport::HttpApi ? " selected" : "") + ">HTTP API</option><option value='udp'" + std::string(snapshot.telemetry.simHubTransport == SimHubTransport::JsonUdp ? " selected" : "") + ">UDP JSON</option></select></div>";
        page += "<div class='field'><label for='simhubPort'>SimHub Port</label><input id='simhubPort' name='simhubPort' type='number' min='1' max='65535' value='" + std::to_string(snapshot.telemetry.simHubTransport == SimHubTransport::HttpApi ? snapshot.telemetry.httpPort : snapshot.telemetry.udpPort) + "'></div>";
        page += "<div class='field'><label for='simhubPollMs'>Poll / Refresh ms</label><input id='simhubPollMs' name='simhubPollMs' type='number' min='15' max='1000' value='" + std::to_string(snapshot.telemetry.pollIntervalMs) + "'></div>";
        page += "</div></section>";
        page += "<section class='panel'><div class='panel-head'><div><h2 class='panel-title'>Kurzuebersicht</h2><div class='panel-copy'>Damit du beim Einstellen sofort siehst, welche Schluesselwerte gerade aktiv sind.</div></div></div><div class='metric-grid'>";
        page += "<div class='metric'><div class='metric-label'>LED Modus</div><div class='metric-value'>" + html_escape(simulator_led_mode_label(snapshot.ledBar.mode)) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>Max RPM</div><div class='metric-value'>" + std::to_string(snapshot.ledBar.effectiveMaxRpm) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>Focus</div><div class='metric-value'>" + html_escape(display_focus_label(snapshot.runtime.settings.displayFocus)) + "</div></div>";
        page += "<div class='metric'><div class='metric-label'>WiFi</div><div class='metric-value'>" + html_escape(snapshot.device.staSsid) + "</div></div>";
        page += "</div><div class='callout' style='margin-top:16px'>Der Simulator nutzt diese Seite jetzt als vollstaendige Konfigurationsoberflaeche. Was hardwaregebunden ist, wird hier fuer den Desktop zumindest gespeichert und sichtbar gehalten; was simulatorrelevant ist, wirkt sofort live.</div></section></div>";

        page += "<div class='button-row' style='margin-top:18px'><button class='btn btn-primary' type='submit'>Einstellungen speichern</button><a class='btn btn-secondary' href='/'>Zurueck zum Dashboard</a></div>";
        page += savedNotice ? "<div class='toast'>Einstellungen wurden uebernommen und direkt auf den laufenden Simulator angewendet.</div>"
                            : "";
        page += "</form>";

        append_shell_footer(page);
        return page;
    }

    void apply_simulator_form_config(SimulatorApp &app, const std::map<std::string, std::string> &values)
    {
        SimulatorLedBarConfig ledConfig = app.ledBarConfigSnapshot();
        const auto ledModeIt = values.find("ledMode");
        if (ledModeIt != values.end())
        {
            ledConfig.mode = parse_led_mode_value(ledModeIt->second, ledConfig.mode);
        }
        if (values.find("autoScaleMaxRpm") != values.end())
        {
            ledConfig.autoScaleMaxRpm = parse_flag(values, "autoScaleMaxRpm", ledConfig.autoScaleMaxRpm);
        }
        ledConfig.fixedMaxRpm = clamp_int(parse_int_or(values, "fixedMaxRpm", ledConfig.fixedMaxRpm), 1000, 14000);
        ledConfig.activeLedCount = clamp_int(parse_int_or(values, "ledCount", ledConfig.activeLedCount), 1, 60);
        ledConfig.brightness = clamp_int(parse_int_or(values, "ledBrightness", ledConfig.brightness), 0, 255);
        ledConfig.startRpm = clamp_int(parse_int_or(values, "startRpm", ledConfig.startRpm), 0, 12000);
        ledConfig.greenEndPct = clamp_int(parse_int_or(values, "greenEndPct", ledConfig.greenEndPct), 0, 100);
        ledConfig.yellowEndPct = clamp_int(parse_int_or(values, "yellowEndPct", ledConfig.yellowEndPct), ledConfig.greenEndPct, 100);
        ledConfig.redEndPct = clamp_int(parse_int_or(values, "redEndPct", ledConfig.redEndPct), ledConfig.yellowEndPct, 100);
        ledConfig.blinkStartPct = clamp_int(parse_int_or(values, "blinkStartPct", ledConfig.blinkStartPct), ledConfig.redEndPct, 100);
        ledConfig.blinkSpeedPct = clamp_int(parse_int_or(values, "blinkSpeedPct", ledConfig.blinkSpeedPct), 0, 100);
        ledConfig.greenColor = parse_hex_color(values, "greenColor", ledConfig.greenColor);
        ledConfig.yellowColor = parse_hex_color(values, "yellowColor", ledConfig.yellowColor);
        ledConfig.redColor = parse_hex_color(values, "redColor", ledConfig.redColor);
        ledConfig.greenLabel = parse_string_or(values, "greenLabel", ledConfig.greenLabel);
        ledConfig.yellowLabel = parse_string_or(values, "yellowLabel", ledConfig.yellowLabel);
        ledConfig.redLabel = parse_string_or(values, "redLabel", ledConfig.redLabel);
        app.applyLedBarConfig(ledConfig);

        UiSettings settings = app.stateSnapshot().settings;
        settings.displayBrightness = clamp_int(parse_int_or(values, "displayBrightness", settings.displayBrightness), 10, 255);
        if (values.find("nightMode") != values.end())
        {
            settings.nightMode = parse_flag(values, "nightMode", settings.nightMode);
        }
        const auto displayFocusIt = values.find("displayFocus");
        if (displayFocusIt != values.end())
        {
            settings.displayFocus = parse_display_focus_value(displayFocusIt->second, settings.displayFocus);
        }
        app.saveSettings(settings);

        SimulatorDeviceConfig device = app.deviceConfigSnapshot();
        if (values.find("autoBrightnessEnabled") != values.end())
        {
            device.autoBrightnessEnabled = parse_flag(values, "autoBrightnessEnabled", device.autoBrightnessEnabled);
        }
        device.ambientLightSdaPin = clamp_int(parse_int_or(values, "ambientLightSdaPin", device.ambientLightSdaPin), 0, 48);
        device.ambientLightSclPin = clamp_int(parse_int_or(values, "ambientLightSclPin", device.ambientLightSclPin), 0, 48);
        device.autoBrightnessStrengthPct = clamp_int(parse_int_or(values, "autoBrightnessStrengthPct", device.autoBrightnessStrengthPct), 25, 200);
        device.autoBrightnessMin = clamp_int(parse_int_or(values, "autoBrightnessMin", device.autoBrightnessMin), 0, 255);
        device.autoBrightnessResponsePct = clamp_int(parse_int_or(values, "autoBrightnessResponsePct", device.autoBrightnessResponsePct), 1, 100);
        device.autoBrightnessLuxMin = clamp_int(parse_int_or(values, "autoBrightnessLuxMin", device.autoBrightnessLuxMin), 0, 120000);
        device.autoBrightnessLuxMax = clamp_int(parse_int_or(values, "autoBrightnessLuxMax", device.autoBrightnessLuxMax), device.autoBrightnessLuxMin + 1, 120000);
        if (values.find("logoIgnOn") != values.end())
        {
            device.logoOnIgnitionOn = parse_flag(values, "logoIgnOn", device.logoOnIgnitionOn);
        }
        if (values.find("logoEngStart") != values.end())
        {
            device.logoOnEngineStart = parse_flag(values, "logoEngStart", device.logoOnEngineStart);
        }
        if (values.find("logoIgnOff") != values.end())
        {
            device.logoOnIgnitionOff = parse_flag(values, "logoIgnOff", device.logoOnIgnitionOff);
        }
        if (values.find("simFxLed") != values.end())
        {
            device.simSessionLedEffectsEnabled = parse_flag(values, "simFxLed", device.simSessionLedEffectsEnabled);
        }
        if (values.find("gestureControlEnabled") != values.end())
        {
            device.gestureControlEnabled = parse_flag(values, "gestureControlEnabled", device.gestureControlEnabled);
        }
        if (values.find("useMph") != values.end())
        {
            device.useMph = parse_flag(values, "useMph", device.useMph);
        }
        if (values.find("autoReconnect") != values.end())
        {
            device.autoReconnect = parse_flag(values, "autoReconnect", device.autoReconnect);
        }
        const auto wifiModeIt = values.find("wifiModePreference");
        if (wifiModeIt != values.end())
        {
            device.wifiModePreference = parse_wifi_mode_value(wifiModeIt->second, device.wifiModePreference);
        }
        device.staSsid = parse_string_or(values, "staSsid", device.staSsid);
        device.staPassword = parse_string_or(values, "staPassword", device.staPassword);
        device.apSsid = parse_string_or(values, "apSsid", device.apSsid);
        device.apPassword = parse_string_or(values, "apPassword", device.apPassword);
        app.applyDeviceConfig(device);

        TelemetryServiceConfig telemetry = app.telemetryConfigSnapshot();
        const auto telemetryModeIt = values.find("telemetryMode");
        if (telemetryModeIt != values.end())
        {
            if (telemetryModeIt->second == "auto")
            {
                telemetry.mode = TelemetryInputMode::SimHub;
                telemetry.allowSimulatorFallback = true;
            }
            else if (telemetryModeIt->second == "simhub")
            {
                telemetry.mode = TelemetryInputMode::SimHub;
                telemetry.allowSimulatorFallback = false;
            }
            else
            {
                telemetry.mode = TelemetryInputMode::Simulator;
                telemetry.allowSimulatorFallback = false;
            }
        }
        const auto transportIt = values.find("simhubTransport");
        if (transportIt != values.end())
        {
            telemetry.simHubTransport = transportIt->second == "udp" ? SimHubTransport::JsonUdp : SimHubTransport::HttpApi;
        }
        const int portValue =
            clamp_int(parse_int_or(values, "simhubPort", telemetry.simHubTransport == SimHubTransport::HttpApi ? telemetry.httpPort : telemetry.udpPort), 1, 65535);
        if (telemetry.simHubTransport == SimHubTransport::HttpApi)
        {
            telemetry.httpPort = static_cast<uint16_t>(portValue);
        }
        else
        {
            telemetry.udpPort = static_cast<uint16_t>(portValue);
        }
        telemetry.pollIntervalMs = static_cast<uint32_t>(clamp_int(parse_int_or(values, "simhubPollMs", static_cast<int>(telemetry.pollIntervalMs)), 15, 1000));
        if (values.find("allowFallback") != values.end())
        {
            telemetry.allowSimulatorFallback = parse_flag(values, "allowFallback", telemetry.allowSimulatorFallback);
        }
        app.configureTelemetry(telemetry);
    }

    std::string handle_command(SimulatorApp &app, const std::string &action)
    {
        if (action == "rpm_up")
        {
            app.execute(SimulatorCommand::IncreaseRpm);
        }
        else if (action == "rpm_down")
        {
            app.execute(SimulatorCommand::DecreaseRpm);
        }
        else if (action == "toggle_anim")
        {
            app.execute(SimulatorCommand::ToggleAnimation);
        }
        else if (action == "toggle_shift")
        {
            app.execute(SimulatorCommand::ToggleShift);
        }
        else if (action == "toggle_wifi")
        {
            app.execute(SimulatorCommand::CycleWifiState);
        }
        else if (action == "toggle_ble")
        {
            app.execute(SimulatorCommand::ToggleBleState);
        }
        else if (action == "reset")
        {
            app.execute(SimulatorCommand::ResetState);
        }
        else if (action == "ui_prev")
        {
            app.queueUiAction(UiDebugAction::PreviousCard);
        }
        else if (action == "ui_next")
        {
            app.queueUiAction(UiDebugAction::NextCard);
        }
        else if (action == "ui_open")
        {
            app.queueUiAction(UiDebugAction::OpenSelectedCard);
        }
        else if (action == "ui_home")
        {
            app.queueUiAction(UiDebugAction::GoHome);
        }
        else if (action == "ui_logo")
        {
            app.queueUiAction(UiDebugAction::ShowLogo);
        }

        return http_response("200 OK", "application/json", "{\"status\":\"ok\"}");
    }

    std::string handle_http_request(SimulatorApp &app, const HttpRequest &request)
    {
        if (request.method == "GET" && (request.path == "/" || request.path == "/index.html"))
        {
            return http_response("200 OK", "text/html; charset=utf-8", build_dashboard_page(app.statusSnapshot()));
        }

        if (request.method == "GET" && request.path == "/settings")
        {
            const bool savedNotice = query_param(request, "saved") == "1";
            return http_response("200 OK", "text/html; charset=utf-8", build_settings_page(app.statusSnapshot(), savedNotice));
        }

        if (request.method == "POST" && request.path == "/settings")
        {
            apply_simulator_form_config(app, parse_form_urlencoded(request.body));
            return redirect_response("/settings?saved=1");
        }

        if (request.method == "GET" && (request.path == "/status" || request.path == "/api/status"))
        {
            return http_response("200 OK", "application/json", build_status_json(app.statusSnapshot()));
        }

        if (request.method == "GET" && request.path == "/wifi/status")
        {
            return http_response("200 OK", "application/json", build_wifi_status_json(app.statusSnapshot()));
        }

        if (request.method == "GET" && request.path == "/ble/status")
        {
            return http_response("200 OK", "application/json", build_ble_status_json(app.statusSnapshot()));
        }

        if (request.method == "POST" && (request.path == "/command" || request.path == "/api/command"))
        {
            const auto values = parse_form_urlencoded(request.body);
            const auto it = values.find("action");
            return handle_command(app, it == values.end() ? std::string() : it->second);
        }

        if (request.method == "POST" && request.path == "/api/config")
        {
            apply_simulator_form_config(app, parse_form_urlencoded(request.body));
            return http_response("200 OK", "application/json", "{\"status\":\"saved\"}");
        }

        if (request.method == "GET" && request.path == "/health")
        {
            return http_response("200 OK", "text/plain; charset=utf-8", "ok");
        }

        return http_response("404 Not Found", "text/plain; charset=utf-8", "Not found");
    }
}

SimulatorWebServer::SimulatorWebServer(SimulatorApp &app)
    : app_(app)
{
}

SimulatorWebServer::~SimulatorWebServer()
{
    stop();
}

bool SimulatorWebServer::start(uint16_t port)
{
    stop();

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return false;
    }
    winsockReady_ = true;
#endif

    const intptr_t socketHandle = static_cast<intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (socketHandle == kInvalidSocketHandle)
    {
        stop();
        return false;
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt(static_cast<SOCKET>(socketHandle), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#else
    setsockopt(static_cast<int>(socketHandle), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int bindResult = 0;
#ifdef _WIN32
    bindResult = ::bind(static_cast<SOCKET>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#else
    bindResult = ::bind(static_cast<int>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#endif
    if (bindResult != 0)
    {
        close_socket_handle(socketHandle);
        stop();
        return false;
    }

#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(static_cast<SOCKET>(socketHandle), FIONBIO, &nonBlocking);
#else
    const int flags = fcntl(static_cast<int>(socketHandle), F_GETFL, 0);
    fcntl(static_cast<int>(socketHandle), F_SETFL, flags | O_NONBLOCK);
#endif

#ifdef _WIN32
    if (::listen(static_cast<SOCKET>(socketHandle), SOMAXCONN) != 0)
#else
    if (::listen(static_cast<int>(socketHandle), SOMAXCONN) != 0)
#endif
    {
        close_socket_handle(socketHandle);
        stop();
        return false;
    }

    listenSocket_ = socketHandle;
    port_ = port;
    app_.setWebServerPort(port_);
    stopRequested_ = false;
    running_ = true;
    worker_ = std::thread(&SimulatorWebServer::run, this);
    return true;
}

void SimulatorWebServer::stop()
{
    stopRequested_ = true;
    running_ = false;
    if (worker_.joinable())
    {
        worker_.join();
    }
    close_socket_handle(listenSocket_);
    listenSocket_ = kInvalidSocketHandle;
    port_ = 0;
#ifdef _WIN32
    if (winsockReady_)
    {
        WSACleanup();
        winsockReady_ = false;
    }
#endif
}

bool SimulatorWebServer::isRunning() const
{
    return running_;
}

uint16_t SimulatorWebServer::port() const
{
    return port_;
}

void SimulatorWebServer::run()
{
    while (!stopRequested_)
    {
        sockaddr_in clientAddress{};
#ifdef _WIN32
        int clientLength = sizeof(clientAddress);
        const intptr_t clientSocket = static_cast<intptr_t>(::accept(static_cast<SOCKET>(listenSocket_), reinterpret_cast<sockaddr *>(&clientAddress), &clientLength));
        const bool noClient = clientSocket == static_cast<intptr_t>(INVALID_SOCKET) && WSAGetLastError() == WSAEWOULDBLOCK;
#else
        socklen_t clientLength = sizeof(clientAddress);
        const intptr_t clientSocket = static_cast<intptr_t>(::accept(static_cast<int>(listenSocket_), reinterpret_cast<sockaddr *>(&clientAddress), &clientLength));
        const bool noClient = clientSocket < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
#endif
        if (noClient)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (clientSocket == kInvalidSocketHandle)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::string request;
        char buffer[2048]{};
        while (true)
        {
#ifdef _WIN32
            const int bytesRead = ::recv(static_cast<SOCKET>(clientSocket), buffer, static_cast<int>(sizeof(buffer)), 0);
#else
            const int bytesRead = static_cast<int>(::recv(static_cast<int>(clientSocket), buffer, sizeof(buffer), 0));
#endif
            if (bytesRead <= 0)
            {
                break;
            }
            request.append(buffer, static_cast<size_t>(bytesRead));

            const size_t headerEnd = request.find("\r\n\r\n");
            if (headerEnd != std::string::npos)
            {
                size_t contentLength = 0;
                const std::string headers = request.substr(0, headerEnd);
                const std::string needle = "Content-Length:";
                const size_t contentPos = headers.find(needle);
                if (contentPos != std::string::npos)
                {
                    const size_t lineEnd = headers.find("\r\n", contentPos);
                    const std::string lenValue =
                        trim_copy(headers.substr(contentPos + needle.size(), lineEnd - contentPos - needle.size()));
                    contentLength = static_cast<size_t>(std::max(0, std::atoi(lenValue.c_str())));
                }
                if (request.size() >= headerEnd + 4 + contentLength)
                {
                    break;
                }
            }
        }

        const std::string response = handle_http_request(app_, parse_request(request));
#ifdef _WIN32
        ::send(static_cast<SOCKET>(clientSocket), response.data(), static_cast<int>(response.size()), 0);
#else
        ::send(static_cast<int>(clientSocket), response.data(), response.size(), 0);
#endif
        close_socket_handle(clientSocket);
    }

    running_ = false;
}
