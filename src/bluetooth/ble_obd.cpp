#include "ble_obd.h"

#if __has_include(<NimBLEDevice.h>)
#ifndef RPMCOUNTER_USE_NIMBLE
#define RPMCOUNTER_USE_NIMBLE 1
#endif
#include <NimBLEDevice.h>
#else
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#endif
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/config.h"
#include "hardware/led_bar.h"
#include "hardware/logo_anim.h"
#include "core/state.h"
#include "core/vehicle_info.h"
#include "core/utils.h"
#include "hardware/display.h"
#include "core/logging.h"
#include "telemetry/telemetry_manager.h"

namespace
{
#if RPMCOUNTER_USE_NIMBLE
    using BleDeviceClass = NimBLEDevice;
    using BleScan = NimBLEScan;
    using BleScanResults = NimBLEScanResults;
    using BleAdvertisedDevice = NimBLEAdvertisedDevice;
    using BleClient = NimBLEClient;
    using BleClientCallbacks = NimBLEClientCallbacks;
    using BleAddress = NimBLEAddress;
    using BleRemoteService = NimBLERemoteService;
    using BleRemoteCharacteristic = NimBLERemoteCharacteristic;
#else
    using BleDeviceClass = BLEDevice;
    using BleScan = BLEScan;
    using BleScanResults = BLEScanResults;
    using BleAdvertisedDevice = BLEAdvertisedDevice;
    using BleClient = BLEClient;
    using BleClientCallbacks = BLEClientCallbacks;
    using BleAddress = BLEAddress;
    using BleRemoteService = BLERemoteService;
    using BleRemoteCharacteristic = BLERemoteCharacteristic;
#endif
    constexpr float GEAR_RATIO_TABLE[] = {0.f, 95.f, 65.f, 48.f, 36.f, 28.f, 23.f, 19.f, 16.f};
    constexpr size_t GEAR_RATIO_COUNT = sizeof(GEAR_RATIO_TABLE) / sizeof(GEAR_RATIO_TABLE[0]);
    unsigned long lastGearDecisionMs = 0;

