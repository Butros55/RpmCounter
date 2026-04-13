#include "usb_sim_bridge.h"

#include <Arduino.h>

#include "core/config.h"
#include "core/logging.h"
#include "core/state.h"
#include "core/utils.h"
#include "core/wifi.h"
#include "web/web_helpers.h"

namespace
{
    constexpr unsigned long USB_BRIDGE_HEARTBEAT_TIMEOUT_MS = 1600;
    constexpr unsigned long USB_TELEMETRY_FRESH_TIMEOUT_MS = 2000;
    constexpr unsigned long USB_BRIDGE_STICKY_TIMEOUT_MS = 5000;
    constexpr size_t USB_LINE_BUFFER_LIMIT = 384;

    String g_usbRxLine;
    unsigned long g_lastHelloTxMs = 0;

    Stream &usbStream()
    {
        return Serial;
    }

    bool usbSerialConnected()
    {
        return g_usbBridgeConnected || static_cast<bool>(Serial);
    }

    String telemetryPreferenceLabel(TelemetryPreference preference)
    {
        switch (preference)
        {
        case TelemetryPreference::Obd:
            return F("OBD");
        case TelemetryPreference::SimHub:
            return F("SIM");
        case TelemetryPreference::Auto:
        default:
            return F("AUTO");
        }
    }

    String simTransportLabel(SimTransportPreference preference)
    {
        switch (preference)
        {
        case SimTransportPreference::UsbSerial:
            return F("USB");
        case SimTransportPreference::Network:
            return F("NETWORK");
        case SimTransportPreference::Auto:
        default:
            return F("AUTO");
        }
    }

    String displayFocusLabel(DisplayFocusMetric metric)
    {
        switch (metric)
        {
        case DisplayFocusMetric::Gear:
            return F("GEAR");
        case DisplayFocusMetric::Speed:
            return F("SPEED");
        case DisplayFocusMetric::Rpm:
        default:
            return F("RPM");
        }
    }

    String activeTelemetryLabel(ActiveTelemetrySource source)
    {
        switch (source)
        {
        case ActiveTelemetrySource::Obd:
            return F("OBD");
        case ActiveTelemetrySource::SimHubNetwork:
            return F("SIM_NET");
        case ActiveTelemetrySource::UsbSim:
            return F("USB_SIM");
        case ActiveTelemetrySource::None:
        default:
            return F("NONE");
        }
    }

    String usbStateLabel(UsbBridgeConnectionState state)
    {
        switch (state)
        {
        case UsbBridgeConnectionState::Disconnected:
            return F("DISCONNECTED");
        case UsbBridgeConnectionState::WaitingForBridge:
            return F("WAITING_BRIDGE");
        case UsbBridgeConnectionState::WaitingForData:
            return F("WAITING_DATA");
        case UsbBridgeConnectionState::Live:
            return F("LIVE");
        case UsbBridgeConnectionState::Error:
            return F("ERROR");
        case UsbBridgeConnectionState::Disabled:
        default:
            return F("DISABLED");
        }
    }

    String urlDecode(const String &value)
    {
        String out;
        out.reserve(value.length());
        for (size_t i = 0; i < value.length(); ++i)
        {
            const char ch = value[i];
            if (ch == '+')
            {
                out += ' ';
                continue;
            }
            if (ch == '%' && i + 2 < value.length())
            {
                const String hex = value.substring(i + 1, i + 3);
                out += static_cast<char>(strtol(hex.c_str(), nullptr, 16));
                i += 2;
                continue;
            }
            out += ch;
        }
        return out;
    }

    String queryValue(const String &payload, const char *key)
    {
        const String needle = String(key) + "=";
        const int start = payload.indexOf(needle);
        if (start < 0)
        {
            return String();
        }
        const int valueStart = start + needle.length();
        int end = payload.indexOf('&', valueStart);
        if (end < 0)
        {
            end = payload.length();
        }
        return urlDecode(payload.substring(valueStart, end));
    }

    bool queryBool(const String &payload, const char *key, bool fallback)
    {
        const String value = queryValue(payload, key);
        if (value.isEmpty())
        {
            return fallback;
        }
        return !(value == "0" || value == "false" || value == "FALSE" || value == "off");
    }

    int queryInt(const String &payload, const char *key, int fallback, int minValue, int maxValue)
    {
        const String value = queryValue(payload, key);
        if (value.isEmpty())
        {
            return fallback;
        }
        return clampInt(value.toInt(), minValue, maxValue);
    }

    float kvFloat(const String &payload, const char *key, float fallback)
    {
        const String value = queryValue(payload, key);
        if (value.isEmpty())
        {
            return fallback;
        }
        return value.toFloat();
    }

