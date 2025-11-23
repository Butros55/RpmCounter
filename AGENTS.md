# ✅ AGENTS.md – RpmCounter / ShiftLight (mit Netzwerk-Debug-Modus)

## 🌟 Projektüberblick

Dieses Repository enthält die Firmware für ein **ShiftLight-/RPM-Anzeige-System** auf Basis eines **ESP32 (PlatformIO, Arduino)**.

Die Firmware:

- verbindet sich über OBD-II per **BLE**
- steuert eine **mehrfarbige LED-Bar**
- zeigt Animationen und Statusinformationen auf einem **Display**
- stellt eine **WLAN-Weboberfläche** (Access Point + optional STA) bereit
- speichert Einstellungen dauerhaft (Preferences / NVS)

**Ziel:**  
Eine stabile, non-blocking Firmware, in der **BLE, WLAN/AP/STA, Webserver, LED-Bar und Display parallel** laufen, ohne sich gegenseitig zu blockieren.

---

# 🤖 Agent-Rollen & Modi

Der Agent („Codex“) läuft **in der Cloud** und arbeitet über VS Code / CLI auf diesem Repository.

## 🟦 STANDARD-MODUS (Default)

> Dieser Modus ist der Standard und wird in ~95 % der Fälle verwendet.

Der Agent darf im STANDARD-MODUS:

- Dateien im Workspace lesen/schreiben
- Projektstruktur einhalten und erweitern
- Build- & Test-Befehle im Workspace ausführen:
  - `pio run`
  - `pio run -t upload`
  - `pio device monitor` (falls verfügbar)
  - `pio test -e native`
  - `pio test -e esp32dev`
- Log-Ausgaben aus dem seriellen Monitor analysieren (vom User bereitgestellt)
- Fehler reproduzieren, soweit mit den zur Verfügung stehenden Tools möglich
- Fix-Vorschläge umsetzen

Der Agent führt **keine** Befehle aus, die:

- ins Internet gehen (HTTP-Requests nach außen, `curl` auf fremde Hosts, etc.)
- außerhalb des Projekt-Workspaces Dateien manipulieren

Der STANDARD-MODUS reicht aus, um:

- Firmware zu bauen und zu flashen
- Tests auf echter Hardware (über `pio test -e esp32dev`) mechanisch anzustoßen
- State-Machine/BLE/Webserver-Verhalten anhand serieller Logs zu debuggen
- die Web-UI-/WLAN-Logik in Code & Unit-Tests zu verbessern

---

## 🟩 NETZWERK-DEBUG-MODUS („FULL ACCESS“)

> Dieser Modus darf **nur** genutzt werden, wenn ich ihn ausdrücklich erlaube.

Zusätzlich zu den Rechten im STANDARD-MODUS darf der Agent dann:

- Befehle mit **Netzwerkzugriff** ausführen (z. B. `curl`, `ping`, `wget`)
- HTTP-Requests an Geräte im lokalen Netzwerk senden (z. B. an den ESP32-AP)
- Terminalbefehle außerhalb des Workspaces ausführen, falls für Debugging nötig

Typische Einsätze:

- Analyse, warum der ESP32-Webserver im **AP-Modus** nicht oder nur sporadisch antwortet  
  → z. B. `curl http://192.168.4.1/`
- Endpoints wie `/`, `/settings`, `/status`, `/wifi_scan` etc. testen
- Verhalten bei parallelen Requests während BLE-Scans/WLAN-Scans untersuchen
- Vergleiche zwischen Heim-WLAN (STA-Mode) und AP-Mode

**Wichtig:**  
Der Agent darf diesen Modus **nicht eigenständig** aktivieren.  
Nur wenn ich explizit schreibe:

> **„Nutze bitte Netzwerk-Debug / full access“**

darf der Agent Netzwerkzugriffe verwenden.

---

# 📡 Netzwerk-Szenarien

## 1️⃣ Access-Point-Modus (AP)

Wenn der ESP32 im AP-Modus läuft und mein PC mit dem AP verbunden ist:

- Internet ist ggf. getrennt – das ist in Ordnung.
- Im FULL-ACCESS-Modus darf der Agent:
  - HTTP-Requests an `http://192.168.4.1` senden
  - Antwortzeiten, Timeouts und Fehlercodes analysieren
- Der Agent beurteilt:
  - ob BLE-Connect-Loops den Webserver blockieren
  - ob WLAN-Scan/Connect den AP überlastet
  - ob HTTP-Handler non-blocking implementiert sind

## 2️⃣ Heim-WLAN (STA-Modus)

Wenn der ESP32 sich ins Heim-WLAN einwählt:

- Der PC hat Internet + Verbindung zum ESP32.
- Im FULL-ACCESS-Modus kann der Agent:
  - sowohl ins Internet als auch zum ESP32 (z. B. `http://esp32.local` oder IP)
  - HTTP-Requests während BLE/WLAN-Aktivität senden
- Gut geeignet für komplexeres Integration-Debugging.

---

## 📁 Projektstruktur (verbindlich)

> Bitte diese Struktur respektieren und neue Dateien nur an sinnvollen Stellen ergänzen.

