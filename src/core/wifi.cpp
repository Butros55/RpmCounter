#include "wifi.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <cstdio>

#include "config.h"
#include "logging.h"

namespace
{
    constexpr unsigned long WIFI_SCAN_MAX_DURATION_MS = 15000;
    constexpr uint8_t WIFI_STA_MAX_RETRIES = 5;
    constexpr unsigned long WIFI_STA_RETRY_DELAY_MS = 5000;

    struct WifiRuntimeState
    {
        WifiMode configuredMode = AP_ONLY;
        WifiMode activeMode = AP_ONLY;
        bool staConnected = false;
        bool staConnecting = false;
        unsigned long staConnectStartMs = 0;
        uint32_t staConnectTimeoutMs = 15000;
        bool apActive = false;
        int apClients = 0;
        String apIp;
        bool scanRunning = false;
        unsigned long scanStartMs = 0;
        std::vector<WifiScanResult> scanResults;
        String staLastError;
        String targetSsid;
        String targetPass;
        String currentSsid;
        String staIp;
        String lastIp;
        uint8_t staRetryCount = 0;
        unsigned long lastRetryMs = 0;
    };

    WifiRuntimeState g_wifi;
    bool g_wifiEventsRegistered = false;
    WiFiEventId_t g_wifiEventId = 0;

    String trimmed(const String &value)
    {
        String out = value;
        out.trim();
        return out;
    }

    String macToString(const uint8_t mac[6])
    {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return String(buf);
    }

    void refreshApState()
    {
        wifi_mode_t mode = WiFi.getMode();
        g_wifi.apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
        g_wifi.apClients = g_wifi.apActive ? WiFi.softAPgetStationNum() : 0;
        g_wifi.apIp = g_wifi.apActive ? WiFi.softAPIP().toString() : "";
    }

    void updateIp()
    {
        if (WiFi.status() == WL_CONNECTED || g_wifi.staConnected)
        {
            g_wifi.staIp = WiFi.localIP().toString();
        }
        else
        {
            g_wifi.staIp.clear();
        }

        if (g_wifi.apActive)
        {
            g_wifi.apIp = WiFi.softAPIP().toString();
        }
        else
        {
            g_wifi.apIp.clear();
        }

        if (g_wifi.staConnected)
        {
            g_wifi.lastIp = g_wifi.staIp;
        }
        else
        {
            g_wifi.lastIp = g_wifi.apIp.length() > 0 ? g_wifi.apIp : WiFi.localIP().toString();
        }
    }

