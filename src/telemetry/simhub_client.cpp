#include "simhub_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/config.h"
#include "core/logging.h"
#include "core/state.h"
#include "core/wifi.h"
#include "telemetry/telemetry_manager.h"
#include "telemetry/usb_sim_bridge.h"

namespace
{
    struct SimHubClientConfigSnapshot
    {
        char host[64] = {};
        uint16_t port = 8888;
        uint16_t pollMs = 75;
        TelemetryPreference preference = TelemetryPreference::Auto;
        SimTransportPreference transport = SimTransportPreference::Auto;
        bool networkAvailable = false;
        bool enabled = true;
    };

    portMUX_TYPE g_simHubConfigMux = portMUX_INITIALIZER_UNLOCKED;
    SimHubClientConfigSnapshot g_simHubConfig{};
    TaskHandle_t g_simHubTaskHandle = nullptr;
    bool g_simHubPollSuppressed = false;

    bool shouldPollSimHubNetwork(SimRuntimeTransportMode mode,
                                 ActiveTelemetrySource activeSource,
                                 bool usbFresh)
    {
        switch (mode)
        {
        case SimRuntimeTransportMode::NetworkOnly:
            return true;
        case SimRuntimeTransportMode::UsbOnly:
        case SimRuntimeTransportMode::Disabled:
            return false;
        case SimRuntimeTransportMode::Auto:
        default:
            return !(activeSource == ActiveTelemetrySource::UsbSim || usbFresh);
        }
    }

    String trimmed(const String &value)
    {
        String out = value;
        out.trim();
        return out;
    }

    void setSimHubState(SimHubConnectionState state, bool reachable, bool waiting)
    {
        g_simHubConnectionState = state;
        g_simHubReachable = reachable;
        g_simHubWaitingForData = waiting;
    }

    bool extractJsonToken(const String &payload, const char *key, String &token)
    {
        String needle = String("\"") + key + "\"";
        int keyPos = payload.indexOf(needle);
        if (keyPos < 0)
        {
            return false;
        }

        int valuePos = payload.indexOf(':', keyPos + needle.length());
        if (valuePos < 0)
        {
            return false;
        }

        ++valuePos;
        while (valuePos < payload.length() && isspace(static_cast<unsigned char>(payload[valuePos])) != 0)
        {
            ++valuePos;
        }
        if (valuePos >= payload.length())
        {
            return false;
        }

        if (payload[valuePos] == '"')
        {
            const int start = valuePos + 1;
            const int end = payload.indexOf('"', start);
            if (end < 0)
            {
                return false;
            }

            token = payload.substring(start, end);
            return true;
        }

        int end = valuePos;
        while (end < payload.length())
        {
            const char ch = payload[end];
            if (isspace(static_cast<unsigned char>(ch)) != 0 || ch == ',' || ch == '}' || ch == ']')
            {
                break;
            }
            ++end;
        }

        token = payload.substring(valuePos, end);
        token.trim();
        return token.length() > 0;
    }

    bool extractJsonBool(const String &payload, const char *key, bool &value)
    {
        String token;
        if (!extractJsonToken(payload, key, token))
        {
            return false;
        }

        token.toLowerCase();
        if (token == "true")
        {
            value = true;
            return true;
        }
        if (token == "false")
        {
            value = false;
            return true;
        }
        return false;
    }

    bool extractJsonFloat(const String &payload, const char *key, float &value)
    {
        String token;
        if (!extractJsonToken(payload, key, token))
        {
            return false;
        }

        value = token.toFloat();
        return true;
    }

    bool parseLooseBool(const String &payload, bool &value)
    {
        String token = payload;
        token.trim();
        token.toLowerCase();

        if (token == "true" || token == "1" || token == "yes" || token == "on")
        {
            value = true;
            return true;
        }
        if (token == "false" || token == "0" || token == "no" || token == "off")
        {
            value = false;
            return true;
        }
        return false;
    }

