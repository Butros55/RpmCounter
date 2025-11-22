#include "wifi.h"

#include <WiFi.h>

#include "config.h"
#include "logging.h"

namespace
{
    struct WifiRuntimeState
    {
        WifiMode configuredMode = AP_ONLY;
        WifiMode activeMode = AP_ONLY;
        bool staConnected = false;
        bool staConnecting = false;
        unsigned long staConnectStartMs = 0;
        uint32_t staConnectTimeoutMs = 15000;
        bool apActive = false;
        bool scanRunning = false;
        unsigned long scanStartMs = 0;
        std::vector<WifiScanResult> scanResults;
        String staLastError;
        String targetSsid;
        String targetPass;
        String currentSsid;
        String lastIp;
    };

    WifiRuntimeState g_wifi;

    String trimmed(const String &value)
    {
        String out = value;
        out.trim();
        return out;
    }

    void updateIp()
    {
        if (g_wifi.staConnected)
        {
            g_wifi.lastIp = WiFi.localIP().toString();
        }
        else if (g_wifi.apActive)
        {
            g_wifi.lastIp = WiFi.softAPIP().toString();
        }
        else
        {
            g_wifi.lastIp = WiFi.localIP().toString();
        }
    }

    wifi_mode_t pickApMode()
    {
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_STA || mode == WIFI_AP_STA)
            return WIFI_AP_STA;
        return WIFI_AP;
    }

    void clearStaState()
    {
        g_wifi.staConnected = false;
        g_wifi.staConnecting = false;
        g_wifi.currentSsid = "";
    }

    void ensureApForFallback(const AppConfig &config, bool keepSta)
    {
        if (config.wifiMode != STA_WITH_AP_FALLBACK)
            return;

        String ssid = trimmed(config.apSsid);
        String pass = trimmed(config.apPassword);
        if (ssid.isEmpty())
            ssid = AP_SSID;
        if (pass.length() < 8)
            pass = AP_PASS;

        wifi_mode_t mode = keepSta ? WIFI_AP_STA : pickApMode();
        WiFi.mode(mode);
        bool ok = WiFi.softAP(ssid.c_str(), pass.c_str());
        g_wifi.apActive = ok;
        updateIp();
        if (ok)
        {
            LOG_INFO("WIFI", "WIFI_AP_READY", String("mode=fallback ssid=") + ssid + " ip=" + WiFi.softAPIP().toString());
        }
        else
        {
            LOG_ERROR("WIFI", "WIFI_AP_FALLBACK_FAIL", String("ssid=") + ssid);
        }
    }

    void handleStaFailure(const String &reason)
    {
        WiFi.disconnect(true);
        g_wifi.staLastError = reason;
        clearStaState();
    }
}

bool startApMode(const AppConfig &config)
{
    String ssid = trimmed(config.apSsid);
    String pass = trimmed(config.apPassword);
    if (ssid.isEmpty())
        ssid = AP_SSID;
    if (pass.length() < 8)
        pass = AP_PASS;

    WiFi.disconnect(true);
    WiFi.mode(pickApMode());
    bool ok = WiFi.softAP(ssid.c_str(), pass.c_str());

    g_wifi.configuredMode = config.wifiMode;
    g_wifi.activeMode = AP_ONLY;
    g_wifi.apActive = ok;
    g_wifi.currentSsid = ssid;
    updateIp();

    if (ok)
    {
        LOG_INFO("WIFI", "WIFI_AP_START", String("ssid=") + ssid + " ip=" + WiFi.softAPIP().toString());
    }
    else
    {
        LOG_ERROR("WIFI", "WIFI_AP_FAIL", String("ssid=") + ssid);
    }

    return ok;
}