    int pickGearFromRatio(float ratio)
    {
        int bestGear = 0;
        float bestDiff = 1e9f;
        for (size_t i = 1; i < GEAR_RATIO_COUNT; ++i)
        {
            float diff = fabsf(ratio - GEAR_RATIO_TABLE[i]);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                bestGear = static_cast<int>(i);
            }
        }
        return bestGear;
    }

    void updateGearEstimate()
    {
        if (g_obdVehicleSpeedKmh < 2 || g_obdCurrentRpm < 500)
        {
            if (g_obdEstimatedGear != 0)
            {
                g_obdEstimatedGear = 0;
            }
            return;
        }

        float ratio = (float)g_obdCurrentRpm / (float)max(g_obdVehicleSpeedKmh, 1);
        int candidate = pickGearFromRatio(ratio);
        unsigned long now = millis();

        if (candidate == g_obdEstimatedGear)
        {
            return;
        }

        if (abs(candidate - g_obdEstimatedGear) > 1 && (now - lastGearDecisionMs) < 300)
        {
            return;
        }

        g_obdEstimatedGear = candidate;
        lastGearDecisionMs = now;
    }

    void processObdLine(const String &lineIn)
    {
        if (lineIn.length() == 0)
            return;

        String line = lineIn;
        line.trim();
        if (line.length() == 0)
            return;

        // Serial.print("[OBD] ");
        // Serial.println(line);

        g_lastObdInfo = line;

        String compact;
        for (int i = 0; i < line.length(); i++)
        {
            char c = line[i];
            if (c != ' ' && c != '\r' && c != '\n')
            {
                compact += c;
            }
        }
        compact.toUpperCase();

        handleVehicleInfoResponse(compact);

        int idx = compact.indexOf("410C");
        if (idx >= 0 && idx + 8 <= compact.length())
        {
            int a = hexByte(compact, idx + 4);
            int b = hexByte(compact, idx + 6);
            if (a >= 0 && b >= 0)
            {
                int raw = (a << 8) | b;
                int rpm = raw / 4;

                if (rpm > g_obdMaxSeenRpm)
                {
                    g_obdMaxSeenRpm = rpm;
                }

                g_obdCurrentRpm = rpm;
                static int lastLoggedRpm = -1;
                if (abs(rpm - lastLoggedRpm) >= 100)
                {
                    Serial.printf("[OBD] RPM: %d  (max: %d)\n", rpm, g_obdMaxSeenRpm);
                    lastLoggedRpm = rpm;
                }

                unsigned long nowMs = millis();
                g_lastObdTelemetryMs = nowMs;

                bool ignitionBefore = g_ignitionOn;
                bool engineBefore = g_engineRunning;

                g_ignitionOn = true;
                g_engineRunning = (rpm > ENGINE_START_RPM_THRESHOLD);

                if (!ignitionBefore && g_ignitionOn)
                {
                    g_engineStartLogoShown = false;
                    if (cfg.logoOnIgnitionOn && nowMs - g_lastLogoMs > LOGO_COOLDOWN_MS && !g_ignitionLogoShown)
                    {
                        Serial.println("[LOGO] Ignition ON - Playing M-Logo Animation");
                        g_logoPlayedThisCycle = true;
                        g_leavingPlayedThisCycle = false;
                        g_lastLogoMs = nowMs;
                        g_ignitionLogoShown = true;
                        showMLogoAnimation();
                    }
                }
                else if (!engineBefore && g_engineRunning)
                {
                    if (cfg.logoOnEngineStart && !g_engineStartLogoShown && nowMs - g_lastLogoMs > LOGO_COOLDOWN_MS)
                    {
                        Serial.println("[LOGO] Engine START - Playing M-Logo Animation");
                        g_logoPlayedThisCycle = true;
                        g_leavingPlayedThisCycle = false;
                        g_lastLogoMs = nowMs;
                        g_engineStartLogoShown = true;
                        showMLogoAnimation();
                    }
                }

                telemetryOnObdSample(g_obdCurrentRpm, g_obdVehicleSpeedKmh, g_obdEstimatedGear, nowMs);
            }
        }

        int speedIdx = compact.indexOf("410D");
        if (speedIdx >= 0 && speedIdx + 6 <= compact.length())
        {
            int value = hexByte(compact, speedIdx + 4);
            if (value >= 0)
            {
                g_obdVehicleSpeedKmh = value;
            }
        }

        updateGearEstimate();
        if (g_lastObdTelemetryMs > 0)
        {
            telemetryOnObdSample(g_obdCurrentRpm, g_obdVehicleSpeedKmh, g_obdEstimatedGear, g_lastObdTelemetryMs);
        }
    }

    static void notifyCallback(BleRemoteCharacteristic * /*characteristic*/, uint8_t *pData, size_t length, bool /*isNotify*/)
    {
        for (size_t i = 0; i < length; i++)
        {
            char c = (char)pData[i];

            if (c == '>' || c == '\r' || c == '\n')
            {
                if (g_obdLine.length() > 0)
                {
                    processObdLine(g_obdLine);
                    g_obdLine = "";
                }
                // if (c == '>')
                // {
                //     Serial.println(">");
                // }
            }
            else
            {
                g_obdLine += c;
            }
        }
    }

    bool subscribeToNotifications(BleRemoteCharacteristic *characteristic)
    {
        if (!characteristic)
        {
            return false;
        }

#if RPMCOUNTER_USE_NIMBLE
        if (characteristic->canNotify())
        {
            return characteristic->subscribe(true, notifyCallback);
        }
        if (characteristic->canIndicate())
        {
            return characteristic->subscribe(false, notifyCallback);
        }
        return false;
#else
        if (characteristic->canNotify())
        {
            characteristic->registerForNotify(notifyCallback);
            return true;
        }
        return false;
#endif
    }

    class MyClientCallback : public BleClientCallbacks
    {
        void onConnect(BleClient * /*pclient*/) override
        {
            LOG_INFO("BLE", "BLE_CONNECTED", "BLE client connected");
            g_connected = true;
            setStatusLED(true);
            g_manualConnectActive = false;
            g_manualConnectRequested = false;
            g_manualConnectFailed = false;
        }

        void onDisconnect(BleClient * /*pclient*/) override
        {
            LOG_INFO("BLE", "BLE_DISCONNECTED", "BLE client disconnected");
            bool wasIgnition = g_ignitionOn;

            g_connected = false;
            g_charWrite = nullptr;
            g_charNotify = nullptr;
            g_ignitionOn = false;
            g_engineRunning = false;
            telemetryOnObdDisconnected();
            displaySetGear(0);
            displaySetShiftBlink(false);
            setStatusLED(false);
            handleVehicleDisconnect();

            if (wasIgnition && cfg.logoOnIgnitionOff && !g_leavingPlayedThisCycle)
            {
                g_leavingPlayedThisCycle = true;
                showMLogoLeavingAnimation();
            }

            g_logoPlayedThisCycle = false;
            g_engineStartLogoShown = false;
            g_ignitionLogoShown = false;

            if (g_manualConnectActive)
            {
                g_manualConnectFailed = true;
                g_manualConnectActive = false;
            }
        }
    };
}