    bool extractJsonInt(const String &payload, const char *key, int &value)
    {
        String token;
        if (!extractJsonToken(payload, key, token))
        {
            return false;
        }

        if (token == "N" || token == "n" || token == "R" || token == "r")
        {
            value = 0;
            return true;
        }

        value = token.toInt();
        return true;
    }

    bool httpGetText(const char *host, uint16_t port, const char *path, String &payload)
    {
        WiFiClient client;
        HTTPClient http;
        http.setReuse(false);
        http.setConnectTimeout(250);
        http.setTimeout(250);

        String url = String("http://") + host + ":" + String(port) + path;
        if (!http.begin(client, url))
        {
            return false;
        }

        const int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            http.end();
            return false;
        }

        payload = http.getString();
        http.end();
        return true;
    }

    bool tryFetchPitLimiterFlag(const SimHubClientConfigSnapshot &config, bool &pitLimiterActive)
    {
        static constexpr const char *kPitLimiterPaths[] = {
            "/Api/GetProperty/DataCorePlugin.GameData.NewData.PitLimiterOn",
            "/Api/GetProperty/DataCorePlugin.GameData.NewData.PitLimiter",
            "/Api/GetProperty/DataCorePlugin.GameData.PitLimiterOn",
            "/Api/GetProperty/DataCorePlugin.GameData.PitLimiter"};

        for (const char *path : kPitLimiterPaths)
        {
            String payload;
            if (!httpGetText(config.host, config.port, path, payload))
            {
                continue;
            }

            if (parseLooseBool(payload, pitLimiterActive))
            {
                return true;
            }
        }

        pitLimiterActive = false;
        return false;
    }

    float normalizeSimHubThrottle(float value)
    {
        if (value > 1.0f)
        {
            value /= 100.0f;
        }
        return constrain(value, 0.0f, 1.0f);
    }

    bool fetchSimHubFrame(const SimHubClientConfigSnapshot &config)
    {
        String gameData;
        if (!httpGetText(config.host, config.port, "/Api/GetGameData", gameData))
        {
            ++g_simHubDebug.pollErrorCount;
            g_simHubDebug.lastErrorMs = millis();
            g_simHubDebug.lastError = F("GetGameData failed");
            setSimHubState(SimHubConnectionState::Error, false, true);
            return false;
        }

        bool gameRunning = false;
        extractJsonBool(gameData, "GameRunning", gameRunning);
        const bool hasNewData = gameData.indexOf("\"NewData\":null") < 0 && gameData.indexOf("\"NewData\": null") < 0;

        if (!gameRunning || !hasNewData)
        {
            setSimHubState(SimHubConnectionState::WaitingForData, true, true);
            return false;
        }

        String simpleData;
        if (!httpGetText(config.host, config.port, "/Api/GetGameDataSimple", simpleData))
        {
            ++g_simHubDebug.pollErrorCount;
            g_simHubDebug.lastErrorMs = millis();
            g_simHubDebug.lastError = F("GetGameDataSimple failed");
            setSimHubState(SimHubConnectionState::Error, false, true);
            return false;
        }

        int rpm = 0;
        int speedKmh = 0;
        int gear = 0;
        int maxRpm = 0;
        float throttle = 0.0f;
        bool pitLimiterActive = false;

        const bool hasRpm = extractJsonInt(simpleData, "rpms", rpm) || extractJsonInt(simpleData, "rpm", rpm);
        const bool hasSpeed = extractJsonInt(simpleData, "speed", speedKmh);
        const bool hasGear = extractJsonInt(simpleData, "gear", gear);
        extractJsonInt(simpleData, "maxRpm", maxRpm);

        String throttlePayload;
        if (httpGetText(config.host, config.port, "/Api/GetProperty/DataCorePlugin.GameData.NewData.Throttle", throttlePayload))
        {
            throttle = throttlePayload.toFloat();
        }
        else
        {
            extractJsonFloat(simpleData, "throttle", throttle);
        }
        tryFetchPitLimiterFlag(config, pitLimiterActive);

        if (!hasRpm && !hasSpeed && !hasGear)
        {
            setSimHubState(SimHubConnectionState::WaitingForData, true, true);
            return false;
        }

        g_simHubCurrentRpm = max(0, rpm);
        g_simHubVehicleSpeedKmh = max(0, speedKmh);
        g_simHubGear = max(0, gear);
        g_simHubThrottle = normalizeSimHubThrottle(throttle);
        g_simHubPitLimiterActive = pitLimiterActive;
        g_simHubMaxSeenRpm = max(max(g_simHubMaxSeenRpm, g_simHubCurrentRpm), maxRpm);
        // Only the network timestamp is authoritative for the network source.
        // g_lastSimHubTelemetryMs is kept as a mirror of the network value
        // purely for legacy readers; it is no longer written by the USB path.
        const unsigned long nowMs = millis();
        g_lastSimHubNetworkTelemetryMs = nowMs;
        g_lastSimHubTelemetryMs = nowMs;
        g_simHubEverReceived = true;
        ++g_simHubDebug.pollSuccessCount;
        g_simHubDebug.lastSuccessMs = nowMs;
        g_simHubDebug.lastError = "";
        setSimHubState(SimHubConnectionState::Live, true, false);

        LOG_DEBUG("SIMHUB", "SIMHUB_SAMPLE", String("rpm=") + g_simHubCurrentRpm + " speed=" + g_simHubVehicleSpeedKmh + " gear=" + g_simHubGear);
        return true;
    }

    void simHubTask(void *)
    {
        for (;;)
        {
            SimHubClientConfigSnapshot snapshot{};
            portENTER_CRITICAL(&g_simHubConfigMux);
            snapshot = g_simHubConfig;
            portEXIT_CRITICAL(&g_simHubConfigMux);

            if (!snapshot.enabled)
            {
                setSimHubState(SimHubConnectionState::Disabled, false, false);
                vTaskDelay(pdMS_TO_TICKS(250));
                continue;
            }

            if (snapshot.host[0] == '\0')
            {
                setSimHubState(SimHubConnectionState::WaitingForHost, false, true);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            if (!snapshot.networkAvailable)
            {
                setSimHubState(SimHubConnectionState::WaitingForNetwork, false, true);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            fetchSimHubFrame(snapshot);
            vTaskDelay(pdMS_TO_TICKS(snapshot.pollMs));
        }
    }
}

void initSimHubClient()
{
    if (g_simHubTaskHandle != nullptr)
    {
        return;
    }

    setSimHubState(SimHubConnectionState::WaitingForHost, false, true);
    xTaskCreate(simHubTask, "simHubTask", 7168, nullptr, 1, &g_simHubTaskHandle);
}

void simHubClientUpdateConfig()
{
    const WifiStatus wifiStatus = getWifiStatus();
    const String host = trimmed(cfg.simHubHost);
    const SimRuntimeTransportMode runtimeMode =
        resolveSimRuntimeTransportMode(cfg.telemetryPreference, cfg.simTransportPreference);
    const bool usbFresh = usbSimTelemetryFresh(millis());
    const bool shouldPoll = shouldPollSimHubNetwork(runtimeMode, g_activeTelemetrySource, usbFresh);

    SimHubClientConfigSnapshot next{};
    host.toCharArray(next.host, sizeof(next.host));
    next.port = cfg.simHubPort;
    next.pollMs = cfg.simHubPollMs;
    next.preference = cfg.telemetryPreference;
    next.transport = cfg.simTransportPreference;
    next.networkAvailable = wifiStatus.staConnected || wifiStatus.apActive;
    if (!shouldPoll && telemetryAllowsNetworkSim(cfg.telemetryPreference, cfg.simTransportPreference))
    {
        if (!g_simHubPollSuppressed)
        {
            ++g_simHubDebug.suppressedWhileUsbCount;
            g_simHubPollSuppressed = true;
        }
    }
    else
    {
        g_simHubPollSuppressed = false;
    }
    next.enabled = telemetryAllowsNetworkSim(cfg.telemetryPreference, cfg.simTransportPreference) && shouldPoll;

    portENTER_CRITICAL(&g_simHubConfigMux);
    g_simHubConfig = next;
    portEXIT_CRITICAL(&g_simHubConfigMux);
}
