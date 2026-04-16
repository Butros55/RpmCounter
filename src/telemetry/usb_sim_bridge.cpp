#include "usb_sim_bridge.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "core/config.h"
#include "core/logging.h"
#include "core/state.h"
#include "core/utils.h"
#include "core/wifi.h"
#include "hardware/led_bar.h"
#include "telemetry/telemetry_manager.h"
#include "web/web_helpers.h"
#include "signal_utils.h"

namespace
{
    // Heartbeat/sticky thresholds — relaxed so short lulls don't drop the bridge.
    constexpr unsigned long USB_BRIDGE_HEARTBEAT_TIMEOUT_MS = 5000;
    constexpr unsigned long USB_BRIDGE_STICKY_TIMEOUT_MS = 20000;

    // Hysteresis: two separate thresholds so the state does NOT flap between
    // Live and stale while the bridge keeps the heartbeat alive.
    //
    //   - below LIVE_THRESHOLD   -> Live
    //   - above STALE_THRESHOLD  -> downgrade (Error / WaitingForData)
    //   - in between             -> keep current state (dead zone)
    constexpr unsigned long USB_TELEMETRY_LIVE_THRESHOLD_MS  = 500;
    constexpr unsigned long USB_TELEMETRY_STALE_THRESHOLD_MS = 2000;
    // Public "fresh" check used by the source selector — generous so Auto does
    // not rip USB away during a brief lull while SimHub restarts a session.
    constexpr unsigned long USB_TELEMETRY_FRESH_TIMEOUT_MS = 2500;

    // Minimum time a candidate state must be pending before it actually commits
    // to the global state. Prevents single-iteration flips.
    constexpr unsigned long STATE_COMMIT_DEBOUNCE_MS = 200;

    constexpr size_t USB_LINE_BUFFER_LIMIT = 384;

    // Dedicated task cadence — reads serial every 2 ms and refreshes state every
    // 20 ms regardless of whether data arrived.
    constexpr uint32_t USB_TASK_POLL_INTERVAL_MS = 1;
    constexpr uint32_t USB_STATE_REFRESH_INTERVAL_MS = 10;
    constexpr unsigned long USB_TELEMETRY_DIAG_GAP_MS = 60;
    constexpr unsigned long USB_TELEMETRY_GLITCH_SHORT_GAP_MS = 40;
    constexpr int USB_TELEMETRY_GLITCH_RPM_DELTA = 1200;

    String g_usbRxLine;
    unsigned long g_lastHelloTxMs = 0;

    // State machine bookkeeping for the debouncer.
    UsbBridgeConnectionState g_pendingUsbState = UsbBridgeConnectionState::Disabled;
    String g_pendingUsbError;
    unsigned long g_pendingUsbStateSinceMs = 0;

    // Serialise writes to Serial so the reader task and any other emitters
    // cannot interleave bytes.
    SemaphoreHandle_t g_usbWriteMutex = nullptr;

    TaskHandle_t g_usbBridgeTaskHandle = nullptr;
    volatile bool g_usbBridgeTaskRunning = false;

    struct UsbTelemetryFrame
    {
        bool idle = false;
        bool hasSeq = false;
        uint32_t seq = 0;
        int rpm = 0;
        int speed = 0;
        int gear = 0;
        float throttle = 0.0f;
        bool pitLimiterActive = false;
        int reportedMaxRpm = 0;
    };

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