    void setUsbState(UsbBridgeConnectionState state, const String &error = String())
    {
        g_usbBridgeConnectionState = state;
        g_usbBridgeLastError = error;
    }

    void sendProtocolLine(const String &line)
    {
        usbStream().println(line);
        usbStream().flush();
    }

    void sendRpcResponse(int requestId, const String &json)
    {
        sendProtocolLine(String(F("USBSIM RES ")) + String(requestId) + " " + json);
    }

    void sendHelloFrame()
    {
        const String payload =
            String(F("device=ShiftLight")) +
            F("&state=") + usbStateLabel(g_usbBridgeConnectionState) +
            F("&activeTelemetry=") + activeTelemetryLabel(g_activeTelemetrySource) +
            F("&web=1");
        sendProtocolLine(String(F("USBSIM HELLO ")) + payload);
        g_lastHelloTxMs = millis();
    }

    void applyConfigPayload(const String &payload)
    {
        cfg.telemetryPreference = static_cast<TelemetryPreference>(
            queryInt(payload, "telemetryPreference", static_cast<int>(cfg.telemetryPreference), 0, 2));
        cfg.simTransportPreference = static_cast<SimTransportPreference>(
            queryInt(payload, "simTransport", static_cast<int>(cfg.simTransportPreference), 0, 2));
        cfg.uiDisplayFocus = static_cast<DisplayFocusMetric>(
            queryInt(payload, "displayFocus", static_cast<int>(cfg.uiDisplayFocus), 0, 2));
        cfg.displayBrightness = queryInt(payload, "displayBrightness", cfg.displayBrightness, 10, 255);
        cfg.uiNightMode = queryBool(payload, "nightMode", cfg.uiNightMode);
        cfg.useMph = queryBool(payload, "useMph", cfg.useMph);

        const String host = queryValue(payload, "simHubHost");
        if (!host.isEmpty())
        {
            cfg.simHubHost = host;
        }
        cfg.simHubPort = static_cast<uint16_t>(queryInt(payload, "simHubPort", cfg.simHubPort, 1, 65535));
        cfg.simHubPollMs = static_cast<uint16_t>(queryInt(payload, "simHubPollMs", cfg.simHubPollMs, 25, 1000));

        saveConfig();
    }

    void refreshUsbFlags(unsigned long nowMs)
    {
        g_usbSerialConnected = usbSerialConnected();

        if ((nowMs - g_lastUsbBridgeHeartbeatMs) > USB_BRIDGE_HEARTBEAT_TIMEOUT_MS)
        {
            g_usbBridgeConnected = false;
            g_usbBridgeWebActive = false;
        }

        if (!usbSimTransportEnabled())
        {
            setUsbState(UsbBridgeConnectionState::Disabled);
            return;
        }

        if (!g_usbSerialConnected)
        {
            setUsbState(UsbBridgeConnectionState::Disconnected);
            return;
        }

        if (!g_usbBridgeConnected)
        {
            setUsbState(UsbBridgeConnectionState::WaitingForBridge);
            return;
        }

        if (usbSimTelemetryFresh(nowMs))
        {
            setUsbState(UsbBridgeConnectionState::Live);
            return;
        }

        if (g_usbTelemetryEverReceived)
        {
            setUsbState(UsbBridgeConnectionState::Error, F("Telemetry stale"));
        }
        else
        {
            setUsbState(UsbBridgeConnectionState::WaitingForData);
        }
    }

    bool usbBridgeRecentlyActive(unsigned long nowMs)
    {
        if (g_usbBridgeConnected)
        {
            return true;
        }
        if (g_lastUsbBridgeHeartbeatMs > 0 && (nowMs - g_lastUsbBridgeHeartbeatMs) <= USB_BRIDGE_STICKY_TIMEOUT_MS)
        {
            return true;
        }
        if (g_lastUsbTelemetryMs > 0 && (nowMs - g_lastUsbTelemetryMs) <= USB_BRIDGE_STICKY_TIMEOUT_MS)
        {
            return true;
        }
        return false;
    }