    void ensureWifiEventsRegistered()
    {
        if (g_wifiEventsRegistered)
        {
            return;
        }

        g_wifiEventId = WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info)
                                     {
                                         switch (event)
                                         {
                                         case ARDUINO_EVENT_WIFI_AP_START:
                                             LOG_INFO("WIFI", "WIFI_EVT_AP_START", String("ip=") + WiFi.softAPIP().toString());
                                             break;
                                         case ARDUINO_EVENT_WIFI_AP_STOP:
                                             LOG_INFO("WIFI", "WIFI_EVT_AP_STOP", "softAP stopped");
                                             break;
                                        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
                                            LOG_INFO("WIFI", "WIFI_EVT_AP_STACONNECT", String("mac=") + macToString(info.wifi_ap_staconnected.mac));
                                            break;
                                        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
                                            LOG_INFO("WIFI", "WIFI_EVT_AP_STADISCONNECT", String("mac=") + macToString(info.wifi_ap_stadisconnected.mac));
                                            break;
                                         case ARDUINO_EVENT_WIFI_STA_START:
                                             LOG_INFO("WIFI", "WIFI_EVT_STA_START", "STA interface started");
                                             break;
                                         case ARDUINO_EVENT_WIFI_STA_CONNECTED:
                                             LOG_INFO("WIFI", "WIFI_EVT_STA_CONNECTED", String("ssid=") + WiFi.SSID());
                                             break;
                                         case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                                             ESP_LOGW("WIFI", "WIFI_EVT_STA_DISCONNECTED reason=%d", info.wifi_sta_disconnected.reason);
                                             break;
                                         case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                                         {
                                             IPAddress ip(info.got_ip.ip_info.ip.addr);
                                             LOG_INFO("WIFI", "WIFI_EVT_STA_GOT_IP", String("ip=") + ip.toString());
                                             break;
                                         }
                                         case ARDUINO_EVENT_WIFI_STA_LOST_IP:
                                             ESP_LOGW("WIFI", "WIFI_EVT_STA_LOST_IP");
                                             break;
                                         default:
                                             break;
                                         } });
        g_wifiEventsRegistered = true;
    }

    void clearStaState()
    {
        g_wifi.staConnected = false;
        g_wifi.staConnecting = false;
        g_wifi.currentSsid = "";
        g_wifi.staIp = "";
    }

    bool startSoftAp(const AppConfig &config, WifiMode activeMode)
    {
        String ssid = trimmed(config.apSsid);
        String pass = trimmed(config.apPassword);
        if (ssid.isEmpty())
            ssid = AP_SSID;
        if (pass.length() < 8)
            pass = AP_PASS;

        ensureWifiEventsRegistered();

        // Check if AP is already running with same SSID - avoid restart
        wifi_mode_t currentMode = WiFi.getMode();
        if ((currentMode == WIFI_AP || currentMode == WIFI_AP_STA) &&
            WiFi.softAPSSID() == ssid && WiFi.softAPIP() != IPAddress(0, 0, 0, 0))
        {
            // AP already running, just update state
            g_wifi.apActive = true;
            g_wifi.apClients = WiFi.softAPgetStationNum();
            g_wifi.apIp = WiFi.softAPIP().toString();
            g_wifi.configuredMode = config.wifiMode;
            g_wifi.activeMode = activeMode;
            g_wifi.currentSsid = ssid;
            updateIp();
            LOG_INFO("WIFI", "AP_ALREADY_ACTIVE", String("ssid=") + ssid + " ip=" + g_wifi.apIp);
            return true;
        }

        // Set mode first, then configure - prevents AP restart
        if (currentMode != WIFI_AP_STA && currentMode != WIFI_AP)
        {
            WiFi.mode(WIFI_AP_STA);
            delay(100); // Give WiFi stack time to initialize
        }

        WiFi.setSleep(false);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);

        IPAddress apIp(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(apIp, apIp, subnet);

        // Start AP with fixed channel for iPhone compatibility
        bool ok = WiFi.softAP(ssid.c_str(), pass.c_str(), 1, false, 4); // channel 1, not hidden, max 4 clients

        // Small delay to let AP stabilize
        delay(100);

        g_wifi.apActive = ok;
        g_wifi.apClients = ok ? WiFi.softAPgetStationNum() : 0;
        g_wifi.apIp = ok ? WiFi.softAPIP().toString() : "";
        g_wifi.configuredMode = config.wifiMode;
        g_wifi.activeMode = activeMode;
        g_wifi.currentSsid = ssid;

        updateIp();
        if (ok)
        {
            LOG_INFO("WIFI", "WIFI_AP_START", String("ssid=") + ssid + " ip=" + WiFi.softAPIP().toString());
            LOG_INFO("WIFI", "AP_READY", String("ssid=") + ssid + " ip=" + g_wifi.apIp);
            Serial.printf("[WIFI] AP IP: %s\n", g_wifi.apIp.c_str());
        }
        else
        {
            LOG_ERROR("WIFI", "WIFI_AP_FAIL", String("ssid=") + ssid);
        }

        return ok;
    }

    void ensureApForFallback(const AppConfig &config, bool keepSta)
    {
        if (config.wifiMode != STA_WITH_AP_FALLBACK)
            return;
        startSoftAp(config, keepSta ? STA_WITH_AP_FALLBACK : AP_ONLY);
    }

    void handleStaFailure(const String &reason)
    {
        if (WiFi.getMode() != WIFI_MODE_NULL)
        {
            WiFi.disconnect(true);
        }
        g_wifi.staLastError = reason;
        clearStaState();
    }
}

