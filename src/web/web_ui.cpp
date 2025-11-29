/**
 * web_ui.cpp - Web Server with static file serving from LittleFS
 *
 * This is the refactored version that serves HTML/CSS/JS from the filesystem
 * instead of building them as C++ string concatenations.
 *
 * Replace your existing web_ui.cpp with this file.
 */

#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "bluetooth/ble_obd.h"
#include "core/config.h"
#include "core/wifi.h"
#include "hardware/led_bar.h"
#include "hardware/logo_anim.h"
#include "core/vehicle_info.h"
#include "core/state.h"
#include "hardware/display.h"
#include "core/logging.h"
#include <core/utils.h>

namespace
{
    WebServer server(80);
    bool g_filesystemReady = false;

    const char *MIME_HTML = "text/html";
    const char *MIME_CSS = "text/css";
    const char *MIME_JS = "application/javascript";
    const char *MIME_JSON = "application/json";

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

    String argTrimmed(const char *name, const String &fallback)
    {
        String value = server.hasArg(name) ? server.arg(name) : fallback;
        value.trim();
        return value;
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

    String safeLabel(const String &value, const String &fallback)
    {
        String trimmed = value;
        trimmed.trim();
        if (trimmed.isEmpty())
            return fallback;
        return trimmed;
    }

    void enforceOrder(int &g, int &y, int &b)
    {
        g = clampInt(g, 0, 100);
        if (y < g)
            y = g;
        y = clampInt(y, 0, 100);
        if (b < y)
            b = y;
        b = clampInt(b, 0, 100);
    }

    // Serve a static file from LittleFS
    bool serveStaticFile(const char *path, const char *contentType)
    {
        if (!g_filesystemReady)
        {
            server.send(500, "text/plain", "Filesystem not ready");
            return false;
        }

        File file = LittleFS.open(path, "r");
        if (!file)
        {
            LOG_ERROR("WEB", "WEB_FILE_404", String("File not found: ") + path);
            server.send(404, "text/plain", "File not found");
            return false;
        }

        server.streamFile(file, contentType);
        file.close();
        return true;
    }

    // === HANDLERS ===

    void handleRoot()
    {
        markHttpActivity("WEB_ROOT");
        serveStaticFile("/index.html", MIME_HTML);
    }

    void handleBrightness()
    {
        markHttpActivity("WEB_BRIGHTNESS");
        if (server.hasArg("val"))
        {
            int v = clampInt(server.arg("val").toInt(), 0, 255);
            cfg.brightness = v;
            strip.setBrightness(cfg.brightness);
            strip.show();
            g_brightnessPreviewActive = true;
            g_lastBrightnessChangeMs = millis();
            showMLogoPreview();
        }
        server.send(200, "text/plain", "OK");
    }

    void applyConfigFromRequest(bool allowAutoReconnect)
    {
        if (server.hasArg("mode"))
        {
            int m = server.arg("mode").toInt();
            if (m < 0 || m > 2)
                m = 0;
            cfg.mode = m;
        }
        if (server.hasArg("brightness"))
        {
            cfg.brightness = clampInt(server.arg("brightness").toInt(), 0, 255);
            strip.setBrightness(cfg.brightness);
            strip.show();
        }

        cfg.autoScaleMaxRpm = server.hasArg("autoscale");
        if (server.hasArg("fixedMaxRpm"))
            cfg.fixedMaxRpm = clampInt(server.arg("fixedMaxRpm").toInt(), 1000, 8000);

        int gPct = cfg.greenEndPct;
        int yPct = cfg.yellowEndPct;
        int bPct = cfg.blinkStartPct;
        if (server.hasArg("greenEndPct"))
            gPct = server.arg("greenEndPct").toInt();
        if (server.hasArg("yellowEndPct"))
            yPct = server.arg("yellowEndPct").toInt();
        if (server.hasArg("blinkStartPct"))
            bPct = server.arg("blinkStartPct").toInt();
        enforceOrder(gPct, yPct, bPct);
        cfg.greenEndPct = gPct;
        cfg.yellowEndPct = yPct;
        cfg.blinkStartPct = bPct;

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
            g_testActive = true;
            g_testStartMs = millis();
            if (cfg.autoScaleMaxRpm)
                g_testMaxRpm = (g_maxSeenRpm > 0) ? g_maxSeenRpm : 4000;
            else
                g_testMaxRpm = (cfg.fixedMaxRpm > 0) ? cfg.fixedMaxRpm : 4000;
            server.send(200, "text/plain", "OK");
            return;
        }
        server.send(405, "text/plain", "Method Not Allowed");
    }

