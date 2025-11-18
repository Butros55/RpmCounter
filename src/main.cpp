#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>

// ================== NeoPixel-Konfiguration ==================
#define LED_PIN 5             // GPIO für Datenleitung des Strips
#define NUM_LEDS 30           // Länge deines Strips anpassen
#define DEFAULT_BRIGHTNESS 80 // 0–255

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ================== BLE-OBD-Konfiguration ===================
// UUIDs laut Car Scanner / OBD-Dongle
static BLEUUID SERVICE_UUID("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID CHAR_UUID_NOTIFY("0000fff1-0000-1000-8000-00805f9b34fb"); // OBD -> ESP32
static BLEUUID CHAR_UUID_WRITE("0000fff2-0000-1000-8000-00805f9b34fb");  // ESP32 -> OBD

// MAC-Adresse deines OBDII-Adapters
static const char *TARGET_ADDR = "66:1e:32:9d:2e:5d";

// ================== Status-LED ==============================
const int STATUS_LED_PIN = 2;

// ================== Globaler BLE/Zustand ====================
BLEClient *g_client = nullptr;
BLERemoteCharacteristic *g_charWrite = nullptr;
BLERemoteCharacteristic *g_charNotify = nullptr;

bool g_connected = false;
String g_serialLine; // Eingaben vom Serial-Monitor
String g_obdLine;    // Eingehende OBD-Zeilen (Notify)

int g_currentRpm = 0; // letzte erkannte Drehzahl
int g_maxSeenRpm = 0; // höchste bisher gesehene Drehzahl

unsigned long g_lastRpmRequest = 0;
const unsigned long RPM_INTERVAL_MS = 100; // etwas schneller: alle 100 ms 010C senden

// ================== ShiftLight-Konfiguration =================
struct ShiftConfig
{
  bool autoScaleMaxRpm; // true = nutze g_maxSeenRpm, false = fixedMaxRpm
  int fixedMaxRpm;      // nur genutzt wenn autoScaleMaxRpm == false
  int greenEndPct;      // Prozent 0–100: Ende Grün-Bereich
  int yellowEndPct;     // Prozent 0–100: Ende Gelb-Bereich
  int blinkStartPct;    // Prozent 0–100: Ab hier blinkt Rot
  int brightness;       // 0–255
  int mode;             // 0 = Casual, 1 = F1-Style, 2 = Überempfindlich

  // Logo-Optionen
  bool logoOnIgnitionOn;  // M-Logo bei Zündung an
  bool logoOnEngineStart; // M-Logo bei Motorstart
  bool logoOnIgnitionOff; // Leaving-Animation bei Zündung aus
};

ShiftConfig cfg = {
    true,  // autoScaleMaxRpm
    5000,  // fixedMaxRpm
    60,    // greenEndPct
    85,    // yellowEndPct
    90,    // blinkStartPct
    DEFAULT_BRIGHTNESS,
    1,     // mode = F1-Style
    true,  // logoOnIgnitionOn
    true,  // logoOnEngineStart
    true   // logoOnIgnitionOff
};

// ================== Reconnect / Developer ====================
bool g_autoReconnect = true;
bool g_devMode = true; // Developer-Modus standardmäßig AN

// ================== Test-Sweep-Konfiguration =================
bool g_testActive = false;
unsigned long g_testStartMs = 0;
const unsigned long TEST_SWEEP_DURATION = 5000; // 5 Sekunden
int g_testMaxRpm = 4000;

// ================== BMW M Logo / Zündung / Motor =============
bool g_brightnessPreviewActive = false;
unsigned long g_lastBrightnessChangeMs = 0;

// Zündung / Motor / Timings
bool g_ignitionOn = false;
bool g_engineRunning = false;
unsigned long g_lastObdMs = 0;  // wann zuletzt OBD-Daten
unsigned long g_lastLogoMs = 0; // wann zuletzt Logo-Animation

const unsigned long IGNITION_TIMEOUT_MS = 8000; // nach 8 s ohne OBD -> Zündung aus
const unsigned long LOGO_COOLDOWN_MS = 2000;    // mind. 2s zwischen Logos
const int ENGINE_START_RPM_THRESHOLD = 400;     // ab ~400 rpm = Motor läuft

// Animations-Zustände
bool g_animationActive = false;     // solange true, keine RPM-Bar-Updates
bool g_logoPlayedThisCycle = false; // pro Zündungszyklus max. 1 Logo
bool g_leavingPlayedThisCycle = false;

// ================== Debug / Logging ==========================
String g_lastTxInfo;
String g_lastObdInfo;
unsigned long g_lastTxLogMs = 0;
const unsigned long TX_LOG_INTERVAL_MS = 2500; // ~2,5 s für 010C-Logs

// ================== WiFi / Webserver ========================
const char *AP_SSID = "ShiftLight-ESP32";
const char *AP_PASS = "shift1234";

WebServer server(80);

// Letzter HTTP-Zugriff (für Auto-Reconnect-„Grace Period“)
unsigned long g_lastHttpMs = 0;

// Vorwärtsdeklarationen
void updateRpmBar(int rpm);
void processObdLine(const String &lineIn);
bool connectToObd();
void sendObdCommand(const String &cmd);
void handleRoot();
void handleSave();
void handleTest();
void handleBrightness();
void handleConnect();
void handleDisconnect();
void handleStatus();
void handleSettingsGet();
void handleSettingsSave();
void showMLogoAnimation();
void showMLogoPreview();
void showMLogoLeavingAnimation();

// ================== LED-Helfer ==============================
void setStatusLED(bool on)
{
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
}

// ================== BMW M Logo (statisch, für Preview) ======
void showMLogoPreview()
{
  const uint8_t lbR = 0, lbG = 120, lbB = 255; // light blue
  const uint8_t dbR = 0, dbG = 0, dbB = 120;   // dark blue
  const uint8_t rR = 255, rG = 0, rB = 0;      // red

  int segLen = NUM_LEDS / 3;
  if (segLen < 2)
    segLen = 2;

  float brightnessFactor = cfg.brightness / 255.0f;
  if (brightnessFactor < 0.02f)
    brightnessFactor = 0.02f;

  for (int i = 0; i < NUM_LEDS; i++)
  {
    uint8_t br = 0, bg = 0, bb = 0;
    if (i < segLen)
    {
      br = lbR;
      bg = lbG;
      bb = lbB;
    }
    else if (i < 2 * segLen)
    {
      br = dbR;
      bg = dbG;
      bb = dbB;
    }
    else if (i < 3 * segLen)
    {
      br = rR;
      bg = rG;
      bb = rB;
    }
    else
    {
      br = bg = bb = 0;
    }

    uint8_t r = (uint8_t)(br * brightnessFactor);
    uint8_t g = (uint8_t)(bg * brightnessFactor);
    uint8_t b = (uint8_t)(bb * brightnessFactor);
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

// ================== BMW M Logo Animation (Auf) ===============
void showMLogoAnimation()
{
  if (g_animationActive)
    return;
  g_animationActive = true;

  Serial.println("[MLOGO] Starte BMW M Boot-Animation");

  const uint8_t lbR = 0, lbG = 120, lbB = 255;
  const uint8_t dbR = 0, dbG = 0, dbB = 120;
  const uint8_t rR = 255, rG = 0, rB = 0;

  int segLen = NUM_LEDS / 3;
  if (segLen < 2)
    segLen = 2;

  uint8_t baseR[NUM_LEDS];
  uint8_t baseG[NUM_LEDS];
  uint8_t baseB[NUM_LEDS];

  for (int i = 0; i < NUM_LEDS; i++)
  {
    if (i < segLen)
    {
      baseR[i] = lbR;
      baseG[i] = lbG;
      baseB[i] = lbB;
    }
    else if (i < 2 * segLen)
    {
      baseR[i] = dbR;
      baseG[i] = dbG;
      baseB[i] = dbB;
    }
    else if (i < 3 * segLen)
    {
      baseR[i] = rR;
      baseG[i] = rG;
      baseB[i] = rB;
    }
    else
    {
      baseR[i] = baseG[i] = baseB[i] = 0;
    }
  }

  const int steps = 40;      // halb so viele Schritte
  const int frameDelay = 10; // schneller -> ~0,8s gesamt

  // Fade-In
  for (int s = 0; s <= steps; s++)
  {
    float t = (float)s / (float)steps; // 0..1
    float eased = t * t;               // etwas smoother
    float f = eased;

    for (int i = 0; i < NUM_LEDS; i++)
    {
      uint8_t r = (uint8_t)(baseR[i] * f);
      uint8_t g = (uint8_t)(baseG[i] * f);
      uint8_t b = (uint8_t)(baseB[i] * f);
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
    delay(frameDelay);
  }

  delay(200);

  // Fade-Out
  for (int s = steps; s >= 0; s--)
  {
    float t = (float)s / (float)steps; // 1..0
    float eased = t * t;
    float f = eased;

    for (int i = 0; i < NUM_LEDS; i++)
    {
      uint8_t r = (uint8_t)(baseR[i] * f);
      uint8_t g = (uint8_t)(baseG[i] * f);
      uint8_t b = (uint8_t)(baseB[i] * f);
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
    delay(frameDelay);
  }

  strip.clear();
  strip.show();

  Serial.println("[MLOGO] Animation fertig");

  g_animationActive = false;

  // Nach Logo direkt wieder RPM-Bar zeichnen (falls Zündung an und Test aus)
  if (!g_testActive && g_currentRpm > 0)
  {
    updateRpmBar(g_currentRpm);
  }
}

// ================== BMW M Leaving Animation =================
void showMLogoLeavingAnimation()
{
  if (g_animationActive)
    return;
  g_animationActive = true;

  Serial.println("[MLOGO] Starte Leaving-Animation");

  const uint8_t lbR = 0, lbG = 120, lbB = 255;
  const uint8_t dbR = 0, dbG = 0, dbB = 120;
  const uint8_t rR = 255, rG = 0, rB = 0;

  int segLen = NUM_LEDS / 3;
  if (segLen < 2)
    segLen = 2;

  uint8_t baseR[NUM_LEDS];
  uint8_t baseG[NUM_LEDS];
  uint8_t baseB[NUM_LEDS];

  for (int i = 0; i < NUM_LEDS; i++)
  {
    if (i < segLen)
    {
      baseR[i] = lbR;
      baseG[i] = lbG;
      baseB[i] = lbB;
    }
    else if (i < 2 * segLen)
    {
      baseR[i] = dbR;
      baseG[i] = dbG;
      baseB[i] = dbB;
    }
    else if (i < 3 * segLen)
    {
      baseR[i] = rR;
      baseG[i] = rG;
      baseB[i] = rB;
    }
    else
    {
      baseR[i] = baseG[i] = baseB[i] = 0;
    }
  }

  const int steps = 30;
  const int frameDelay = 15;

  // Einfacher Fade-Out von vollem Logo
  for (int s = 0; s <= steps; s++)
  {
    float t = (float)s / (float)steps; // 0..1
    float eased = t * t;
    float f = 1.0f - eased; // 1 -> 0

    for (int i = 0; i < NUM_LEDS; i++)
    {
      uint8_t r = (uint8_t)(baseR[i] * f);
      uint8_t g = (uint8_t)(baseG[i] * f);
      uint8_t b = (uint8_t)(baseB[i] * f);
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
    delay(frameDelay);
  }

  strip.clear();
  strip.show();

  Serial.println("[MLOGO] Leaving-Animation fertig");
  g_animationActive = false;
}

// ================== RPM -> LED-Bar Logik ====================

void updateRpmBar(int rpm)
{
  if (g_animationActive)
  {
    // während Logo/Leaving keine RPM-Bar schreiben
    return;
  }

  if (rpm < 0)
    rpm = 0;

  int maxRpmForBar;
  if (cfg.autoScaleMaxRpm)
  {
    maxRpmForBar = g_maxSeenRpm;
    if (maxRpmForBar < 2000)
    {
      maxRpmForBar = 2000;
    }
  }
  else
  {
    maxRpmForBar = (cfg.fixedMaxRpm > 1000) ? cfg.fixedMaxRpm : 2000;
  }

  float fraction = (float)rpm / (float)maxRpmForBar;
  if (fraction > 1.0f)
    fraction = 1.0f;

  float greenEnd = cfg.greenEndPct / 100.0f;
  float yellowEnd = cfg.yellowEndPct / 100.0f;
  float blinkStart = cfg.blinkStartPct / 100.0f;

  if (greenEnd < 0.0f)
    greenEnd = 0.0f;
  if (greenEnd > 1.0f)
    greenEnd = 1.0f;
  if (yellowEnd < greenEnd)
    yellowEnd = greenEnd;
  if (yellowEnd > 1.0f)
    yellowEnd = 1.0f;
  if (blinkStart < yellowEnd)
    blinkStart = yellowEnd;
  if (blinkStart > 1.0f)
    blinkStart = 1.0f;

  int ledsOn = (int)round(fraction * NUM_LEDS);

  static unsigned long lastBlink = 0;
  static bool blinkState = false;
  unsigned long now = millis();

  if (g_brightnessPreviewActive && (now - g_lastBrightnessChangeMs > 1000))
  {
    g_brightnessPreviewActive = false;

    if (!g_testActive)
    {
      if (g_currentRpm > 0)
      {
        updateRpmBar(g_currentRpm);
      }
      else
      {
        strip.clear();
        strip.show();
      }
    }
  }

  bool shiftBlink = ((cfg.mode == 1 || cfg.mode == 2) && fraction >= blinkStart);

  if (shiftBlink && now - lastBlink > 100)
  {
    lastBlink = now;
    blinkState = !blinkState;
  }

  if (cfg.mode == 2 && fraction >= blinkStart)
  {
    ledsOn = NUM_LEDS;
  }

  strip.clear();

  for (int i = 0; i < NUM_LEDS; i++)
  {
    uint32_t color = strip.Color(0, 0, 0);

    if (i < ledsOn)
    {
      float pos = (float)i / (float)(NUM_LEDS - 1);

      if (cfg.mode == 2 && fraction >= blinkStart)
      {
        color = blinkState ? strip.Color(255, 0, 0) : strip.Color(0, 0, 0);
      }
      else
      {
        if (pos < greenEnd)
        {
          color = strip.Color(0, 255, 0);
        }
        else if (pos < yellowEnd)
        {
          color = strip.Color(255, 180, 0);
        }
        else
        {
          if (cfg.mode == 1 && shiftBlink)
          {
            color = blinkState ? strip.Color(255, 0, 0) : strip.Color(0, 0, 0);
          }
          else
          {
            color = strip.Color(255, 0, 0);
          }
        }
      }
    }

    strip.setPixelColor(i, color);
  }

  strip.show();

  Serial.print("[LED] rpm=");
  Serial.print(rpm);
  Serial.print(" fraction=");
  Serial.print(fraction, 2);
  Serial.print(" ledsOn=");
  Serial.println(ledsOn);
}

// ================== HEX-Helfer ==============================
int hexByte(const String &s, int idx)
{
  if (idx + 2 > s.length())
    return -1;
  char buf[3];
  buf[0] = s[idx];
  buf[1] = s[idx + 1];
  buf[2] = '\0';
  return (int)strtol(buf, nullptr, 16);
}

// ================== OBD-Zeile verarbeiten ===================
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

  // fürs Web-UI speichern
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

  // Zündung / Motor aktualisieren
  unsigned long nowMs = millis();
  g_lastObdMs = nowMs;

  bool ignitionBefore = g_ignitionOn;
  bool engineBefore = g_engineRunning;

  g_ignitionOn = true;
  g_engineRunning = (rpm > ENGINE_START_RPM_THRESHOLD);

  // Nur eine Logo-Animation pro Zyklus
  if (!g_logoPlayedThisCycle)
  {
    // Zündung an
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
    // Motorstart
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

// ================== BLE Notify Callback =====================
static void notifyCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
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

// ================== BLE Client Callback =====================
class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient) override
  {
    Serial.println("BLE-Client: onConnect()");
    g_connected = true;
    setStatusLED(true);
  }

  void onDisconnect(BLEClient *pclient) override
  {
    Serial.println("BLE-Client: onDisconnect()");
    bool wasIgnition = g_ignitionOn;

    g_connected = false;
    g_ignitionOn = false;
    g_engineRunning = false;
    setStatusLED(false);

    if (wasIgnition && cfg.logoOnIgnitionOff && !g_leavingPlayedThisCycle)
    {
      g_leavingPlayedThisCycle = true;
      showMLogoLeavingAnimation();
    }

    // Neuer Zyklus möglich
    g_logoPlayedThisCycle = false;
  }
};

// ================== OBD-Verbindung ==========================
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
  return true;
}

// ================== OBD-Befehl senden =======================
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

  // Manuelle Befehle immer loggen, RPM-Requests nur alle 2,5 s
  if (!isRpmCmd || (nowMs - g_lastTxLogMs > TX_LOG_INTERVAL_MS))
  {
    String info = cmd + " @ " + String(nowMs / 1000) + "s";
    Serial.print("[TX] ");
    Serial.println(info);

    g_lastTxInfo = info; // fürs Web-UI
    g_lastTxLogMs = nowMs;
  }

  g_charWrite->writeValue((uint8_t *)s.data(), s.size(), false);
}

// ================== Webserver: HTML-Seite ===================

String htmlPage()
{
  String page;
  page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>ShiftLight Setup</title>");
  page += F("<style>"
            "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#111;"
            "color:#eee;padding:16px;margin:0;}"
            "h1{font-size:20px;margin:0 0 12px 0;display:flex;"
            "align-items:center;justify-content:space-between;}"
            "h2{font-size:16px;margin:16px 0 8px 0;}"
            "label{display:block;margin-top:8px;}"
            "input,select{width:100%;padding:6px;margin-top:4px;"
            "border-radius:6px;border:1px solid #444;background:#222;color:#eee;}"
            "input[type=range]{padding:0;margin-top:4px;}"
            "button{margin-top:12px;width:100%;padding:10px;border:none;border-radius:6px;"
            "background:#0af;color:#000;font-weight:bold;font-size:14px;}"
            "button:disabled{background:#555;color:#888;}"
            ".btn-danger{background:#d33;color:#fff;}"
            ".row{margin-bottom:6px;}"
            ".small{font-size:12px;color:#aaa;}"
            ".section{margin-top:12px;padding:10px 12px;border-radius:8px;"
            "background:#181818;border:1px solid #333;}"
            ".section-title{font-weight:600;margin-bottom:4px;font-size:14px;}"
            ".toggle-row{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
            ".toggle-label{font-size:14px;}"
            ".switch{position:relative;display:inline-block;width:46px;height:24px;margin-left:8px;}"
            ".switch input{opacity:0;width:0;height:0;}"
            ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;"
            "background:#555;transition:.2s;border-radius:24px;}"
            ".slider:before{position:absolute;content:'';height:18px;width:18px;left:3px;top:3px;"
            "background:#fff;transition:.2s;border-radius:50%;}"
            ".switch input:checked + .slider{background:#0af;}"
            ".switch input:checked + .slider:before{transform:translateX(22px);}"
            ".status-line{font-size:12px;color:#ccc;margin-top:4px;}"
            ".spinner{display:inline-block;width:12px;height:12px;border-radius:50%;"
            "border:2px solid rgba(255,255,255,0.2);border-top-color:#0af;"
            "animation:spin 1s linear infinite;margin-left:6px;}"
            ".hidden{display:none;}"
            "@keyframes spin{from{transform:rotate(0deg);}to{transform:rotate(360deg);}}"
            "</style></head><body>");

  page += "<h1><span>ShiftLight Setup</span>"
          "<a href=\"/settings\" style='text-decoration:none;color:#0af;font-size:20px;'>⚙️</a>"
          "</h1>";

  page += F("<form id='mainForm' method='POST' action='/save'>");

  // === Allgemein / Mode / Brightness ===
  page += F("<div class='section'>");
  page += F("<div class='section-title'>Allgemein</div>");

  // Mode
  page += F("<label>Mode</label><select name='mode'>");
  page += "<option value='0'";
  if (cfg.mode == 0)
    page += " selected";
  page += ">Casual</option>";
  page += "<option value='1'";
  if (cfg.mode == 1)
    page += " selected";
  page += ">F1-Style</option>";
  page += "<option value='2'";
  if (cfg.mode == 2)
    page += " selected";
  page += ">Überempfindlich</option>";
  page += "</select>";

  // Brightness – Slider + Live-Value + hidden field fürs POST
  page += F("<label>Brightness (0-255)</label>");
  page += "<input type='range' min='0' max='255' value='";
  page += String(cfg.brightness);
  page += "' id='brightness_slider' oninput='onBrightnessChange(this.value)'>";
  page += "<div class='small'>Wert: <span id='bval'>";
  page += String(cfg.brightness);
  page += "</span></div>";
  page += "<input type='hidden' name='brightness' id='brightness' value='";
  page += String(cfg.brightness);
  page += "'>";

  page += F("</div>"); // section Allgemein

  // === Drehzahl-Bereich ===
  page += F("<div class='section'>");
  page += F("<div class='section-title'>Drehzahl-Bereich</div>");

  // autoScale als Toggle
  page += F("<div class='toggle-row'><span class='toggle-label'>"
            "Auto-Scale Max RPM (benutze max gesehene Drehzahl)"
            "</span><label class='switch'>");
  page += "<input type='checkbox' name='autoscale' ";
  if (cfg.autoScaleMaxRpm)
    page += "checked";
  page += "><span class='slider'></span></label></div>";

  // fixed max RPM
  page += F("<label>Fixed Max RPM (wenn Auto-Scale aus)</label>");
  page += "<input type='number' name='fixedMaxRpm' min='1000' max='8000' value='";
  page += String(cfg.fixedMaxRpm);
  page += "'>";

  // Green End – Slider
  page += F("<label>Green End (% von Max RPM)</label>");
  page += "<input type='range' name='greenEndPct' min='0' max='100' value='";
  page += String(cfg.greenEndPct);
  page += "' id='greenEndSlider'>";
  page += "<div class='small'>Wert: <span id='greenEndVal'>";
  page += String(cfg.greenEndPct);
  page += "%</span></div>";

  // Yellow End – Slider
  page += F("<label>Yellow End (% von Max RPM)</label>");
  page += "<input type='range' name='yellowEndPct' min='0' max='100' value='";
  page += String(cfg.yellowEndPct);
  page += "' id='yellowEndSlider'>";
  page += "<div class='small'>Wert: <span id='yellowEndVal'>";
  page += String(cfg.yellowEndPct);
  page += "%</span></div>";

  // Blink Start – Slider
  page += F("<label>Blink Start (% von Max RPM)</label>");
  page += "<input type='range' name='blinkStartPct' min='0' max='100' value='";
  page += String(cfg.blinkStartPct);
  page += "' id='blinkStartSlider'>";
  page += "<div class='small'>Wert: <span id='blinkStartVal'>";
  page += String(cfg.blinkStartPct);
  page += "%</span></div>";

  page += F("</div>"); // section Drehzahl-Bereich

  // === Coming-Home / Leaving (Logos) ===
  page += F("<div class='section'>");
  page += F("<div class='section-title'>Coming-Home / Leaving</div>");

  // Logo-Trigger-Checkboxen als Toggles
  page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Zündung an</span>"
            "<label class='switch'>");
  page += "<input type='checkbox' name='logoIgnOn' ";
  if (cfg.logoOnIgnitionOn)
    page += "checked";
  page += "><span class='slider'></span></label></div>";

  page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Motorstart</span>"
            "<label class='switch'>");
  page += "<input type='checkbox' name='logoEngStart' ";
  if (cfg.logoOnEngineStart)
    page += "checked";
  page += "><span class='slider'></span></label></div>";

  page += F("<div class='toggle-row'><span class='toggle-label'>Leaving-Animation bei Zündung aus</span>"
            "<label class='switch'>");
  page += "<input type='checkbox' name='logoIgnOff' ";
  if (cfg.logoOnIgnitionOff)
    page += "checked";
  page += "><span class='slider'></span></label></div>";

  page += F("</div>"); // section Logos

  // === Verbindungs- & Debug-Bereich (nur Developer-Mode) ===
  if (g_devMode)
  {
    page += F("<div class='section'>");
    page += F("<div class='section-title'>OBD / Verbindung</div>");

    // Auto-Reconnect Toggle
    page += F("<div class='toggle-row'><span class='toggle-label'>"
              "OBD automatisch verbinden (Reconnect)"
              "</span><label class='switch'>");
    page += "<input type='checkbox' name='autoReconnect' ";
    if (g_autoReconnect)
      page += "checked";
    page += "><span class='slider'></span></label></div>";

    // BLE-Status
    page += F("<div class='status-line'>BLE-Status: <span id='bleStatus'>");
    if (g_connected)
      page += "Verbunden";
    else
      page += "Getrennt";
    if (g_autoReconnect)
      page += " (Auto-Reconnect AN)";
    else
      page += " (Auto-Reconnect AUS)";
    page += F("</span></div>");

    // RPM-Status
    page += F("<div class='status-line'>Aktuelle RPM: <span id='rpmVal'>");
    page += String(g_currentRpm);
    page += F("</span> / Max gesehen: <span id='rpmMaxVal'>");
    page += String(g_maxSeenRpm);
    page += F("</span></div>");

    page += F("</div>"); // section Verbindung

    // Debug-Infos
    page += F("<div class='section'>");
    page += F("<div class='section-title'>Debug"
              "<span id='debugSpinner' class='spinner hidden'></span>"
              "</div>");
    page += F("<div class='row small'>Letzter TX: <span id='lastTx'>");
    page += g_lastTxInfo;
    page += F("</span></div>");
    page += F("<div class='row small'>Letzte OBD-Zeile: <span id='lastObd'>");
    page += g_lastObdInfo;
    page += F("</span></div>");
    page += F("</div>"); // section Debug
  }

  // Buttons: Save
  page += F("<button type='button' id='btnSave' disabled>Speichern</button>");

  // Testlauf-Button (immer sichtbar, nutzt aktuelle Formularwerte)
  page += F("<button type='button' id='btnTest'>Testlauf: RPM-Sweep</button>");

  // Connect / Disconnect Buttons (nur im Dev-Modus sichtbar)
  if (g_devMode)
  {
    page += "<button type='button' id='btnConnect' ";
    if (g_connected)
      page += "style='display:none'";
    page += ">Jetzt mit OBD verbinden</button>";

    page += "<button type='button' class='btn-danger' id='btnDisconnect' ";
    if (!g_connected)
      page += "style='display:none'";
    page += ">OBD trennen</button>";
  }

  page += F("</form>");

  // Footer
  page += F("<div class='small' style='text-align:center;margin-top:16px;'>");
  page += WiFi.softAPIP().toString();
  page += F("</div>");

  // JS
  page += F("<script>"
            "let saveDirty=false;"
            "function markDirty(){"
            " saveDirty=true;"
            " var b=document.getElementById('btnSave');"
            " if(b) b.disabled=false;"
            "}"
            "function onBrightnessChange(v){"
            " document.getElementById('bval').innerText=v;"
            " document.getElementById('brightness').value=v;"
            " markDirty();"
            " fetch('/brightness?val='+v).catch(()=>{});"
            "}"
            "function onSaveClicked(){"
            " var btn=document.getElementById('btnSave');"
            " btn.disabled=true;"
            " var form=document.getElementById('mainForm');"
            " var data=new FormData(form);"
            " fetch('/save',{method:'POST',body:data})"
            "  .then(r=>r.text())"
            "  .then(_=>{saveDirty=false;btn.disabled=true;})"
            "  .catch(_=>{btn.disabled=false;});"
            "}"
            "function onTestClicked(){"
            " var btn=document.getElementById('btnTest');"
            " btn.disabled=true;"
            " var form=document.getElementById('mainForm');"
            " var data=new FormData(form);"
            " fetch('/test',{method:'POST',body:data})"
            "  .then(r=>r.text())"
            "  .finally(()=>{btn.disabled=false;});"
            "}"
            "function postSimple(path){"
            " var sp=document.getElementById('debugSpinner');"
            " if(sp) sp.classList.remove('hidden');"
            " fetch(path,{method:'POST'}).catch(()=>{});"
            "}"
            "function fetchStatus(){"
            " var sp=document.getElementById('debugSpinner');"
            " if(sp) sp.classList.remove('hidden');"
            " fetch('/status')"
            "  .then(r=>r.json())"
            "  .then(s=>{"
            "    var e;"
            "    if((e=document.getElementById('rpmVal'))) e.innerText=s.rpm;"
            "    if((e=document.getElementById('rpmMaxVal'))) e.innerText=s.maxRpm;"
            "    if((e=document.getElementById('lastTx'))) e.innerText=s.lastTx;"
            "    if((e=document.getElementById('lastObd'))) e.innerText=s.lastObd;"
            "    if((e=document.getElementById('bleStatus'))) e.innerText=s.bleText;"
            "    var btnC=document.getElementById('btnConnect');"
            "    var btnD=document.getElementById('btnDisconnect');"
            "    if(btnC && btnD){"
            "      if(s.connected){btnC.style.display='none';btnD.style.display='block';}"
            "      else{btnC.style.display='block';btnD.style.display='none';}"
            "    }"
            "  })"
            "  .finally(()=>{if(sp) sp.classList.add('hidden');});"
            "}"
            "function initUI(){"
            " var form=document.getElementById('mainForm');"
            " if(form){"
            "  form.querySelectorAll('input,select').forEach(el=>{"
            "    if(el.id==='brightness_slider') return;"
            "    el.addEventListener('change',markDirty);"
            "    el.addEventListener('input',markDirty);"
            "  });"
            " }"
            " var g=document.getElementById('greenEndSlider');"
            " if(g) g.addEventListener('input',e=>{"
            "   var v=e.target.value;var t=document.getElementById('greenEndVal');"
            "   if(t) t.innerText=v+'%';});"
            " var y=document.getElementById('yellowEndSlider');"
            " if(y) y.addEventListener('input',e=>{"
            "   var v=e.target.value;var t=document.getElementById('yellowEndVal');"
            "   if(t) t.innerText=v+'%';});"
            " var b=document.getElementById('blinkStartSlider');"
            " if(b) b.addEventListener('input',e=>{"
            "   var v=e.target.value;var t=document.getElementById('blinkStartVal');"
            "   if(t) t.innerText=v+'%';});"
            " var sb=document.getElementById('btnSave');"
            " if(sb) sb.addEventListener('click',onSaveClicked);"
            " var tb=document.getElementById('btnTest');"
            " if(tb) tb.addEventListener('click',onTestClicked);"
            " var bc=document.getElementById('btnConnect');"
            " if(bc) bc.addEventListener('click',()=>postSimple('/connect'));"
            " var bd=document.getElementById('btnDisconnect');"
            " if(bd) bd.addEventListener('click',()=>postSimple('/disconnect'));"
            " fetchStatus();"
            " setInterval(fetchStatus,1000);"
            "}"
            "document.addEventListener('DOMContentLoaded',initUI);"
            "</script>");

  page += F("</body></html>");
  return page;
}

// ================== Settings-Seite ==========================

String settingsPage()
{
  String page;
  page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>ShiftLight Einstellungen</title>");
  page += F("<style>"
            "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#111;"
            "color:#eee;padding:16px;margin:0;}"
            "h1{font-size:20px;margin:0 0 12px 0;display:flex;"
            "align-items:center;justify-content:space-between;}"
            "a{color:#0af;text-decoration:none;}"
            ".section{margin-top:12px;padding:10px 12px;border-radius:8px;"
            "background:#181818;border:1px solid #333;}"
            ".section-title{font-weight:600;margin-bottom:4px;font-size:14px;}"
            ".toggle-row{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
            ".toggle-label{font-size:14px;}"
            ".switch{position:relative;display:inline-block;width:46px;height:24px;margin-left:8px;}"
            ".switch input{opacity:0;width:0;height:0;}"
            ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;"
            "background:#555;transition:.2s;border-radius:24px;}"
            ".slider:before{position:absolute;content:'';height:18px;width:18px;left:3px;top:3px;"
            "background:#fff;transition:.2s;border-radius:50%;}"
            ".switch input:checked + .slider{background:#0af;}"
            ".switch input:checked + .slider:before{transform:translateX(22px);}"
            "button{margin-top:16px;width:100%;padding:10px;border:none;border-radius:6px;"
            "background:#0af;color:#000;font-weight:bold;font-size:14px;}"
            "</style></head><body>");

  page += "<h1><a href=\"/\">‹ Zurück</a><span>Einstellungen</span></h1>";

  page += F("<form method='POST' action='/settings'>");
  page += F("<div class='section'>");
  page += F("<div class='section-title'>Modus</div>");
  page += F("<div class='toggle-row'><span class='toggle-label'>Entwicklermodus</span>"
            "<label class='switch'>");
  page += "<input type='checkbox' name='devMode' ";
  if (g_devMode)
    page += "checked";
  page += "><span class='slider'></span></label></div>";
  page += F("<div style='font-size:12px;color:#aaa;margin-top:8px;'>"
            "Wenn der Entwicklermodus deaktiviert ist, werden Debug-Infos und OBD-Steuerung "
            "im Hauptbildschirm ausgeblendet und OBD-Auto-Reconnect bleibt immer aktiv."
            "</div>");
  page += F("</div>");
  page += F("<button type='submit'>Speichern</button>");
  page += F("</form>");

  page += F("</body></html>");
  return page;
}

// ================== Webserver-Handler =======================

void handleRoot()
{
  g_lastHttpMs = millis();
  server.send(200, "text/html", htmlPage());
}

void handleSave()
{
  g_lastHttpMs = millis();

  if (server.hasArg("mode"))
  {
    cfg.mode = server.arg("mode").toInt();
    if (cfg.mode < 0 || cfg.mode > 2)
      cfg.mode = 1;
  }

  if (server.hasArg("brightness"))
  {
    int b = server.arg("brightness").toInt();
    if (b < 0)
      b = 0;
    if (b > 255)
      b = 255;
    cfg.brightness = b;
    strip.setBrightness(cfg.brightness);
    strip.show();
  }

  cfg.autoScaleMaxRpm = server.hasArg("autoscale");

  if (server.hasArg("fixedMaxRpm"))
  {
    int fm = server.arg("fixedMaxRpm").toInt();
    if (fm < 1000)
      fm = 1000;
    if (fm > 8000)
      fm = 8000;
    cfg.fixedMaxRpm = fm;
  }

  if (server.hasArg("greenEndPct"))
  {
    int v = server.arg("greenEndPct").toInt();
    if (v < 0)
      v = 0;
    if (v > 100)
      v = 100;
    cfg.greenEndPct = v;
  }

  if (server.hasArg("yellowEndPct"))
  {
    int v = server.arg("yellowEndPct").toInt();
    if (v < 0)
      v = 0;
    if (v > 100)
      v = 100;
    cfg.yellowEndPct = v;
  }

  if (server.hasArg("blinkStartPct"))
  {
    int v = server.arg("blinkStartPct").toInt();
    if (v < 0)
      v = 0;
    if (v > 100)
      v = 100;
    cfg.blinkStartPct = v;
  }

  // Logo-Optionen
  cfg.logoOnIgnitionOn = server.hasArg("logoIgnOn");
  cfg.logoOnEngineStart = server.hasArg("logoEngStart");
  cfg.logoOnIgnitionOff = server.hasArg("logoIgnOff");

  // Auto-Reconnect nur im Dev-Mode verstellbar
  if (g_devMode)
  {
    g_autoReconnect = server.hasArg("autoReconnect");
  }
  else
  {
    g_autoReconnect = true;
  }

  server.send(200, "text/plain", "OK");
}

void handleTest()
{
  g_lastHttpMs = millis();

  // Für den Test verwenden wir die Werte aus dem Request (aktuelle UI),
  // setzen sie aber auch direkt in cfg, damit die Simulation mit den
  // gleichen Werten läuft.
  if (server.hasArg("fixedMaxRpm"))
  {
    int fm = server.arg("fixedMaxRpm").toInt();
    if (fm < 1000)
      fm = 1000;
    if (fm > 8000)
      fm = 8000;
    cfg.fixedMaxRpm = fm;
  }
  cfg.autoScaleMaxRpm = server.hasArg("autoscale");

  if (server.hasArg("greenEndPct"))
  {
    int v = server.arg("greenEndPct").toInt();
    if (v < 0)
      v = 0;
    if (v > 100)
      v = 100;
    cfg.greenEndPct = v;
  }

  if (server.hasArg("yellowEndPct"))
  {
    int v = server.arg("yellowEndPct").toInt();
    if (v < 0)
      v = 0;
    if (v > 100)
      v = 100;
    cfg.yellowEndPct = v;
  }

  if (server.hasArg("blinkStartPct"))
  {
    int v = server.arg("blinkStartPct").toInt();
    if (v < 0)
      v = 0;
    if (v > 100)
      v = 100;
    cfg.blinkStartPct = v;
  }

  if (!cfg.autoScaleMaxRpm)
  {
    g_testMaxRpm = (cfg.fixedMaxRpm > 1000) ? cfg.fixedMaxRpm : 4000;
  }
  else
  {
    g_testMaxRpm = (g_maxSeenRpm > 0) ? g_maxSeenRpm : 4000;
  }

  g_testActive = true;
  g_testStartMs = millis();

  Serial.print("Starte Test-Sweep bis ");
  Serial.print(g_testMaxRpm);
  Serial.println(" RPM");

  server.send(200, "text/plain", "OK");
}

// Live-Brightness-Endpoint
void handleBrightness()
{
  g_lastHttpMs = millis();

  if (server.hasArg("val"))
  {
    int b = server.arg("val").toInt();
    if (b < 0)
      b = 0;
    if (b > 255)
      b = 255;

    cfg.brightness = b;
    strip.setBrightness(cfg.brightness);

    showMLogoPreview();

    g_brightnessPreviewActive = true;
    g_lastBrightnessChangeMs = millis();
  }

  server.send(200, "text/plain", "OK");
}

// Manueller Connect-Handler
void handleConnect()
{
  g_lastHttpMs = millis();

  Serial.println("[WEB] Manueller Connect-Button gedrückt.");
  bool ok = connectToObd();
  if (!ok)
  {
    Serial.println("[WEB] Manueller Connect fehlgeschlagen.");
  }

  server.send(200, "text/plain", ok ? "OK" : "FAIL");
}

// Disconnect-Handler
void handleDisconnect()
{
  g_lastHttpMs = millis();

  Serial.println("[WEB] Manueller Disconnect-Button gedrückt.");
  if (g_client && g_connected)
  {
    g_client->disconnect(); // ruft Callback auf, der States resettet
  }

  server.send(200, "text/plain", "OK");
}

// Status-Endpoint für Auto-Refresh
void handleStatus()
{
  g_lastHttpMs = millis();

  String json = "{";
  json += "\"rpm\":" + String(g_currentRpm);
  json += ",\"maxRpm\":" + String(g_maxSeenRpm);
  json += ",\"lastTx\":\"" + g_lastTxInfo + "\"";
  json += ",\"lastObd\":\"" + g_lastObdInfo + "\"";
  json += ",\"connected\":" + String(g_connected ? "true" : "false");
  json += ",\"autoReconnect\":" + String(g_autoReconnect ? "true" : "false");
  json += ",\"devMode\":" + String(g_devMode ? "true" : "false");
  json += ",\"bleText\":\"";
  if (g_connected)
    json += "Verbunden";
  else
    json += "Getrennt";
  if (g_autoReconnect)
    json += " (Auto-Reconnect AN)";
  else
    json += " (Auto-Reconnect AUS)";
  json += "\"}";
  server.send(200, "application/json", json);
}

// Settings-Handler
void handleSettingsGet()
{
  g_lastHttpMs = millis();
  server.send(200, "text/html", settingsPage());
}

void handleSettingsSave()
{
  g_lastHttpMs = millis();

  g_devMode = server.hasArg("devMode");
  if (!g_devMode)
  {
    // Wenn Dev-Mode aus, Auto-Reconnect erzwingen
    g_autoReconnect = true;
  }

  server.sendHeader("Location", "/settings");
  server.send(303);
}

// ================== SETUP ===========================
void setup()
{
  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLED(false);

  strip.begin();
  strip.setBrightness(cfg.brightness);
  strip.clear();
  strip.show();

  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("=== ESP32 BLE-OBD ShiftLight + WebUI ===");

  g_lastTxInfo = "–";
  g_lastObdInfo = "–";

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("Access Point gestartet: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP: ");
  Serial.println(ip);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/test", HTTP_POST, handleTest);
  server.on("/brightness", HTTP_GET, handleBrightness);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/disconnect", HTTP_POST, handleDisconnect);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/settings", HTTP_GET, handleSettingsGet);
  server.on("/settings", HTTP_POST, handleSettingsSave);

  server.begin();
  Serial.println("Webserver gestartet (http://192.168.4.1/)");

  BLEDevice::init("ESP32-OBD-BLE");
  BLEDevice::setPower(ESP_PWR_LVL_P7);

  // Initialer Verbindungsversuch nur, wenn Auto-Reconnect aktiv ist
  if (g_autoReconnect)
  {
    if (!connectToObd())
    {
      Serial.println("❌ Initiale OBD-Verbindung fehlgeschlagen.");
    }
  }
}

