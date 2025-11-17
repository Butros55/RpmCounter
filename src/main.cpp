#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>

// UUIDs laut Car Scanner
static BLEUUID SERVICE_UUID("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID CHAR_UUID_NOTIFY("0000fff1-0000-1000-8000-00805f9b34fb"); // OBD -> ESP32
static BLEUUID CHAR_UUID_WRITE("0000fff2-0000-1000-8000-00805f9b34fb");  // ESP32 -> OBD

// MAC-Adresse deines OBDII-Adapters aus dem Scan
static const char *TARGET_ADDR = "66:1e:32:9d:2e:5d";

// LED zur Statusanzeige (meist GPIO2 beim ESP32-Devkit)
const int LED_PIN = 2;

BLEClient *g_client = nullptr;
BLERemoteCharacteristic *g_charWrite = nullptr;
BLERemoteCharacteristic *g_charNotify = nullptr;

bool g_connected = false;
String g_serialLine;

// ---------- LED-Helfer ----------
void setLED(bool on)
{
  digitalWrite(LED_PIN, on ? HIGH : LOW);
}

// ---------- Notify-Callback: Daten vom OBD-Adapter ----------
static void notifyCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
  for (size_t i = 0; i < length; i++)
  {
    char c = (char)pData[i];

    // ELM327 antwortet oft mit '>' als Prompt -> optisch trennen
    if (c == '>')
    {
      Serial.println(">");
    }
    else
    {
      Serial.print(c);
    }
  }
}

// ---------- BLE-Client-Callbacks ----------
class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient) override
  {
    Serial.println("BLE-Client: onConnect()");
    g_connected = true;
    setLED(true);
  }

  void onDisconnect(BLEClient *pclient) override
  {
    Serial.println("BLE-Client: onDisconnect()");
    g_connected = false;
    setLED(false);
  }
};

// ---------- Verbindung zum OBD-II per MAC ----------
bool connectToObd()
{
  Serial.print("Versuche Verbindung zu OBDII bei ");
  Serial.println(TARGET_ADDR);

  BLEAddress obdAddress(TARGET_ADDR);

  g_client = BLEDevice::createClient();
  g_client->setClientCallbacks(new MyClientCallback());

  // Optional: MTU hochsetzen (mehr Bytes pro Paket)
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

  Serial.println("🎉 BLE-Verbindung steht! Jetzt kannst du AT/OBD-Befehle eintippen.");
  Serial.println("Beispiel:  ATI  oder  0100   (ENTER drücken)");
  return true;
}

// ---------- Befehl an OBD schreiben ----------
void sendObdCommand(const String &cmd)
{
  if (!g_connected || !g_charWrite)
  {
    Serial.println("\r\n[!] Nicht verbunden, kann nicht senden.");
    return;
  }

  // String -> std::string + CR anhängen
  std::string s(cmd.c_str());
  if (s.empty() || s.back() != '\r')
  {
    s.push_back('\r');
  }

  Serial.print("[TX] ");
  Serial.println(cmd);

  // ohne Write-Response
  g_charWrite->writeValue((uint8_t *)s.data(), s.size(), false);
}

// ---------- SETUP ----------
void setup()
{
  pinMode(LED_PIN, OUTPUT);
  setLED(false);

  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("=== ESP32 BLE-OBD Client (MAC-basiert) ===");

  BLEDevice::init("ESP32-OBD-BLE");
  BLEDevice::setPower(ESP_PWR_LVL_P7); // etwas mehr Sendeleistung

  // Direkt versuchen, per MAC zu verbinden
  if (!connectToObd())
  {
    Serial.println("❌ Initiale Verbindung fehlgeschlagen. Prüfe:");
    Serial.println("  • Dongle steckt und Auto-Zündung an?");
    Serial.println("  • Kein Handy/Car Scanner gerade verbunden?");
  }
}

// ---------- LOOP ----------
void loop()
{
  // Wenn Verbindung weg ist, einfach alle paar Sekunden neu probieren
  static unsigned long lastRetry = 0;
  unsigned long now = millis();

  if (!g_connected && now - lastRetry > 5000)
  {
    lastRetry = now;
    Serial.println("🔄 Verbindung verloren – versuche Reconnect...");
    connectToObd();
  }

  // ----- Serial-Monitor -> OBD-Befehl -----
  while (Serial.available())
  {
    char c = (char)Serial.read();

    // Echo
    Serial.write(c);

    if (c == '\r' || c == '\n')
    {
      // ganze Zeile fertig
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

  delay(10);
}