    void handleTelemetryMessage(const String &payload)
    {
        g_simHubCurrentRpm = max(0, queryInt(payload, "rpm", g_simHubCurrentRpm, 0, 20000));
        g_simHubVehicleSpeedKmh = max(0, queryInt(payload, "speed", g_simHubVehicleSpeedKmh, 0, 600));
        g_simHubGear = max(0, queryInt(payload, "gear", g_simHubGear, 0, 20));
        g_simHubThrottle = constrain(kvFloat(payload, "throttle", g_simHubThrottle), 0.0f, 1.0f);
        g_simHubPitLimiterActive = queryBool(payload, "pit", g_simHubPitLimiterActive);
        const int reportedMaxRpm = queryInt(payload, "maxrpm", g_simHubMaxSeenRpm, 0, 25000);
        g_simHubMaxSeenRpm = max(reportedMaxRpm, max(g_simHubMaxSeenRpm, g_simHubCurrentRpm));
        g_lastSimHubTelemetryMs = millis();
        g_lastUsbTelemetryMs = g_lastSimHubTelemetryMs;
        g_simHubEverReceived = true;
        g_usbTelemetryEverReceived = true;
        g_usbBridgeConnected = true;
        setUsbState(UsbBridgeConnectionState::Live);
    }

    void handlePingMessage(const String &payload)
    {
        g_lastUsbBridgeHeartbeatMs = millis();
        g_usbBridgeConnected = true;
        g_usbBridgeHost = queryValue(payload, "host");
        g_usbBridgeWebActive = queryBool(payload, "web", g_usbBridgeWebActive);
        if (g_usbBridgeHost.isEmpty())
        {
            g_usbBridgeHost = F("USB Bridge");
        }
        sendHelloFrame();
    }

    void handleRpcMessage(const String &payload)
    {
        const int firstSpace = payload.indexOf(' ');
        if (firstSpace < 0)
        {
            return;
        }

        const int requestId = payload.substring(0, firstSpace).toInt();
        String rest = payload.substring(firstSpace + 1);
        rest.trim();

        int secondSpace = rest.indexOf(' ');
        String command = secondSpace >= 0 ? rest.substring(0, secondSpace) : rest;
        const String commandPayload = secondSpace >= 0 ? rest.substring(secondSpace + 1) : String();

        g_lastUsbBridgeHeartbeatMs = millis();
        g_lastUsbRpcMs = g_lastUsbBridgeHeartbeatMs;
        g_usbBridgeConnected = true;
        g_usbBridgeWebActive = true;

        if (command == "STATUS")
        {
            sendRpcResponse(requestId, usbSimBuildStatusJson());
            return;
        }
        if (command == "CONFIG_GET")
        {
            sendRpcResponse(requestId, usbSimBuildConfigJson());
            return;
        }
        if (command == "SET")
        {
            applyConfigPayload(commandPayload);
            sendRpcResponse(requestId, usbSimBuildConfigJson());
            return;
        }

        sendRpcResponse(requestId, String(F("{\"ok\":false,\"error\":\"unknown-command\"}")));
    }

    void handleProtocolLine(const String &line)
    {
        if (!line.startsWith("USBSIM "))
        {
            return;
        }

        String rest = line.substring(7);
        rest.trim();

        const int firstSpace = rest.indexOf(' ');
        String command = firstSpace >= 0 ? rest.substring(0, firstSpace) : rest;
        const String payload = firstSpace >= 0 ? rest.substring(firstSpace + 1) : String();

        if (command == "PING" || command == "HELLO")
        {
            handlePingMessage(payload);
            return;
        }
        if (command == "TELEMETRY")
        {
            handleTelemetryMessage(payload);
            return;
        }
        if (command == "RPC")
        {
            handleRpcMessage(payload);
            return;
        }
    }
}

void initUsbSimBridge()
{
    g_usbRxLine.reserve(USB_LINE_BUFFER_LIMIT);
    refreshUsbFlags(millis());
}

void usbSimBridgeUpdateConfig()
{
    refreshUsbFlags(millis());
    setWifiSuspendedForUsb(usbSimShouldSuspendWifi());
}

void usbSimBridgeLoop()
{
    Stream &stream = usbStream();
    while (stream.available() > 0)
    {
        const char ch = static_cast<char>(stream.read());
        if (ch == '\r')
        {
            continue;
        }
        if (ch == '\n')
        {
            if (!g_usbRxLine.isEmpty())
            {
                handleProtocolLine(g_usbRxLine);
                g_usbRxLine = "";
            }
            continue;
        }

        if (g_usbRxLine.length() < USB_LINE_BUFFER_LIMIT)
        {
            g_usbRxLine += ch;
        }
        else
        {
            g_usbRxLine = "";
            setUsbState(UsbBridgeConnectionState::Error, F("Line overflow"));
        }
    }

    refreshUsbFlags(millis());
    if (g_usbBridgeConnected && millis() - g_lastHelloTxMs > 900)
    {
        sendHelloFrame();
    }
}

bool usbSimTransportEnabled()
{
    return cfg.telemetryPreference != TelemetryPreference::Obd &&
           cfg.simTransportPreference != SimTransportPreference::Network;
}

bool usbSimBridgeOnline()
{
    return g_usbBridgeConnected;
}