```text
.
├─ platformio.ini
├─ src/
│  ├─ bluetooth/
│  │  ├─ ble_obd.cpp
│  │  └─ ble_obd.h
│  ├─ core/
│  │  ├─ config.cpp
│  │  ├─ config.h
│  │  ├─ state.cpp
│  │  ├─ state.h
│  │  ├─ utils.cpp
│  │  ├─ utils.h
│  │  ├─ vehicle_info.cpp
│  │  ├─ vehicle_info.h
│  │  ├─ wifi.cpp          # zentrale WLAN-Logik (AP/STA, Scan, Connect)
│  │  └─ wifi.h
│  ├─ hardware/
│  │  ├─ display.cpp
│  │  ├─ display.h
│  │  ├─ led_bar.cpp
│  │  ├─ led_bar.h
│  │  ├─ logo_anim.cpp
│  │  └─ logo_anim.h
│  ├─ web/
│  │  ├─ web_ui.cpp        # HTML-Generierung & Routen-Handler
│  │  ├─ web_ui.h
│  │  ├─ web_helpers.cpp
│  │  └─ web_helpers.h
│  └─ main.cpp
├─ include/                # gemeinsame Header (falls benötigt)
├─ lib/                    # externe / eigene Libraries
└─ test/                   # PlatformIO-Tests (Unity)
   ├─ test_main.cpp        # zentrales Unity-Setup (einziges setup()/loop())
   ├─ unit_core/
   │  └─ test_clamp_int.cpp
   ├─ unit_bluetooth/
   │  └─ (geplant)
   ├─ integration_connectivity/
   │  └─ (geplant)        # BLE/WLAN/Webserver-Integration
   └─ unit_ap/
      └─ (geplant)        # Tests für Access-Point-/WLAN-Logik
Alle Tests werden über test/test_main.cpp gestartet.
Wichtig: keine weiteren setup()/loop() in anderen Testdateien definieren.

⚙️ Build & Flash
Entwicklungsumgebung: PlatformIO

Relevanter Ausschnitt aus platformio.ini (kann vom Agent ergänzt/angepasst werden, aber nicht grundlegend umgebaut):

ini
Code kopieren
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
Standard-Workflows:

Build: pio run

Flash: pio run -t upload

Serieller Monitor: pio device monitor

Unit-/Integration-Tests:

pio test -e native

pio test -e esp32dev

🔁 Arbeits- & Test-Workflow für den Agent
Der Agent MUSS nach Änderungen:

Projekt bauen

bash
Code kopieren
pio run
Tests ausführen

Immer:

bash
Code kopieren
pio test -e native
Wenn WLAN-/Webserver-/BLE-/Hardware-nahe Logik verändert wurde, zusätzlich:

bash
Code kopieren
pio test -e esp32dev
Bei Testfehlschlägen:

Fehler analysieren

Code nachbessern

Build & Tests wiederholen

Erst wenn alle relevanten Tests grün sind, gilt der Fix als abgeschlossen.

Im Ergebnis soll der Agent immer kurz dokumentieren:

Welche Dateien geändert wurden

Welche Probleme behoben wurden (inkl. Fehlermeldungen vorher/nachher)

Welche Kommandos (Build/Test) ausgeführt wurden und ob sie erfolgreich waren

📌 Regeln für Änderungen
Der Agent MUSS:

die bestehende Architektur (core/hardware/web/bluetooth) respektieren

non-blocking Verhalten sicherstellen (kein langes delay(), keine blockierenden Scans im HTTP-Handler)

NVS/Preferences-Zugriff über das bestehende Config-System führen

neue Zustände sauber in state.* integrieren

Tests niemals löschen oder deaktivieren, sondern erweitern

Der Agent DARF:

neue Unit- und Integrationstests im test/-Ordner hinzufügen

kleine UI-Verbesserungen in der Weboberfläche vornehmen

Logging konsolidieren (z. B. zentrale Debug-Funktion)

neue State-Machine-Zustände hinzufügen, wenn für Stabilität nötig

die WLAN-Logik so umbauen, dass:

Scan & Connect asynchron ablaufen

AP/STA-Modus sauber verwaltet werden

die Web-UI sofort reagiert (z. B. mit Status „Scan läuft…“)

Der Agent DARF NICHT:

neue Frameworks oder große externe Libraries einführen

Plattform/Board ohne Rücksprache wechseln

Pinbelegung eigenmächtig ändern

Funktionsnamen ändern, die von anderen Modulen (v. a. Web-UI) verwendet werden, ohne alle Call-Sites konsequent anzupassen

Tests entfernen oder „grün patchen“, ohne das eigentliche Problem zu lösen


📌 Wann soll der Agent Rückfragen stellen?
Wenn Hardwareverhalten unklar ist (z. B. bestimmte OBD-Werte, LED-Verhalten)

Wenn Logs fehlen, um einen Fehler nachzuvollziehen

Wenn für die Lösung Netzwerkzugriff im FULL-ACCESS-Modus notwendig ist

Wenn sicherheitsrelevante Aktionen außerhalb des Workspaces erforderlich wären

bash
Code kopieren
```
