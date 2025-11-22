# ✅ AGENTS.md – RpmCounter / ShiftLight (mit Netzwerk-Debug-Modus)

## 🌟 Projektüberblick

Dieses Repository enthält die Firmware für ein ShiftLight-/RPM-Anzeige-System auf Basis eines ESP32 (PlatformIO, Arduino).

Die Firmware:

- verbindet sich über OBD-II per **BLE**
- steuert eine **mehrfarbige LED-Bar**
- zeigt Animationen auf einem **Display**
- stellt eine **WLAN-Access-Point-Weboberfläche** bereit
- speichert Einstellungen dauerhaft (Preferences / NVS)

**Ziel:**  
Eine stabile, non-blocking Firmware, in der BLE, Webserver, WLAN-AP, LED-Bar und Display parallel laufen.

---

# ⚙️ Agent-Modi

## 🟦 **STANDARD-MODUS (normaler Agent)**

> _Dieser Modus ist der Standard. Er ist sicherer und wird in 95% der Fälle verwendet._

Der Agent darf:

- Dateien lesen/schreiben
- Projekt verändern
- Build- & Test-Befehle ausführen:
  - `pio run`
  - `pio run -t upload`
  - `pio device monitor`
  - `pio test -e esp32dev`
- Log-Ausgaben analysieren
- Fehler reproduzieren (über echten ESP32)
- Fix-Vorschläge machen
- nur Befehle im Terminal ausführen, die **keinen externen Netzwerkzugriff** benötigen  
  (lokale serielle Verbindung, lokale PIO-Tools, Git-Befehle im Workspace, etc.)

Der Standardmodus **reicht vollständig**, um:

- Firmware zu bauen
- Firmware zu flashen
- Tests auf echter Hardware auszuführen
- State-Machine/BLE/Webserver Verhalten anhand serieller Logs zu debuggen

---

## 🟩 **NETZWERK-DEBUG-MODUS (Agent: FULL ACCESS)**

> _Dieser Modus darf **nur** aktiviert werden, wenn ich ihn dir ausdrücklich erlaube._

Dieser Modus erlaubt zusätzlich:

- Befehle mit Netzwerkzugriff (curl, ping, wget,…)
- Zugriff auf Geräte im lokalen Netzwerk (z. B. ESP32-AP)
- Terminalbefehle außerhalb des Workspace
- komplexere Diagnose über mehrere Interfaces

Beispiele, wo dieser Modus erlaubt ist:

- Analyse, warum der ESP32-Webserver im AP-Modus nicht antwortet  
  → `curl http://192.168.4.1/`
- HTTP-Debugging während BLE-Verbindungsversuchen
- Vergleich zwischen Heim-WLAN (STA-Mode) und Access-Point-Modus

Wichtig:

**Der Agent darf diesen Modus NICHT eigenständig nutzen.  
Nur wenn ich explizit schreibe:**

> **„Nutze bitte Netzwerk-Debug / full access“**

---

# 📡 Netzwerk-Szenarien für Debugging

## 1️⃣ Access-Point-Modus debuggen (Standardfall)

Wenn mein PC mit dem ESP32-AP verbunden ist:

- Internet ist evtl. getrennt → das ist ok
- Agent darf:
  - HTTP-Requests an `http://192.168.4.1` senden (nur in full-access)
  - Timeout-Verhalten analysieren
  - Webserver-Fehler sichtbar machen (`404`, blockiert, keine Antwort)
- Agent beurteilt:
  - ob BLE-Connect Loops den Webserver blockieren
  - ob der ESP32 im AP-Modus überlastet wird
  - ob die HTTP-Handler non-blocking sind

## 2️⃣ Heim-WLAN (STA-Modus)

Wenn der ESP32 ins Heim-WLAN eingebunden wird (Variante C):

- AP- und Internetprobleme werden realitätsnah simuliert
- Agent kann _gleichzeitig_:
  - Internet nutzen
  - HTTP-Requests zum ESP senden
  - BLE/Webserver testen
- Eignet sich hervorragend zum **Remote-Debugging** und für PIO Remote.

Beide Modi werden im Agent unterstützt, aber AP-Modus ist nötig, um reale spätere Nutzung zu simulieren.

---

## 📁 Projektstruktur (verbindlich)

> Wichtig: Bitte diese Struktur respektieren und neue Dateien in den passenden Ordnern anlegen.

