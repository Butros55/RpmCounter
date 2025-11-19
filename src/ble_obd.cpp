#include "ble_obd.h"

#include <BLEDevice.h>
#include <BLEUtils.h>

#include "config.h"
#include "led_bar.h"
#include "logo_anim.h"
#include "state.h"
#include "utils.h"
#include "vehicle_info.h"

namespace
{
    unsigned long s_nextConnectAttemptMs = 0;

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

        int idx = compact.indexOf("410C");
        if (idx < 0)
        {
            return;
        }

        if (idx + 8 > compact.length())
        {
            return;
        }

        int a = hexByte(compact, idx + 4);
        int b = hexByte(compact, idx + 6);
        if (a < 0 || b < 0)
            return;

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

        if (!g_logoPlayedThisCycle)
        {
            if (!ignitionBefore && g_ignitionOn && cfg.logoOnIgnitionOn)
            {
                if (nowMs - g_lastLogoMs > LOGO_COOLDOWN_MS)
                {
                    Serial.println("[MLOGO] Zündung an – Animation");
                    g_logoPlayedThisCycle = true;
                    g_leavingPlayedThisCycle = false;
                    g_lastLogoMs = nowMs;
                    showMLogoAnimation();
                }
            }
            else if (!engineBefore && g_engineRunning && cfg.logoOnEngineStart)
            {
                if (nowMs - g_lastLogoMs > LOGO_COOLDOWN_MS)
                {
                    Serial.println("[MLOGO] Motorstart – Animation");
                    g_logoPlayedThisCycle = true;
                    g_leavingPlayedThisCycle = false;
                    g_lastLogoMs = nowMs;
                    showMLogoAnimation();
                }
            }
        }

        if (!g_testActive && !g_animationActive)
        {
            updateRpmBar(rpm);
        }
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

    void stopConnectLoop()
    {
        g_connectLoopActive = false;
        g_manualConnectLoop = false;
        g_connectLoopRemaining = 0;
        g_connectLoopTotal = 0;
        s_nextConnectAttemptMs = 0;
    }

    void handleConnectLoop()
    {
        if (!g_connectLoopActive)
        {
            return;
        }

        if (g_connected)
        {
            stopConnectLoop();
            return;
        }

        unsigned long now = millis();
        if (s_nextConnectAttemptMs != 0 && now < s_nextConnectAttemptMs)
        {
            return;
        }

        int attemptIdx = g_connectLoopTotal - g_connectLoopRemaining + 1;
        if (attemptIdx < 1)
            attemptIdx = 1;

        Serial.print(g_manualConnectLoop ? "[BLE] Manueller Versuch " : "[BLE] Auto-Versuch ");
        Serial.print(attemptIdx);
        Serial.print("/");
        Serial.print(g_connectLoopTotal);
        Serial.println("...");

        bool ok = connectToObd();
        if (ok)
        {
            stopConnectLoop();
            return;
        }

        if (g_connectLoopRemaining > 0)
        {
            g_connectLoopRemaining--;
        }

        if (g_connectLoopRemaining <= 0)
        {
            if (g_manualConnectLoop)
            {
                Serial.println("❌ Alle manuellen Verbindungsversuche ausgeschöpft.");
            }
            else
            {
                Serial.println("❌ Auto-Reconnect-Schleife beendet.");
            }
            stopConnectLoop();
        }
        else
        {
            s_nextConnectAttemptMs = now + 1500;
        }
    }

    class MyClientCallback : public BLEClientCallbacks
    {
        void onConnect(BLEClient * /*pclient*/) override
        {
            Serial.println("BLE-Client: onConnect()");
            g_connected = true;
            setStatusLED(true);
        }

        void onDisconnect(BLEClient * /*pclient*/) override
        {
            Serial.println("BLE-Client: onDisconnect()");
            bool wasIgnition = g_ignitionOn;

            g_connected = false;
            g_ignitionOn = false;
            g_engineRunning = false;
            g_lastObdMs = 0;
            g_vehicleDiagOk = false;
            g_vehicleInfoLoaded = false;
            setStatusLED(false);

            if (wasIgnition && cfg.logoOnIgnitionOff && !g_leavingPlayedThisCycle)
            {
                g_leavingPlayedThisCycle = true;
                showMLogoLeavingAnimation();
            }

            g_logoPlayedThisCycle = false;
        }
    };
}

bool connectToObd()
{
    Serial.print("Versuche Verbindung zu OBDII bei ");
    Serial.println(TARGET_ADDR);

    BLEAddress obdAddress(TARGET_ADDR);

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

void cancelConnectLoop()
{
    stopConnectLoop();
}

void scheduleConnectLoop(int attempts, bool manual)
{
    attempts = clampInt(attempts, 5, 10);

    g_connectLoopActive = true;
    g_manualConnectLoop = manual;
    g_connectLoopTotal = attempts;
    g_connectLoopRemaining = attempts;
    s_nextConnectAttemptMs = 0;
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

void initBle()
{
    BLEDevice::init("ESP32-OBD-BLE");
    BLEDevice::setPower(ESP_PWR_LVL_P7);

    if (g_autoReconnect)
    {
        scheduleConnectLoop(cfg.manualConnectAttempts, false);
    }
}

void bleObdLoop()
{
    unsigned long now = millis();

    handleConnectLoop();

    static unsigned long lastAutoSchedule = 0;
    const unsigned long RECONNECT_INTERVAL_MS = 5000;
    const unsigned long HTTP_GRACE_MS = 5000;

    if (g_autoReconnect && !g_connected && !g_connectLoopActive && now - lastAutoSchedule > RECONNECT_INTERVAL_MS && now - g_lastHttpMs > HTTP_GRACE_MS)
    {
        lastAutoSchedule = now;
        Serial.println("🔄 Verbindung verloren – starte Auto-Reconnect-Schleife...");
        scheduleConnectLoop(cfg.manualConnectAttempts, false);
    }

    if (!g_testActive && g_connected && g_charWrite && (now - g_lastRpmRequest > RPM_INTERVAL_MS))
    {
        g_lastRpmRequest = now;
        sendObdCommand("010C");
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
