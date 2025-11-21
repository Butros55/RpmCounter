#include "ble_obd.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <math.h>

#include "core/config.h"
#include "hardware/led_bar.h"
#include "hardware/logo_anim.h"
#include "core/state.h"
#include "core/vehicle_info.h"
#include "core/utils.h"
#include "hardware/display.h"

namespace
{
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
        if (g_vehicleSpeedKmh < 2 || g_currentRpm < 500)
        {
            if (g_estimatedGear != 0)
            {
                g_estimatedGear = 0;
                displaySetGear(g_estimatedGear);
            }
            return;
        }

        float ratio = (float)g_currentRpm / (float)max(g_vehicleSpeedKmh, 1);
        int candidate = pickGearFromRatio(ratio);
        unsigned long now = millis();

        if (candidate == g_estimatedGear)
        {
            return;
        }

        if (abs(candidate - g_estimatedGear) > 1 && (now - lastGearDecisionMs) < 300)
        {
            return;
        }

        g_estimatedGear = candidate;
        lastGearDecisionMs = now;
        displaySetGear(g_estimatedGear);
    }

    void processObdLine(const String &lineIn)
    {
        if (lineIn.length() == 0)
            return;

        String line = lineIn;
        line.trim();
        if (line.length() == 0)
            return;

        Serial.print("[OBD] ");
        Serial.println(line);

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

                if (rpm > g_maxSeenRpm)
                {
                    g_maxSeenRpm = rpm;
                }

                g_currentRpm = rpm;
                Serial.print("=> RPM: ");
                Serial.print(rpm);
                Serial.print("   (max: ");
                Serial.print(g_maxSeenRpm);
                Serial.println(")");

                unsigned long nowMs = millis();
                g_lastObdMs = nowMs;

                bool ignitionBefore = g_ignitionOn;
                bool engineBefore = g_engineRunning;

                g_ignitionOn = true;
                g_engineRunning = (rpm > ENGINE_START_RPM_THRESHOLD);

                if (!ignitionBefore && g_ignitionOn)
                {
                    g_engineStartLogoShown = false;
                    if (cfg.logoOnIgnitionOn && nowMs - g_lastLogoMs > LOGO_COOLDOWN_MS && !g_ignitionLogoShown)
                    {
                        Serial.println("[MLOGO] Zündung an – Animation");
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
                        Serial.println("[MLOGO] Motorstart – Animation");
                        g_logoPlayedThisCycle = true;
                        g_leavingPlayedThisCycle = false;
                        g_lastLogoMs = nowMs;
                        g_engineStartLogoShown = true;
                        showMLogoAnimation();
                    }
                }

                if (!g_testActive && !g_animationActive)
                {
                    updateRpmBar(rpm);
                }
            }
        }

        int speedIdx = compact.indexOf("410D");
        if (speedIdx >= 0 && speedIdx + 6 <= compact.length())
        {
            int value = hexByte(compact, speedIdx + 4);
            if (value >= 0)
            {
                g_vehicleSpeedKmh = value;
            }
        }

        updateGearEstimate();
    }

    static void notifyCallback(BLERemoteCharacteristic * /*characteristic*/, uint8_t *pData, size_t length, bool /*isNotify*/)
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
                if (c == '>')
                {
                    Serial.println(">");
                }
            }
            else
            {
                g_obdLine += c;
            }
        }
    }

    class MyClientCallback : public BLEClientCallbacks
    {
    void onConnect(BLEClient * /*pclient*/) override
    {
        Serial.println("BLE-Client: onConnect()");
        g_connected = true;
        setStatusLED(true);
        g_manualConnectActive = false;
        g_manualConnectRequested = false;
        g_manualConnectFailed = false;
    }

    void onDisconnect(BLEClient * /*pclient*/) override
    {
        Serial.println("BLE-Client: onDisconnect()");
        bool wasIgnition = g_ignitionOn;

            g_connected = false;
            g_charWrite = nullptr;
            g_charNotify = nullptr;
            g_ignitionOn = false;
            g_engineRunning = false;
            g_vehicleSpeedKmh = 0;
            g_estimatedGear = 0;
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

    Serial.print("Versuche Verbindung zu OBDII bei ");
    Serial.println(targetAddr);

    BLEAddress obdAddress(targetAddr.c_str());

    g_client = BLEDevice::createClient();
    g_client->setClientCallbacks(new MyClientCallback());

    g_client->setMTU(200);

    if (!g_client->connect(obdAddress))
    {
        Serial.println("❌ BLE connect() fehlgeschlagen.");
        return false;
    }

    Serial.println("✅ Verbunden, suche Service FFF0...");

    BLERemoteService *pService = g_client->getService(SERVICE_UUID);
    if (pService == nullptr)
    {
        Serial.println("❌ Service FFF0 nicht gefunden, trenne.");
        g_client->disconnect();
        return false;
    }

    g_charWrite = pService->getCharacteristic(CHAR_UUID_WRITE);
    g_charNotify = pService->getCharacteristic(CHAR_UUID_NOTIFY);

    if (!g_charWrite || !g_charNotify)
    {
        Serial.println("❌ Write-/Notify-Characteristic nicht gefunden, trenne.");
        g_client->disconnect();
        return false;
    }

    Serial.println("✅ Characteristics gefunden.");

    if (g_charNotify->canNotify())
    {
        g_charNotify->registerForNotify(notifyCallback);
    }
    else
    {
        Serial.println("WARNUNG: Notify-Characteristic kann nicht notify-en.");
    }

    Serial.println("🎉 BLE-Verbindung steht! Serial-Monitor kann weiterhin AT/OBD-Befehle schicken.");
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
        Serial.println("\r\n[!] Nicht verbunden, kann nicht senden.");
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
        Serial.print("[TX] ");
        Serial.println(info);

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
}

bool startConnectTask(bool manualAttempt)
{
    if (g_connectTaskRunning)
    {
        return false;
    }

    g_connectTaskRunning = true;
    g_connectTaskWasManual = manualAttempt;
    g_connectTaskResult = false;
    g_connectTaskStartMs = millis();
    g_bleConnectInProgress = true;
    g_bleConnectLastError = "";

    g_bleConnectTargetAddr = g_currentTargetAddr;
    g_bleConnectTargetName = g_currentTargetName;

    auto task = [](void *param) {
        bool manual = (bool)param;
        bool ok = connectToObd();
        g_connectTaskResult = ok;

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

    xTaskCreate(task, "bleConnectTask", 6144, (void *)manualAttempt, 1, nullptr);
    return true;
}

bool startBleScan(uint32_t durationSeconds)
{
    if (g_bleScanRunning || g_connectTaskRunning)
    {
        return false;
    }

    g_bleScanRunning = true;
    g_bleScanStartMs = millis();
    g_bleScanResults.clear();

    auto task = [](void *param) {
        uint32_t duration = (uint32_t)(uintptr_t)param;
        BLEScan *pScan = BLEDevice::getScan();
        pScan->setActiveScan(true);
        pScan->setInterval(100);
        pScan->setWindow(80);

        BLEScanResults results = pScan->start(duration, false);
        g_bleScanResults.clear();
        int count = results.getCount();
        for (int i = 0; i < count; i++)
        {
            BLEAdvertisedDevice dev = results.getDevice(i);
            BleDeviceInfo info;
            info.address = String(dev.getAddress().toString().c_str());
            std::string name = dev.getName();
            info.name = name.empty() ? info.address : String(name.c_str());
            if (g_autoReconnectPaused && !g_lastSuccessfulAddr.isEmpty() && info.address == g_lastSuccessfulAddr)
            {
                g_autoReconnectPaused = false;
                g_forceImmediateReconnect = true;
            }
            g_bleScanResults.push_back(info);
        }

        g_bleScanFinishedMs = millis();
        g_bleScanRunning = false;
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
    BLEDevice::init("ESP32-OBD-BLE");
    BLEDevice::setPower(ESP_PWR_LVL_P7);

    if (g_autoReconnect)
    {
        g_lastBleRetryMs = millis();
        startConnectTask(false);
    }
}

void bleObdLoop()
{
    unsigned long now = millis();
    const unsigned long HTTP_GRACE_MS = 5000;

    bool graceElapsed = (now - g_lastHttpMs > HTTP_GRACE_MS) || g_forceImmediateReconnect;

    g_bleConnectInProgress = g_connectTaskRunning || g_manualConnectActive;

    if (g_manualConnectRequested && !g_manualConnectActive)
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

    if (g_manualConnectActive && !g_connected && g_manualConnectAttempts > 0 && !g_connectTaskRunning && (now - g_lastBleRetryMs > MANUAL_CONNECT_RETRY_DELAY_MS))
    {
        g_lastBleRetryMs = now;
        Serial.println("[BLE] Manueller Verbindungsversuch...");
        startConnectTask(true);
    }

    if (!g_manualConnectActive && g_autoReconnect && !g_autoReconnectPaused && !g_connected && !g_connectTaskRunning && graceElapsed)
    {
        unsigned long interval = (g_autoReconnectAttempts < AUTO_RECONNECT_FAST_ATTEMPTS) ? AUTO_RECONNECT_FAST_INTERVAL_MS : AUTO_RECONNECT_SLOW_INTERVAL_MS;
        bool intervalReady = (now - g_lastBleRetryMs > interval) || g_forceImmediateReconnect;
        if (intervalReady)
        {
            g_lastBleRetryMs = now;
            bool immediate = g_forceImmediateReconnect;
            g_forceImmediateReconnect = false;
            if (immediate)
            {
                Serial.println("🔄 Manueller Sofort-Reconnect nach Save.");
            }
            else
            {
                Serial.println("🔄 Verbindung verloren – versuche Reconnect (auto)...");
            }
            startConnectTask(false);
        }
    }

    if (!g_testActive && g_connected && g_charWrite && (now - g_lastRpmRequest > RPM_INTERVAL_MS))
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

    while (Serial.available())
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
    }

    delay(1);
}