bool connectToObd(const String &address, const String &name)
{
    String targetAddr = address.length() > 0 ? address : String(TARGET_ADDR);
    if (name.length() > 0)
        g_currentTargetName = name;
    g_currentTargetAddr = targetAddr;

    g_charWrite = nullptr;
    g_charNotify = nullptr;

    unsigned long startMs = millis();
    LOG_INFO("BLE", "BLE_CONNECT_START", String("target=") + targetAddr + " name=" + g_currentTargetName);

    BleAddress obdAddress(targetAddr.c_str());

    g_client = BleDeviceClass::createClient();
    if (!g_client)
    {
        LOG_ERROR("BLE", "BLE_CLIENT_NULL", "BLE device client allocation failed");
        return false;
    }
    delay(1);
    g_client->setClientCallbacks(new MyClientCallback());

    g_client->setMTU(200);
    delay(1);

    if (!g_client->connect(obdAddress))
    {
        LOG_ERROR("BLE", "BLE_CONNECT_FAIL", "connect() returned false");
        return false;
    }

    LOG_INFO("BLE", "BLE_CONNECT_LINK", "Connected, discovering service FFF0");

    BleRemoteService *pService = g_client->getService(SERVICE_UUID);
    if (pService == nullptr)
    {
        LOG_ERROR("BLE", "BLE_SERVICE_MISSING", "Service FFF0 not found, disconnecting");
        g_client->disconnect();
        return false;
    }

    g_charWrite = pService->getCharacteristic(CHAR_UUID_WRITE);
    g_charNotify = pService->getCharacteristic(CHAR_UUID_NOTIFY);

    if (!g_charWrite || !g_charNotify)
    {
        LOG_ERROR("BLE", "BLE_CHAR_MISSING", "Write/Notify characteristics missing, disconnecting");
        g_client->disconnect();
        return false;
    }

    LOG_INFO("BLE", "BLE_CONNECT_CHARS", "Characteristics found");

    if (!subscribeToNotifications(g_charNotify))
    {
        LOG_ERROR("BLE", "BLE_NOTIFY_DISABLED", "Notify characteristic cannot notify");
    }

    LOG_INFO("BLE", "BLE_CONNECT_READY", String("Connected in ") + String(millis() - startMs) + "ms");
    requestVehicleInfo();
    return true;
}

