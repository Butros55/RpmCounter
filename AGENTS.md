# ✅ AGENTS.md – RpmCounter / ShiftLight

## 🌟 Projektüberblick

Dieses Repository enthält die Firmware für ein ShiftLight-/RPM-Anzeige-System auf Basis eines ESP32 (PlatformIO, Arduino).

Die Firmware:

- verbindet sich über OBD-II per **BLE**, um Live-Daten wie Motordrehzahl auszulesen
- steuert eine **mehrfarbige LED-Bar** (grün → gelb → rot, inkl. Modi und Animationen)
- zeigt animierte Informationen auf einem **Display**
- stellt eine **WLAN-Access-Point-Weboberfläche** zur Konfiguration bereit
- speichert Einstellungen dauerhaft (NVS / Preferences)

**Ziel:**  
Eine stabile, nicht-blockierende Firmware, bei der BLE, Webserver, LED-Bar und Display parallel laufen, ohne sich gegenseitig zu stören.

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

## ✅ Wichtige Anforderungen

Keine Blockierung des Webservers:

- BLE-Verbindungsversuche dürfen den HTTP-Server nicht blockieren.
- WLAN-AP muss stabil bleiben (kein unerwarteter Disconnect / Passwort-Fehler).

BLE-Stabilität:

- OBD-II-Verbindung darf nicht sporadisch abbrechen.
- Wiederverbindungslogik muss kontrollierbar konfiguriert sein.

LED-Bar & Display:

- Updates sollten nicht mit BLE/WLAN kollidieren.
- Animationen sollen möglichst non-blocking (Timer, millis-basiert) sein.

Konfiguration:

- Einstellungen (z. B. Auto-Reconnect, Retry-Counts etc.) müssen aus der globalen Konfiguration stammen und konsistent genutzt werden.

---

## 🧪 Tests (Zielbild)

Tests sind noch im Aufbau. Ziel:

Unit-Tests für:

- core/state.\* (Zustandsmaschine, Retry-Zähler, Flags)
- core/utils.cpp (Hilfsfunktionen wie clampInt, Mapping/Parsing)

Unit-Tests mit Stubs für:

- BLE-Logik (Verbindungsstatus, Retry-Handling ohne echte Hardware)

Integrationstests für:

- Zusammenspiel von BLE-Connect-Versuchen und Webserver/WLAN
- Sicherstellen, dass HTTP-Requests auch während BLE-Aktivität beantwortet werden

Framework: PlatformIO Tests mit Unity (test/\*/test_main.cpp).

Alle Tests laufen über test/test_main.cpp und können mit

- pio test -e esp32dev (auf echter Hardware)
- später: pio test -e native (ohne Hardware, hostseitig) ausgeführt werden.

---

## 🧩 Coding-Guidelines

C++17 (soweit vom Board/Framework unterstützt), Arduino-Stil ok, aber:

- lieber klare Funktionen statt langer loop()-Monster
- keine unnötigen globalen Variablen – wenn nötig, klar in state.\* dokumentieren

Namenskonventionen:

- Globale Zustände/Flags: `g_...`
- Konstante Konfiguration: `k...` oder `CONST_NAME`

Hilfsfunktionen wiederverwenden:

- `clampInt` nur an einer zentralen Stelle definieren (core/utils.cpp + Header)
- Keine Duplikate derselben Funktion in mehreren Dateien

---

## 🚫 Was Codex NICHT tun soll

- Keine radikale Umstrukturierung des Repos (keine neuen Frameworks / Libraries einführen).
- Keine großen Refactorings quer durchs Projekt, wenn nicht explizit angefordert.
- Keine Hardware-spezifischen Konstanten ohne Kommentar ändern (Pins, Timer, Partitionen…).
- Keine sensiblen Konfigurationsdaten in den Code hardcoden, die aktuell über UI/Konfig kommen sollen.

---

## 💡 Beispiele für sinnvolle Aufgaben an Codex

### Bugfixes

- „Warum blockiert der Webserver bei laufenden BLE-Connect-Versuchen? Bitte analysieren und ein nicht-blockierendes Verhalten implementieren, ohne vorhandene Funktionen zu löschen.“
- „Analysiere, warum die WLAN-Verbindung nach einiger Zeit abbricht, und behebe das Problem möglichst ohne neue globale Variablen einzuführen.“

### Tests schreiben

- „Lege Unit-Tests für core/state.cpp in test/unit_core/ an, die die Retry-Logik, Flags und Zeitstempel prüfen. Die Tests sollen mit `pio test -e esp32dev` laufen und bei Fehlern eine gut lesbare Ausgabe liefern.“
- „Erweitere die bestehenden Tests in test/unit_core/test_main.cpp um Randfälle (z. B. Grenzwerte, ungültige Parameter) für clampInt, ohne das Produktionsverhalten zu ändern.“

### Debugging-Helfer

- „Baue strukturierte Debug-Logs für WLAN/BLE ein (mit Zeitstempeln und klaren Statuscodes), ohne den normalen Ablauf zu verlangsamen. Logs sollen sich zentral ein- und ausschalten lassen (z. B. über ein `DEBUG_*`-Flag) und klar anzeigen, wann Verbindungen aufgebaut, verloren oder neu versucht werden.“
- „Füge im BLE- und WLAN-Code an sinnvollen Stellen Logging hinzu, damit ersichtlich wird, warum Verbindungen abreißen oder blockieren (inkl. Fehlercodes und Retry-Zähler).“

### UI-Anpassungen

- „Verbessere die Bluetooth-Geräteliste in web_ui.cpp, ohne das Backend-Verhalten zu ändern (nur HTML/CSS/JS). Die Liste soll Öffnen/Schließen animiert darstellen und einen Loading-Indikator im jeweiligen Button zeigen, während ein Verbindungsversuch läuft.“
- „Passe die Weboberfläche so an, dass bei Verbindungsfehlern eine klare Statusmeldung angezeigt wird (z. B. ‚BLE-Verbindung fehlgeschlagen – siehe Debug-Logs‘), ohne die bestehenden REST-Endpunkte zu verändern.“

### Neue Features / Refactorings

- „Implementiere eine optionale ‚F1-Shiftlight‘-Logik als zusätzlichen Modus, ohne die bestehende RPM-Logik zu brechen. Der Modus soll sauber in die Konfiguration integriert sein und in der Vorschau sichtbar werden.“
- „Räume den BLE- und WLAN-Code auf (Refactoring), ohne Verhalten zu verändern: Duplikate entfernen, gemeinsame Hilfsfunktionen in core/utils.cpp auslagern, Kommentare ergänzen und Namenskonventionen vereinheitlichen.“
- „Implementiere ein zentrales Logging-Modul (z. B. core/logging.{h,cpp}), das sowohl Webserver, BLE als auch WLAN verwenden können, anstatt überall eigene `Serial.println`-Aufrufe zu haben.“