    void handleConnect()
    {
        markHttpActivity("WEB_CONNECT");
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
        if (g_vehicleInfoAvailable && g_vehicleInfoLastUpdate > 0)
            vehicleAge = static_cast<int>((now - g_vehicleInfoLastUpdate) / 1000UL);

        String json = "{";
        json += "\"rpm\":" + String(g_currentRpm);
        json += ",\"maxRpm\":" + String(g_maxSeenRpm);
        json += ",\"speed\":" + String(g_vehicleSpeedKmh);
        json += ",\"gear\":" + String(g_estimatedGear);
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
        json += ",\"bleConnectInProgress\":" + String(g_bleConnectInProgress ? "true" : "false");
        json += ",\"bleConnectTargetAddr\":\"" + jsonEscape(g_bleConnectTargetAddr) + "\"";
        json += ",\"bleConnectTargetName\":\"" + jsonEscape(g_bleConnectTargetName) + "\"";
        json += ",\"bleConnectError\":\"" + jsonEscape(g_bleConnectLastError) + "\"";
        json += "}";
        server.send(200, MIME_JSON, json);
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
        server.send(200, MIME_JSON, json);
    }

    void handleBleScan()
    {
        markHttpActivity("WEB_BLE_SCAN");
        if (g_bleConnectInProgress || g_connectTaskRunning || g_manualConnectActive)
        {
            server.send(200, MIME_JSON, "{\"status\":\"busy\",\"reason\":\"connecting\"}");
            return;
        }
        bool started = startBleScan();
        if (started)
            server.send(200, MIME_JSON, "{\"status\":\"started\"}");
        else
            server.send(200, MIME_JSON, "{\"status\":\"busy\"}");
    }

    void handleBleConnectDevice()
    {
        markHttpActivity("WEB_BLE_CONNECT_DEVICE");
        if (!server.hasArg("address"))
        {
            server.send(400, MIME_JSON, "{\"status\":\"error\",\"reason\":\"missing-address\"}");
            return;
        }

        String address = server.arg("address");
        String name = server.hasArg("name") ? server.arg("name") : "";
        int attempts = server.hasArg("attempts") ? server.arg("attempts").toInt() : MANUAL_CONNECT_RETRY_COUNT;

        requestManualConnect(address, name, attempts);
        server.send(200, MIME_JSON, "{\"status\":\"queued\"}");
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
        server.send(200, MIME_JSON, json);
    }

    void handleWifiScan()
    {
        markHttpActivity("WEB_WIFI_SCAN");
        bool started = startWifiScan();
        if (started)
            server.send(200, MIME_JSON, "{\"status\":\"started\"}");
        else
            server.send(200, MIME_JSON, "{\"status\":\"busy\"}");
    }

    void handleWifiConnect()
    {
        markHttpActivity("WEB_WIFI_CONNECT");
        if (!server.hasArg("ssid"))
        {
            server.send(400, MIME_JSON, "{\"status\":\"error\",\"reason\":\"missing-ssid\"}");
            return;
        }

        String ssid = argTrimmed("ssid", "");
        String password = argTrimmed("password", "");
        WifiMode mode = cfg.wifiMode;
        WifiStatus st = getWifiStatus();
        if (st.staConnecting)
        {
            server.send(200, MIME_JSON, "{\"status\":\"busy\",\"reason\":\"connecting\"}");
            return;
        }
        if (st.scanRunning)
        {
            server.send(200, MIME_JSON, "{\"status\":\"busy\",\"reason\":\"scan-running\"}");
            return;
        }
        if (server.hasArg("mode"))
        {
            mode = static_cast<WifiMode>(clampInt(server.arg("mode").toInt(), 0, 2));
        }

        bool started = requestWifiConnect(ssid, password, mode);
        if (started)
        {
            server.send(200, MIME_JSON, "{\"status\":\"started\"}");
        }
        else
        {
            server.send(200, MIME_JSON, "{\"status\":\"error\",\"reason\":\"connect-not-started\"}");
        }
    }