// ================== LOOP ============================
void loop()
{
  server.handleClient();

  unsigned long now = millis();

  // Zündung "aus" erkennen: lange keine OBD-Daten
  if (g_ignitionOn && (now - g_lastObdMs > IGNITION_TIMEOUT_MS))
  {
    Serial.println("[MLOGO] Zündung aus (Timeout) erkannt");
    g_ignitionOn = false;
    g_engineRunning = false;

    if (cfg.logoOnIgnitionOff && !g_leavingPlayedThisCycle)
    {
      g_leavingPlayedThisCycle = true;
      showMLogoLeavingAnimation();
    }

    // Neuer Zyklus -> Logo wieder erlaubt
    g_logoPlayedThisCycle = false;
  }

  // Auto-Reconnect-Schleife (nur wenn aktiviert)
  static unsigned long lastRetry = 0;
  const unsigned long RECONNECT_INTERVAL_MS = 5000;
  const unsigned long HTTP_GRACE_MS = 5000;

  if (g_autoReconnect &&
      !g_connected &&
      now - lastRetry > RECONNECT_INTERVAL_MS &&
      now - g_lastHttpMs > HTTP_GRACE_MS)
  {
    lastRetry = now;
    Serial.println("🔄 Verbindung verloren – versuche Reconnect (auto)...");
    connectToObd();
  }

  if (g_testActive)
  {
    unsigned long elapsed = now - g_testStartMs;
    if (elapsed >= TEST_SWEEP_DURATION)
    {
      g_testActive = false;
      updateRpmBar(g_currentRpm);
    }
    else
    {
      float t = (float)elapsed / (float)TEST_SWEEP_DURATION; // 0..1
      int simRpm = 0;

      if (t < 0.25f)
      {
        float tt = t / 0.25f;            // 0..1
        float pct = sinf(tt * 3.14159f); // 0..1..0
        if (pct < 0.0f)
          pct = 0.0f;
        simRpm = (int)(pct * g_testMaxRpm);
      }
      else if (t < 0.70f)
      {
        float tt = (t - 0.25f) / 0.45f; // 0..1
        if (tt < 0.0f)
          tt = 0.0f;
        if (tt > 1.0f)
          tt = 1.0f;

        float pct = tt * tt * (3.0f - 2.0f * tt);
        if (pct < 0.0f)
          pct = 0.0f;
        if (pct > 1.0f)
          pct = 1.0f;

        simRpm = (int)(pct * g_testMaxRpm);
      }
      else
      {
        float tt = (t - 0.70f) / 0.30f; // 0..1
        if (tt < 0.0f)
          tt = 0.0f;
        if (tt > 1.0f)
          tt = 1.0f;

        float base = 1.0f - tt;
        float wobble = 0.05f * sinf(tt * 3.14159f * 4.0f);
        float pct = base + wobble;

        if (pct < 0.0f)
          pct = 0.0f;
        if (pct > 1.0f)
          pct = 1.0f;

        simRpm = (int)(pct * g_testMaxRpm);
      }

      updateRpmBar(simRpm);
    }
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