    String gearSourceLabel()
    {
        switch (g_activeTelemetrySource)
        {
        case ActiveTelemetrySource::UsbSim:
        case ActiveTelemetrySource::SimHubNetwork:
            return F("SimHub direkt");
        case ActiveTelemetrySource::Obd:
            return F("OBD berechnet");
        case ActiveTelemetrySource::None:
        default:
            return F("Keine Quelle");
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

    String simHubStateLabel(SimHubConnectionState state)
    {
        switch (state)
        {
        case SimHubConnectionState::WaitingForHost:
            return F("WAITING_HOST");
        case SimHubConnectionState::WaitingForNetwork:
            return F("WAITING_NETWORK");
        case SimHubConnectionState::WaitingForData:
            return F("WAITING_DATA");
        case SimHubConnectionState::Live:
            return F("LIVE");
        case SimHubConnectionState::Error:
            return F("ERROR");
        case SimHubConnectionState::Disabled:
        default:
            return F("DISABLED");
        }
    }

    String simTransportModeLabel(SimRuntimeTransportMode mode)
    {
        switch (mode)
        {
        case SimRuntimeTransportMode::UsbOnly:
            return F("USB_ONLY");
        case SimRuntimeTransportMode::NetworkOnly:
            return F("NETWORK_ONLY");
        case SimRuntimeTransportMode::Auto:
            return F("AUTO");
        case SimRuntimeTransportMode::Disabled:
        default:
            return F("DISABLED");
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

    bool parseTelemetryFrame(const String &payload, UsbTelemetryFrame &frame)
    {
        frame.idle = queryBool(payload, "idle", false);
        if (frame.idle)
        {
            const String seqValue = queryValue(payload, "seq");
            if (!seqValue.isEmpty())
            {
                frame.hasSeq = true;
                frame.seq = static_cast<uint32_t>(strtoul(seqValue.c_str(), nullptr, 10));
            }
            return true;
        }

        const String rpmValue = queryValue(payload, "rpm");
        const String speedValue = queryValue(payload, "speed");
        const String gearValue = queryValue(payload, "gear");
        if (rpmValue.isEmpty() || speedValue.isEmpty() || gearValue.isEmpty())
        {
            return false;
        }

        frame.rpm = clampInt(rpmValue.toInt(), 0, 20000);
        frame.speed = clampInt(speedValue.toInt(), 0, 600);
        frame.gear = clampInt(gearValue.toInt(), 0, 20);
        frame.throttle = constrain(kvFloat(payload, "throttle", 0.0f), 0.0f, 1.0f);
        frame.pitLimiterActive = queryBool(payload, "pit", false);
        frame.reportedMaxRpm = clampInt(queryInt(payload, "maxrpm", frame.rpm, 0, 25000), 0, 25000);

        const String seqValue = queryValue(payload, "seq");
        if (!seqValue.isEmpty())
        {
            frame.hasSeq = true;
            frame.seq = static_cast<uint32_t>(strtoul(seqValue.c_str(), nullptr, 10));
        }

        return true;
    }

    // Debounced state transition: candidate states must hold for at least
    // STATE_COMMIT_DEBOUNCE_MS before they overwrite the exposed global state.
    // This is the core fix for the "Verbindung wechselt ständig"-flicker.
    void setUsbState(UsbBridgeConnectionState state, const String &error = String())
    {
        const unsigned long nowMs = millis();

        // "Live" is trusted immediately — we never want to delay showing data.
        if (state == UsbBridgeConnectionState::Live)
        {
            g_pendingUsbState = state;
            g_pendingUsbError = error;
            g_pendingUsbStateSinceMs = nowMs;
            g_usbBridgeConnectionState = state;
            g_usbBridgeLastError = error;
            return;
        }

        // Same as current → nothing to do.
        if (state == g_usbBridgeConnectionState && error == g_usbBridgeLastError)
        {
            g_pendingUsbState = state;
            g_pendingUsbError = error;
            g_pendingUsbStateSinceMs = nowMs;
            return;
        }

        // New candidate — start its debounce timer.
        if (state != g_pendingUsbState || error != g_pendingUsbError)
        {
            g_pendingUsbState = state;
            g_pendingUsbError = error;
            g_pendingUsbStateSinceMs = nowMs;
            return;
        }

        // Candidate held long enough → commit.
        if ((nowMs - g_pendingUsbStateSinceMs) >= STATE_COMMIT_DEBOUNCE_MS)
        {
            g_usbBridgeConnectionState = state;
            g_usbBridgeLastError = error;
        }
    }

    unsigned long lastUsbBridgeActivityMs()
    {
        unsigned long latest = g_lastUsbBridgeHeartbeatMs;
        latest = max(latest, g_lastUsbRpcMs);
        latest = max(latest, g_lastUsbTelemetryMs);
        return latest;
    }

    void sendProtocolLine(const String &line)
    {
        // Serialise writes: reader task + loop() context may both want to emit.
        if (g_usbWriteMutex != nullptr && xSemaphoreTake(g_usbWriteMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            usbStream().println(line);
            xSemaphoreGive(g_usbWriteMutex);
        }
        else
        {
            usbStream().println(line);
        }
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

    // Returns the "data age" bucket using hysteresis so small jitter around the
    // boundary does not cause the exposed state to oscillate.
    //
    //   0 = fresh (Live zone)
    //   1 = dead zone (keep current state)
    //   2 = stale (downgrade zone)
    int telemetryAgeBucket(unsigned long nowMs)
    {
        if (g_lastUsbTelemetryMs == 0)
        {
            return 2;
        }
        const unsigned long age = nowMs - g_lastUsbTelemetryMs;
        if (age <= USB_TELEMETRY_LIVE_THRESHOLD_MS)
        {
            return 0;
        }
        if (age >= USB_TELEMETRY_STALE_THRESHOLD_MS)
        {
            return 2;
        }
        return 1;
    }

    void refreshUsbFlags(unsigned long nowMs)
    {
        g_usbSerialConnected = usbSerialConnected();

        const unsigned long lastActivityMs = lastUsbBridgeActivityMs();
        if (lastActivityMs == 0 || (nowMs - lastActivityMs) > USB_BRIDGE_HEARTBEAT_TIMEOUT_MS)
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

        const int ageBucket = telemetryAgeBucket(nowMs);
        if (ageBucket == 0)
        {
            setUsbState(UsbBridgeConnectionState::Live);
            return;
        }

        if (ageBucket == 1)
        {
            // Dead zone — keep whatever the committed state is. The debouncer in
            // setUsbState already refuses to commit changes that arrive faster
            // than STATE_COMMIT_DEBOUNCE_MS, so simply not proposing a new
            // state here is enough to keep things stable.
            setUsbState(g_usbBridgeConnectionState, g_usbBridgeLastError);
            return;
        }

        // ageBucket == 2 → definitively stale. Treat this as "waiting for
        // live game data" instead of an error so an idle SimHub session or a
        // just-closed game does not flash a red transport fault animation.
        setUsbState(UsbBridgeConnectionState::WaitingForData);
    }

    bool usbBridgeRecentlyActive(unsigned long nowMs)
    {
        if (g_usbBridgeConnected)
        {
            return true;
        }
        const unsigned long lastActivityMs = lastUsbBridgeActivityMs();
        return lastActivityMs > 0 && (nowMs - lastActivityMs) <= USB_BRIDGE_STICKY_TIMEOUT_MS;
    }

    void handleTelemetryMessage(const String &payload)
    {
        const unsigned long nowMs = millis();
        g_lastUsbBridgeHeartbeatMs = nowMs;
        g_usbBridgeConnected = true;

        UsbTelemetryFrame frame{};
        if (!parseTelemetryFrame(payload, frame))
        {
            ++g_usbTelemetryDebug.parseErrors;
            return;
        }

        // A Python-side "idle" marker tells us the bridge is alive but no real
        // game data is flowing. We count that as a heartbeat, NOT as telemetry,
        // so Auto-mode is free to fall back to OBD.
        if (frame.idle)
        {
            return;
        }

        const unsigned long previousFrameMs = g_usbTelemetryDebug.lastFrameMs;
        const int previousAcceptedRpm = g_usbTelemetryDebug.lastAcceptedRpm;
        const uint32_t previousSeq = g_usbTelemetryDebug.lastSeq;

        if (frame.hasSeq && previousSeq > 0)
        {
            if (frame.seq == previousSeq)
            {
                ++g_usbTelemetryDebug.seqDuplicates;
                return;
            }
            if (frame.seq > previousSeq + 1)
            {
                ++g_usbTelemetryDebug.seqGapEvents;
                g_usbTelemetryDebug.seqGapFrames += (frame.seq - previousSeq - 1U);
            }
        }

        if (previousFrameMs > 0)
        {
            const unsigned long gapMs = nowMs - previousFrameMs;
            g_usbTelemetryDebug.lastGapMs = gapMs;
            if (gapMs > g_usbTelemetryDebug.maxGapMs)
            {
                g_usbTelemetryDebug.maxGapMs = gapMs;
            }
            if (gapMs >= USB_TELEMETRY_DIAG_GAP_MS)
            {
                ++g_usbTelemetryDebug.gapEvents;
            }
        }

        const bool contiguousSeq =
            !frame.hasSeq || previousSeq == 0 || frame.seq == (previousSeq + 1U);
        if (previousFrameMs > 0 && contiguousSeq)
        {
            const unsigned long frameGapMs = nowMs - previousFrameMs;
            if (frame.rpm > previousAcceptedRpm &&
                isShortGapSpike(previousAcceptedRpm,
                                frame.rpm,
                                frameGapMs,
                                USB_TELEMETRY_GLITCH_RPM_DELTA,
                                USB_TELEMETRY_GLITCH_SHORT_GAP_MS))
            {
                ++g_usbTelemetryDebug.glitchRejects;
                ++g_usbTelemetryDebug.glitchRejectUpCount;
                g_usbTelemetryDebug.lastRawRpm = frame.rpm;
                g_usbTelemetryDebug.lastRejectedRpm = frame.rpm;
                return;
            }

            if (isShortGapDip(previousAcceptedRpm,
                              frame.rpm,
                              frameGapMs,
                              USB_TELEMETRY_GLITCH_RPM_DELTA,
                              USB_TELEMETRY_GLITCH_SHORT_GAP_MS))
            {
                ++g_usbTelemetryDebug.glitchRejects;
                ++g_usbTelemetryDebug.glitchRejectDownCount;
                g_usbTelemetryDebug.lastRawRpm = frame.rpm;
                g_usbTelemetryDebug.lastRejectedRpm = frame.rpm;
                return;
            }
        }

        // USB and network keep separate sample buffers so Auto can switch sources without
        // accidentally reusing the other transport's last values.
        g_usbSimCurrentRpm = frame.rpm;
        g_usbSimVehicleSpeedKmh = frame.speed;
        g_usbSimGear = frame.gear;
        g_usbSimThrottle = frame.throttle;
        g_usbSimPitLimiterActive = frame.pitLimiterActive;
        g_usbSimMaxSeenRpm = max(frame.reportedMaxRpm, max(g_usbSimMaxSeenRpm, g_usbSimCurrentRpm));
        g_lastUsbTelemetryMs = nowMs;
        g_usbTelemetryEverReceived = true;
        ++g_usbTelemetryDebug.framesReceived;
        g_usbTelemetryDebug.lastFrameMs = nowMs;
        g_usbTelemetryDebug.lastRawRpm = frame.rpm;
        g_usbTelemetryDebug.lastAcceptedRpm = frame.rpm;
        g_usbTelemetryDebug.lastRejectedRpm = 0;
        if (frame.hasSeq)
        {
            g_usbTelemetryDebug.lastSeq = frame.seq;
        }
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

    void handleProtocolLine(const String &rawLine)
    {
        // Resync: if garbage from a previous partial frame got concatenated in
        // front of a new frame (e.g. reconnect / briefly dropped bytes), the
        // valid "USBSIM " marker may sit somewhere in the middle of the line.
        // Search for the LAST occurrence so we always parse the freshest
        // message and drop whatever preceded it.
        int markerPos = rawLine.lastIndexOf("USBSIM ");
        if (markerPos < 0)
        {
            return;
        }

        // Anything before the marker is discarded silently. (Logging noise for
        // every malformed line would spam the serial console during reboots.)
        const String line = rawLine.substring(markerPos);

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

namespace
{
    // Body of the dedicated reader task: drains the CDC RX buffer every few
    // milliseconds, parses complete lines, and advances the state machine.
    // Running this outside of loop() means serial data is serviced even if
    // another module (HTTP, BLE, LED render) takes a long time.
    void usbSimBridgeTaskBody(void *)
    {
        TickType_t lastWake = xTaskGetTickCount();
        uint32_t stateRefreshAccum = 0;

        for (;;)
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
                    ++g_usbTelemetryDebug.lineOverflows;
                    setUsbState(UsbBridgeConnectionState::Error, F("Line overflow"));
                }
            }

            const unsigned long nowMs = millis();
            stateRefreshAccum += USB_TASK_POLL_INTERVAL_MS;
            if (stateRefreshAccum >= USB_STATE_REFRESH_INTERVAL_MS)
            {
                refreshUsbFlags(nowMs);
                stateRefreshAccum = 0;
                if (g_usbBridgeConnected && nowMs - g_lastHelloTxMs > 900)
                {
                    sendHelloFrame();
                }
            }

            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(USB_TASK_POLL_INTERVAL_MS));
        }
    }
}

void initUsbSimBridge()
{
    g_usbRxLine.reserve(USB_LINE_BUFFER_LIMIT);
    if (g_usbWriteMutex == nullptr)
    {
        g_usbWriteMutex = xSemaphoreCreateMutex();
    }
    refreshUsbFlags(millis());
}

void startUsbSimBridgeTask()
{
    if (g_usbBridgeTaskRunning)
    {
        return;
    }
    g_usbBridgeTaskRunning = true;

    // Core 0 keeps the reader independent from the Arduino loop (Core 1) and
    // the LED renderer. Priority 3 is above the loop task so CDC drain never
    // starves even when the rest of the system is busy.
    xTaskCreatePinnedToCore(
        usbSimBridgeTaskBody,
        "usbSimBridge",
        4096,
        nullptr,
        3,
        &g_usbBridgeTaskHandle,
        0);
}

void usbSimBridgeUpdateConfig()
{
    refreshUsbFlags(millis());
    setWifiSuspendedForUsb(usbSimShouldSuspendWifi());
}

void usbSimBridgeLoop()
{
    // Legacy entry point kept for backwards compatibility with any callers
    // that still pump the bridge from the main loop. When the dedicated task
    // is running this is a no-op; otherwise it serves as a fallback so the
    // bridge still works if someone skips startUsbSimBridgeTask().
    if (g_usbBridgeTaskRunning)
    {
        return;
    }

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
            ++g_usbTelemetryDebug.lineOverflows;
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
    return telemetryAllowsUsbSim(cfg.telemetryPreference, cfg.simTransportPreference);
}

bool usbSimBridgeOnline()
{
    return usbBridgeRecentlyActive(millis());
}

bool usbSimTelemetryFresh(unsigned long nowMs)
{
    return g_usbTelemetryEverReceived &&
           g_lastUsbTelemetryMs > 0 &&
           (nowMs - g_lastUsbTelemetryMs) <= USB_TELEMETRY_FRESH_TIMEOUT_MS;
}

bool usbSimShouldBlockObd()
{
    if (cfg.telemetryPreference == TelemetryPreference::SimHub)
    {
        return true;
    }

    if (cfg.telemetryPreference == TelemetryPreference::Auto)
    {
        return g_activeTelemetrySource == ActiveTelemetrySource::UsbSim;
    }

    return false;
}

bool usbSimShouldSuspendWifi()
{
    // Keep WiFi available even in USB-only so the local ESP web UI stays
    // reachable while telemetry itself remains locked to USB.
    return false;
}

String usbSimBuildStatusJson()
{
    const SimRuntimeTransportMode transportMode =
        resolveSimRuntimeTransportMode(cfg.telemetryPreference, cfg.simTransportPreference);
    const unsigned long nowMs = millis();
    const TelemetryDebugInfo telemetryInfo = telemetryGetDebugInfo();
    const LedRenderHistoryInfo ledHistory = ledBarGetRenderHistoryInfo();
    const TelemetryRenderSnapshot &telemetrySnapshot = telemetryInfo.snapshot;
    const WifiStatus wifiStatus = getWifiStatus();
    String wifiIp = wifiStatus.ip;
    if (wifiIp.isEmpty())
    {
        wifiIp = wifiStatus.staIp;
    }
    if (wifiIp.isEmpty())
    {
        wifiIp = wifiStatus.apIp;
    }
    const unsigned long usbTelemetryAgeMs =
        (g_usbTelemetryDebug.lastFrameMs > 0) ? (nowMs - g_usbTelemetryDebug.lastFrameMs) : 0;
    const unsigned long simHubLastOkAgeMs =
        (g_simHubDebug.lastSuccessMs > 0) ? (nowMs - g_simHubDebug.lastSuccessMs) : 0;
    const unsigned long simHubLastErrAgeMs =
        (g_simHubDebug.lastErrorMs > 0) ? (nowMs - g_simHubDebug.lastErrorMs) : 0;
    String json = "{";
    json += "\"ok\":true";
    json += ",\"rpm\":" + String(telemetrySnapshot.rpm);
    json += ",\"maxRpm\":" + String(telemetrySnapshot.maxSeenRpm);
    json += ",\"speed\":" + String(telemetrySnapshot.speedKmh);
    json += ",\"gear\":" + String(telemetrySnapshot.gear);
    json += ",\"gearSource\":\"" + jsonEscape(gearSourceLabel()) + "\"";
    json += ",\"throttle\":" + String(telemetrySnapshot.throttle, 3);
    json += ",\"pitLimiter\":" + String(telemetrySnapshot.pitLimiter ? "true" : "false");
    json += ",\"activeTelemetry\":\"" + activeTelemetryLabel(g_activeTelemetrySource) + "\"";
    json += ",\"telemetryPreference\":\"" + telemetryPreferenceLabel(cfg.telemetryPreference) + "\"";
    json += ",\"simTransport\":\"" + simTransportLabel(cfg.simTransportPreference) + "\"";
    json += ",\"simTransportMode\":\"" + simTransportModeLabel(transportMode) + "\"";
    json += ",\"telemetryFallback\":" + String(telemetrySourceIsFallback(g_activeTelemetrySource, cfg.telemetryPreference, cfg.simTransportPreference) ? "true" : "false");
    json += ",\"telemetrySnapshotVersion\":" + String(telemetrySnapshot.version);
    json += ",\"telemetrySnapshotAgeMs\":" + String(telemetrySnapshot.sampleTimestampMs > 0 ? (nowMs - telemetrySnapshot.sampleTimestampMs) : 0);
    json += ",\"telemetrySnapshotSource\":\"" + jsonEscape(String(telemetrySourceName(telemetrySnapshot.source))) + "\"";
    json += ",\"telemetrySnapshotFresh\":" + String(telemetrySnapshot.telemetryFresh ? "true" : "false");
    json += ",\"simSessionState\":\"" + jsonEscape(String(simSessionStateName(telemetrySnapshot.simSessionState))) + "\"";
    json += ",\"telemetrySourceTransitionCount\":" + String(telemetryInfo.sourceTransitionCount);
    json += ",\"telemetryLastSourceTransition\":\"" + jsonEscape(String(telemetrySourceName(telemetryInfo.lastSourceTransition.fromSource)) + " -> " + telemetrySourceName(telemetryInfo.lastSourceTransition.toSource)) + "\"";
    json += ",\"simSessionTransitionCount\":" + String(telemetryInfo.simSessionTransitionCount);
    json += ",\"simSessionSuppressedCount\":" + String(telemetryInfo.simSessionSuppressedCount);
    json += ",\"simSessionLastTransition\":\"" + jsonEscape(String(simSessionTransitionTypeName(telemetryInfo.lastSimSessionTransition.transition))) + "\"";
    json += ",\"simHubState\":\"" + simHubStateLabel(g_simHubConnectionState) + "\"";
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
    json += ",\"wifiIp\":\"" + jsonEscape(wifiIp) + "\"";
    json += ",\"wifiStaConnected\":" + String(wifiStatus.staConnected ? "true" : "false");
    json += ",\"wifiApActive\":" + String(wifiStatus.apActive ? "true" : "false");
    json += ",\"wifiSuspended\":" + String(isWifiSuspendedForUsb() ? "true" : "false");
    json += ",\"bleConnected\":" + String(g_connected ? "true" : "false");
    json += ",\"bleBlocked\":" + String(usbSimShouldBlockObd() ? "true" : "false");
    json += ",\"obdAllowed\":" + String(usbSimShouldBlockObd() ? "false" : "true");
    json += ",\"usbTelemetryAgeMs\":" + String(usbTelemetryAgeMs);
    json += ",\"usbTelemetryFrames\":" + String(g_usbTelemetryDebug.framesReceived);
    json += ",\"usbTelemetryParseErrors\":" + String(g_usbTelemetryDebug.parseErrors);
    json += ",\"usbTelemetryGlitchRejects\":" + String(g_usbTelemetryDebug.glitchRejects);
    json += ",\"usbTelemetryGlitchRejectUps\":" + String(g_usbTelemetryDebug.glitchRejectUpCount);
    json += ",\"usbTelemetryGlitchRejectDowns\":" + String(g_usbTelemetryDebug.glitchRejectDownCount);
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
    json += ",\"simHubLastOkAgeMs\":" + String(simHubLastOkAgeMs);
    json += ",\"simHubLastErrAgeMs\":" + String(simHubLastErrAgeMs);
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
    json += ",\"ledDiagnosticMode\":\"" + jsonEscape(String(ledBarGetDiagnosticModeName())) + "\"";
    json += ",\"ledRenderMode\":\"" + jsonEscape(String(ledBarGetLastRenderModeName())) + "\"";
    json += ",\"ledLastWriter\":\"" + jsonEscape(String(ledBarGetLastWriterName())) + "\"";
    json += ",\"ledLastShowAgeMs\":" + String(g_ledRenderDebug.lastShowMs > 0 ? (nowMs - g_ledRenderDebug.lastShowMs) : 0);
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
