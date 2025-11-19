#include "ble_obd.h"

#include <BLEDevice.h>
#include <BLEUtils.h>

#include "config.h"
#include "led_bar.h"
#include "logo_anim.h"
#include "state.h"
#include "vehicle_info.h"
#include "utils.h"

namespace
{
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
        }

        void onDisconnect(BLEClient * /*pclient*/) override
        {
            Serial.println("BLE-Client: onDisconnect()");
            bool wasIgnition = g_ignitionOn;

            g_connected = false;
            g_ignitionOn = false;
            g_engineRunning = false;
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
        if (!connectToObd())
        {
            Serial.println("❌ Initiale OBD-Verbindung fehlgeschlagen.");
        }
    }
}

void bleObdLoop()
{
    unsigned long now = millis();

    const unsigned long RECONNECT_INTERVAL_MS = 5000;
    const unsigned long HTTP_GRACE_MS = 5000;

    bool graceElapsed = (now - g_lastHttpMs > HTTP_GRACE_MS) || g_forceImmediateReconnect;
    bool intervalElapsed = (now - g_lastBleRetryMs > RECONNECT_INTERVAL_MS) || g_forceImmediateReconnect;
    if (g_autoReconnect && !g_connected && graceElapsed && intervalElapsed)
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
        connectToObd();
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
