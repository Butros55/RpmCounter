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
    constexpr unsigned long AP_CLIENT_CHECK_INTERVAL_MS = 2000;  // Check client status every 2s
    constexpr unsigned long AP_INACTIVE_LOG_INTERVAL_MS = 10000; // Log AP status every 10s when no clients

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
        int apClientsLastLogged = -1; // Track client count changes for logging
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
        unsigned long lastApCheckMs = 0;
        unsigned long lastApLogMs = 0;
        bool apWasActive = false; // Track AP state changes
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
        bool wasActive = g_wifi.apActive;
        g_wifi.apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);

        int prevClients = g_wifi.apClients;
        g_wifi.apClients = g_wifi.apActive ? WiFi.softAPgetStationNum() : 0;
        g_wifi.apIp = g_wifi.apActive ? WiFi.softAPIP().toString() : "";

        // Log AP state changes
        if (g_wifi.apActive != wasActive)
        {
            if (g_wifi.apActive)
            {
                LOG_INFO("WIFI", "AP_STATE_ACTIVE", String("ip=") + g_wifi.apIp);
            }
            else
            {
                LOG_WARN("WIFI", "AP_STATE_INACTIVE", "AP stopped unexpectedly");
            }
            g_wifi.apWasActive = g_wifi.apActive;
        }

        // Log client count changes
        if (g_wifi.apClients != prevClients && g_wifi.apClients != g_wifi.apClientsLastLogged)
        {
            g_wifi.apClientsLastLogged = g_wifi.apClients;
            LOG_INFO("WIFI", "AP_CLIENTS_CHANGED", String("clients=") + g_wifi.apClients);
            Serial.printf("[WIFI] AP clients: %d\n", g_wifi.apClients);
        }
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

        // Configure WiFi for AP stability
        WiFi.persistent(false);       // Don't save to flash on every change
        WiFi.setAutoReconnect(false); // We handle reconnection manually

        // Set mode first, then configure - prevents AP restart
        if (currentMode != WIFI_AP_STA && currentMode != WIFI_AP)
        {
            WiFi.mode(WIFI_AP_STA);
            delay(100); // Give WiFi stack time to initialize
        }

        WiFi.setSleep(false);                // Disable power saving for better responsiveness
        WiFi.setTxPower(WIFI_POWER_19_5dBm); // Maximum power for better range

        // Configure AP with static IP
        IPAddress apIp(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);

        if (!WiFi.softAPConfig(apIp, gateway, subnet))
        {
            LOG_ERROR("WIFI", "AP_CONFIG_FAIL", "softAPConfig failed");
        }

        // ESP-IDF level configuration for better AP stability
        wifi_config_t wifi_config = {};
        strncpy((char *)wifi_config.ap.ssid, ssid.c_str(), sizeof(wifi_config.ap.ssid) - 1);
        strncpy((char *)wifi_config.ap.password, pass.c_str(), sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.ssid_len = ssid.length();
        wifi_config.ap.channel = 6; // Channel 6 is often less congested
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.ap.ssid_hidden = 0;
        wifi_config.ap.max_connection = 4;
        wifi_config.ap.beacon_interval = 100;                   // 100ms beacon interval (standard)
        wifi_config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP; // AES encryption
        wifi_config.ap.ftm_responder = false;
        wifi_config.ap.pmf_cfg.required = false;

        // Apply ESP-IDF config before Arduino softAP
        esp_wifi_set_config(WIFI_IF_AP, &wifi_config);

        // Start AP with Arduino API
        bool ok = WiFi.softAP(ssid.c_str(), pass.c_str(), 6, false, 4);

        if (ok)
        {
            // Additional delay for AP to fully stabilize
            delay(200);

            // Verify AP is actually running
            wifi_mode_t verifyMode = WiFi.getMode();
            if (verifyMode != WIFI_AP && verifyMode != WIFI_AP_STA)
            {
                LOG_ERROR("WIFI", "AP_VERIFY_FAIL", String("mode=") + verifyMode);
                ok = false;
            }
        }

        g_wifi.apActive = ok;
        g_wifi.apClients = ok ? WiFi.softAPgetStationNum() : 0;
        g_wifi.apIp = ok ? WiFi.softAPIP().toString() : "";
        g_wifi.configuredMode = config.wifiMode;
        g_wifi.activeMode = activeMode;
        g_wifi.currentSsid = ssid;
        g_wifi.apWasActive = ok;
        g_wifi.apClientsLastLogged = -1; // Reset to trigger first log

        updateIp();
        if (ok)
        {
            LOG_INFO("WIFI", "WIFI_AP_START", String("ssid=") + ssid + " ch=6 ip=" + WiFi.softAPIP().toString());
            Serial.println();
            Serial.println("========================================");
            Serial.println("[WIFI] Access Point READY");
            Serial.printf("[WIFI] SSID: %s\n", ssid.c_str());
            Serial.printf("[WIFI] Password: %s\n", pass.c_str());
            Serial.printf("[WIFI] >>> http://%s <<<\n", g_wifi.apIp.c_str());
            Serial.println("========================================");
            Serial.println();
        }
        else
        {
            LOG_ERROR("WIFI", "WIFI_AP_FAIL", String("ssid=") + ssid);
            Serial.println("[WIFI] ERROR: Access Point failed to start!");
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

    // AP health monitoring - restart AP if it unexpectedly stops
    if (g_wifi.configuredMode == AP_ONLY || g_wifi.configuredMode == STA_WITH_AP_FALLBACK)
    {
        // Check AP status periodically
        if (now - g_wifi.lastApCheckMs >= AP_CLIENT_CHECK_INTERVAL_MS)
        {
            g_wifi.lastApCheckMs = now;

            // If AP should be active but isn't, restart it
            if (!g_wifi.apActive && g_wifi.apWasActive)
            {
                LOG_WARN("WIFI", "AP_RECOVERY", "AP stopped unexpectedly, restarting...");
                Serial.println("[WIFI] AP recovery - restarting AP");
                startSoftAp(cfg, g_wifi.activeMode);
            }

            // Periodic status log when AP is active (every 10s)
            if (g_wifi.apActive && (now - g_wifi.lastApLogMs >= AP_INACTIVE_LOG_INTERVAL_MS))
            {
                g_wifi.lastApLogMs = now;
                // Only log detailed status if there are clients or if debugging
                if (g_wifi.apClients > 0)
                {
                    LOG_DEBUG("WIFI", "AP_STATUS", String("clients=") + g_wifi.apClients + " ip=" + g_wifi.apIp);
                }
            }
        }
    }

    if (g_wifi.staConnecting)
    {
        if (status == WL_CONNECTED)
        {
            g_wifi.staConnected = true;
            g_wifi.staConnecting = false;
            g_wifi.staLastError = "";
            g_wifi.currentSsid = WiFi.SSID();
            updateIp();

            // Log STA IP prominently so user knows where to access webserver
            String staIp = WiFi.localIP().toString();
            LOG_INFO("WIFI", "WIFI_STA_READY", String("ssid=") + g_wifi.currentSsid + " ip=" + staIp);
            Serial.println("========================================");
            Serial.printf("[WIFI] STA Connected to: %s\n", g_wifi.currentSsid.c_str());
            Serial.printf("[WIFI] >>> Webserver: http://%s <<<\n", staIp.c_str());
            Serial.println("========================================");
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