```text
.
├─ platformio.ini
├─ src/
│  ├─ bluetooth/
│  │  ├─ ble_obd.cpp
│  │  └─ ble_obd.h
│  ├─ core/
│  │  ├─ config.cpp
│  │  ├─ state.cpp
│  │  ├─ state.h
│  │  ├─ utils.cpp
│  │  └─ vehicle_info.cpp
│  ├─ hardware/
│  │  ├─ display.cpp
│  │  ├─ led_bar.cpp
│  │  └─ logo_anim.cpp
│  └─ web/
│     ├─ web_ui.cpp
│     └─ web_helpers.cpp
├─ include/          # gemeinsame Header (falls benötigt)
├─ lib/              # externe / eigene Libraries (falls genutzt)
└─ test/             # PlatformIO-Tests (Unity)
   ├─ test_main.cpp                  # zentrales Unity-Setup (einziges setup()/loop())
   ├─ unit_core/
   │  └─ test_clamp_int.cpp          # Unit-Tests für core/* (z.B. Utils, State)
   ├─ unit_bluetooth/
   │  └─ (geplant)                  # Unit-Tests für BLE-Logik (mit Stubs/Mocks)
   ├─ integration_connectivity/
   │  └─ (geplant)                  # Integrationstests BLE/WLAN/Webserver
   └─ unit_ap/
      └─ (geplant)                  # Tests für Access-Point-/WLAN-Logik
```

Alle Tests in den Unterordnern werden über test/test_main.cpp gestartet.
Bitte kein weiteres setup()/loop() in anderen Test-Dateien definieren.

---

## ⚙️ Build & Flash

Entwicklungsumgebung: PlatformIO

platformio.ini (relevant):

```
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
test_framework = unity
test_build_src = yes

upload_port = COM3

lib_deps =
  Adafruit NeoPixel
  ESP32 BLE Arduino
  adafruit/Adafruit GFX Library
  adafruit/Adafruit ST7735 and ST7789 Library

board_build.partitions = huge_app.csv
```

Standard-Workflows:

Build: `pio run`  
Flash: `pio run -t upload`  
Serieller Monitor: `pio device monitor`

---

# 📌 Betriebsregeln für Codex

## Der Agent MUSS:

- Architektur & Struktur beibehalten
- state/config/utils zentral halten
- Webserver + BLE + Display non-blocking halten
- Fixes erklären (Dateien + Gründe)
- Tests grün halten
- keine Interfaces ungewollt umbauen
- niemals Tests löschen

## Der Agent DARF:

- neue Tests im `test/` Ordner anlegen
- UI minimal verbessern
- Debug-Logs zentralisieren
- neue States hinzufügen (wenn nötig)
- Fehler im bestehenden Code beheben

## Der Agent DARF NICHT:

- neue Frameworks einführen
- Libraries austauschen
- große Refactorings ohne Not
- Pinbelegung ändern
- Funktionsnamen ändern, die im Web UI genutzt werden
- Netzwerkzugriff im Standardmodus durchführen

---

# 🧪 Tests

Alle Tests laufen über:

- `pio test -e esp32dev` (echter ESP32)

Später optional:

- `pio test -e native`

---

# 📌 Wann soll der Agent mich um Interaktion bitten?

- Wenn Tests fehlschlagen
- Wenn Hardware erwartet wird (BLE-Scanner, Fahrzeug)
- Wenn ein Netzwerkzugriff nur im Full-Access-Modus erlaubt wäre
- Wenn ein Sicherheitsrisiko besteht (File-Write außerhalb Workspace)

---

# 🔥 Beispiele für sinnvolle Full-Access-Jobs

- „Teste über `curl` die Endpunkte `/status`, `/settings`, `/debug` während BLE-Connect.“
- „Simuliere 5 parallele Web-Requests, während BLE scannt.“
- „Analysiere HTTP-Timeouts beim ESP32-AP und verbinde das Verhalten mit der State-Machine.“
- „Prüfe, ob `/rpm` während BLE-Reconnect zuverlässig antwortet.“

---

# 🧩 Beispiele für Standard-Modus-Aufgaben

- BLE-State-Fixes
- Non-blocking Webserver-Aufbereitung
- LED-Bar-F1-Modus implementieren
- Retry-Logik in state.cpp erweitern
- Utils-Tests um Randfälle erweitern
- Log-System einbauen