    void handleWifiDisconnect()
    {
        markHttpActivity("WEB_WIFI_DISCONNECT");
        requestWifiDisconnect();
        server.send(200, MIME_JSON, "{\"status\":\"ok\"}");
    }

    void handleWifiSave()
    {
        markHttpActivity("WEB_WIFI_SAVE");
        applyWifiConfigFromRequest();
        saveConfig();
        setupWifiFromConfig(cfg);
        server.send(200, MIME_JSON, "{\"status\":\"ok\"}");
    }

    void handleSettingsGet()
    {
        markHttpActivity("WEB_SETTINGS_GET");
        serveStaticFile("/settings.html", MIME_HTML);
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

        applyWifiConfigFromRequest();
        saveConfig();
        setupWifiFromConfig(cfg);

        server.sendHeader("Location", "/settings", true);
        server.send(303, "text/plain", "Redirect");
    }

    void handleSettingsVehicleRefresh()
    {
        markHttpActivity("WEB_SETTINGS_VEHICLE_REFRESH");
        if (!g_connected)
        {
            server.send(200, MIME_JSON, "{\"status\":\"error\",\"reason\":\"no-connection\"}");
            return;
        }
        requestVehicleInfo(true);
        server.send(200, MIME_JSON, "{\"status\":\"started\"}");
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

        server.send(200, MIME_JSON, json);
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
        server.send(200, MIME_JSON, "{\"status\":\"ok\"}");
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

        String prevObd = g_lastObdInfo;
        unsigned long start = millis();
        const unsigned long timeoutMs = 800;

        sendObdCommand(cmd);

        while (millis() - start < timeoutMs)
        {
            if (g_lastObdInfo != prevObd)
            {
                break;
            }
            delay(10);
            yield();
        }

        String json = "{";
        json += "\"status\":\"ok\"";
        json += ",\"lastTx\":\"" + jsonEscape(g_lastTxInfo) + "\"";
        json += ",\"lastObd\":\"" + jsonEscape(g_lastObdInfo) + "\"";
        json += "}";

        server.send(200, MIME_JSON, json);
    }

    // API endpoint to get initial config data for the frontend
    void handleApiConfig()
    {
        markHttpActivity("WEB_API_CONFIG");

        String json = "{";
        // Main config
        json += "\"mode\":" + String(cfg.mode);
        json += ",\"brightness\":" + String(cfg.brightness);
        json += ",\"autoScaleMaxRpm\":" + String(cfg.autoScaleMaxRpm ? "true" : "false");
        json += ",\"fixedMaxRpm\":" + String(cfg.fixedMaxRpm);
        json += ",\"greenEndPct\":" + String(cfg.greenEndPct);
        json += ",\"yellowEndPct\":" + String(cfg.yellowEndPct);
        json += ",\"blinkStartPct\":" + String(cfg.blinkStartPct);
        json += ",\"greenColor\":\"" + colorToHex(cfg.greenColor) + "\"";
        json += ",\"yellowColor\":\"" + colorToHex(cfg.yellowColor) + "\"";
        json += ",\"redColor\":\"" + colorToHex(cfg.redColor) + "\"";
        json += ",\"greenLabel\":\"" + jsonEscape(safeLabel(cfg.greenLabel, "Farbe 1")) + "\"";
        json += ",\"yellowLabel\":\"" + jsonEscape(safeLabel(cfg.yellowLabel, "Farbe 2")) + "\"";
        json += ",\"redLabel\":\"" + jsonEscape(safeLabel(cfg.redLabel, "Farbe 3")) + "\"";
        json += ",\"logoOnIgnitionOn\":" + String(cfg.logoOnIgnitionOn ? "true" : "false");
        json += ",\"logoOnEngineStart\":" + String(cfg.logoOnEngineStart ? "true" : "false");
        json += ",\"logoOnIgnitionOff\":" + String(cfg.logoOnIgnitionOff ? "true" : "false");

        // WiFi config
        json += ",\"wifiMode\":" + String(static_cast<int>(cfg.wifiMode));

        // Constants
        json += ",\"numLeds\":" + String(NUM_LEDS);
        json += ",\"testSweepDuration\":" + String(TEST_SWEEP_DURATION);
        json += ",\"manualConnectRetryCount\":" + String(MANUAL_CONNECT_RETRY_COUNT);

        // Runtime state
        json += ",\"devMode\":" + String(g_devMode ? "true" : "false");
        json += ",\"autoReconnect\":" + String(g_autoReconnect ? "true" : "false");
        json += ",\"connected\":" + String(g_connected ? "true" : "false");
        json += ",\"vehicleModel\":\"" + jsonEscape(g_vehicleModel) + "\"";
        json += ",\"vehicleVin\":\"" + jsonEscape(g_vehicleVin) + "\"";
        json += ",\"vehicleDiag\":\"" + jsonEscape(g_vehicleDiagStatus) + "\"";
        json += ",\"currentTargetName\":\"" + jsonEscape(g_currentTargetName) + "\"";
        json += ",\"currentTargetAddr\":\"" + jsonEscape(g_currentTargetAddr) + "\"";
        json += ",\"currentRpm\":" + String(g_currentRpm);
        json += ",\"maxSeenRpm\":" + String(g_maxSeenRpm);
        json += ",\"lastTx\":\"" + jsonEscape(g_lastTxInfo) + "\"";
        json += ",\"lastObd\":\"" + jsonEscape(g_lastObdInfo) + "\"";
        json += "}";

        server.send(200, MIME_JSON, json);
    }