bool connectToObd()
{
    return connectToObd(g_currentTargetAddr, g_currentTargetName);
}

void sendObdCommand(const String &cmd)
{
    if (!g_connected || !g_charWrite)
    {
        LOG_ERROR("BLE", "OBD_TX_DISCONNECTED", "Not connected, cannot send");
        return;
    }

    std::string s(cmd.c_str());
    if (s.empty() || s.back() != '\r')
    {
        s.push_back('\r');
    }

    unsigned long nowMs = millis();
    bool isRpmCmd = (cmd == "010C");

    if (!isRpmCmd || (nowMs - g_lastTxLogMs > TX_LOG_INTERVAL_MS))
    {
        String info = cmd + " @ " + String(nowMs / 1000) + "s";
        LOG_DEBUG("BLE", "OBD_TX", info);

        g_lastTxInfo = info;
        g_lastTxLogMs = nowMs;
    }

    g_charWrite->writeValue((uint8_t *)s.data(), s.size(), false);
}

void requestManualConnect(const String &address, const String &name, int attempts)
{
    g_currentTargetAddr = address.length() > 0 ? address : String(TARGET_ADDR);
    g_currentTargetName = name;
    g_manualConnectAttempts = (attempts > 0) ? attempts : MANUAL_CONNECT_RETRY_COUNT;
    g_manualConnectRequested = true;
    g_manualConnectFailed = false;
    g_manualConnectFinishMs = 0;
    g_autoReconnectPaused = true;
    g_forceImmediateReconnect = true;
    LOG_INFO("BLE", "BLE_MANUAL_REQUEST", String("addr=") + g_currentTargetAddr + " name=" + g_currentTargetName + " attempts=" + String(g_manualConnectAttempts));
}

bool startConnectTask(bool manualAttempt)
{
    if (g_connectTaskRunning)
    {
        return false;
    }

    if (g_bleScanRunning)
    {
        BleDeviceClass::getScan()->stop();
        g_bleScanRunning = false;
        g_bleScanFinishedMs = millis();
    }

    g_connectTaskRunning = true;
    g_connectTaskWasManual = manualAttempt;
    g_connectTaskResult = false;
    g_connectTaskStartMs = millis();
    g_bleConnectInProgress = true;
    g_bleConnectLastError = "";

    g_bleConnectTargetAddr = g_currentTargetAddr;
    g_bleConnectTargetName = g_currentTargetName;
    LOG_INFO("BLE", manualAttempt ? "BLE_CONNECT_TASK_MANUAL" : "BLE_CONNECT_TASK_AUTO", String("target=") + g_bleConnectTargetAddr + " name=" + g_bleConnectTargetName);

    auto task = [](void *param)
    {
        bool manual = (bool)param;
        bool ok = connectToObd();
        g_connectTaskResult = ok;
        unsigned long finishedMs = millis();
        LOG_INFO("BLE", manual ? "BLE_CONNECT_DONE_MANUAL" : "BLE_CONNECT_DONE_AUTO",
                 String(ok ? "ok" : "fail") + " duration_ms=" + String(finishedMs - g_connectTaskStartMs));

        if (manual)
        {
            if (ok)
            {
                g_manualConnectActive = false;
                g_manualConnectFailed = false;
                g_manualConnectAttempts = 0;
                g_autoReconnectAttempts = 0;
                g_lastSuccessfulAddr = g_currentTargetAddr;
                g_autoReconnectPaused = false;
                g_manualConnectFinishMs = millis();
                g_bleConnectLastError = "";
            }
            else
            {
                g_manualConnectAttempts--;
                if (g_manualConnectAttempts <= 0)
                {
                    g_manualConnectActive = false;
                    g_manualConnectFailed = true;
                    // Only keep auto reconnect paused if we have a previously known
                    // device to wait for; otherwise resume the automatic loop so it
                    // can retry once the dongle becomes available again.
                    g_autoReconnectPaused = !g_lastSuccessfulAddr.isEmpty();
                    g_manualConnectFinishMs = millis();
                    g_bleConnectLastError = F("Verbindung fehlgeschlagen");
                }
            }
        }
        else
        {
            if (ok)
            {
                g_autoReconnectAttempts = 0;
                g_lastSuccessfulAddr = g_currentTargetAddr;
                g_autoReconnectPaused = false;
            }
            else
            {
                g_autoReconnectAttempts++;
            }
        }

        g_connectTaskRunning = false;
        g_connectTaskFinishedMs = millis();
        g_bleConnectInProgress = false;
        vTaskDelete(nullptr);
    };

    xTaskCreate(task, "bleConnectTask", 6144, (void *)(uintptr_t)manualAttempt, 1, nullptr);
    return true;
}

