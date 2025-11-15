#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

#define DEBUG_PORT Serial
#define ELM_PORT SerialBT

#define LED_PIN 2

bool lastConnected = false;
unsigned long lastConnectTry = 0;
const unsigned long CONNECT_INTERVAL = 5000; // alle 5s neuer Versuch

void setLEDConnected()
{
  digitalWrite(LED_PIN, HIGH); // AN
}

void setLEDDisconnected()
{
  digitalWrite(LED_PIN, LOW); // AUS
}

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  setLEDDisconnected();

  DEBUG_PORT.begin(115200);
  delay(200);

  // ESP32 Bluetooth im Master-Mode starten
  if (!ELM_PORT.begin("ESP32-OBD", true))
  { // Name vom ESP32, egal wie
    DEBUG_PORT.println("❌ Bluetooth konnte nicht initialisiert werden!");
    while (true)
    {
      delay(1000);
    }
  }

  // PIN NACH begin() setzen
  ELM_PORT.setPin("1234");

  DEBUG_PORT.println("ESP32 OBD-Connector gestartet");

  // Direkt beim ersten loop()-Durchlauf einen Verbindungsversuch erlauben
  lastConnectTry = millis() - CONNECT_INTERVAL;
}

void loop()
{
  unsigned long now = millis();
  bool connected = ELM_PORT.connected();

  // Übergang: wurde gerade VERBUNDEN?
  if (connected && !lastConnected)
  {
    DEBUG_PORT.println("✅ Verbunden mit ELM327!");
    setLEDConnected();
  }

  // Übergang: wurde gerade GETRENNT?
  if (!connected && lastConnected)
  {
    DEBUG_PORT.println("⚠️ Verbindung zu ELM327 verloren.");
    setLEDDisconnected();
  }

  // Wenn NICHT verbunden → in Abständen aktiv connecten
  if (!connected && (now - lastConnectTry >= CONNECT_INTERVAL))
  {
    lastConnectTry = now;
    DEBUG_PORT.println("🔄 Versuche Verbindung zu 'OBDII'...");

    // Das hier kann einige Sekunden blocken (Suche/Pairing)
    bool ok = ELM_PORT.connect("OBDII"); // Name des OBD2-Adapters

    if (ok)
    {
      DEBUG_PORT.println("👉 connect(\"OBDII\") hat true zurückgegeben");
      // LED wird im nächsten loop-Durchlauf oben eingeschaltet
    }
    else
    {
      DEBUG_PORT.println("❌ connect(\"OBDII\") fehlgeschlagen");
      // LED bleibt AUS, es wird später nochmal versucht
    }
  }

  // Zustand für nächsten Loop-Moment merken
  lastConnected = connected;

  // --- Serieller Durchsatz PC → ELM ---
  if (DEBUG_PORT.available())
  {
    char c = DEBUG_PORT.read();
    DEBUG_PORT.write(c);
    ELM_PORT.write(c);
  }

  // --- Serieller Durchsatz ELM → PC ---
  if (ELM_PORT.available())
  {
    char c = ELM_PORT.read();

    if (c == '>')
      DEBUG_PORT.println();

    DEBUG_PORT.write(c);
  }
}