    void handleNotFound()
    {
        markHttpActivity("WEB_NOT_FOUND");
        server.send(404, "text/plain", "Not found");
    }
}

void initWebUi()
{
    // Initialize LittleFS
    if (!LittleFS.begin(true))
    {
        LOG_ERROR("WEB", "WEB_FS_FAIL", "LittleFS mount failed");
        g_filesystemReady = false;
    }
    else
    {
        LOG_INFO("WEB", "WEB_FS_OK", "LittleFS mounted successfully");
        g_filesystemReady = true;
    }

    // Serve static assets with caching
    server.serveStatic("/assets/style.css", LittleFS, "/assets/style.css", "max-age=86400");
    server.serveStatic("/assets/main.js", LittleFS, "/assets/main.js", "max-age=86400");
    server.serveStatic("/assets/settings.js", LittleFS, "/assets/settings.js", "max-age=86400");

    // HTML pages
    server.on("/", HTTP_GET, handleRoot);

    // Main page API
    server.on("/brightness", HTTP_GET, handleBrightness);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/test", HTTP_POST, handleTest);
    server.on("/connect", HTTP_POST, handleConnect);
    server.on("/disconnect", HTTP_POST, handleDisconnect);
    server.on("/status", HTTP_GET, handleStatus);

    // BLE API
    server.on("/ble/status", HTTP_GET, handleBleStatus);
    server.on("/ble/scan", HTTP_POST, handleBleScan);
    server.on("/ble/connect-device", HTTP_POST, handleBleConnectDevice);

    // WiFi API
    server.on("/wifi/status", HTTP_GET, handleWifiStatus);
    server.on("/wifi/scan", HTTP_POST, handleWifiScan);
    server.on("/wifi/connect", HTTP_POST, handleWifiConnect);
    server.on("/wifi/disconnect", HTTP_POST, handleWifiDisconnect);
    server.on("/wifi/save", HTTP_POST, handleWifiSave);

    // Settings page
    server.on("/settings", HTTP_GET, handleSettingsGet);
    server.on("/settings/", HTTP_GET, handleSettingsGet);
    server.on("/settings", HTTP_POST, handleSettingsSave);
    server.on("/settings/", HTTP_POST, handleSettingsSave);
    server.on("/settings/vehicle-refresh", HTTP_POST, handleSettingsVehicleRefresh);

    // Dev API
    server.on("/dev/display-logo", HTTP_POST, handleDevDisplayLogo);
    server.on("/dev/display-status", HTTP_GET, handleDevDisplayStatus);
    server.on("/dev/display-pattern", HTTP_POST, handleDevDisplayPattern);
    server.on("/dev/obd-send", HTTP_POST, handleDevObdSend);

    // Config API - provides initial state for frontend
    server.on("/api/config", HTTP_GET, handleApiConfig);

    server.onNotFound(handleNotFound);

    LOG_INFO("WEB", "WEB_INIT", "Web UI routes registered (static files from LittleFS)");
    server.begin();
}

void webUiLoop()
{
    server.handleClient();
}