bool startBleScan(uint32_t durationSeconds)
{
    if (g_bleScanRunning || g_connectTaskRunning || g_manualConnectActive || g_bleConnectInProgress)
    {
        LOG_DEBUG("BLE", "BLE_SCAN_SKIP", "Scan skipped because another task is active");
        return false;
    }

    g_bleScanRunning = true;
    g_bleScanStartMs = millis();
    g_bleScanResults.clear();
    LOG_INFO("BLE", "BLE_SCAN_START", String("duration_s=") + String(durationSeconds));

    auto task = [](void *param)
    {
        uint32_t duration = (uint32_t)(uintptr_t)param;
        BleScan *pScan = BleDeviceClass::getScan();
        pScan->setActiveScan(true);
        // Slightly reduce duty cycle to keep WiFi/AP responsive during scans.
        pScan->setInterval(140);
        pScan->setWindow(60);
        vTaskDelay(pdMS_TO_TICKS(2));

        BleScanResults *results = pScan->start(duration, false);
        if (results == nullptr)
        {
            g_bleScanFinishedMs = millis();
            g_bleScanRunning = false;
            LOG_ERROR("BLE", "BLE_SCAN_NULL", "scan returned no results");
            vTaskDelete(nullptr);
            return;
        }

        g_bleScanResults.clear();
        int count = results->getCount();
        for (int i = 0; i < count; i++)
        {
            BleAdvertisedDevice dev = results->getDevice(i);
            BleDeviceInfo info;
            info.address = String(dev.getAddress().toString().c_str());
            String name = dev.getName();
            info.name = name.length() ? name : info.address;
            if (g_autoReconnectPaused && !g_lastSuccessfulAddr.isEmpty() && info.address == g_lastSuccessfulAddr)
            {
                g_autoReconnectPaused = false;
                g_forceImmediateReconnect = true;
            }
            g_bleScanResults.push_back(info);
        }

        g_bleScanFinishedMs = millis();
        g_bleScanRunning = false;
        LOG_INFO("BLE", "BLE_SCAN_DONE", String("found=") + String(g_bleScanResults.size()) + " duration_ms=" + String(g_bleScanFinishedMs - g_bleScanStartMs));
        vTaskDelete(nullptr);
    };

    xTaskCreate(task, "bleScanTask", 4096, (void *)(uintptr_t)durationSeconds, 1, nullptr);
    return true;
}

bool isBleScanRunning()
{
    return g_bleScanRunning;
}

unsigned long lastBleScanFinished()
{
    return g_bleScanFinishedMs;
}

const std::vector<BleDeviceInfo> &getBleScanResults()
{
    return g_bleScanResults;
}

void initBle()
{
    BleDeviceClass::init("ESP32-OBD-BLE");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    BleDeviceClass::setPower(ESP_PWR_LVL_P9); // S3
#else
    BleDeviceClass::setPower(ESP_PWR_LVL_P7); // „normaler“ ESP32
#endif

    if (g_autoReconnect && telemetryShouldAllowObd())
    {
        g_lastBleRetryMs = millis();
        startConnectTask(false);
    }
}

