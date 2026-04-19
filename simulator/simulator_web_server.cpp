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

    SideLedPreset parse_side_led_preset_value(const std::string &value, SideLedPreset fallback)
    {
        std::string normalized = trim_copy(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (normalized == "gt3" || normalized == "0")
        {
            return SideLedPreset::Gt3;
        }
        if (normalized == "casual" || normalized == "1")
        {
            return SideLedPreset::Casual;
        }
        if (normalized == "minimal" || normalized == "2")
        {
            return SideLedPreset::Minimal;
        }
        return fallback;
    }

    SideLedPriorityMode parse_side_led_priority_mode_value(const std::string &value, SideLedPriorityMode fallback)
    {
        std::string normalized = trim_copy(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (normalized == "low" || normalized == "0")
        {
            return SideLedPriorityMode::Low;
        }
        if (normalized == "normal" || normalized == "1")
        {
            return SideLedPriorityMode::Normal;
        }
        if (normalized == "high" || normalized == "2")
        {
            return SideLedPriorityMode::High;
        }
        if (normalized == "override" || normalized == "3")
        {
            return SideLedPriorityMode::Override;
        }
        return fallback;
    }

    SideLedWarningPriorityMode parse_side_led_warning_priority_mode_value(const std::string &value, SideLedWarningPriorityMode fallback)
    {
        std::string normalized = trim_copy(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (normalized == "normal" || normalized == "0")
        {
            return SideLedWarningPriorityMode::Normal;
        }
        if (normalized == "critical" || normalized == "criticalonly" || normalized == "1")
        {
            return SideLedWarningPriorityMode::CriticalOnly;
        }
        if (normalized == "override" || normalized == "alwaysoverride" || normalized == "2")
        {
            return SideLedWarningPriorityMode::AlwaysOverride;
        }
        return fallback;
    }

    std::string side_led_priority_mode_name(SideLedPriorityMode mode)
    {
        switch (mode)
        {
        case SideLedPriorityMode::Low:
            return "low";
        case SideLedPriorityMode::High:
            return "high";
        case SideLedPriorityMode::Override:
            return "override";
        case SideLedPriorityMode::Normal:
        default:
            return "normal";
        }
    }

    std::string side_led_warning_priority_mode_name(SideLedWarningPriorityMode mode)
    {
        switch (mode)
        {
        case SideLedWarningPriorityMode::CriticalOnly:
            return "critical";
        case SideLedWarningPriorityMode::AlwaysOverride:
            return "override";
        case SideLedWarningPriorityMode::Normal:
        default:
            return "normal";
        }
    }

    SideLedTestPattern parse_side_led_test_pattern_value(const std::string &value)
    {
        std::string normalized = trim_copy(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (normalized == "side_test_accel" || normalized == "accelerate")
        {
            return SideLedTestPattern::Accelerate;
        }
        if (normalized == "side_test_brake" || normalized == "brake")
        {
            return SideLedTestPattern::Brake;
        }
        if (normalized == "side_test_traction_left" || normalized == "traction-left")
        {
            return SideLedTestPattern::TractionLeft;
        }
        if (normalized == "side_test_traction_right" || normalized == "traction-right")
        {
            return SideLedTestPattern::TractionRight;
        }
        if (normalized == "side_test_traction_both" || normalized == "traction-both" || normalized == "side_test_both" || normalized == "both-cars")
        {
            return SideLedTestPattern::TractionBoth;
        }
        return SideLedTestPattern::None;
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

    std::string selected_attr(bool value)
    {
        return value ? " selected" : "";
    }

    std::string checked_attr(bool value)
    {
        return value ? " checked" : "";
    }

    std::string safe_label(const std::string &value, const char *fallback)
    {
        const std::string trimmed = trim_copy(value);
        return trimmed.empty() ? std::string(fallback) : trimmed;
    }

    bool has_drive_gear(int gear)
    {
        return gear >= 1 && gear <= static_cast<int>(kSimulatorGearCount);
    }

    int active_fixed_max_rpm(const SimulatorStatusSnapshot &snapshot)
    {
        if (snapshot.ledBar.maxRpmPerGearEnabled && has_drive_gear(snapshot.runtime.gear))
        {
            return snapshot.ledBar.fixedMaxRpmByGear[static_cast<size_t>(snapshot.runtime.gear - 1)];
        }
        return snapshot.ledBar.fixedMaxRpm;
    }

    int detected_gear_count(const SimulatorStatusSnapshot &snapshot)
    {
        int detected = 0;
        for (size_t i = 0; i < kSimulatorGearCount; ++i)
        {
            if (snapshot.ledBar.learnedMaxRpmByGear[i])
            {
                detected = static_cast<int>(i + 1);
            }
        }
        return detected;
    }

    int learned_gear_total(const SimulatorStatusSnapshot &snapshot)
    {
        int learned = 0;
        for (bool value : snapshot.ledBar.learnedMaxRpmByGear)
        {
            if (value)
            {
                ++learned;
            }
        }
        return learned;
    }

    std::string wifi_mode_api_name(UiWifiMode mode)
    {
        switch (mode)
        {
        case UiWifiMode::StaOnly:
            return "STA_ONLY";
        case UiWifiMode::StaWithApFallback:
            return "STA_WITH_AP_FALLBACK";
        case UiWifiMode::ApOnly:
        default:
            return "AP_ONLY";
        }
    }

    std::string simhub_state_label(UiSimHubState state)
    {
        switch (state)
        {
        case UiSimHubState::WaitingForHost:
            return "Host fehlt";
        case UiSimHubState::WaitingForNetwork:
            return "WLAN fehlt";
        case UiSimHubState::WaitingForData:
            return "Warte auf Daten";
        case UiSimHubState::Live:
            return "Live";
        case UiSimHubState::Error:
            return "Fehler";
        case UiSimHubState::Disabled:
        default:
            return "Deaktiviert";
        }
    }

    std::string current_ip_string(const SimulatorStatusSnapshot &snapshot)
    {
        if (!snapshot.runtime.ip.empty())
        {
            return snapshot.runtime.ip;
        }
        if (!snapshot.runtime.staIp.empty())
        {
            return snapshot.runtime.staIp;
        }
        if (!snapshot.runtime.apIp.empty())
        {
            return snapshot.runtime.apIp;
        }
        return snapshot.webBaseUrl;
    }

    std::string sta_summary(const SimulatorStatusSnapshot &snapshot)
    {
        if (snapshot.runtime.staConnected)
        {
            return snapshot.runtime.currentSsid.empty() ? std::string("Verbunden") : snapshot.runtime.currentSsid;
        }
        if (snapshot.runtime.staConnecting)
        {
            return "Verbindung laeuft";
        }
        if (!snapshot.runtime.staLastError.empty())
        {
            return snapshot.runtime.staLastError;
        }
        if (snapshot.runtime.apActive)
        {
            return snapshot.device.apSsid.empty() ? std::string("AP aktiv") : snapshot.device.apSsid;
        }
        return "Offline";
    }

    std::string ble_summary(const SimulatorStatusSnapshot &snapshot)
    {
        if (snapshot.runtime.bleConnecting)
        {
            return "Verbindet";
        }
        if (snapshot.runtime.bleConnected)
        {
            return snapshot.bleTargetName.empty() ? std::string("Verbunden") : snapshot.bleTargetName;
        }
        return "Nicht verbunden";
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
        json += ",\"activeTelemetry\":\"" + json_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "\"";
        json += ",\"telemetrySource\":\"" + json_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "\"";
        json += ",\"telemetryMode\":\"" + telemetry_mode_name(snapshot.telemetry) + "\"";
        json += ",\"telemetryModeLabel\":\"" + json_escape(telemetry_mode_label(snapshot.telemetry)) + "\"";
        json += ",\"telemetryTransport\":\"" + transport_name(snapshot.telemetry.simHubTransport) + "\"";
        json += ",\"telemetryTransportLabel\":\"" + json_escape(transport_label(snapshot.telemetry.simHubTransport)) + "\"";
        json += ",\"telemetryStale\":" + std::string(snapshot.runtime.telemetryStale ? "true" : "false");
        json += ",\"telemetryFallback\":" + std::string(snapshot.runtime.telemetryUsingFallback ? "true" : "false");
        json += ",\"telemetryUsingFallback\":" + std::string(snapshot.runtime.telemetryUsingFallback ? "true" : "false");
        json += ",\"simHubState\":\"" + json_escape(simhub_state_label(snapshot.runtime.simHubState)) + "\"";
        json += ",\"wifiMode\":\"" + json_escape(wifi_mode_label(snapshot.runtime.wifiMode)) + "\"";
        json += ",\"wifiModeCode\":\"" + json_escape(wifi_mode_api_name(snapshot.runtime.wifiMode)) + "\"";
        json += ",\"wifiName\":\"" + json_escape(snapshot.runtime.currentSsid) + "\"";
        json += ",\"wifiSummary\":\"" + json_escape(sta_summary(snapshot)) + "\"";
        json += ",\"wifiConnected\":" + std::string(snapshot.runtime.staConnected ? "true" : "false");
        json += ",\"bleConnected\":" + std::string(snapshot.runtime.bleConnected ? "true" : "false");
        json += ",\"bleSummary\":\"" + json_escape(ble_summary(snapshot)) + "\"";
        json += ",\"bleTargetName\":\"" + json_escape(snapshot.bleTargetName) + "\"";
        json += ",\"bleTargetAddr\":\"" + json_escape(snapshot.bleTargetAddress) + "\"";
        json += ",\"webBaseUrl\":\"" + json_escape(snapshot.webBaseUrl) + "\"";
        json += ",\"currentIp\":\"" + json_escape(current_ip_string(snapshot)) + "\"";
        json += ",\"uiScreen\":\"" + json_escape(ui_screen_name(snapshot.ui.activeScreen)) + "\"";
        json += ",\"displayBrightness\":" + std::to_string(snapshot.runtime.settings.displayBrightness);
        json += ",\"nightMode\":" + std::string(snapshot.runtime.settings.nightMode ? "true" : "false");
        json += ",\"showShiftStrip\":" + std::string(snapshot.runtime.settings.showShiftStrip ? "true" : "false");
        json += ",\"displayFocus\":\"" + json_escape(display_focus_label(snapshot.runtime.settings.displayFocus)) + "\"";
        json += ",\"vehicleModel\":\"Desktop Simulator\"";
        json += ",\"vehicleVin\":\"SIM-LOCAL-8765\"";
        json += ",\"vehicleDiag\":\"" + json_escape(snapshot.runtime.telemetryStale ? std::string("Warte auf Daten") : std::string("Simulator live")) + "\"";
        json += ",\"useMph\":" + std::string(snapshot.device.useMph ? "true" : "false");
        json += ",\"session\":{";
        json += "\"hasAnyData\":" + std::string(snapshot.runtime.session.hasAnyData ? "true" : "false");
        json += ",\"hasDelta\":" + std::string(snapshot.runtime.session.hasDelta ? "true" : "false");
        json += ",\"deltaSeconds\":" + std::to_string(snapshot.runtime.session.deltaSeconds);
        json += ",\"hasPredictedLap\":" + std::string(snapshot.runtime.session.hasPredictedLap ? "true" : "false");
        json += ",\"predictedLapMs\":" + std::to_string(snapshot.runtime.session.predictedLapMs);
        json += ",\"hasLastLap\":" + std::string(snapshot.runtime.session.hasLastLap ? "true" : "false");
        json += ",\"lastLapMs\":" + std::to_string(snapshot.runtime.session.lastLapMs);
        json += ",\"hasBestLap\":" + std::string(snapshot.runtime.session.hasBestLap ? "true" : "false");
        json += ",\"bestLapMs\":" + std::to_string(snapshot.runtime.session.bestLapMs);
        json += ",\"hasSessionClock\":" + std::string(snapshot.runtime.session.hasSessionClock ? "true" : "false");
        json += ",\"sessionClockMs\":" + std::to_string(snapshot.runtime.session.sessionClockMs);
        json += ",\"hasPosition\":" + std::string(snapshot.runtime.session.hasPosition ? "true" : "false");
        json += ",\"position\":" + std::to_string(snapshot.runtime.session.position);
        json += ",\"hasTotalPositions\":" + std::string(snapshot.runtime.session.hasTotalPositions ? "true" : "false");
        json += ",\"totalPositions\":" + std::to_string(snapshot.runtime.session.totalPositions);
        json += ",\"hasLap\":" + std::string(snapshot.runtime.session.hasLap ? "true" : "false");
        json += ",\"lap\":" + std::to_string(snapshot.runtime.session.lap);
        json += ",\"hasTotalLaps\":" + std::string(snapshot.runtime.session.hasTotalLaps ? "true" : "false");
        json += ",\"totalLaps\":" + std::to_string(snapshot.runtime.session.totalLaps);
        json += "}";
        json += ",\"ledBar\":{";
        json += "\"mode\":\"" + json_escape(simulator_led_mode_name(snapshot.ledBar.mode)) + "\"";
        json += ",\"modeLabel\":\"" + json_escape(simulator_led_mode_label(snapshot.ledBar.mode)) + "\"";
        json += ",\"autoScaleMaxRpm\":" + std::string(snapshot.ledBar.autoScaleMaxRpm ? "true" : "false");
        json += ",\"maxRpmPerGearEnabled\":" + std::string(snapshot.ledBar.maxRpmPerGearEnabled ? "true" : "false");
        json += ",\"activeGear\":" + std::to_string(snapshot.runtime.gear);
        json += ",\"fixedMaxRpm\":" + std::to_string(snapshot.ledBar.fixedMaxRpm);
        json += ",\"activeFixedMaxRpm\":" + std::to_string(active_fixed_max_rpm(snapshot));
        json += ",\"effectiveMaxRpm\":" + std::to_string(snapshot.ledBar.effectiveMaxRpm);
        json += ",\"detectedGearCount\":" + std::to_string(detected_gear_count(snapshot));
        json += ",\"learnedGearTotal\":" + std::to_string(learned_gear_total(snapshot));
        json += ",\"fixedMaxRpmByGear\":[";
        for (size_t i = 0; i < kSimulatorGearCount; ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += std::to_string(snapshot.ledBar.fixedMaxRpmByGear[i]);
        }
        json += "]";
        json += ",\"effectiveMaxRpmByGear\":[";
        for (size_t i = 0; i < kSimulatorGearCount; ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += std::to_string(snapshot.ledBar.effectiveMaxRpmByGear[i]);
        }
        json += "]";
        json += ",\"learnedMaxRpmByGear\":[";
        for (size_t i = 0; i < kSimulatorGearCount; ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += snapshot.ledBar.learnedMaxRpmByGear[i] ? "true" : "false";
        }
        json += "]";
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
        json += ",\"sideLeds\":{";
        json += "\"enabled\":" + std::string(snapshot.sideLeds.enabled ? "true" : "false");
        json += ",\"preset\":\"" + json_escape(side_led_preset_name(snapshot.sideLeds.preset)) + "\"";
        json += ",\"presetLabel\":\"" + json_escape(side_led_preset_label(snapshot.sideLeds.preset)) + "\"";
        json += ",\"ledCountPerSide\":" + std::to_string(snapshot.sideLeds.ledCountPerSide);
        json += ",\"brightness\":" + std::to_string(snapshot.sideLeds.brightness);
        json += ",\"allowSpotter\":" + std::string(snapshot.sideLeds.allowSpotter ? "true" : "false");
        json += ",\"allowFlags\":" + std::string(snapshot.sideLeds.allowFlags ? "true" : "false");
        json += ",\"allowWarnings\":" + std::string(snapshot.sideLeds.allowWarnings ? "true" : "false");
        json += ",\"allowTraction\":" + std::string(snapshot.sideLeds.allowTraction ? "true" : "false");
        json += ",\"blinkSpeedSlowMs\":" + std::to_string(snapshot.sideLeds.blinkSpeedSlowMs);
        json += ",\"blinkSpeedFastMs\":" + std::to_string(snapshot.sideLeds.blinkSpeedFastMs);
        json += ",\"blueFlagPriority\":\"" + json_escape(side_led_priority_mode_name(snapshot.sideLeds.blueFlagPriority)) + "\"";
        json += ",\"yellowFlagPriority\":\"" + json_escape(side_led_priority_mode_name(snapshot.sideLeds.yellowFlagPriority)) + "\"";
        json += ",\"warningPriorityMode\":\"" + json_escape(side_led_warning_priority_mode_name(snapshot.sideLeds.warningPriorityMode)) + "\"";
        json += ",\"invertLeftRight\":" + std::string(snapshot.sideLeds.invertLeftRight ? "true" : "false");
        json += ",\"mirrorMode\":" + std::string(snapshot.sideLeds.mirrorMode ? "true" : "false");
        json += ",\"closeCarBlinkingEnabled\":" + std::string(snapshot.sideLeds.closeCarBlinkingEnabled ? "true" : "false");
        json += ",\"severityLevelsEnabled\":" + std::string(snapshot.sideLeds.severityLevelsEnabled ? "true" : "false");
        json += ",\"idleAnimationEnabled\":" + std::string(snapshot.sideLeds.idleAnimationEnabled ? "true" : "false");
        json += ",\"testMode\":" + std::string(snapshot.sideLeds.testMode ? "true" : "false");
        json += "}";
        json += ",\"sideFrame\":{";
        json += "\"source\":\"" + json_escape(side_led_source_name(snapshot.runtime.sideLedFrame.source)) + "\"";
        json += ",\"priority\":\"" + json_escape(side_led_priority_name(snapshot.runtime.sideLedFrame.priority)) + "\"";
        json += ",\"event\":\"" + json_escape(side_led_event_name(snapshot.runtime.sideLedFrame.event)) + "\"";
        json += ",\"direction\":\"" + json_escape(side_led_traction_direction_name(snapshot.runtime.sideLedFrame.direction)) + "\"";
        json += ",\"visible\":" + std::string(snapshot.runtime.sideLedFrame.visible ? "true" : "false");
        json += ",\"blinkFast\":" + std::string(snapshot.runtime.sideLedFrame.blinkFast ? "true" : "false");
        json += ",\"blinkSlow\":" + std::string(snapshot.runtime.sideLedFrame.blinkSlow ? "true" : "false");
        json += ",\"ledCountPerSide\":" + std::to_string(snapshot.runtime.sideLedFrame.ledCountPerSide);
        json += ",\"leftLevel\":" + std::to_string(snapshot.runtime.sideLedFrame.leftLevel);
        json += ",\"rightLevel\":" + std::to_string(snapshot.runtime.sideLedFrame.rightLevel);
        json += ",\"left\":[";
        for (size_t i = 0; i < snapshot.runtime.sideLedFrame.left.size(); ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += "\"" + html_color_hex(snapshot.runtime.sideLedFrame.left[i]) + "\"";
        }
        json += "]";
        json += ",\"right\":[";
        for (size_t i = 0; i < snapshot.runtime.sideLedFrame.right.size(); ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += "\"" + html_color_hex(snapshot.runtime.sideLedFrame.right[i]) + "\"";
        }
        json += "]";
        json += "}";
        json += ",\"sideTelemetry\":{";
        json += "\"flag\":\"" + json_escape(side_led_flag_name(snapshot.runtime.sideTelemetry.flags.current)) + "\"";
        json += ",\"flags\":{\"green\":" + std::string(snapshot.runtime.sideTelemetry.flags.green ? "true" : "false");
        json += ",\"yellow\":" + std::string(snapshot.runtime.sideTelemetry.flags.yellow ? "true" : "false");
        json += ",\"blue\":" + std::string(snapshot.runtime.sideTelemetry.flags.blue ? "true" : "false");
        json += ",\"red\":" + std::string(snapshot.runtime.sideTelemetry.flags.red ? "true" : "false");
        json += ",\"white\":" + std::string(snapshot.runtime.sideTelemetry.flags.white ? "true" : "false");
        json += ",\"black\":" + std::string(snapshot.runtime.sideTelemetry.flags.black ? "true" : "false");
        json += ",\"orange\":" + std::string(snapshot.runtime.sideTelemetry.flags.orange ? "true" : "false");
        json += ",\"checkered\":" + std::string(snapshot.runtime.sideTelemetry.flags.checkered ? "true" : "false");
        json += "}";
        json += ",\"spotter\":{\"left\":" + std::string(snapshot.runtime.sideTelemetry.spotter.left ? "true" : "false");
        json += ",\"right\":" + std::string(snapshot.runtime.sideTelemetry.spotter.right ? "true" : "false");
        json += ",\"leftClose\":" + std::string(snapshot.runtime.sideTelemetry.spotter.leftClose ? "true" : "false");
        json += ",\"rightClose\":" + std::string(snapshot.runtime.sideTelemetry.spotter.rightClose ? "true" : "false");
        json += ",\"leftSeverity\":" + std::to_string(snapshot.runtime.sideTelemetry.spotter.leftSeverity);
        json += ",\"rightSeverity\":" + std::to_string(snapshot.runtime.sideTelemetry.spotter.rightSeverity);
        json += "}";
        json += ",\"warnings\":{\"pitLimiter\":" + std::string(snapshot.runtime.sideTelemetry.warnings.pitLimiter ? "true" : "false");
        json += ",\"inPitlane\":" + std::string(snapshot.runtime.sideTelemetry.warnings.inPitlane ? "true" : "false");
        json += ",\"lowFuel\":" + std::string(snapshot.runtime.sideTelemetry.warnings.lowFuel ? "true" : "false");
        json += ",\"engine\":" + std::string(snapshot.runtime.sideTelemetry.warnings.engine ? "true" : "false");
        json += ",\"oil\":" + std::string(snapshot.runtime.sideTelemetry.warnings.oil ? "true" : "false");
        json += ",\"waterTemp\":" + std::string(snapshot.runtime.sideTelemetry.warnings.waterTemp ? "true" : "false");
        json += ",\"damage\":" + std::string(snapshot.runtime.sideTelemetry.warnings.damage ? "true" : "false");
        json += "}";
        json += ",\"traction\":{\"active\":" + std::string(snapshot.runtime.sideTelemetry.traction.active ? "true" : "false");
        json += ",\"direction\":\"" + json_escape(side_led_traction_direction_name(snapshot.runtime.sideTelemetry.traction.direction)) + "\"";
        json += ",\"throttle\":" + std::to_string(snapshot.runtime.sideTelemetry.traction.throttle);
        json += ",\"brake\":" + std::to_string(snapshot.runtime.sideTelemetry.traction.brake);
        json += ",\"leftSlip\":" + std::to_string(snapshot.runtime.sideTelemetry.traction.leftSlip);
        json += ",\"rightSlip\":" + std::to_string(snapshot.runtime.sideTelemetry.traction.rightSlip);
        json += ",\"leftLevel\":" + std::to_string(snapshot.runtime.sideTelemetry.traction.leftLevel);
        json += ",\"rightLevel\":" + std::to_string(snapshot.runtime.sideTelemetry.traction.rightLevel);
        json += ",\"leftCritical\":" + std::string(snapshot.runtime.sideTelemetry.traction.leftCritical ? "true" : "false");
        json += ",\"rightCritical\":" + std::string(snapshot.runtime.sideTelemetry.traction.rightCritical ? "true" : "false");
        json += "}";
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
        json += "}";
        json += ",\"wifiScanResults\":[";
        for (size_t i = 0; i < snapshot.runtime.wifiScanResults.size(); ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += "{\"ssid\":\"" + json_escape(snapshot.runtime.wifiScanResults[i].ssid) + "\",\"rssi\":" + std::to_string(snapshot.runtime.wifiScanResults[i].rssi) + "}";
        }
        json += "]";
        json += ",\"bleScanResults\":[";
        for (size_t i = 0; i < snapshot.runtime.bleScanResults.size(); ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += "{\"name\":\"" + json_escape(snapshot.runtime.bleScanResults[i].name) + "\",\"addr\":\"" + json_escape(snapshot.runtime.bleScanResults[i].address) + "\"}";
        }
        json += "]";
        json += "}";
        return json;
    }

    std::string build_wifi_status_json(const SimulatorStatusSnapshot &snapshot)
    {
        std::string json = "{";
        json += "\"mode\":\"" + json_escape(wifi_mode_api_name(snapshot.runtime.wifiMode)) + "\"";
        json += ",\"apActive\":" + std::string(snapshot.runtime.apActive ? "true" : "false");
        json += ",\"apClients\":" + std::to_string(snapshot.runtime.apClients);
        json += ",\"apIp\":\"" + json_escape(snapshot.runtime.apIp) + "\"";
        json += ",\"staConnected\":" + std::string(snapshot.runtime.staConnected ? "true" : "false");
        json += ",\"staConnecting\":" + std::string(snapshot.runtime.staConnecting ? "true" : "false");
        json += ",\"staLastError\":\"" + json_escape(snapshot.runtime.staLastError) + "\"";
        json += ",\"currentSsid\":\"" + json_escape(snapshot.runtime.currentSsid) + "\"";
        json += ",\"staIp\":\"" + json_escape(snapshot.runtime.staIp) + "\"";
        json += ",\"ip\":\"" + json_escape(current_ip_string(snapshot)) + "\"";
        json += ",\"scanRunning\":false";
        json += ",\"scanResults\":[";
        for (size_t i = 0; i < snapshot.runtime.wifiScanResults.size(); ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += "{\"ssid\":\"" + json_escape(snapshot.runtime.wifiScanResults[i].ssid) + "\",\"rssi\":" + std::to_string(snapshot.runtime.wifiScanResults[i].rssi) + "}";
        }
        json += "]";
        json += "}";
        return json;
    }

    std::string build_ble_status_json(const SimulatorStatusSnapshot &snapshot)
    {
        std::string json = "{";
        json += "\"connected\":" + std::string(snapshot.runtime.bleConnected ? "true" : "false");
        json += ",\"targetName\":\"" + json_escape(snapshot.bleTargetName) + "\"";
        json += ",\"targetAddr\":\"" + json_escape(snapshot.bleTargetAddress) + "\"";
        json += ",\"autoReconnect\":" + std::string(snapshot.device.autoReconnect ? "true" : "false");
        json += ",\"manualActive\":false";
        json += ",\"manualFailed\":false";
        json += ",\"manualAttempts\":0";
        json += ",\"autoAttempts\":0";
        json += ",\"connectBusy\":false";
        json += ",\"connectManual\":false";
        json += ",\"lastConnectOk\":" + std::string(snapshot.runtime.bleConnected ? "true" : "false");
        json += ",\"connectInProgress\":" + std::string(snapshot.runtime.bleConnecting ? "true" : "false");
        json += ",\"connectTargetAddr\":\"" + json_escape(snapshot.bleTargetAddress) + "\"";
        json += ",\"connectTargetName\":\"" + json_escape(snapshot.bleTargetName) + "\"";
        json += ",\"connectError\":\"\"";
        json += ",\"scanRunning\":false";
        json += ",\"scanAge\":0";
        json += ",\"connectAge\":-1";
        json += ",\"source\":\"" + json_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "\"";
        json += ",\"results\":[";
        for (size_t i = 0; i < snapshot.runtime.bleScanResults.size(); ++i)
        {
            if (i > 0)
            {
                json += ",";
            }
            json += "{\"name\":\"" + json_escape(snapshot.runtime.bleScanResults[i].name) + "\",\"addr\":\"" + json_escape(snapshot.runtime.bleScanResults[i].address) + "\"}";
        }
        json += "]";
        json += "}";
        return json;
    }

    void append_shell_head(std::string &page, const char *title, bool settingsActive)
    {
        page += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
        page += "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>";
        page += "<meta name='theme-color' content='#08111b'><title>";
        page += title;
        page += "</title><style>";
        page += R"CSS(
:root{--bg:#08111b;--panel:#101a27;--panel-2:#121f2f;--text:#eef5ff;--muted:#8da2ba;--accent:#4bb7ff;--success:#40d39c;--warn:#ffb84d;--danger:#ff6f81;--border:#24364a;--border-strong:#33506d;--radius:18px;--radius-sm:12px;--savebar-space:150px}
*{box-sizing:border-box}html{scroll-behavior:smooth}
body{margin:0;min-height:100vh;font:500 15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;background:radial-gradient(circle at top right,rgba(56,118,255,.18),transparent 28%),radial-gradient(circle at top left,rgba(43,208,255,.10),transparent 24%),linear-gradient(180deg,#060c14 0%,#0a121d 100%);color:var(--text)}
a{color:inherit;text-decoration:none}button,input,select,textarea{font:inherit}
.app{width:min(1180px,100%);margin:0 auto;padding:calc(18px + env(safe-area-inset-top)) 16px calc(var(--savebar-space) + env(safe-area-inset-bottom))}
.topbar{display:flex;flex-direction:column;gap:14px;margin-bottom:18px}.topbar-row{display:flex;align-items:flex-start;justify-content:space-between;gap:14px;flex-wrap:wrap}
.brand{display:flex;flex-direction:column;gap:4px}.eyebrow{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.brand h1{margin:0;font-size:clamp(28px,8vw,42px);line-height:1;letter-spacing:-.04em}.brand p{margin:0;color:var(--muted);font-size:14px}
.tabs{display:inline-flex;gap:6px;padding:6px;border-radius:999px;background:rgba(12,21,32,.86);border:1px solid var(--border)}
.tab{min-height:42px;padding:10px 16px;border-radius:999px;color:var(--muted);font-weight:700}.tab.active{background:linear-gradient(180deg,#18293d,#142435);color:var(--text);box-shadow:inset 0 0 0 1px rgba(99,165,255,.18)}
.hero{display:grid;gap:14px;margin-bottom:18px}.hero-card{border:1px solid var(--border);background:linear-gradient(180deg,rgba(16,26,39,.95),rgba(12,20,31,.95));border-radius:22px;padding:18px}
.hero-card--accent{background:linear-gradient(180deg,rgba(20,31,48,.98),rgba(14,23,35,.98)),linear-gradient(135deg,rgba(75,183,255,.18),transparent 46%)}
.hero-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}.hero-kicker{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.hero-title{font-size:20px;font-weight:800;line-height:1.1}.hero-sub{margin-top:6px;color:var(--muted);font-size:13px}
.metric-grid{display:grid;gap:10px;grid-template-columns:repeat(2,minmax(0,1fr))}.metric{padding:12px;border-radius:16px;background:rgba(8,14,22,.56);border:1px solid rgba(255,255,255,.05)}
.metric-label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}.metric-value{margin-top:6px;font-size:clamp(22px,7vw,34px);line-height:1;font-weight:800;letter-spacing:-.04em}.metric-value--compact{font-size:24px}.metric-note{margin-top:4px;color:var(--muted);font-size:12px}
.status-list,.status-inline{display:flex;gap:8px;flex-wrap:wrap}.pill{display:inline-flex;align-items:center;gap:8px;min-height:34px;padding:8px 12px;border-radius:999px;border:1px solid var(--border);background:rgba(8,14,22,.55);color:var(--text);font-size:12px;font-weight:700}
.pill::before{content:"";width:8px;height:8px;border-radius:50%;background:#64768b}.pill.ok::before{background:var(--success)}.pill.warn::before{background:var(--warn)}.pill.bad::before{background:var(--danger)}.pill.neutral::before{background:var(--accent)}
.app-grid,.panel-grid,.field-grid,.stack,.info-list,.dashboard-layout,.dashboard-col{display:grid;gap:16px}.dashboard-col{align-content:start}.panel{border:1px solid var(--border);background:linear-gradient(180deg,rgba(16,26,39,.95),rgba(12,20,31,.95));border-radius:var(--radius);padding:18px}
.panel-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}.panel-title{margin:0;font-size:18px;line-height:1.1;letter-spacing:-.02em}.panel-copy{margin-top:4px;color:var(--muted);font-size:13px}
.field-grid.two{grid-template-columns:repeat(1,minmax(0,1fr))}.field label,.field-label{display:block;margin-bottom:8px;font-size:12px;font-weight:700;color:var(--muted);letter-spacing:.06em;text-transform:uppercase}
.field input[type=text],.field input[type=number],.field input[type=password],.field select,.field textarea{width:100%;min-height:48px;padding:12px 14px;border-radius:14px;border:1px solid var(--border);background:#0b121b;color:var(--text);outline:none}
.field input:focus,.field select:focus,.field textarea:focus{border-color:rgba(75,183,255,.72);box-shadow:0 0 0 1px rgba(75,183,255,.2)}.field-note,.seg-note{margin-top:8px;color:var(--muted);font-size:12px}
.field-inline,.range-wrap,.color-card,.info-row,.badge-card,.device-item{padding:14px;border-radius:16px;border:1px solid var(--border);background:#0b121b}
.field-inline,.info-row,.device-item-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.field-inline strong,.device-name{display:block;font-weight:800}.field-inline span,.info-label,.device-meta,.badge-card span{display:block;color:var(--muted);font-size:13px}
.switch{position:relative;width:56px;height:32px;display:inline-block;flex:0 0 auto}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;inset:0;border-radius:999px;background:#223243;border:1px solid rgba(255,255,255,.06);transition:.18s ease}.slider::before{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:#f5f8fb;transition:.18s ease}.switch input:checked + .slider{background:linear-gradient(180deg,#329bff,#2bd0ff)}.switch input:checked + .slider::before{transform:translateX(24px)}
.range-wrap{display:grid;gap:10px}.range-head{display:flex;align-items:baseline;justify-content:space-between;gap:12px}.range-title{font-weight:700}.range-value{font-size:12px;color:var(--muted)}input[type=range]{width:100%;margin:0;accent-color:var(--accent);background:transparent}
.color-grid{display:grid;gap:12px;grid-template-columns:repeat(1,minmax(0,1fr))}.color-card input[type=color]{width:100%;min-height:52px;border:none;padding:0;border-radius:12px;background:transparent}.color-card input[type=color]::-webkit-color-swatch-wrapper{padding:0}.color-card input[type=color]::-webkit-color-swatch{border:none;border-radius:12px}.color-card input[type=color]::-moz-color-swatch{border:none;border-radius:12px}
.led-preview{display:grid;grid-template-columns:repeat(15,minmax(0,1fr));gap:6px;padding:14px;border-radius:16px;border:1px solid var(--border);background:#0b121b}.led-dot{width:100%;aspect-ratio:1/1;border-radius:999px;background:#26384d}.side-preview{display:grid;grid-template-columns:1fr auto 1fr;gap:14px;align-items:center;padding:14px;border-radius:16px;border:1px solid var(--border);background:#0b121b}.side-stack{display:flex;flex-direction:column;gap:10px;align-items:flex-start}.side-stack.right{align-items:flex-end}.side-led-dot{width:16px;height:28px;border-radius:999px;background:#26384d;border:1px solid rgba(255,255,255,.08)}.side-meta{text-align:center;color:var(--muted);font-size:12px}.button-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.button-row{display:flex;flex-wrap:wrap;gap:10px}.btn{appearance:none;border:none;min-height:48px;padding:12px 16px;border-radius:14px;font-weight:800;letter-spacing:-.01em;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;gap:8px}
.btn:disabled{opacity:.55;cursor:not-allowed}.btn-primary{background:linear-gradient(180deg,#46b0ff,#2bd0ff);color:#06111c}.btn-secondary{background:#1b2938;color:var(--text);border:1px solid var(--border)}.btn-danger{background:#26141a;color:#ffd7de;border:1px solid rgba(255,111,129,.28)}.btn-ghost{background:transparent;color:var(--text);border:1px solid var(--border)}
.badge-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.badge-card strong{display:block;margin-top:6px;font-size:16px}.info-value{text-align:right;font-weight:700;word-break:break-word}.device-list{display:grid;gap:10px;margin-top:12px}.device-empty{margin-top:12px;padding:14px;border-radius:14px;border:1px dashed var(--border);color:var(--muted);text-align:center;font-size:13px}
.callout,.toast{padding:14px;border-radius:16px;font-size:13px}.callout{border:1px solid rgba(75,183,255,.28);background:rgba(37,74,110,.14);color:#bfd9ff}.toast{margin-top:12px;border:1px solid rgba(64,211,156,.28);background:rgba(16,59,48,.36);color:#d9fff0}
.savebar{position:fixed;left:12px;right:12px;bottom:calc(12px + env(safe-area-inset-bottom));z-index:40;display:flex;flex-direction:column;gap:12px;padding:14px;border-radius:18px;border:1px solid var(--border-strong);background:rgba(7,12,18,.96)}
.savebar-title{font-size:14px;font-weight:800}.savebar-copy{margin-top:4px;color:var(--muted);font-size:12px}.savebar-actions{display:grid;grid-template-columns:1fr 1fr;gap:10px}.savebar-title span{display:none}.savebar[data-dirty='0'] .savebar-title::before{content:"Gespeichert"}.savebar[data-dirty='1'] .savebar-title::before{content:"Ungespeicherte Aenderungen"}
.details summary{list-style:none;cursor:pointer;display:flex;align-items:center;justify-content:space-between;gap:12px}.details summary::-webkit-details-marker{display:none}.details summary::after{content:"+";font-size:18px;color:var(--muted)}.details[open] summary::after{content:"-"}
.console{min-height:180px;max-height:280px;overflow:auto;padding:12px;border-radius:16px;border:1px solid var(--border);background:#071019;font:12px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}.console-line{margin:0 0 4px}.console-line.tx{color:#8fd0ff}.console-line.rx{color:#aef5bf}.console-line.err{color:#ff9cab}
.spinner{display:inline-block;width:14px;height:14px;border-radius:50%;border:2px solid rgba(255,255,255,.18);border-top-color:currentColor;animation:spin 1s linear infinite}@keyframes spin{from{transform:rotate(0deg)}to{transform:rotate(360deg)}}
.hidden{display:none!important}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
@media(min-width:720px){.hero{grid-template-columns:1.3fr .9fr}.panel-grid.two,.field-grid.two{grid-template-columns:repeat(2,minmax(0,1fr))}.color-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.info-list.compact{grid-template-columns:repeat(2,minmax(0,1fr))}.savebar{left:50%;right:auto;width:min(820px,calc(100% - 24px));transform:translateX(-50%);flex-direction:row;align-items:center;justify-content:space-between}.savebar-actions{width:min(340px,100%);flex:0 0 auto}}
@media(min-width:980px){.app-grid{grid-template-columns:1.05fr .95fr;align-items:start}.dashboard-layout{grid-template-columns:1.05fr .95fr;align-items:start}.hero-card{padding:22px}}
)CSS";
        page += "</style></head><body><div class='app'><header class='topbar'><div class='topbar-row'><div class='brand'><span class='eyebrow'>RPMCounter / ShiftLight</span><h1>ShiftLight</h1><p>Leichtgewichtige Embedded-Weboberflaeche mit Live-Status, Telemetrie und Konfiguration.</p></div><nav class='tabs'>";
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

    void append_dashboard_script(std::string &page)
    {
        page += "<script>";
        page += "const NUM_LEDS=60;";
        page += R"JS(
const $ = (selector, scope=document) => scope.querySelector(selector);
const $$ = (selector, scope=document) => Array.from(scope.querySelectorAll(selector));
const dashboardState = { dirty:false, initial:null, previewTimer:null, blinkPreview:false, blinkPreviewUntil:0, activeGear:0, applyTimer:null, applyInFlight:false, applyQueued:false };
function serializeForm(form){ const out = {}; $$('input,select,textarea', form).forEach((el) => { if(!el.name){ return; } out[el.name] = el.type === 'checkbox' ? (el.checked ? 'on' : '') : el.value; }); return out; }
function captureInitialState(){ const form = $('#mainForm'); if(form){ dashboardState.initial = serializeForm(form); } }
function syncSavebarSpace(){ const bar = $('#saveBar'); if(!bar){ return; } const space = Math.ceil(bar.getBoundingClientRect().height + 28); document.documentElement.style.setProperty('--savebar-space', space + 'px'); }
function recomputeDirty(){ const form = $('#mainForm'); if(!form){ return; } if(!dashboardState.initial){ captureInitialState(); } const current = serializeForm(form); let dirty = false; Object.keys(dashboardState.initial || {}).forEach((key) => { if((dashboardState.initial || {})[key] !== current[key]){ dirty = true; } }); Object.keys(current).forEach((key) => { if((dashboardState.initial || {})[key] !== current[key]){ dirty = true; } }); dashboardState.dirty = dirty; const bar = $('#saveBar'); if(bar){ bar.dataset.dirty = dirty ? '1' : '0'; $('.savebar-copy', bar).textContent = dirty ? 'Bereit zum Speichern. Live-Werte laufen weiter.' : 'Konfiguration ist synchron.'; } $('#btnSave') && ($('#btnSave').disabled = !dirty); $('#btnReset') && ($('#btnReset').disabled = !dirty); }
function markDirty(){ recomputeDirty(); }
function updateText(id, value){ const el = document.getElementById(id); if(el && value !== undefined && value !== null){ el.textContent = String(value); } }
function setPill(id, tone, text){ const el = document.getElementById(id); if(!el){ return; } el.classList.remove('ok','warn','bad','neutral'); el.classList.add(tone || 'neutral'); el.textContent = text; }
function updateSliderValue(el){ const target = document.getElementById(el.dataset.valueTarget); if(target){ target.textContent = el.value + (el.dataset.suffix || ''); } }
function currentMode(){ const value = $('#modeSelect')?.value || 'f1'; if(value === 'casual'){ return 0; } if(value === 'aggressive'){ return 2; } if(value === 'gt3'){ return 3; } return 1; }
function currentGearMaxInput(){ const gear = dashboardState.activeGear; if(gear >= 1 && gear <= 8){ return $('#fixedMaxRpmGear' + gear); } return null; }
function currentPreviewMaxRpm(){ const perGear = !!$('#perGearToggle')?.checked; const autoscale = !!$('#autoscaleToggle')?.checked; const activeInput = currentGearMaxInput(); const detectedValue = parseInt(document.getElementById('autoDetectedMaxRpmGear' + dashboardState.activeGear)?.value || '0', 10); if(perGear && autoscale && detectedValue > 0){ return Math.max(detectedValue, 2000); } if(perGear && activeInput){ return Math.max(parseInt(activeInput.value || '7000', 10), 2000); } return Math.max(parseInt($('#fixedMaxRpmInput')?.value || '7000', 10), 2000); }
function renderAutoGearCards(data){ const learned = data?.ledBar?.learnedMaxRpmByGear || []; const effective = data?.ledBar?.effectiveMaxRpmByGear || []; let visible = 0; for(let i = 0; i < 8; i += 1){ const card = $('#autoGearCard' + (i + 1)); const input = $('#autoDetectedMaxRpmGear' + (i + 1)); const note = $('#autoDetectedNoteGear' + (i + 1)); const isLearned = !!learned[i]; if(card){ card.style.display = isLearned ? '' : 'none'; } if(input && effective[i] !== undefined){ input.value = String(effective[i]); } if(note){ note.textContent = isLearned ? ('Auto erkannt: ' + effective[i] + ' rpm') : 'Noch nicht erkannt.'; } if(isLearned){ visible += 1; } } $('#gearAutoEmpty')?.classList.toggle('hidden', visible > 0); updateText('detectedGearCountValue', data?.ledBar?.detectedGearCount ?? 0); updateText('learnedGearTotalValue', (data?.ledBar?.learnedGearTotal ?? 0) + ' / 8'); }
function syncAutoscaleUi(){ const autoscale = !!$('#autoscaleToggle')?.checked; const perGear = !!$('#perGearToggle')?.checked; const autoPerGear = autoscale && perGear; $('#fixedMaxWrap')?.classList.toggle('hidden', autoscale || perGear); $('#gearMaxWrap')?.classList.toggle('hidden', !perGear || autoscale); $('#gearAutoWrap')?.classList.toggle('hidden', !autoPerGear); const activeGear = dashboardState.activeGear; updateText('gearMaxHint', (activeGear >= 1 && activeGear <= 8) ? ('Aktiver Gang: ' + activeGear) : 'Aktiver Gang: Fallback / Neutral'); updateText('gearAutoHint', (activeGear >= 1 && activeGear <= 8) ? ('Aktiver Gang: ' + activeGear + ' | Auto-Max folgt nur bereits gelernten Gangwerten.') : 'Aktiver Gang: Fallback / Neutral | Auto-Max nutzt nur erkannte Fahrgaenge.'); }
function syncAmbientUi(){ const max = parseInt($('#brightnessSlider')?.value || '255', 10); const min = $('#autoBrightnessMinSlider'); if(min){ min.max = String(max); if(parseInt(min.value || '0', 10) > max){ min.value = String(max); updateSliderValue(min); } } }
function enforceOrder(changedId){ const green = $('#greenEndSlider'); const yellow = $('#yellowEndSlider'); const red = $('#redEndSlider'); const blink = $('#blinkStartSlider'); let g = parseInt(green?.value || '0', 10); let y = parseInt(yellow?.value || '0', 10); let r = parseInt(red?.value || '0', 10); let b = parseInt(blink?.value || '0', 10); if(changedId === 'greenEndSlider' && y < g){ y = g; } if((changedId === 'greenEndSlider' || changedId === 'yellowEndSlider') && r < y){ r = y; } if(changedId === 'redEndSlider' && r < y){ r = y; } if(changedId === 'yellowEndSlider' && y < g){ y = g; } b = Math.max(0, Math.min(100, b)); if(green){ green.value = String(g); } if(yellow){ yellow.value = String(y); } if(red){ red.value = String(r); } if(blink){ blink.value = String(b); } [green,yellow,red,blink].forEach((el) => { if(el){ updateSliderValue(el); } }); }
function currentLedCount(){ const input = $('#activeLedCountSlider'); const raw = parseInt(input?.value || String(NUM_LEDS), 10); if(Number.isNaN(raw)){ return NUM_LEDS; } return Math.max(1, Math.min(NUM_LEDS, raw)); }
function syncLedPreviewCount(){ const host = $('#ledPreview'); if(!host){ return []; } const count = currentLedCount(); let dots = $$('.led-dot', host); if(dots.length !== count){ host.innerHTML = ''; for(let i = 0; i < count; i += 1){ const dot = document.createElement('span'); dot.className = 'led-dot'; host.appendChild(dot); } dots = $$('.led-dot', host); } updateText('activeLedCountValue', count); return dots; }
function blinkPreviewState(){ const speed = parseInt($('#blinkSpeedSlider')?.value || '80', 10); if(speed <= 0){ return false; } if(speed >= 100){ return true; } const intervalMs = Math.round(480 - ((speed / 99) * 440)); return Math.floor(Date.now() / Math.max(40, intervalMs)) % 2 === 0; }
function renderLedPreview(fraction, blinking){ const dots = syncLedPreviewCount(); if(!dots.length){ return; } const mode = currentMode(); const greenEnd = parseInt($('#greenEndSlider')?.value || '0', 10) / 100; const yellowEnd = parseInt($('#yellowEndSlider')?.value || '0', 10) / 100; const redEnd = parseInt($('#redEndSlider')?.value || '0', 10) / 100; const blinkStart = parseInt($('#blinkStartSlider')?.value || '0', 10) / 100; const startRpm = parseInt($('#rpmStartSlider')?.value || '0', 10); const previewMaxRpm = currentPreviewMaxRpm(); const startFraction = Math.min(0.95, Math.max(0, startRpm / Math.max(1, previewMaxRpm))); const safeRedEnd = Math.max(0.01, redEnd); const blinkTrigger = Math.max(0, Math.min(1, blinkStart)); const fillEnd = Math.max(0.001, blinkTrigger * safeRedEnd); const zoneGreen = Math.max(0, Math.min(1, greenEnd / safeRedEnd)); const zoneYellow = Math.max(zoneGreen, Math.min(1, yellowEnd / safeRedEnd)); const colors = { green: $('#greenColorInput')?.value || '#2DFF7A', yellow: $('#yellowColorInput')?.value || '#FFC34D', red: $('#redColorInput')?.value || '#FF5A72' }; const dark = '#26384d'; const rawFrac = Math.max(0, Math.min(1, fraction)); const frac = rawFrac <= startFraction ? 0 : Math.max(0, Math.min(1, (rawFrac - startFraction) / Math.max(0.01, 1 - startFraction))); const displayFrac = frac <= 0 ? 0 : Math.max(0, Math.min(1, frac / fillEnd)); const blinkOn = blinkPreviewState(); if(mode === 3){ const pairCount = Math.ceil(dots.length / 2); const pairsOn = Math.round(displayFrac * pairCount); const finalBlink = blinking && frac >= blinkTrigger; dots.forEach((dot, index) => { let color = dark; if(finalBlink){ color = blinkOn ? colors.red : '#091019'; } else { const rank = Math.min(index, dots.length - 1 - index); if(rank < pairsOn){ const pos = pairCount <= 1 ? 1 : rank / (pairCount - 1); color = pos < zoneGreen ? colors.green : (pos < zoneYellow ? colors.yellow : colors.red); } } dot.style.backgroundColor = color; }); return; } const onCount = Math.round(displayFrac * dots.length); dots.forEach((dot, index) => { let color = dark; if(index < onCount){ const pos = dots.length <= 1 ? 1 : index / (dots.length - 1); if((mode === 1 || mode === 2) && blinking && frac >= blinkTrigger){ color = blinkOn ? colors.red : '#091019'; } else if(pos < zoneGreen){ color = colors.green; } else if(pos < zoneYellow){ color = colors.yellow; } else { color = colors.red; } } dot.style.backgroundColor = color; }); }
function updatePreviewLoop(){ if(dashboardState.blinkPreview){ renderLedPreview(1, true); if(Date.now() >= dashboardState.blinkPreviewUntil){ dashboardState.blinkPreview = false; } return; } renderLedPreview(1, false); }
function ensurePreviewLoop(){ if(!dashboardState.previewTimer){ dashboardState.previewTimer = setInterval(updatePreviewLoop, 80); } }
function triggerBlinkPreview(){ dashboardState.blinkPreview = true; dashboardState.blinkPreviewUntil = Date.now() + 1600; ensurePreviewLoop(); }
function renderSidePreview(frame){ const left = frame?.left || []; const right = frame?.right || []; const ledCount = Math.max(4, Math.min(16, parseInt(frame?.ledCountPerSide ?? $('#sideLedCountPerSide')?.value ?? '8', 10) || 8)); for(let i = 0; i < 16; i += 1){ const leftEl = document.getElementById('sideLeftLed' + (i + 1)); const rightEl = document.getElementById('sideRightLed' + (i + 1)); const active = i < ledCount; const leftColor = left[i] || '#26384d'; const rightColor = right[i] || '#26384d'; if(leftEl){ leftEl.style.display = active ? '' : 'none'; leftEl.style.backgroundColor = leftColor; leftEl.style.opacity = (leftColor === '#000000' || leftColor === '#26384d') ? '0.45' : '1'; } if(rightEl){ rightEl.style.display = active ? '' : 'none'; rightEl.style.backgroundColor = rightColor; rightEl.style.opacity = (rightColor === '#000000' || rightColor === '#26384d') ? '0.45' : '1'; } } }
function setButtonLoading(id, active, label){ const btn = document.getElementById(id); if(!btn){ return; } btn.classList.toggle('loading', !!active); btn.disabled = !!active; if(active){ btn.dataset.label = btn.dataset.label || btn.textContent; btn.innerHTML = '<span class="spinner"></span><span>' + (label || btn.dataset.label) + '</span>'; } else { btn.textContent = btn.dataset.label || btn.textContent; } }
function showDashboardToast(){ $('#dashboardToast')?.classList.remove('hidden'); setTimeout(() => $('#dashboardToast')?.classList.add('hidden'), 1800); }
function applyForm(showToast){ const form = $('#mainForm'); if(!form){ return Promise.resolve(); } if(dashboardState.applyInFlight){ dashboardState.applyQueued = true; return Promise.resolve(); } dashboardState.applyInFlight = true; const params = new URLSearchParams(new FormData(form)); setButtonLoading('btnSave', true, showToast ? 'Speichere' : 'Synchronisiere'); return fetch('/api/config', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:params.toString() }).then(() => { captureInitialState(); recomputeDirty(); if(showToast){ showDashboardToast(); } return fetchStatus(); }).finally(() => { dashboardState.applyInFlight = false; setButtonLoading('btnSave', false); if(dashboardState.applyQueued){ dashboardState.applyQueued = false; scheduleFormApply(true); } }); }
function scheduleFormApply(immediate){ if(dashboardState.applyTimer){ clearTimeout(dashboardState.applyTimer); } dashboardState.applyTimer = setTimeout(() => { dashboardState.applyTimer = null; applyForm(false); }, immediate ? 70 : 220); }
function saveForm(){ return applyForm(true); }
function sendCommand(action){ return fetch('/command', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'action=' + encodeURIComponent(action) }).then(() => setTimeout(fetchStatus, 80)); }
function updateStatus(data){ if(!data){ return; } dashboardState.activeGear = parseInt(data.gear ?? 0, 10) || 0; updateText('heroRpm', data.rpm ?? 0); updateText('heroMaxRpm', data.ledBar?.effectiveMaxRpm ?? 0); updateText('heroSpeed', (data.speedKmh ?? 0) + (data.useMph ? ' mph' : ' km/h')); updateText('heroGear', data.gear ?? 0); updateText('gearMaxHint', dashboardState.activeGear >= 1 && dashboardState.activeGear <= 8 ? ('Aktiver Gang: ' + dashboardState.activeGear + ' | LED-Max: ' + (data.ledBar?.effectiveMaxRpm ?? 0) + ' rpm') : ('Aktiver Gang: Fallback / Neutral | LED-Max: ' + (data.ledBar?.effectiveMaxRpm ?? 0) + ' rpm')); setPill('telemetryPill', data.telemetryFallback ? 'warn' : 'neutral', data.activeTelemetry || 'Keine'); setPill('blePill', data.bleConnected ? 'ok' : 'bad', data.bleSummary || 'Nicht verbunden'); setPill('simHubPill', data.telemetryStale ? 'warn' : 'ok', data.simHubState || 'Deaktiviert'); updateText('badgeTelemetry', data.activeTelemetry || 'Keine'); updateText('badgeTransport', data.telemetryTransportLabel || '-'); updateText('badgeBle', data.bleSummary || 'Nicht verbunden'); updateText('badgeWifi', data.wifiSummary || 'Offline'); updateText('vehicleTransportValue', data.telemetryTransportLabel || '-'); updateText('vehicleScreenValue', data.uiScreen || 'home'); updateText('vehicleDisplayFocusValue', data.displayFocus || 'RPM'); updateText('vehicleBleTargetValue', data.bleTargetName || '(unbekannt)'); updateText('vehicleWebValue', data.currentIp || '-'); updateText('liveLedInfo', 'Aktive LEDs: ' + (data.ledBar?.count ?? 0) + ' | Brightness: ' + (data.ledBar?.brightness ?? 0) + ' | Leuchtet: ' + (data.ledBar?.litCount ?? 0)); updateText('desktopStatusValue', (data.displayBrightness ?? 220) + ' / ' + (data.nightMode ? 'Night' : 'Day')); updateText('desktopWifiValue', data.wifiSummary || 'Offline'); updateText('desktopBleValue', data.bleSummary || 'Nicht verbunden'); updateText('desktopUiValue', data.uiScreen || 'home'); updateText('sideSourceValue', data.sideFrame?.source || 'off'); updateText('sideEventValue', data.sideFrame?.event || 'none'); updateText('sideTelemetryValue', 'Grip Load ' + Math.round((data.sideTelemetry?.traction?.throttle ?? 0) * 100) + '% | Brake Load ' + Math.round((data.sideTelemetry?.traction?.brake ?? 0) * 100) + '% | Grip L ' + (data.sideTelemetry?.traction?.leftLevel ?? 0) + ' / R ' + (data.sideTelemetry?.traction?.rightLevel ?? 0)); updateText('sideStatusNote', 'Richtung: ' + (data.sideFrame?.direction || 'off') + ' | LEDs / Seite: ' + (data.sideFrame?.ledCountPerSide ?? data.sideLeds?.ledCountPerSide ?? 0)); renderSidePreview(data.sideFrame); renderAutoGearCards(data); syncAutoscaleUi(); }
function initDashboard(){ captureInitialState(); recomputeDirty(); syncAutoscaleUi(); syncAmbientUi(); syncLedPreviewCount(); ensurePreviewLoop(); updatePreviewLoop(); renderSidePreview(); $$('input,select,textarea', $('#mainForm')).forEach((el) => { const handler = () => { if(el.type === 'range'){ updateSliderValue(el); if(['greenEndSlider','yellowEndSlider','redEndSlider','blinkStartSlider'].includes(el.id)){ enforceOrder(el.id); triggerBlinkPreview(); } if(el.id === 'brightnessSlider' || el.id === 'autoBrightnessMinSlider'){ syncAmbientUi(); } if(el.id === 'activeLedCountSlider'){ syncLedPreviewCount(); } if(el.id === 'sideLedCountPerSide'){ renderSidePreview({ ledCountPerSide: el.value }); } } if(el.id === 'autoscaleToggle' || el.id === 'perGearToggle' || (el.id || '').startsWith('fixedMaxRpmGear')){ syncAutoscaleUi(); triggerBlinkPreview(); } markDirty(); scheduleFormApply(el.type === 'checkbox' || el.tagName === 'SELECT'); }; el.addEventListener('input', handler); el.addEventListener('change', handler); }); $('#btnSave')?.addEventListener('click', () => saveForm()); $('#btnReset')?.addEventListener('click', () => window.location.reload()); $('#btnRpmDown')?.addEventListener('click', () => sendCommand('rpm_down')); $('#btnRpmUp')?.addEventListener('click', () => sendCommand('rpm_up')); $('#btnShift')?.addEventListener('click', () => sendCommand('toggle_shift')); $('#btnAnimation')?.addEventListener('click', () => sendCommand('toggle_anim')); $('#btnResetSim')?.addEventListener('click', () => sendCommand('reset')); $('#btnUiPrev')?.addEventListener('click', () => sendCommand('ui_prev')); $('#btnUiNext')?.addEventListener('click', () => sendCommand('ui_next')); $('#btnUiOpen')?.addEventListener('click', () => sendCommand('ui_open')); $('#btnUiHome')?.addEventListener('click', () => sendCommand('ui_home')); $('#btnUiLogo')?.addEventListener('click', () => sendCommand('ui_logo')); $('#btnWifiCycle')?.addEventListener('click', () => sendCommand('toggle_wifi')); $('#btnBleCycle')?.addEventListener('click', () => sendCommand('toggle_ble')); $('#btnSideAccel')?.addEventListener('click', () => sendCommand('side_test_accel')); $('#btnSideBrake')?.addEventListener('click', () => sendCommand('side_test_brake')); $('#btnSideTractionLeft')?.addEventListener('click', () => sendCommand('side_test_traction_left')); $('#btnSideTractionRight')?.addEventListener('click', () => sendCommand('side_test_traction_right')); $('#btnSideTractionBoth')?.addEventListener('click', () => sendCommand('side_test_traction_both')); $('#btnSideClear')?.addEventListener('click', () => sendCommand('side_test_clear')); window.addEventListener('resize', syncSavebarSpace); syncSavebarSpace(); fetchStatus(); setInterval(fetchStatus, 1500); }
document.addEventListener('DOMContentLoaded', initDashboard);
)JS";
        page += "</script>";
    }

    void append_settings_script(std::string &page)
    {
        page += "<script>";
        page += R"JS(
const $ = (selector, scope=document) => scope.querySelector(selector);
const $$ = (selector, scope=document) => Array.from(scope.querySelectorAll(selector));
const settingsState = { dirty:false, initial:null, wifiModalSsid:'', applyTimer:null, applyInFlight:false, applyQueued:false };
function serializeSettings(form){ const out = {}; $$('input,select,textarea', form).forEach((el) => { if(!el.name){ return; } out[el.name] = el.type === 'checkbox' ? (el.checked ? 'on' : '') : el.value; }); return out; }
function captureInitialSettingsState(){ const form = $('#settingsForm'); if(form){ settingsState.initial = serializeSettings(form); } }
function syncSavebarSpace(){ const bar = $('#settingsSaveBar'); if(!bar){ return; } const space = Math.ceil(bar.getBoundingClientRect().height + 28); document.documentElement.style.setProperty('--savebar-space', space + 'px'); }
function recomputeSettingsDirty(){ const form = $('#settingsForm'); if(!form){ return; } if(!settingsState.initial){ captureInitialSettingsState(); } const current = serializeSettings(form); let dirty = false; Object.keys(settingsState.initial || {}).forEach((key) => { if((settingsState.initial || {})[key] !== current[key]){ dirty = true; } }); Object.keys(current).forEach((key) => { if((settingsState.initial || {})[key] !== current[key]){ dirty = true; } }); settingsState.dirty = dirty; const bar = $('#settingsSaveBar'); if(bar){ bar.dataset.dirty = dirty ? '1' : '0'; $('.savebar-copy', bar).textContent = dirty ? 'Netzwerk- und Telemetrie-Einstellungen sind geaendert.' : 'Alles ist synchron.'; } $('#settingsSave') && ($('#settingsSave').disabled = !dirty); $('#settingsReset') && ($('#settingsReset').disabled = !dirty); }
function markDirty(){ recomputeSettingsDirty(); }
function updateText(id, value){ const el = document.getElementById(id); if(el && value !== undefined && value !== null){ el.textContent = String(value); } }
function setPill(id, tone, text){ const el = document.getElementById(id); if(!el){ return; } el.classList.remove('ok','warn','bad','neutral'); el.classList.add(tone || 'neutral'); el.textContent = text; }
function setButtonLoading(id, active, label){ const btn = document.getElementById(id); if(!btn){ return; } btn.classList.toggle('loading', !!active); btn.disabled = !!active; if(active){ btn.dataset.label = btn.dataset.label || btn.textContent; btn.innerHTML = '<span class="spinner"></span><span>' + (label || btn.dataset.label) + '</span>'; } else { btn.textContent = btn.dataset.label || btn.textContent; } }
function saveSettings(showToast=true){ const form = $('#settingsForm'); if(!form){ return Promise.resolve(); } if(settingsState.applyInFlight){ settingsState.applyQueued = true; return Promise.resolve(); } settingsState.applyInFlight = true; const params = new URLSearchParams(new FormData(form)); setButtonLoading('settingsSave', true, showToast ? 'Speichere' : 'Synchronisiere'); return fetch('/api/config', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:params.toString() }).then(() => { captureInitialSettingsState(); recomputeSettingsDirty(); if(showToast){ $('#savedToast')?.classList.remove('hidden'); setTimeout(() => $('#savedToast')?.classList.add('hidden'), 1800); } return Promise.all([fetchStatus(), fetchWifiStatus(), fetchBleStatus()]); }).finally(() => { settingsState.applyInFlight = false; setButtonLoading('settingsSave', false); if(settingsState.applyQueued){ settingsState.applyQueued = false; scheduleSettingsApply(true); } }); }
function scheduleSettingsApply(immediate){ if(settingsState.applyTimer){ clearTimeout(settingsState.applyTimer); } settingsState.applyTimer = setTimeout(() => { settingsState.applyTimer = null; saveSettings(false); }, immediate ? 90 : 320); }
function openWifiModal(ssid){ settingsState.wifiModalSsid = ssid || $('#staSsid')?.value || ''; updateText('wifiModalSsid', settingsState.wifiModalSsid || 'Netzwerk'); $('#wifiModalPassword').value = ''; $('#wifiPasswordModal')?.classList.remove('hidden'); $('#wifiModalPassword')?.focus(); }
function closeWifiModal(){ $('#wifiPasswordModal')?.classList.add('hidden'); }
function submitWifiModal(){ const ssid = settingsState.wifiModalSsid || $('#staSsid')?.value || ''; const password = $('#wifiModalPassword')?.value || ''; if(!ssid){ return; } $('#staSsid') && ($('#staSsid').value = ssid); $('#staPassword') && ($('#staPassword').value = password); markDirty(); setButtonLoading('wifiScanBtn', true, 'Verbinde'); fetch('/wifi/connect', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password) }).then(() => { closeWifiModal(); return Promise.all([fetchStatus(), fetchWifiStatus()]); }).finally(() => setButtonLoading('wifiScanBtn', false)); }
function renderWifiResults(data){ const wrapper = $('#wifiResults'); const list = $('#wifiResultsList'); const empty = $('#wifiScanEmpty'); if(!wrapper || !list || !empty){ return; } const results = data?.scanResults || []; list.innerHTML = ''; results.forEach((item) => { const button = document.createElement('button'); button.type = 'button'; button.className = 'device-item'; button.innerHTML = '<div class="device-item-head"><div><div class="device-name">' + (item.ssid || '(versteckt)') + '</div><div class="device-meta">' + item.rssi + ' dBm</div></div><span class="pill neutral">Verbinden</span></div>'; button.addEventListener('click', () => openWifiModal(item.ssid || '')); list.appendChild(button); }); wrapper.classList.toggle('hidden', results.length === 0); empty.classList.toggle('hidden', results.length !== 0); }
function updateWifiUi(data){ if(!data){ return; } const summary = data.staConnected ? (data.currentSsid || 'Verbunden') : (data.staConnecting ? 'Verbindung laeuft' : (data.apActive ? 'AP aktiv' : 'Offline')); updateText('wifiSummaryValue', summary); updateText('wifiModeValue', data.mode === 'STA_ONLY' ? 'Nur Heim-WLAN' : (data.mode === 'STA_WITH_AP_FALLBACK' ? 'WLAN + AP Fallback' : 'Nur Access Point')); updateText('wifiIpValue', data.ip || data.apIp || '-'); updateText('wifiScanStatus', data.staConnecting ? 'Verbindung wird aufgebaut' : (data.staLastError || 'Bereit')); setPill('settingsWifiPill', data.staConnected ? 'ok' : (data.apActive ? 'warn' : 'bad'), data.staConnected ? 'WLAN online' : (data.apActive ? 'AP aktiv' : 'WLAN offline')); renderWifiResults(data); }
function fetchWifiStatus(){ return fetch('/wifi/status').then((r) => r.json()).then(updateWifiUi).catch(() => {}); }
function startWifiScan(){ setButtonLoading('wifiScanBtn', true, 'Suche'); fetch('/wifi/scan', { method:'POST' }).then(fetchWifiStatus).finally(() => setButtonLoading('wifiScanBtn', false)); }
function disconnectWifi(){ fetch('/wifi/disconnect', { method:'POST' }).then(() => Promise.all([fetchStatus(), fetchWifiStatus()])); }
function renderBleResults(data){ const wrapper = $('#bleResults'); const list = $('#bleResultsList'); const empty = $('#bleScanEmpty'); if(!wrapper || !list || !empty){ return; } const results = data?.results || []; list.innerHTML = ''; results.forEach((item) => { const button = document.createElement('button'); button.type = 'button'; button.className = 'device-item'; button.innerHTML = '<div class="device-item-head"><div><div class="device-name">' + (item.name || '(unbekannt)') + '</div><div class="device-meta">' + (item.addr || '') + '</div></div><span class="pill neutral">Koppeln</span></div>'; button.addEventListener('click', () => requestBleConnect(item.addr || '', item.name || '')); list.appendChild(button); }); wrapper.classList.toggle('hidden', results.length === 0); empty.classList.toggle('hidden', results.length !== 0); }
function updateBleUi(data){ if(!data){ return; } setPill('settingsBlePill', data.connected ? 'ok' : (data.connectInProgress ? 'warn' : 'bad'), data.connected ? 'BLE / OBD online' : (data.connectInProgress ? 'BLE verbindet' : 'BLE offline')); updateText('bleTargetName', data.targetName || '(unbekannt)'); updateText('bleTargetAddr', data.targetAddr || '-'); updateText('bleScanStatus', data.connectInProgress ? 'Verbindung wird aufgebaut' : 'Bereit'); renderBleResults(data); }
function fetchBleStatus(){ return fetch('/ble/status').then((r) => r.json()).then(updateBleUi).catch(() => {}); }
function startBleScan(){ setButtonLoading('bleScanBtn', true, 'Suche'); fetch('/ble/scan', { method:'POST' }).then(fetchBleStatus).finally(() => setButtonLoading('bleScanBtn', false)); }
function requestBleConnect(address, name){ fetch('/ble/connect-device', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'address=' + encodeURIComponent(address) + '&name=' + encodeURIComponent(name) }).then(() => Promise.all([fetchBleStatus(), fetchStatus()])); }
function disconnectBle(){ fetch('/ble/disconnect', { method:'POST' }).then(() => Promise.all([fetchBleStatus(), fetchStatus()])); }
function updateStatus(data){ if(!data){ return; } setPill('settingsTelemetryPill', data.telemetryFallback ? 'warn' : 'neutral', data.activeTelemetry || 'Keine'); updateText('activeTelemetryValue', data.activeTelemetry || 'Keine'); updateText('simHubStateValue', data.simHubState || 'Deaktiviert'); updateText('vehicleModel', data.vehicleModel || 'Desktop Simulator'); updateText('vehicleVin', data.vehicleVin || 'SIM-LOCAL-8765'); updateText('vehicleDiag', data.vehicleDiag || 'Simulator live'); updateText('vehicleStatus', data.telemetryStale ? 'Warte auf Daten' : 'Live'); }
function fetchStatus(){ return fetch('/status').then((r) => r.json()).then(updateStatus).catch(() => {}); }
function initSettings(){ captureInitialSettingsState(); recomputeSettingsDirty(); $$('input,select,textarea', $('#settingsForm')).forEach((el) => { const handler = () => { markDirty(); scheduleSettingsApply(el.type === 'checkbox' || el.tagName === 'SELECT'); }; el.addEventListener('input', handler); el.addEventListener('change', handler); }); $('#settingsSave')?.addEventListener('click', () => saveSettings(true)); $('#settingsReset')?.addEventListener('click', () => window.location.reload()); $('#wifiScanBtn')?.addEventListener('click', startWifiScan); $('#wifiDisconnectBtn')?.addEventListener('click', disconnectWifi); $('#wifiModalCancel')?.addEventListener('click', closeWifiModal); $('#wifiModalConnect')?.addEventListener('click', submitWifiModal); $('#bleScanBtn')?.addEventListener('click', startBleScan); $('#bleDisconnectBtn')?.addEventListener('click', disconnectBle); $('#btnVehicleRefresh')?.addEventListener('click', () => fetchStatus()); window.addEventListener('resize', syncSavebarSpace); syncSavebarSpace(); fetchStatus(); fetchWifiStatus(); fetchBleStatus(); setInterval(fetchStatus, 2200); setInterval(fetchWifiStatus, 2600); setInterval(fetchBleStatus, 3000); }
document.addEventListener('DOMContentLoaded', initSettings);
)JS";
        page += "</script>";
    }

    std::string build_dashboard_page(const SimulatorStatusSnapshot &snapshot)
    {
        const VirtualLedBarFrame ledFrame =
            build_virtual_led_bar_frame(snapshot.runtime, snapshot.ledBar, snapshot.runtime.telemetryTimestampMs);
        const std::string greenLabel = safe_label(snapshot.ledBar.greenLabel, "Low RPM");
        const std::string yellowLabel = safe_label(snapshot.ledBar.yellowLabel, "Mid RPM");
        const std::string redLabel = safe_label(snapshot.ledBar.redLabel, "Shift");

        std::string page;
        page.reserve(42000);
        append_shell_head(page, "ShiftLight Dashboard", false);

        page += "<section class='hero'><div class='hero-card hero-card--accent'><div class='hero-head'><div><div class='hero-kicker'>Live Dashboard</div><div class='hero-title'>RPM, Status und ShiftLight</div><div class='hero-sub'>Der Simulator nutzt jetzt denselben Dashboard-Fluss wie das ESP-Webfrontend: LEDs oben, Verbindungen auf der separaten Seite.</div></div><div class='status-list'>";
        page += "<span class='pill neutral' id='telemetryPill'>" + html_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "</span>";
        page += "<span class='pill " + std::string(snapshot.runtime.bleConnected ? "ok" : "bad") + "' id='blePill'>" + html_escape(ble_summary(snapshot)) + "</span>";
        page += "<span class='pill " + std::string(snapshot.runtime.telemetryStale ? "warn" : "ok") + "' id='simHubPill'>" + html_escape(simhub_state_label(snapshot.runtime.simHubState)) + "</span>";
        page += "</div></div><div class='metric-grid'>";
        page += "<div class='metric'><div class='metric-label'>RPM</div><div class='metric-value' id='heroRpm'>" + std::to_string(snapshot.runtime.rpm) + "</div><div class='metric-note'>Live aus der aktiven Quelle</div></div>";
        page += "<div class='metric'><div class='metric-label'>Max RPM</div><div class='metric-value metric-value--compact' id='heroMaxRpm'>" + std::to_string(snapshot.ledBar.effectiveMaxRpm) + "</div><div class='metric-note'>Aktive Referenz fuer die LED-Bar im aktuellen Gang</div></div>";
        page += "<div class='metric'><div class='metric-label'>Geschwindigkeit</div><div class='metric-value metric-value--compact' id='heroSpeed'>" + std::to_string(snapshot.runtime.speedKmh) + (snapshot.device.useMph ? " mph" : " km/h") + "</div><div class='metric-note'>Simulator live</div></div>";
        page += "<div class='metric'><div class='metric-label'>Gang</div><div class='metric-value metric-value--compact' id='heroGear'>" + std::to_string(snapshot.runtime.gear) + "</div><div class='metric-note'>Aktuelle Anzeige</div></div></div></div>";
        page += "<div class='hero-card'><div class='hero-head'><div><div class='hero-kicker'>Verbindung</div><div class='hero-title'>Status in Echtzeit</div><div class='hero-sub'>Die Konfigurationsseite bleibt sauber fuer WLAN, SimHub und OBD. Hier siehst du nur den Live-Zustand.</div></div></div><div class='badge-grid'>";
        page += "<div class='badge-card'><span>Aktive Telemetrie</span><strong id='badgeTelemetry'>" + html_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "</strong></div>";
        page += "<div class='badge-card'><span>Sim Transport</span><strong id='badgeTransport'>" + html_escape(transport_label(snapshot.telemetry.simHubTransport)) + "</strong></div>";
        page += "<div class='badge-card'><span>BLE / OBD</span><strong id='badgeBle'>" + html_escape(ble_summary(snapshot)) + "</strong></div>";
        page += "<div class='badge-card'><span>WLAN</span><strong id='badgeWifi'>" + html_escape(sta_summary(snapshot)) + "</strong></div></div>";
        page += "<div class='status-inline'><span class='pill " + std::string(snapshot.runtime.staConnected ? "ok" : (snapshot.runtime.apActive ? "warn" : "bad")) + "'>" + html_escape(wifi_mode_label(snapshot.runtime.wifiMode)) + "</span><span class='pill neutral'>Web: " + html_escape(current_ip_string(snapshot)) + "</span></div></div></section>";

        page += "<form id='mainForm'><div class='dashboard-layout'><div class='dashboard-col'>";
        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Allgemein</h2><div class='panel-copy'>Die LED-Grundeinstellungen liegen hier wie auf dem ESP-Dashboard.</div></div></div><div class='field-grid two'><div class='field'><label for='modeSelect'>Schaltmodus</label><select id='modeSelect' name='ledMode'>";
        page += "<option value='casual'" + selected_attr(snapshot.ledBar.mode == SimulatorLedMode::Casual) + ">Casual</option>";
        page += "<option value='f1'" + selected_attr(snapshot.ledBar.mode == SimulatorLedMode::F1) + ">F1-Style</option>";
        page += "<option value='aggressive'" + selected_attr(snapshot.ledBar.mode == SimulatorLedMode::Aggressive) + ">Aggressiv</option>";
        page += "<option value='gt3'" + selected_attr(snapshot.ledBar.mode == SimulatorLedMode::Gt3) + ">GT3 / Endurance</option>";
        page += "</select><div class='field-note'>Der Simulator folgt hier denselben LED-Modi wie die Firmware.</div></div>";
        page += "<div class='field'><label for='brightnessSlider'>LED-Helligkeit</label><input type='range' id='brightnessSlider' name='ledBrightness' min='0' max='255' value='" + std::to_string(snapshot.ledBar.brightness) + "' data-value-target='brightnessValue'><div class='field-note'>Aktuell <span id='brightnessValue'>" + std::to_string(snapshot.ledBar.brightness) + "</span> / 255</div></div></div>";
        page += "<div class='button-row'><button type='button' class='btn btn-secondary' id='btnRpmDown'>RPM -</button><button type='button' class='btn btn-primary' id='btnRpmUp'>RPM +</button><button type='button' class='btn btn-secondary' id='btnShift'>Shift</button><button type='button' class='btn btn-secondary' id='btnAnimation'>Animation</button><button type='button' class='btn btn-secondary' id='btnResetSim'>Reset</button></div></section>";

        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Live-Fahrzeugstatus</h2><div class='panel-copy'>Live-Werte und aktive Pfade bleiben sichtbar, ohne die Verbindungsseite zu ueberladen.</div></div></div><div class='info-list'>";
        page += "<div class='info-row'><div class='info-label'>Fahrzeug</div><div class='info-value'>Desktop Simulator</div></div>";
        page += "<div class='info-row'><div class='info-label'>VIN</div><div class='info-value mono'>SIM-LOCAL-8765</div></div>";
        page += "<div class='info-row'><div class='info-label'>Sim Transport</div><div class='info-value' id='vehicleTransportValue'>" + html_escape(transport_label(snapshot.telemetry.simHubTransport)) + "</div></div>";
        page += "<div class='info-row'><div class='info-label'>Display Fokus</div><div class='info-value' id='vehicleDisplayFocusValue'>" + html_escape(display_focus_label(snapshot.runtime.settings.displayFocus)) + "</div></div>";
        page += "<div class='info-row'><div class='info-label'>DDU Screen</div><div class='info-value mono' id='vehicleScreenValue'>" + html_escape(ui_screen_name(snapshot.ui.activeScreen)) + "</div></div>";
        page += "<div class='info-row'><div class='info-label'>BLE Ziel</div><div class='info-value' id='vehicleBleTargetValue'>" + html_escape(snapshot.bleTargetName) + "</div></div>";
        page += "<div class='info-row'><div class='info-label'>Web URL</div><div class='info-value mono' id='vehicleWebValue'>" + html_escape(current_ip_string(snapshot)) + "</div></div></div></section>";

        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>RPM / ShiftLight</h2><div class='panel-copy'>Schwellen, Vorschau und aktive LED-Anzahl sitzen jetzt wieder im Dashboard.</div></div></div>";
        page += "<input type='hidden' name='autoScaleMaxRpm' value='0'><div class='field-inline'><div><strong>Auto-Scale Max RPM</strong><span>Nutze die hoechste beobachtete Drehzahl als Referenz.</span></div><label class='switch'><input type='checkbox' id='autoscaleToggle' name='autoScaleMaxRpm'" + checked_attr(snapshot.ledBar.autoScaleMaxRpm) + "><span class='slider'></span></label></div>";
        page += "<input type='hidden' name='maxRpmPerGearEnabled' value='0'><div class='field-inline'><div><strong>Max RPM pro Gang</strong><span>Aktiviert getrennte RPM-Referenzen und Auto-Erkennung fuer Gang 1 bis 8.</span></div><label class='switch'><input type='checkbox' id='perGearToggle' name='maxRpmPerGearEnabled'" + checked_attr(snapshot.ledBar.maxRpmPerGearEnabled) + "><span class='slider'></span></label></div>";
        page += "<div class='field hidden' id='fixedMaxWrap'><label for='fixedMaxRpmInput'>Feste Max RPM</label><input type='number' id='fixedMaxRpmInput' name='fixedMaxRpm' min='1000' max='14000' value='" + std::to_string(snapshot.ledBar.fixedMaxRpm) + "'><div class='field-note'>Globaler Wert fuer Neutral, Rueckwaerts oder wenn Pro-Gang deaktiviert ist.</div></div>";
        page += "<div class='field-grid two hidden' id='gearMaxWrap'><div class='field' style='grid-column:1 / -1'><div class='field-note' id='gearMaxHint'>Aktiver Gang: " + std::string(has_drive_gear(snapshot.runtime.gear) ? std::to_string(snapshot.runtime.gear) : "Fallback / Neutral") + "</div></div>";
        for (size_t i = 0; i < kSimulatorGearCount; ++i)
        {
            page += "<div class='field'><label for='fixedMaxRpmGear" + std::to_string(i + 1) + "'>Gang " + std::to_string(i + 1) + " Max RPM</label><input type='number' class='gear-max-input' id='fixedMaxRpmGear" + std::to_string(i + 1) + "' name='fixedMaxRpmGear" + std::to_string(i + 1) + "' min='1000' max='14000' value='" + std::to_string(snapshot.ledBar.fixedMaxRpmByGear[i]) + "'><div class='field-note'>Startwert und Fallback fuer Gang " + std::to_string(i + 1) + ".</div></div>";
        }
        page += "</div>";
        page += "<div class='field-grid two hidden' id='gearAutoWrap'><div class='field' style='grid-column:1 / -1'><div class='field-note' id='gearAutoHint'>Automodus aktiv. Der Simulator zeigt nur bereits erkannte Gang-Maxwerte.</div></div>";
        page += "<div class='field'><label>Erkannte Ganganzahl</label><div class='pill neutral' id='detectedGearCountValue'>" + std::to_string(detected_gear_count(snapshot)) + "</div><div class='field-note'>Hoechster bereits gelernter Fahrgang.</div></div>";
        page += "<div class='field'><label>Gelernte Gaenge</label><div class='pill neutral' id='learnedGearTotalValue'>" + std::to_string(learned_gear_total(snapshot)) + " / " + std::to_string(kSimulatorGearCount) + "</div><div class='field-note'>Nur diese Gaenge fliessen aktuell in die Auto-Erkennung ein.</div></div>";
        for (size_t i = 0; i < kSimulatorGearCount; ++i)
        {
            const bool learned = snapshot.ledBar.learnedMaxRpmByGear[i];
            page += "<div class='field' id='autoGearCard" + std::to_string(i + 1) + "'" + std::string(learned ? "" : " style='display:none'") + "><label>Gang " + std::to_string(i + 1) + " Auto Max RPM</label><input type='number' id='autoDetectedMaxRpmGear" + std::to_string(i + 1) + "' value='" + std::to_string(snapshot.ledBar.effectiveMaxRpmByGear[i]) + "' readonly><div class='field-note' id='autoDetectedNoteGear" + std::to_string(i + 1) + "'>" + std::string(learned ? "Auto erkannt und gesperrt." : "Noch nicht erkannt.") + "</div></div>";
        }
        page += "<div class='field hidden' id='gearAutoEmpty' style='grid-column:1 / -1'><div class='field-note'>Noch keine Gang-Maxwerte erkannt. Fahr die betreffenden Gaenge einmal bis in den Shift-Bereich.</div></div></div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>LED Start</div><div class='range-value' id='rpmStartValue'>" + std::to_string(snapshot.ledBar.startRpm) + " rpm</div></div><input type='range' id='rpmStartSlider' name='startRpm' min='0' max='10000' value='" + std::to_string(snapshot.ledBar.startRpm) + "' data-value-target='rpmStartValue' data-suffix=' rpm'><div class='seg-note'>Unterhalb dieser Drehzahl bleibt die Bar komplett aus.</div></div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>" + html_escape(greenLabel) + "</div><div class='range-value' id='greenEndValue'>" + std::to_string(snapshot.ledBar.greenEndPct) + "%</div></div><input type='range' id='greenEndSlider' name='greenEndPct' min='0' max='100' value='" + std::to_string(snapshot.ledBar.greenEndPct) + "' data-value-target='greenEndValue' data-suffix='%'><div class='seg-note'>Ende des ruhigen Bereichs.</div></div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>" + html_escape(yellowLabel) + "</div><div class='range-value' id='yellowEndValue'>" + std::to_string(snapshot.ledBar.yellowEndPct) + "%</div></div><input type='range' id='yellowEndSlider' name='yellowEndPct' min='0' max='100' value='" + std::to_string(snapshot.ledBar.yellowEndPct) + "' data-value-target='yellowEndValue' data-suffix='%'><div class='seg-note'>Uebergang in die Warnzone.</div></div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>" + html_escape(redLabel) + "</div><div class='range-value' id='redEndValue'>" + std::to_string(snapshot.ledBar.redEndPct) + "%</div></div><input type='range' id='redEndSlider' name='redEndPct' min='0' max='100' value='" + std::to_string(snapshot.ledBar.redEndPct) + "' data-value-target='redEndValue' data-suffix='%'><div class='seg-note'>Der feste rote Bereich vor dem Blinken.</div></div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Blink Start</div><div class='range-value' id='blinkStartValue'>" + std::to_string(snapshot.ledBar.blinkStartPct) + "%</div></div><input type='range' id='blinkStartSlider' name='blinkStartPct' min='0' max='100' value='" + std::to_string(snapshot.ledBar.blinkStartPct) + "' data-value-target='blinkStartValue' data-suffix='%'><div class='seg-note'>Ab hier blinkt die Bar komplett rot.</div></div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Blink Tempo</div><div class='range-value' id='blinkSpeedValue'>" + std::to_string(snapshot.ledBar.blinkSpeedPct) + "%</div></div><input type='range' id='blinkSpeedSlider' name='blinkSpeedPct' min='0' max='100' value='" + std::to_string(snapshot.ledBar.blinkSpeedPct) + "' data-value-target='blinkSpeedValue' data-suffix='%'><div class='seg-note'>Nur die Vorschau blinkt hier direkt mit.</div></div>";
        append_led_preview(page, ledFrame);
        page += "<div class='field-note' id='liveLedInfo'>Aktive LEDs: " + std::to_string(snapshot.ledBar.activeLedCount) + " | Brightness: " + std::to_string(snapshot.ledBar.brightness) + " | Leuchtet: " + std::to_string(ledFrame.litCount) + "</div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Aktive LEDs</div><div class='range-value' id='activeLedCountValue'>" + std::to_string(snapshot.ledBar.activeLedCount) + "</div></div><input type='range' id='activeLedCountSlider' name='ledCount' min='1' max='60' value='" + std::to_string(snapshot.ledBar.activeLedCount) + "' data-value-target='activeLedCountValue'><div class='seg-note'>Nur fuer den Simulator. So kannst du kuerzere Bars nachstellen.</div></div></section>";

        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Side LEDs / Traction Bars</h2><div class='panel-copy'>Die Seitenleisten zeigen jetzt SimHub-Traction statt Spotter oder Flags. Beim Bremsen laufen sie von oben nach unten, beim Beschleunigen von unten nach oben.</div></div></div>";
        page += "<input type='hidden' name='sideEnabled' value='0'><div class='field-inline'><div><strong>Side LEDs aktiv</strong><span>Blendet die seitlichen Traction-Bars ein oder aus. Die obere RPM-Bar bleibt separat.</span></div><label class='switch'><input type='checkbox' id='sideEnabledToggle' name='sideEnabled'" + checked_attr(snapshot.sideLeds.enabled) + "><span class='slider'></span></label></div>";
        page += "<div class='field-grid two'><div class='field'><label for='sidePresetSelect'>Preset</label><select id='sidePresetSelect' name='sidePreset'>";
        page += "<option value='gt3'" + selected_attr(snapshot.sideLeds.preset == SideLedPreset::Gt3) + ">GT3</option>";
        page += "<option value='casual'" + selected_attr(snapshot.sideLeds.preset == SideLedPreset::Casual) + ">Casual</option>";
        page += "<option value='minimal'" + selected_attr(snapshot.sideLeds.preset == SideLedPreset::Minimal) + ">Minimal</option>";
        page += "</select></div>";
        page += "<div class='field'><label for='sideBrightnessSlider'>Side LED Helligkeit</label><input type='range' id='sideBrightnessSlider' name='sideBrightness' min='0' max='255' value='" + std::to_string(snapshot.sideLeds.brightness) + "' data-value-target='sideBrightnessValue'><div class='field-note'><span id='sideBrightnessValue'>" + std::to_string(snapshot.sideLeds.brightness) + "</span> / 255</div></div></div>";
        page += "<div class='field'><label for='sideLedCountPerSide'>LEDs pro Seite</label><input type='range' id='sideLedCountPerSide' name='sideLedCountPerSide' min='" + std::to_string(static_cast<int>(SIDE_LED_MIN_COUNT_PER_SIDE)) + "' max='" + std::to_string(static_cast<int>(SIDE_LED_MAX_COUNT_PER_SIDE)) + "' value='" + std::to_string(snapshot.sideLeds.ledCountPerSide) + "' data-value-target='sideLedCountValue'><div class='field-note'><span id='sideLedCountValue'>" + std::to_string(snapshot.sideLeds.ledCountPerSide) + "</span> LEDs je Seite</div></div>";
        page += "<div class='field-grid two'>";
        page += "<div class='field'><input type='hidden' name='sideAllowTraction' value='0'><div class='field-inline'><div><strong>Traction aus SimHub</strong><span>Nutzt Brake, Throttle und Wheel-Slip fuer die seitlichen Bars.</span></div><label class='switch'><input type='checkbox' name='sideAllowTraction'" + checked_attr(snapshot.sideLeds.allowTraction) + "><span class='slider'></span></label></div></div>";
        page += "<div class='field'><input type='hidden' name='sideTestMode' value='0'><div class='field-inline'><div><strong>Test Override</strong><span>Erlaubt die Testknopfe unten fuer Beschleunigen, Bremsen und Slip.</span></div><label class='switch'><input type='checkbox' name='sideTestMode'" + checked_attr(snapshot.sideLeds.testMode) + "><span class='slider'></span></label></div></div>";
        page += "</div>";
        page += "<div class='side-preview' id='sidePreview'><div class='side-stack' id='sideLeftStack'>";
        for (size_t i = 0; i < SIDE_LED_MAX_COUNT_PER_SIDE; ++i)
        {
            page += "<span class='side-led-dot' id='sideLeftLed" + std::to_string(i + 1) + "' style='display:" + std::string(i < snapshot.sideLeds.ledCountPerSide ? "block" : "none") + "'></span>";
        }
        page += "</div><div class='side-meta'><div><strong id='sideSourceValue'>" + html_escape(side_led_source_name(snapshot.runtime.sideLedFrame.source)) + "</strong></div><div id='sideEventValue'>" + html_escape(side_led_event_name(snapshot.runtime.sideLedFrame.event)) + "</div><div id='sideTelemetryValue'>Grip Load " + std::to_string(static_cast<int>(snapshot.runtime.sideTelemetry.traction.throttle * 100.0f)) + "% | Brake Load " + std::to_string(static_cast<int>(snapshot.runtime.sideTelemetry.traction.brake * 100.0f)) + "% | Grip L " + std::to_string(snapshot.runtime.sideTelemetry.traction.leftLevel) + " / R " + std::to_string(snapshot.runtime.sideTelemetry.traction.rightLevel) + "</div></div><div class='side-stack right' id='sideRightStack'>";
        for (size_t i = 0; i < SIDE_LED_MAX_COUNT_PER_SIDE; ++i)
        {
            page += "<span class='side-led-dot' id='sideRightLed" + std::to_string(i + 1) + "' style='display:" + std::string(i < snapshot.sideLeds.ledCountPerSide ? "block" : "none") + "'></span>";
        }
        page += "</div></div>";
        page += "<div class='field-note' id='sideStatusNote'>Richtung: " + html_escape(side_led_traction_direction_name(snapshot.runtime.sideLedFrame.direction)) + " | LEDs / Seite: " + std::to_string(snapshot.sideLeds.ledCountPerSide) + "</div>";
        page += "<details class='panel details' style='padding:16px'><summary><div><h3 class='panel-title'>Side LED Feinabstimmung</h3><div class='panel-copy'>Blinktempo und Spiegelung fuer die seitlichen Traction-Bars.</div></div></summary><div class='stack' style='margin-top:14px'>";
        page += "<div class='field-grid two'><div class='field'><label for='sideBlinkSpeedSlowMs'>Blink langsam (ms)</label><input type='number' id='sideBlinkSpeedSlowMs' name='sideBlinkSpeedSlowMs' min='80' max='1500' value='" + std::to_string(snapshot.sideLeds.blinkSpeedSlowMs) + "'></div><div class='field'><label for='sideBlinkSpeedFastMs'>Blink schnell (ms)</label><input type='number' id='sideBlinkSpeedFastMs' name='sideBlinkSpeedFastMs' min='40' max='900' value='" + std::to_string(snapshot.sideLeds.blinkSpeedFastMs) + "'></div>";
        page += "<div class='field'><div class='field-note'>Langsam fuer ruhige Bars oder Idle. Schnell wird fuer kritischen Slip genutzt.</div></div><div class='field'><div class='field-note'>Spotter-, Flag- und Warning-Overrides sind in diesem Traction-Layout bewusst deaktiviert.</div></div></div>";
        page += "<div class='field-grid two'>";
        page += "<div class='field'><input type='hidden' name='sideCloseCarBlinkingEnabled' value='0'><div class='field-inline'><div><strong>Kritischen Slip blinken</strong><span>Schnelles Blinken bei starkem Wheel-Slip oder Blockieren.</span></div><label class='switch'><input type='checkbox' name='sideCloseCarBlinkingEnabled'" + checked_attr(snapshot.sideLeds.closeCarBlinkingEnabled) + "><span class='slider'></span></label></div></div>";
        page += "<div class='field'><input type='hidden' name='sideSeverityLevelsEnabled' value='0'><div class='field-inline'><div><strong>Slip-Intensitaet nutzen</strong><span>Zieht die Bars je nach Gripverlust weiter auf.</span></div><label class='switch'><input type='checkbox' name='sideSeverityLevelsEnabled'" + checked_attr(snapshot.sideLeds.severityLevelsEnabled) + "><span class='slider'></span></label></div></div>";
        page += "<div class='field'><input type='hidden' name='sideInvertLeftRight' value='0'><div class='field-inline'><div><strong>Links / Rechts invertieren</strong><span>Tauscht beide Seiten fuer Sonderaufbauten.</span></div><label class='switch'><input type='checkbox' name='sideInvertLeftRight'" + checked_attr(snapshot.sideLeds.invertLeftRight) + "><span class='slider'></span></label></div></div>";
        page += "<div class='field'><input type='hidden' name='sideMirrorMode' value='0'><div class='field-inline'><div><strong>Mirror Mode</strong><span>Beide Seiten zeigen denselben Zustand.</span></div><label class='switch'><input type='checkbox' name='sideMirrorMode'" + checked_attr(snapshot.sideLeds.mirrorMode) + "><span class='slider'></span></label></div></div>";
        page += "<div class='field'><input type='hidden' name='sideIdleAnimationEnabled' value='0'><div class='field-inline'><div><strong>Idle Animation</strong><span>Leichtes Sweep wenn gerade nichts aktiv ist.</span></div><label class='switch'><input type='checkbox' name='sideIdleAnimationEnabled'" + checked_attr(snapshot.sideLeds.idleAnimationEnabled) + "><span class='slider'></span></label></div></div>";
        page += "<div class='field'><div class='field-note'>Bremsen fuellt oben nach unten. Beschleunigen fuellt unten nach oben. Die Anzahl folgt direkt dem Slider oben.</div></div>";
        page += "</div></div></details>";
        page += "<div class='button-grid'><button type='button' class='btn btn-secondary' id='btnSideAccel'>Beschleunigen</button><button type='button' class='btn btn-secondary' id='btnSideBrake'>Bremsen</button><button type='button' class='btn btn-secondary' id='btnSideTractionLeft'>Slip links</button><button type='button' class='btn btn-secondary' id='btnSideTractionRight'>Slip rechts</button><button type='button' class='btn btn-secondary' id='btnSideTractionBoth'>Slip beide</button><button type='button' class='btn btn-ghost' id='btnSideClear'>Test Clear</button></div></section>";

        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Farbzonen</h2><div class='panel-copy'>Wie auf dem ESP bleiben die drei Hauptfarben direkt im Dashboard.</div></div></div><div class='color-grid'>";
        page += "<div class='color-card'><label for='greenColorInput'>" + html_escape(greenLabel) + "</label><input type='color' id='greenColorInput' name='greenColor' value='" + html_color_hex(snapshot.ledBar.greenColor) + "'><div class='seg-note'>Low RPM Bereich</div><input type='hidden' name='greenLabel' value='" + html_escape(greenLabel) + "'></div>";
        page += "<div class='color-card'><label for='yellowColorInput'>" + html_escape(yellowLabel) + "</label><input type='color' id='yellowColorInput' name='yellowColor' value='" + html_color_hex(snapshot.ledBar.yellowColor) + "'><div class='seg-note'>Mid RPM Bereich</div><input type='hidden' name='yellowLabel' value='" + html_escape(yellowLabel) + "'></div>";
        page += "<div class='color-card'><label for='redColorInput'>" + html_escape(redLabel) + "</label><input type='color' id='redColorInput' name='redColor' value='" + html_color_hex(snapshot.ledBar.redColor) + "'><div class='seg-note'>Shift / Warnung</div><input type='hidden' name='redLabel' value='" + html_escape(redLabel) + "'></div></div></section>";

        page += "<details class='panel details'><summary><div><h2 class='panel-title'>Simulator-Steuerung</h2><div class='panel-copy'>Desktop-spezifische Extras bleiben verfuegbar, aber druecken das Dashboard nicht mehr in die Verbindungsseite.</div></div></summary><div class='stack' style='margin-top:14px'>";
        page += "<div class='field-grid two'><div class='field'><label for='displayBrightnessInput'>Display Brightness</label><input type='number' id='displayBrightnessInput' name='displayBrightness' min='10' max='255' value='" + std::to_string(snapshot.runtime.settings.displayBrightness) + "'><div class='field-note'>Nur fuer die lokale Simulator-DDU.</div></div>";
        page += "<div class='field'><label for='nightModeToggleWrap'>Night Mode</label><input type='hidden' name='nightMode' value='0'><div class='field-inline' id='nightModeToggleWrap'><div><strong>Night Mode</strong><span id='desktopStatusValue'>" + std::to_string(snapshot.runtime.settings.displayBrightness) + " / " + std::string(snapshot.runtime.settings.nightMode ? "Night" : "Day") + "</span></div><label class='switch'><input type='checkbox' name='nightMode'" + checked_attr(snapshot.runtime.settings.nightMode) + "><span class='slider'></span></label></div></div>";
        page += "<div class='field' style='grid-column:1 / -1'><label for='showShiftStripToggleWrap'>Display LED-Leiste</label><input type='hidden' name='showShiftStrip' value='0'><div class='field-inline' id='showShiftStripToggleWrap'><div><strong>Obere LED-Leiste anzeigen</strong><span>Blendet die Display-Leiste ein oder aus und skaliert das Fahrdashboard neu.</span></div><label class='switch'><input type='checkbox' name='showShiftStrip'" + checked_attr(snapshot.runtime.settings.showShiftStrip) + "><span class='slider'></span></label></div></div></div>";
        page += "<div class='info-list compact'><div class='info-row'><div class='info-label'>WLAN</div><div class='info-value' id='desktopWifiValue'>" + html_escape(sta_summary(snapshot)) + "</div></div><div class='info-row'><div class='info-label'>BLE</div><div class='info-value' id='desktopBleValue'>" + html_escape(ble_summary(snapshot)) + "</div></div><div class='info-row'><div class='info-label'>UI</div><div class='info-value mono' id='desktopUiValue'>" + html_escape(ui_screen_name(snapshot.ui.activeScreen)) + "</div></div></div>";
        page += "<div class='button-row'><button type='button' class='btn btn-secondary' id='btnUiPrev'>UI Prev</button><button type='button' class='btn btn-secondary' id='btnUiNext'>UI Next</button><button type='button' class='btn btn-secondary' id='btnUiOpen'>UI Open</button><button type='button' class='btn btn-secondary' id='btnUiHome'>UI Home</button><button type='button' class='btn btn-secondary' id='btnUiLogo'>Logo</button><button type='button' class='btn btn-secondary' id='btnWifiCycle'>WiFi Status</button><button type='button' class='btn btn-secondary' id='btnBleCycle'>BLE Status</button></div></div></details>";
        page += "</div><div class='dashboard-col'>";

        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Auto-Helligkeit</h2><div class='panel-copy'>Das Verhalten bleibt wie auf dem ESP-Dashboard im rechten Bereich gebuendelt.</div></div><span class='pill " + std::string(snapshot.device.autoBrightnessEnabled ? "ok" : "neutral") + "'>" + std::string(snapshot.device.autoBrightnessEnabled ? "Auto aktiv" : "Auto aus") + "</span></div>";
        page += "<input type='hidden' name='autoBrightnessEnabled' value='0'><div class='field-inline'><div><strong>Automatische Helligkeit</strong><span>Desktop-only, aber im selben Aufbau wie im Betrieb.</span></div><label class='switch'><input type='checkbox' id='autoBrightnessToggle' name='autoBrightnessEnabled'" + checked_attr(snapshot.device.autoBrightnessEnabled) + "><span class='slider'></span></label></div>";
        page += "<div class='field-grid two'><div class='range-wrap'><div class='range-head'><div class='range-title'>Minimum</div><div class='range-value' id='autoBrightnessMinValue'>" + std::to_string(snapshot.device.autoBrightnessMin) + "</div></div><input type='range' id='autoBrightnessMinSlider' name='autoBrightnessMin' min='0' max='" + std::to_string(snapshot.ledBar.brightness) + "' value='" + std::to_string(snapshot.device.autoBrightnessMin) + "' data-value-target='autoBrightnessMinValue'><div class='seg-note'>Untergrenze im Dunkeln.</div></div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Faktor</div><div class='range-value' id='autoBrightnessStrengthValue'>" + std::to_string(snapshot.device.autoBrightnessStrengthPct) + "%</div></div><input type='range' id='autoBrightnessStrengthSlider' name='autoBrightnessStrengthPct' min='25' max='200' value='" + std::to_string(snapshot.device.autoBrightnessStrengthPct) + "' data-value-target='autoBrightnessStrengthValue' data-suffix='%'><div class='seg-note'>Skaliert die komplette Kurve.</div></div>";
        page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Reaktion</div><div class='range-value' id='autoBrightnessResponseValue'>" + std::to_string(snapshot.device.autoBrightnessResponsePct) + "%</div></div><input type='range' id='autoBrightnessResponseSlider' name='autoBrightnessResponsePct' min='1' max='100' value='" + std::to_string(snapshot.device.autoBrightnessResponsePct) + "' data-value-target='autoBrightnessResponseValue' data-suffix='%'><div class='seg-note'>Hoeher reagiert schneller.</div></div>";
        page += "<div class='field'><label for='autoBrightnessLuxMin'>Lux Start</label><input type='number' id='autoBrightnessLuxMin' name='autoBrightnessLuxMin' min='0' max='120000' value='" + std::to_string(snapshot.device.autoBrightnessLuxMin) + "'></div>";
        page += "<div class='field'><label for='autoBrightnessLuxMax'>Lux Voll</label><input type='number' id='autoBrightnessLuxMax' name='autoBrightnessLuxMax' min='1' max='120000' value='" + std::to_string(snapshot.device.autoBrightnessLuxMax) + "'></div>";
        page += "<div class='field'><label for='ambientLightSdaPin'>VEML7700 SDA Pin</label><input type='number' id='ambientLightSdaPin' name='ambientLightSdaPin' min='0' max='48' value='" + std::to_string(snapshot.device.ambientLightSdaPin) + "'></div>";
        page += "<div class='field'><label for='ambientLightSclPin'>VEML7700 SCL Pin</label><input type='number' id='ambientLightSclPin' name='ambientLightSclPin' min='0' max='48' value='" + std::to_string(snapshot.device.ambientLightSclPin) + "'></div></div></section>";

        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Animationen</h2><div class='panel-copy'>Wie auf dem ESP bleiben seltenere Schalter separat, aber noch direkt erreichbar.</div></div></div>";
        page += "<input type='hidden' name='logoIgnOn' value='0'><div class='field-inline'><div><strong>Logo bei Zuendung an</strong><span>Lokale Startsequenz fuer den Simulator.</span></div><label class='switch'><input type='checkbox' name='logoIgnOn'" + checked_attr(snapshot.device.logoOnIgnitionOn) + "><span class='slider'></span></label></div>";
        page += "<input type='hidden' name='logoEngStart' value='0'><div class='field-inline'><div><strong>Logo bei Motorstart</strong><span>Bleibt fuer die Desktop-DDU verfuegbar.</span></div><label class='switch'><input type='checkbox' name='logoEngStart'" + checked_attr(snapshot.device.logoOnEngineStart) + "><span class='slider'></span></label></div>";
        page += "<input type='hidden' name='logoIgnOff' value='0'><div class='field-inline'><div><strong>Leaving Animation</strong><span>Abschlussanimation beim Reset / Aus.</span></div><label class='switch'><input type='checkbox' name='logoIgnOff'" + checked_attr(snapshot.device.logoOnIgnitionOff) + "><span class='slider'></span></label></div>";
        page += "<input type='hidden' name='gestureControlEnabled' value='0'><div class='field-inline'><div><strong>Gestensteuerung</strong><span>Bleibt als lokale Simulator-Option sichtbar.</span></div><label class='switch'><input type='checkbox' name='gestureControlEnabled'" + checked_attr(snapshot.device.gestureControlEnabled) + "><span class='slider'></span></label></div>";
        page += "<input type='hidden' name='simFxLed' value='0'><div class='field-inline'><div><strong>Session-Transition-Effekte</strong><span>LED-Effekte fuer SimHub / Simulator.</span></div><label class='switch'><input type='checkbox' name='simFxLed'" + checked_attr(snapshot.device.simSessionLedEffectsEnabled) + "><span class='slider'></span></label></div>";
        page += "<input type='hidden' name='autoReconnect' value='0'><div class='field-inline'><div><strong>Auto Reconnect</strong><span>Fuer WLAN / OBD im Simulator gespeichert.</span></div><label class='switch'><input type='checkbox' name='autoReconnect'" + checked_attr(snapshot.device.autoReconnect) + "><span class='slider'></span></label></div>";
        page += "<input type='hidden' name='useMph' value='0'><div class='field-inline'><div><strong>mph Anzeige</strong><span>Schaltet die Geschwindigkeits-Einheit fuer den Simulator um.</span></div><label class='switch'><input type='checkbox' name='useMph'" + checked_attr(snapshot.device.useMph) + "><span class='slider'></span></label></div></section>";
        page += "</div></div></form>";

        page += "<div class='savebar' id='saveBar' data-dirty='0'><div><div class='savebar-title'><span>Status</span></div><div class='savebar-copy'>Konfiguration ist synchron.</div></div><div class='savebar-actions'><button type='button' class='btn btn-ghost' id='btnReset'>Zuruecksetzen</button><button type='button' class='btn btn-primary' id='btnSave' disabled>Speichern</button></div></div>";
        page += "<div class='toast hidden' id='dashboardToast'>Dashboard-Konfiguration wurde uebernommen.</div>";
        append_dashboard_script(page);
        append_shell_footer(page);
        return page;
    }

    std::string build_settings_page(const SimulatorStatusSnapshot &snapshot, bool savedNotice)
    {
        std::string page;
        page.reserve(24000);
        append_shell_head(page, "ShiftLight Verbindung", true);

        page += "<section class='hero'><div class='hero-card hero-card--accent'><div class='hero-head'><div><div class='hero-kicker'>Verbindung & Telemetrie</div><div class='hero-title'>Netzwerk, SimHub und OBD</div><div class='hero-sub'>Der Simulator folgt jetzt derselben Trennung wie die Firmware: Dashboard fuer LEDs, Verbindung fuer WLAN und Quellen.</div></div><div class='status-list'>";
        page += "<span class='pill neutral' id='settingsTelemetryPill'>" + html_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "</span>";
        page += "<span class='pill " + std::string(snapshot.runtime.staConnected ? "ok" : (snapshot.runtime.apActive ? "warn" : "bad")) + "' id='settingsWifiPill'>" + html_escape(sta_summary(snapshot)) + "</span>";
        page += "<span class='pill " + std::string(snapshot.runtime.bleConnected ? "ok" : "bad") + "' id='settingsBlePill'>" + html_escape(ble_summary(snapshot)) + "</span>";
        page += "</div></div><div class='badge-grid'>";
        page += "<div class='badge-card'><span>Aktive Quelle</span><strong id='activeTelemetryValue'>" + html_escape(telemetry_source_label(snapshot.runtime.telemetrySource, snapshot.runtime.telemetryUsingFallback)) + "</strong></div>";
        page += "<div class='badge-card'><span>SimHub</span><strong id='simHubStateValue'>" + html_escape(simhub_state_label(snapshot.runtime.simHubState)) + "</strong></div>";
        page += "<div class='badge-card'><span>WLAN Modus</span><strong id='wifiModeValue'>" + html_escape(wifi_mode_label(snapshot.runtime.wifiMode)) + "</strong></div>";
        page += "<div class='badge-card'><span>Aktuelle IP</span><strong id='wifiIpValue'>" + html_escape(current_ip_string(snapshot)) + "</strong></div></div></div>";
        page += "<div class='hero-card'><div class='hero-head'><div><div class='hero-kicker'>Hinweis</div><div class='hero-title'>Simulator statt Parallel-UI</div><div class='hero-sub'>Die bisherige Sonderoberflaeche ist raus aus dem Weg. LED-Setup bleibt im Dashboard, hier bleibt nur die Verbindungslogik.</div></div></div><div class='callout'>Wenn du LED-Modus, Farben oder Shift-Slider aendern willst, gehst du oben auf <strong>Dashboard</strong> wie auf dem ESP. Hier liegen nur noch Quelle, WLAN und OBD.</div></div></section>";

        page += "<form id='settingsForm'><div class='app-grid'><section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Telemetrie</h2><div class='panel-copy'>Quelle, Transport und Display-Fokus bleiben zentral gebuendelt.</div></div></div><div class='field-grid'>";
        page += "<div class='field'><label for='telemetryMode'>Bevorzugte Quelle</label><select id='telemetryMode' name='telemetryMode'>";
        page += "<option value='auto'" + selected_attr(snapshot.telemetry.mode == TelemetryInputMode::SimHub && snapshot.telemetry.allowSimulatorFallback) + ">Automatisch (SimHub bevorzugen, sonst Simulator)</option>";
        page += "<option value='simulator'" + selected_attr(snapshot.telemetry.mode == TelemetryInputMode::Simulator) + ">Nur Simulator</option>";
        page += "<option value='simhub'" + selected_attr(snapshot.telemetry.mode == TelemetryInputMode::SimHub && !snapshot.telemetry.allowSimulatorFallback) + ">Nur SimHub</option>";
        page += "</select></div>";
        page += "<div class='field'><label for='simhubTransport'>Sim Link</label><select id='simhubTransport' name='simhubTransport'><option value='http'" + selected_attr(snapshot.telemetry.simHubTransport == SimHubTransport::HttpApi) + ">HTTP API</option><option value='udp'" + selected_attr(snapshot.telemetry.simHubTransport == SimHubTransport::JsonUdp) + ">UDP JSON</option></select><div class='field-note'>Bleibt nah an der echten ESP-Weboberflaeche, nur ohne Hardwarezwang.</div></div>";
        page += "<div class='field-grid two'><div class='field'><label for='simhubPort'>SimHub Port</label><input type='number' id='simhubPort' name='simhubPort' min='1' max='65535' value='" + std::to_string(snapshot.telemetry.simHubTransport == SimHubTransport::HttpApi ? snapshot.telemetry.httpPort : snapshot.telemetry.udpPort) + "'></div><div class='field'><label for='simhubPollMs'>Poll Intervall (ms)</label><input type='number' id='simhubPollMs' name='simhubPollMs' min='15' max='1000' value='" + std::to_string(snapshot.telemetry.pollIntervalMs) + "'></div></div>";
        page += "<div class='field'><label for='displayFocus'>Fahr-Layout auf dem Display</label><select id='displayFocus' name='displayFocus'><option value='rpm'" + selected_attr(snapshot.runtime.settings.displayFocus == UiDisplayFocusMetric::Rpm) + ">RPM gross</option><option value='gear'" + selected_attr(snapshot.runtime.settings.displayFocus == UiDisplayFocusMetric::Gear) + ">Gang gross</option><option value='speed'" + selected_attr(snapshot.runtime.settings.displayFocus == UiDisplayFocusMetric::Speed) + ">Geschwindigkeit gross</option></select></div></div></section>";

        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>WLAN</h2><div class='panel-copy'>Status, Quick Connect und persistente Zugangsdaten bleiben auf einer Seite.</div></div></div>";
        page += "<div class='field'><label for='wifiModePreference'>WLAN Modus</label><select id='wifiModePreference' name='wifiModePreference'><option value='ap'" + selected_attr(snapshot.device.wifiModePreference == UiWifiMode::ApOnly) + ">Access Point (nur AP)</option><option value='sta'" + selected_attr(snapshot.device.wifiModePreference == UiWifiMode::StaOnly) + ">Heim-WLAN (nur STA)</option><option value='fallback'" + selected_attr(snapshot.device.wifiModePreference == UiWifiMode::StaWithApFallback) + ">Heim-WLAN + AP Fallback</option></select></div>";
        page += "<div class='info-list'><div class='info-row'><div class='info-label'>Aktuelles WLAN</div><div class='info-value' id='wifiSummaryValue'>" + html_escape(sta_summary(snapshot)) + "</div></div><div class='info-row'><div class='info-label'>IP Adresse</div><div class='info-value mono' id='wifiIpValue'>" + html_escape(current_ip_string(snapshot)) + "</div></div></div>";
        page += "<div class='button-row'><button type='button' class='btn btn-secondary' id='wifiScanBtn'>Netzwerke suchen</button><button type='button' class='btn btn-secondary' id='wifiDisconnectBtn'>Trennen</button></div><div class='field-note' id='wifiScanStatus'>Bereit</div><div id='wifiResults' class='hidden'><div id='wifiResultsList' class='device-list'></div><div id='wifiScanEmpty' class='device-empty'>Keine Netzwerke gefunden.</div></div>";
        page += "<div class='field-grid two'><div class='field'><label for='staSsid'>Gespeichertes STA SSID</label><input type='text' id='staSsid' name='staSsid' value='" + html_escape(snapshot.device.staSsid) + "'></div><div class='field'><label for='staPassword'>STA Passwort</label><input type='password' id='staPassword' name='staPassword' value='" + html_escape(snapshot.device.staPassword) + "' placeholder='Nur aendern wenn noetig'></div><div class='field'><label for='apSsid'>AP SSID</label><input type='text' id='apSsid' name='apSsid' value='" + html_escape(snapshot.device.apSsid) + "'></div><div class='field'><label for='apPassword'>AP Passwort</label><input type='password' id='apPassword' name='apPassword' value='" + html_escape(snapshot.device.apPassword) + "'></div></div></section></div>";

        page += "<div class='panel-grid two'><section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Fahrzeug</h2><div class='panel-copy'>Aktuelle Sim-Daten und Zustand bleiben kompakt sichtbar.</div></div></div><div class='info-list'>";
        page += "<div class='info-row'><div class='info-label'>Fahrzeug</div><div class='info-value' id='vehicleModel'>Desktop Simulator</div></div>";
        page += "<div class='info-row'><div class='info-label'>VIN</div><div class='info-value mono' id='vehicleVin'>SIM-LOCAL-8765</div></div>";
        page += "<div class='info-row'><div class='info-label'>Diagnose</div><div class='info-value' id='vehicleDiag'>" + html_escape(snapshot.runtime.telemetryStale ? "Warte auf Daten" : "Simulator live") + "</div></div>";
        page += "<div class='info-row'><div class='info-label'>Status</div><div class='info-value' id='vehicleStatus'>" + std::string(snapshot.runtime.telemetryStale ? "Warte auf Daten" : "Live") + "</div></div></div><div class='button-row'><button type='button' class='btn btn-secondary' id='btnVehicleRefresh'>Live-Werte aktualisieren</button></div></section>";

        page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Bluetooth / OBD</h2><div class='panel-copy'>Simulierte Adapterliste und aktuelles Ziel wie auf der Firmware-Seite.</div></div></div><div class='info-list'>";
        page += "<div class='info-row'><div class='info-label'>Zielgeraet</div><div class='info-value' id='bleTargetName'>" + html_escape(snapshot.bleTargetName) + "</div></div>";
        page += "<div class='info-row'><div class='info-label'>MAC</div><div class='info-value mono' id='bleTargetAddr'>" + html_escape(snapshot.bleTargetAddress) + "</div></div></div>";
        page += "<div class='button-row'><button type='button' class='btn btn-secondary' id='bleScanBtn'>BLE Geraete suchen</button><button type='button' class='btn btn-secondary' id='bleDisconnectBtn'>Trennen</button></div><div class='field-note' id='bleScanStatus'>Bereit</div><div id='bleResults' class='hidden'><div id='bleResultsList' class='device-list'></div><div id='bleScanEmpty' class='device-empty'>Keine Geraete gefunden.</div></div></section></div></form>";

        page += "<div class='savebar' id='settingsSaveBar' data-dirty='0'><div><div class='savebar-title'><span>Status</span></div><div class='savebar-copy'>Alles ist synchron.</div></div><div class='savebar-actions'><button type='button' class='btn btn-ghost' id='settingsReset'>Zuruecksetzen</button><button type='button' class='btn btn-primary' id='settingsSave' disabled>Speichern</button></div></div>";
        page += "<div class='toast" + std::string(savedNotice ? "" : " hidden") + "' id='savedToast'>Einstellungen wurden gespeichert.</div>";
        page += "<div id='wifiPasswordModal' class='hidden'><div class='panel' style='max-width:420px;margin:24px auto'><div class='panel-head'><div><h2 class='panel-title'>Mit WLAN verbinden</h2><div class='panel-copy'>Passwort fuer <span id='wifiModalSsid'></span></div></div></div><div class='field'><label for='wifiModalPassword'>Passwort</label><input type='password' id='wifiModalPassword' placeholder='WLAN Passwort'></div><div class='button-row'><button type='button' class='btn btn-secondary' id='wifiModalCancel'>Abbrechen</button><button type='button' class='btn btn-primary' id='wifiModalConnect'>Verbinden</button></div></div></div>";
        append_settings_script(page);
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
        if (values.find("maxRpmPerGearEnabled") != values.end())
        {
            ledConfig.maxRpmPerGearEnabled = parse_flag(values, "maxRpmPerGearEnabled", ledConfig.maxRpmPerGearEnabled);
        }
        ledConfig.fixedMaxRpm = clamp_int(parse_int_or(values, "fixedMaxRpm", ledConfig.fixedMaxRpm), 1000, 14000);
        for (size_t i = 0; i < kSimulatorGearCount; ++i)
        {
            const std::string key = "fixedMaxRpmGear" + std::to_string(i + 1);
            ledConfig.fixedMaxRpmByGear[i] = clamp_int(parse_int_or(values, key.c_str(), ledConfig.fixedMaxRpmByGear[i]), 1000, 14000);
        }
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

        SideLedConfig sideConfig = app.sideLedConfigSnapshot();
        if (values.find("sideEnabled") != values.end())
        {
            sideConfig.enabled = parse_flag(values, "sideEnabled", sideConfig.enabled);
        }
        const auto sidePresetIt = values.find("sidePreset");
        if (sidePresetIt != values.end())
        {
            sideConfig.preset = parse_side_led_preset_value(sidePresetIt->second, sideConfig.preset);
        }
        sideConfig.ledCountPerSide = static_cast<uint8_t>(clamp_int(parse_int_or(values, "sideLedCountPerSide", sideConfig.ledCountPerSide),
                                                                    static_cast<int>(SIDE_LED_MIN_COUNT_PER_SIDE),
                                                                    static_cast<int>(SIDE_LED_MAX_COUNT_PER_SIDE)));
        sideConfig.brightness = static_cast<uint8_t>(clamp_int(parse_int_or(values, "sideBrightness", sideConfig.brightness), 0, 255));
        if (values.find("sideAllowSpotter") != values.end())
        {
            sideConfig.allowSpotter = parse_flag(values, "sideAllowSpotter", sideConfig.allowSpotter);
        }
        if (values.find("sideAllowFlags") != values.end())
        {
            sideConfig.allowFlags = parse_flag(values, "sideAllowFlags", sideConfig.allowFlags);
        }
        if (values.find("sideAllowWarnings") != values.end())
        {
            sideConfig.allowWarnings = parse_flag(values, "sideAllowWarnings", sideConfig.allowWarnings);
        }
        if (values.find("sideAllowTraction") != values.end())
        {
            sideConfig.allowTraction = parse_flag(values, "sideAllowTraction", sideConfig.allowTraction);
        }
        sideConfig.allowSpotter = false;
        sideConfig.allowFlags = false;
        sideConfig.allowWarnings = false;
        sideConfig.blinkSpeedSlowMs = static_cast<uint16_t>(clamp_int(parse_int_or(values, "sideBlinkSpeedSlowMs", sideConfig.blinkSpeedSlowMs), 80, 1500));
        sideConfig.blinkSpeedFastMs = static_cast<uint16_t>(clamp_int(parse_int_or(values, "sideBlinkSpeedFastMs", sideConfig.blinkSpeedFastMs), 40, 900));
        const auto bluePriorityIt = values.find("sideBlueFlagPriority");
        if (bluePriorityIt != values.end())
        {
            sideConfig.blueFlagPriority = parse_side_led_priority_mode_value(bluePriorityIt->second, sideConfig.blueFlagPriority);
        }
        const auto yellowPriorityIt = values.find("sideYellowFlagPriority");
        if (yellowPriorityIt != values.end())
        {
            sideConfig.yellowFlagPriority = parse_side_led_priority_mode_value(yellowPriorityIt->second, sideConfig.yellowFlagPriority);
        }
        const auto warningPriorityIt = values.find("sideWarningPriorityMode");
        if (warningPriorityIt != values.end())
        {
            sideConfig.warningPriorityMode = parse_side_led_warning_priority_mode_value(warningPriorityIt->second, sideConfig.warningPriorityMode);
        }
        if (values.find("sideInvertLeftRight") != values.end())
        {
            sideConfig.invertLeftRight = parse_flag(values, "sideInvertLeftRight", sideConfig.invertLeftRight);
        }
        if (values.find("sideMirrorMode") != values.end())
        {
            sideConfig.mirrorMode = parse_flag(values, "sideMirrorMode", sideConfig.mirrorMode);
        }
        if (values.find("sideCloseCarBlinkingEnabled") != values.end())
        {
            sideConfig.closeCarBlinkingEnabled = parse_flag(values, "sideCloseCarBlinkingEnabled", sideConfig.closeCarBlinkingEnabled);
        }
        if (values.find("sideSeverityLevelsEnabled") != values.end())
        {
            sideConfig.severityLevelsEnabled = parse_flag(values, "sideSeverityLevelsEnabled", sideConfig.severityLevelsEnabled);
        }
        if (values.find("sideIdleAnimationEnabled") != values.end())
        {
            sideConfig.idleAnimationEnabled = parse_flag(values, "sideIdleAnimationEnabled", sideConfig.idleAnimationEnabled);
        }
        if (values.find("sideTestMode") != values.end())
        {
            sideConfig.testMode = parse_flag(values, "sideTestMode", sideConfig.testMode);
        }
        app.applySideLedConfig(sideConfig);

        UiSettings settings = app.stateSnapshot().settings;
        settings.displayBrightness = clamp_int(parse_int_or(values, "displayBrightness", settings.displayBrightness), 10, 255);
        if (values.find("nightMode") != values.end())
        {
            settings.nightMode = parse_flag(values, "nightMode", settings.nightMode);
        }
        if (values.find("showShiftStrip") != values.end())
        {
            settings.showShiftStrip = parse_flag(values, "showShiftStrip", settings.showShiftStrip);
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
        const uint32_t nowMs = app.stateSnapshot().telemetryTimestampMs;
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
        else if (action == "side_test_clear")
        {
            app.clearSideLedTest();
        }
        else
        {
            const SideLedTestPattern pattern = parse_side_led_test_pattern_value(action);
            if (pattern != SideLedTestPattern::None)
            {
                app.triggerSideLedTest(pattern, nowMs);
            }
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

        if (request.method == "POST" && request.path == "/wifi/scan")
        {
            return http_response("200 OK", "application/json", "{\"status\":\"started\"}");
        }

        if (request.method == "POST" && request.path == "/wifi/connect")
        {
            const auto values = parse_form_urlencoded(request.body);
            const std::string ssid = parse_string_or(values, "ssid", app.deviceConfigSnapshot().staSsid);
            const std::string password = parse_string_or(values, "password", app.deviceConfigSnapshot().staPassword);
            app.connectWifi(ssid, password);
            return http_response("200 OK", "application/json", "{\"status\":\"started\"}");
        }

        if (request.method == "POST" && request.path == "/wifi/disconnect")
        {
            app.disconnectWifi();
            return http_response("200 OK", "application/json", "{\"status\":\"ok\"}");
        }

        if (request.method == "GET" && request.path == "/ble/status")
        {
            return http_response("200 OK", "application/json", build_ble_status_json(app.statusSnapshot()));
        }

        if (request.method == "POST" && request.path == "/ble/scan")
        {
            return http_response("200 OK", "application/json", "{\"status\":\"started\"}");
        }

        if (request.method == "POST" && request.path == "/ble/connect-device")
        {
            const auto values = parse_form_urlencoded(request.body);
            const std::string address = parse_string_or(values, "address", app.statusSnapshot().bleTargetAddress);
            const std::string name = parse_string_or(values, "name", app.statusSnapshot().bleTargetName);
            app.connectBleDevice(name, address);
            return http_response("200 OK", "application/json", "{\"status\":\"queued\"}");
        }

        if (request.method == "POST" && request.path == "/ble/disconnect")
        {
            app.disconnectBle();
            return http_response("200 OK", "application/json", "{\"status\":\"ok\"}");
        }

        if (request.method == "POST" && request.path == "/settings/vehicle-refresh")
        {
            return http_response("200 OK", "application/json", "{\"status\":\"started\"}");
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