bool startApMode(const AppConfig &config)
{
    return startSoftAp(config, AP_ONLY);
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

    ensureWifiEventsRegistered();
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
    g_wifi.staIp = "";
    g_wifi.lastIp = "";
    g_wifi.lastRetryMs = millis();
    if (config.wifiMode != STA_WITH_AP_FALLBACK)
    {
        g_wifi.apActive = false;
    }

    if (config.wifiMode == STA_WITH_AP_FALLBACK)
    {
        WiFi.mode(WIFI_AP_STA);
        WiFi.disconnect(false, true);
    }
    else
    {
        if (WiFi.getMode() != WIFI_MODE_NULL)
        {
            WiFi.disconnect(true, true);
        }
        WiFi.mode(WIFI_STA);
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    refreshApState();
    LOG_INFO("WIFI", "WIFI_STA_START", String("ssid=") + ssid);
    return true;
}

void setupWifiFromConfig(const AppConfig &config)
{
    ensureWifiEventsRegistered();
    WiFi.setSleep(false);
    g_wifi.configuredMode = config.wifiMode;
    g_wifi.staLastError = "";
    g_wifi.staRetryCount = 0;
    g_wifi.lastRetryMs = 0; // Allow immediate first retry
    LOG_INFO("WIFI", "WIFI_CFG", String("mode=") + static_cast<int>(config.wifiMode) + " sta=" + config.staSsid);

    switch (config.wifiMode)
    {
    case AP_ONLY:
        clearStaState();
        startApMode(config);
        break;
    case STA_ONLY:
        g_wifi.apActive = false;
        if (startStaMode(config))
        {
            g_wifi.staRetryCount = 1;
            g_wifi.lastRetryMs = millis();
        }
        break;
    case STA_WITH_AP_FALLBACK:
    default:
        // Start AP first for immediate connectivity
        startSoftAp(config, STA_WITH_AP_FALLBACK);

        // Start STA connection in background - non-blocking
        // The wifiLoop will handle retries if needed
        String staSsid = trimmed(config.staSsid);
        if (!staSsid.isEmpty())
        {
            g_wifi.targetSsid = staSsid;
            g_wifi.targetPass = trimmed(config.staPassword);
            g_wifi.staRetryCount = 0;
            g_wifi.lastRetryMs = 0; // Trigger immediate retry in wifiLoop
            LOG_INFO("WIFI", "WIFI_STA_QUEUED", String("ssid=") + staSsid);
        }
        break;
    }
}

bool startWifiScan()
{
    if (g_wifi.scanRunning)
        return false;

    if (g_wifi.staConnecting)
    {
        LOG_DEBUG("WIFI", "WIFI_SCAN_SKIP_CONNECT", "skip scan while STA connecting");
        return false;
    }

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
    handleStaFailure("");
    if (g_wifi.configuredMode != STA_ONLY)
    {
        startApMode(cfg);
    }
}

bool requestWifiConnect(const String &ssid, const String &password, WifiMode mode)
{
    if (g_wifi.staConnecting)
    {
        LOG_DEBUG("WIFI", "WIFI_CONNECT_BUSY", "connect already running");
        return false;
    }

    if (g_wifi.scanRunning)
    {
        LOG_DEBUG("WIFI", "WIFI_CONNECT_SCAN_BUSY", "skip connect while scan running");
        return false;
    }

    String cleanSsid = trimmed(ssid);
    if (cleanSsid.isEmpty())
    {
        LOG_ERROR("WIFI", "WIFI_CONNECT_INVALID", "missing ssid");
        return false;
    }

    AppConfig temp = cfg;
    temp.wifiMode = mode;
    temp.staSsid = cleanSsid;
    temp.staPassword = password;

    cfg.wifiMode = mode;
    cfg.staSsid = cleanSsid;
    cfg.staPassword = password;
    g_wifi.staRetryCount = 1;
    g_wifi.lastRetryMs = millis();

    if (mode == STA_WITH_AP_FALLBACK && !g_wifi.apActive)
    {
        ensureApForFallback(temp, true);
    }

    return startStaMode(temp);
}

WifiStatus getWifiStatus()
{
    WifiStatus status;
    status.mode = g_wifi.configuredMode;
    status.apActive = g_wifi.apActive;
    status.apClients = g_wifi.apClients;
    status.apIp = g_wifi.apIp;
    status.staConnected = g_wifi.staConnected;
    status.staConnecting = g_wifi.staConnecting;
    status.staLastError = g_wifi.staLastError;
    status.currentSsid = g_wifi.currentSsid;
    status.staIp = g_wifi.staIp;
    status.ip = g_wifi.lastIp;
    status.scanRunning = g_wifi.scanRunning;
    status.scanResults = g_wifi.scanResults;
    return status;
}

void wifiLoop()
{
    unsigned long now = millis();
    wl_status_t status = WiFi.status();
    refreshApState();

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
            String reason = (status == WL_NO_SSID_AVAIL) ? "ssid-not-found" : "connect-failed";
            handleStaFailure(reason);
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
            g_wifi.staRetryCount = 0; // Reset retry count on successful connection
        }
        else if (cfg.wifiMode == STA_WITH_AP_FALLBACK)
        {
            String staSsid = trimmed(cfg.staSsid);
            if (g_wifi.staRetryCount < WIFI_STA_MAX_RETRIES && !staSsid.isEmpty())
            {
                // First retry can be immediate (lastRetryMs == 0)
                unsigned long timeSinceLastRetry = (g_wifi.lastRetryMs == 0) ? WIFI_STA_RETRY_DELAY_MS : (now - g_wifi.lastRetryMs);
                if (timeSinceLastRetry >= WIFI_STA_RETRY_DELAY_MS)
                {
                    g_wifi.staRetryCount++;
                    g_wifi.lastRetryMs = now;
                    LOG_INFO("WIFI", "WIFI_STA_RETRY", String("STA attempt ") + g_wifi.staRetryCount + "/" + WIFI_STA_MAX_RETRIES + " ssid=" + staSsid);

                    // Start STA connection without disturbing AP
                    g_wifi.staConnecting = true;
                    g_wifi.staConnectStartMs = now;
                    WiFi.begin(staSsid.c_str(), trimmed(cfg.staPassword).c_str());
                }
            }
        }
    }

    updateIp();

    if (g_wifi.scanRunning)
    {
        if ((now - g_wifi.scanStartMs) > WIFI_SCAN_MAX_DURATION_MS)
        {
            g_wifi.scanRunning = false;
            g_wifi.staLastError = "scan-timeout";
            WiFi.scanDelete();
            LOG_ERROR("WIFI", "WIFI_SCAN_TIMEOUT", "scan timed out");
            return;
        }

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