void bleObdLoop()
{
    unsigned long now = millis();
    const unsigned long HTTP_GRACE_MS = 5000;
    const bool allowAutomaticObd = telemetryShouldAllowObd();

    g_bleConnectInProgress = g_connectTaskRunning || g_manualConnectActive;

    if (!allowAutomaticObd)
    {
        g_manualConnectRequested = false;
        g_manualConnectActive = false;
        g_manualConnectFailed = false;
        g_autoReconnectPaused = true;
        g_forceImmediateReconnect = false;
        g_bleConnectInProgress = g_connectTaskRunning;
        if (g_connected && g_client != nullptr)
        {
            g_client->disconnect();
        }
    }
    else if (g_autoReconnectPaused && !g_manualConnectActive)
    {
        g_autoReconnectPaused = false;
    }

    if (allowAutomaticObd && g_manualConnectRequested && !g_manualConnectActive)
    {
        g_manualConnectActive = true;
        g_manualConnectFailed = false;
        g_manualConnectStartMs = now;
        g_bleConnectTargetAddr = g_currentTargetAddr;
        g_bleConnectTargetName = g_currentTargetName;
        g_bleConnectLastError = "";
        g_bleConnectInProgress = true;
        g_forceImmediateReconnect = true;
        g_lastBleRetryMs = 0;
        g_manualConnectRequested = false;
    }

    if (allowAutomaticObd && g_manualConnectActive && !g_connected && g_manualConnectAttempts > 0 && !g_connectTaskRunning && (now - g_lastBleRetryMs > MANUAL_CONNECT_RETRY_DELAY_MS))
    {
        g_lastBleRetryMs = now;
        LOG_INFO("BLE", "BLE_MANUAL_ATTEMPT", String("attempts_left=") + String(g_manualConnectAttempts));
        startConnectTask(true);
    }

    bool autoReconnectReady = shouldAutoReconnectNow(now,
                                                     g_autoReconnect && allowAutomaticObd,
                                                     g_autoReconnectPaused,
                                                     g_connected,
                                                     g_connectTaskRunning,
                                                     g_manualConnectActive,
                                                     g_lastBleRetryMs,
                                                     g_autoReconnectAttempts,
                                                     HTTP_GRACE_MS,
                                                     g_lastHttpMs,
                                                     g_forceImmediateReconnect);
    if (autoReconnectReady)
    {
        g_lastBleRetryMs = now;
        bool immediate = g_forceImmediateReconnect;
        g_forceImmediateReconnect = false;
        if (immediate)
        {
            LOG_INFO("BLE", "BLE_AUTORECONNECT_FORCE", "Immediate reconnect triggered");
        }
        else
        {
            unsigned long intervalMs = computeAutoReconnectInterval(g_autoReconnectAttempts);
            LOG_INFO("BLE", "BLE_AUTORECONNECT", String("attempt=") + String(g_autoReconnectAttempts + 1) + " interval_ms=" + String(intervalMs));
        }
        startConnectTask(false);
    }

    if (allowAutomaticObd && !g_testActive && g_connected && g_charWrite && (now - g_lastRpmRequest > RPM_INTERVAL_MS))
    {
        static uint8_t speedDivider = 0;
        g_lastRpmRequest = now;
        sendObdCommand("010C");
        speedDivider++;
        if (speedDivider >= 3)
        {
            speedDivider = 0;
            delay(5);
            sendObdCommand("010D");
        }
    }

    while (allowAutomaticObd && Serial.available())
    {
        char c = (char)Serial.read();
        Serial.write(c);

        if (c == '\r' || c == '\n')
        {
            g_serialLine.trim();
            if (g_serialLine.length() > 0)
            {
                sendObdCommand(g_serialLine);
            }
            g_serialLine = "";
        }
        else
        {
            g_serialLine += c;
        }

        yield();
    }

    delay(1);
}