bool usbSimTelemetryFresh(unsigned long nowMs)
{
    return g_usbTelemetryEverReceived &&
           g_lastUsbTelemetryMs > 0 &&
           (nowMs - g_lastUsbTelemetryMs) <= USB_TELEMETRY_FRESH_TIMEOUT_MS;
}

bool usbSimShouldBlockObd()
{
    const unsigned long nowMs = millis();
    const bool usbStickyActive = usbBridgeRecentlyActive(nowMs);

    if (cfg.telemetryPreference == TelemetryPreference::SimHub)
    {
        return cfg.simTransportPreference != SimTransportPreference::Network && usbStickyActive;
    }

    if (cfg.telemetryPreference == TelemetryPreference::Auto)
    {
        return cfg.simTransportPreference != SimTransportPreference::Network && usbStickyActive;
    }

    return false;
}

bool usbSimShouldSuspendWifi()
{
    const unsigned long nowMs = millis();
    const bool usbStickyActive = usbBridgeRecentlyActive(nowMs);

    if (cfg.simTransportPreference == SimTransportPreference::Network)
    {
        return false;
    }

    if (cfg.telemetryPreference == TelemetryPreference::SimHub)
    {
        return usbStickyActive;
    }

    if (cfg.telemetryPreference == TelemetryPreference::Auto)
    {
        return usbStickyActive;
    }

    return false;
}

String usbSimBuildStatusJson()
{
    String json = "{";
    json += "\"ok\":true";
    json += ",\"rpm\":" + String(g_currentRpm);
    json += ",\"maxRpm\":" + String(g_maxSeenRpm);
    json += ",\"speed\":" + String(g_vehicleSpeedKmh);
    json += ",\"gear\":" + String(g_estimatedGear);
    json += ",\"throttle\":" + String(g_currentThrottle, 3);
    json += ",\"pitLimiter\":" + String(g_pitLimiterActive ? "true" : "false");
    json += ",\"activeTelemetry\":\"" + activeTelemetryLabel(g_activeTelemetrySource) + "\"";
    json += ",\"telemetryPreference\":\"" + telemetryPreferenceLabel(cfg.telemetryPreference) + "\"";
    json += ",\"simTransport\":\"" + simTransportLabel(cfg.simTransportPreference) + "\"";
    json += ",\"usbState\":\"" + usbStateLabel(g_usbBridgeConnectionState) + "\"";
    json += ",\"usbConnected\":" + String(g_usbSerialConnected ? "true" : "false");
    json += ",\"usbBridgeConnected\":" + String(g_usbBridgeConnected ? "true" : "false");
    json += ",\"usbBridgeWebActive\":" + String(g_usbBridgeWebActive ? "true" : "false");
    json += ",\"usbHost\":\"" + jsonEscape(g_usbBridgeHost) + "\"";
    json += ",\"usbError\":\"" + jsonEscape(g_usbBridgeLastError) + "\"";
    json += ",\"displayFocus\":\"" + displayFocusLabel(cfg.uiDisplayFocus) + "\"";
    json += ",\"displayBrightness\":" + String(cfg.displayBrightness);
    json += ",\"nightMode\":" + String(cfg.uiNightMode ? "true" : "false");
    json += ",\"useMph\":" + String(cfg.useMph ? "true" : "false");
    json += ",\"wifiSuspended\":" + String(isWifiSuspendedForUsb() ? "true" : "false");
    json += ",\"bleConnected\":" + String(g_connected ? "true" : "false");
    json += ",\"bleBlocked\":" + String(usbSimShouldBlockObd() ? "true" : "false");
    json += ",\"obdAllowed\":" + String(usbSimShouldBlockObd() ? "false" : "true");
    json += "}";
    return json;
}

String usbSimBuildConfigJson()
{
    String json = "{";
    json += "\"ok\":true";
    json += ",\"telemetryPreference\":" + String(static_cast<int>(cfg.telemetryPreference));
    json += ",\"simTransport\":" + String(static_cast<int>(cfg.simTransportPreference));
    json += ",\"displayFocus\":" + String(static_cast<int>(cfg.uiDisplayFocus));
    json += ",\"displayBrightness\":" + String(cfg.displayBrightness);
    json += ",\"nightMode\":" + String(cfg.uiNightMode ? "true" : "false");
    json += ",\"useMph\":" + String(cfg.useMph ? "true" : "false");
    json += ",\"simHubHost\":\"" + jsonEscape(cfg.simHubHost) + "\"";
    json += ",\"simHubPort\":" + String(cfg.simHubPort);
    json += ",\"simHubPollMs\":" + String(cfg.simHubPollMs);
    json += "}";
    return json;
}