bool startStaMode(const AppConfig &config, uint32_t timeoutMs)
{
    String ssid = trimmed(config.staSsid);
    String pass = trimmed(config.staPassword);
    if (ssid.isEmpty())
    {
        handleStaFailure("ssid-missing");
        return false;
    }

    g_wifi.configuredMode = config.wifiMode;
    g_wifi.activeMode = config.wifiMode;
    g_wifi.staLastError = "";
    g_wifi.targetSsid = ssid;
    g_wifi.targetPass = pass;
    g_wifi.currentSsid = ssid;
    g_wifi.staConnectTimeoutMs = timeoutMs;
    g_wifi.staConnectStartMs = millis();
    g_wifi.staConnecting = true;
    g_wifi.staConnected = false;
    g_wifi.lastIp = "";
    if (config.wifiMode != STA_WITH_AP_FALLBACK)
    {
        g_wifi.apActive = false;
    }

    WiFi.disconnect(true);
    if (config.wifiMode == STA_WITH_AP_FALLBACK)
    {
        if (!g_wifi.apActive)
        {
            ensureApForFallback(config, true);
        }
        else
        {
            WiFi.mode(WIFI_AP_STA);
        }
    }
    else
    {
        WiFi.mode(WIFI_STA);
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    LOG_INFO("WIFI", "WIFI_STA_START", String("ssid=") + ssid);
    return true;
}

void setupWifiFromConfig(const AppConfig &config)
{
    g_wifi.configuredMode = config.wifiMode;
    g_wifi.staLastError = "";

    switch (config.wifiMode)
    {
    case AP_ONLY:
        clearStaState();
        startApMode(config);
        break;
    case STA_ONLY:
        g_wifi.apActive = false;
        startStaMode(config);
        break;
    case STA_WITH_AP_FALLBACK:
    default:
        ensureApForFallback(config, true);
        if (!startStaMode(config))
        {
            startApMode(config);
        }
        break;
    }
}

bool startWifiScan()
{
    if (g_wifi.scanRunning)
        return false;

    // Ensure STA interface exists for scanning.
    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_AP)
        WiFi.mode(WIFI_AP_STA);
    else if (mode == WIFI_OFF)
        WiFi.mode(WIFI_STA);

    g_wifi.scanResults.clear();
    g_wifi.scanRunning = true;
    g_wifi.scanStartMs = millis();

    int16_t res = WiFi.scanNetworks(true, false);
    if (res == WIFI_SCAN_FAILED)
    {
        g_wifi.scanRunning = false;
        g_wifi.staLastError = "scan-failed";
        LOG_ERROR("WIFI", "WIFI_SCAN_FAIL", "scan failed to start");
        return false;
    }

    LOG_INFO("WIFI", "WIFI_SCAN_START", "started wifi scan");
    return true;
}

void requestWifiDisconnect()
{
    handleStaFailure("manual-disconnect");
    if (g_wifi.configuredMode != STA_ONLY)
    {
        startApMode(cfg);
    }
}

bool requestWifiConnect(const String &ssid, const String &password, WifiMode mode)
{
    AppConfig temp = cfg;
    temp.wifiMode = mode;
    temp.staSsid = ssid;
    temp.staPassword = password;

    cfg.wifiMode = mode;
    cfg.staSsid = ssid;
    cfg.staPassword = password;

    return startStaMode(temp);
}

WifiStatus getWifiStatus()
{
    WifiStatus status;
    status.mode = g_wifi.configuredMode;
    status.apActive = g_wifi.apActive;
    status.staConnected = g_wifi.staConnected;
    status.staConnecting = g_wifi.staConnecting;
    status.staLastError = g_wifi.staLastError;
    status.currentSsid = g_wifi.currentSsid;
    status.ip = g_wifi.lastIp;
    status.scanRunning = g_wifi.scanRunning;
    status.scanResults = g_wifi.scanResults;
    return status;
}

void wifiLoop()
{
    unsigned long now = millis();
    wl_status_t status = WiFi.status();

    if (g_wifi.staConnecting)
    {
        if (status == WL_CONNECTED)
        {
            g_wifi.staConnected = true;
            g_wifi.staConnecting = false;
            g_wifi.staLastError = "";
            g_wifi.currentSsid = WiFi.SSID();
            updateIp();
            LOG_INFO("WIFI", "WIFI_STA_READY", String("ip=") + WiFi.localIP().toString());
        }
        else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL)
        {
            handleStaFailure("connect-failed");
            if (g_wifi.configuredMode == STA_WITH_AP_FALLBACK)
            {
                startApMode(cfg);
            }
        }
        else if ((now - g_wifi.staConnectStartMs) > g_wifi.staConnectTimeoutMs)
        {
            handleStaFailure("timeout");
            if (g_wifi.configuredMode == STA_WITH_AP_FALLBACK)
            {
                startApMode(cfg);
            }
        }
    }
    else
    {
        g_wifi.staConnected = (status == WL_CONNECTED);
        if (g_wifi.staConnected)
        {
            g_wifi.currentSsid = WiFi.SSID();
        }
    }

    updateIp();

    if (g_wifi.scanRunning)
    {
        int16_t res = WiFi.scanComplete();
        if (res == WIFI_SCAN_RUNNING)
        {
            return;
        }

        g_wifi.scanRunning = false;
        g_wifi.scanResults.clear();
        if (res == WIFI_SCAN_FAILED)
        {
            g_wifi.staLastError = "scan-failed";
            LOG_ERROR("WIFI", "WIFI_SCAN_FAIL_DONE", "scan failed");
        }
        else if (res > 0)
        {
            for (int i = 0; i < res; ++i)
            {
                WifiScanResult item;
                item.ssid = WiFi.SSID(i);
                item.rssi = WiFi.RSSI(i);
                g_wifi.scanResults.push_back(item);
            }
            LOG_INFO("WIFI", "WIFI_SCAN_DONE", String("found=") + String(g_wifi.scanResults.size()));
        }
        WiFi.scanDelete();
    }
}
