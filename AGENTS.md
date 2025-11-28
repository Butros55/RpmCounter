# ✅ AGENTS.md – RpmCounter / ShiftLight (Architektur, Regeln, Display-Pfade)

## 🌟 Projektüberblick

Dieses Repository enthält die Firmware für ein ShiftLight-/RPM-Anzeige-System auf Basis eines ESP32 (PlatformIO, Arduino).

Der Code unterstützt mehrere Hardwarevarianten, insbesondere:

- ältere Boards mit ST7789-Display (240×240)
- neue Waveshare ESP32-S3 Boards mit 1.64" QSPI AMOLED (2800×456)
- BLE-OBD Anbindung
- LED-Bar
- WLAN-Webserver (AP/STA)
- LVGL-UI
- NVS-Konfiguration

**Ziel:**  
Eine stabile, nicht-blockierende Firmware, bei der BLE, WLAN/AP/STA, LED-Bar, Webserver und Display parallel funktionieren und der Code gut strukturiert (refaktoriert) bleibt.

---

## 🧰 Platform & Libraries (wichtige Rahmenbedingungen)

- Build-Umgebung: **PlatformIO**, Umgebung `[env:esp32s3]`
- Platform (aktuell, Stand Pro-Setup):  
  `platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip`
- Framework: `arduino`
- Board: `esp32s3` mit eigener `boards/esp32s3.json` (z. B. T-DisplayS3-Konfiguration, 16 MB Flash, PSRAM)

### BLE-Bibliothek

- Der Arduino Core (3.x) liefert bereits eine eigene BLE-Implementierung (`BLEDevice`, `BLEClient`, …).
- **Externe Library `ESP32 BLE Arduino` darf NICHT mehr über `lib_deps` eingebunden werden**, da sie sonst Header wie  
  `esp_gap_ble_api.h`, `esp_gatt_defs.h`, `esp_bt_main.h` etc. nicht findet.
- Alle BLE-Funktionen (insb. `src/bluetooth/ble_obd.cpp/.h`) müssen gegen die **Core-eigene BLE-Implementierung** kompilieren.

### Display-/GFX-Library

- Für das ESP32-S3 QSPI-AMOLED-Display wird die **GFX Library for Arduino** (`moononournation/GFX Library for Arduino`) verwendet.
- QSPI-Pfad: `Arduino_ESP32QSPI` + passender Panel-Treiber (z. B. `Arduino_CO5300`) für das SH8601-basierte AMOLED.

---

## 🤖 Agent-Modi

### 🟦 STANDARD-MODUS

**Der Agent darf:**

- Dateien im Workspace lesen/schreiben
- Build-/Test-Befehle in **PowerShell** ausführen
- Logs (Build, serieller Monitor) analysieren
- architekturgemäße Änderungen und Refactorings vornehmen (auch größere Umbauten im Code)

**Der Agent darf NICHT:**

- Netzwerkbefehle (curl/wget, HTTP-Tools) ausführen
- Linux/WSL-Syntax verwenden (nur Windows/PowerShell)
- Dateien außerhalb des Repositories verändern

### 🟩 NETZWERK-DEBUG („FULL ACCESS")

Nur erlaubt, wenn der Nutzer explizit schreibt:

> „Nutze bitte Netzwerk-Debug / full access"

Dann darf der Agent zusätzlich Netzwerkbefehle im LAN ausführen (z. B. Ping/HTTP im internen Netz).

---

## ✔ PowerShell-Befehle (Python 3.11, PlatformIO)

### Build

```powershell
'C:\Program Files\PowerShell\7\pwsh.exe' -Command '
  Set-Location "c:\dev\RpmCounter";
  $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home";
  pio run -e esp32s3
'
```

### Flash

```powershell
'C:\Program Files\PowerShell\7\pwsh.exe' -Command '
  Set-Location "c:\dev\RpmCounter";
  $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home";
  pio run -e esp32s3 -t upload
'
```

### Serieller Monitor

```powershell
'C:\Program Files\PowerShell\7\pwsh.exe' -Command '
  Set-Location "c:\dev\RpmCounter";
  $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home";
  pio device monitor
'
```

### Clean

```powershell
'C:\Program Files\PowerShell\7\pwsh.exe' -Command '
  Set-Location "c:\dev\RpmCounter";
  $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home";
  pio run -e esp32s3 -t clean
'
```

### Full-Clean (falls nötig)

```powershell
'C:\Program Files\PowerShell\7\pwsh.exe' -Command '
  Set-Location "c:\dev\RpmCounter";
  $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home";
  pio run -e esp32s3 -t fullclean
'
```

## 🧹 Build-Clean & .pio-Probleme

### Typische Fehler

- **WinError 5** – Zugriff verweigert
  → In diesem Fall den Nutzer explizit nach Permission fragen.
  Nachdem der Nutzer „Allow for this session" bestätigt hat, kann der Agent den Clean erneut versuchen.
- **„Can not remove temporary directory .pio\build …"**

### Eskalationsstufen

#### 1) Normaler Clean

```powershell
'C:\Program Files\PowerShell\7\pwsh.exe' -Command '
  Set-Location "c:\dev\RpmCounter";
  $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home";
  pio run -e esp32s3 -t clean
'
```

#### 2) Full-Clean

```powershell
'C:\Program Files\PowerShell\7\pwsh.exe' -Command '
  Set-Location "c:\dev\RpmCounter";
  $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home";
  pio run -e esp32s3 -t fullclean
'
```

#### 3) .pio\build hart löschen (nur wenn PlatformIO ausdrücklich scheitert!)

````

**Wenn immer noch „Access Denied":**
Nutzer informieren, dass ein Prozess den Ordner blockiert (VS Code, Monitor, Explorer, PlatformIO Build-Prozess).

---

## 🖥️ Display- & LVGL-Regeln

Das Projekt unterstützt zwei klar getrennte Displaypfade:

### 1) hardware/display.cpp (ST7789, 240×240)

- ✔ Bleibt erhalten
- ✔ Wird für ältere Boards verwendet
- ✔ Darf NICHT gelöscht werden
- ✔ Darf weiterhin die ST7789-Library nutzen

Damit bleibt das komplette bisherige Paket kompatibel.

### 2) hardware/display_s3.cpp (Waveshare ESP32-S3 AMOLED)

- ❗ Dieses Display ist kein ST7789
- ❗ Dieses Display ist ein QSPI AMOLED (280×456) mit SH8601
- ❗ Daher darf in display_s3.cpp KEIN ST7789-Code mehr existieren

#### ✔ Anforderungen an den ESP32-S3-Pfad

- **Displaytyp:** Waveshare 1.64" AMOLED
- **Interface:** QSPI (6-Pin: CLK, CS, D0–D3)
- **Auflösung:** 280 × 456 Pixel
- **Farbtiefe:** 16 Bit
- **Touch:** FT3168, I²C (GPIO 47/48)

#### ✔ Pinbelegung (laut Waveshare-Pinout)

| AMOLED Pin   | ESP32-S3 GPIO | Verwendung       |
| ------------ | ------------- | ---------------- |
| QSPI_CS      | GPIO 9        | CS               |
| QSPI_CLK     | GPIO 10       | Clock            |
| QSPI_D0      | GPIO 11       | Data0            |
| QSPI_D1      | GPIO 12       | Data1            |
| QSPI_D2      | GPIO 13       | Data2            |
| QSPI_D3      | GPIO 14       | Data3            |
| AMOLED_RESET | GPIO 21       | Reset (optional) |
| TP_SDA       | GPIO 47       | Touch I²C SDA    |
| TP_SCL       | GPIO 48       | Touch I²C SCL    |

#### ✔ LVGL-Konfiguration

`include/lv_conf.h` MUSS für den S3-Pfad diese Werte widerspiegeln:

```c
#define LV_HOR_RES_MAX 280
#define LV_VER_RES_MAX 456
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1
````

Rotation/Orientierung muss konsistent mit dem Displaytreiber sein.

---

## 🧩 UI-Struktur (nur Architekturregeln)

- `src/ui/ui_main.cpp` ist die zentrale Stelle für LVGL-Screens.
- Display-Update-Loop fließt über `display_s3_loop()`.
- Keine blockierenden Operationen (keine langen `delay()` im Hauptloop).
- Statusanzeigen (WiFi, BLE) laufen über `state.*`.
- UI-Logik bleibt von Hardwaredetails getrennt (Display-Treiber in `hardware/*`, State/Logik in `core/*`).

---

## 📁 Projektstruktur (verbindlich)

```
.
├─ platformio.ini
├─ boards/
│  └─ esp32s3.json
├─ include/
│  └─ lv_conf.h
├─ src/
│  ├─ bluetooth/
│  │  ├─ ble_obd.cpp
│  │  └─ ble_obd.h
│  ├─ core/
│  │  ├─ config.cpp
│  │  ├─ config.h
│  │  ├─ logging.cpp
│  │  ├─ logging.h
│  │  ├─ state.cpp
│  │  ├─ state.h
│  │  ├─ utils.cpp
│  │  ├─ utils.h
│  │  ├─ vehicle_info.cpp
│  │  ├─ vehicle_info.h
│  │  ├─ wifi.cpp        # zentrale WLAN-Logik (AP/STA, Scan, Connect)
│  │  └─ wifi.h
│  ├─ hardware/
│  │  ├─ display_s3.cpp
│  │  ├─ display_s3.h
│  │  ├─ display.cpp
│  │  ├─ display.h
│  │  ├─ led_bar.cpp
│  │  ├─ led_bar.h
│  │  ├─ logo_anim.cpp
│  │  └─ logo_anim.h
│  ├─ ui/
│  │  ├─ ui_main.cpp
│  │  └─ ui_main.h
│  ├─ web/
│  │  ├─ web_ui.cpp      # HTML-Generierung & Routen-Handler
│  │  ├─ web_ui.h
│  │  ├─ web_helpers.cpp
│  │  └─ web_helpers.h
│  └─ main.cpp
├─ lib/                  # externe / eigene Libraries
└─ test/                 # PlatformIO-Tests (Unity)
   ├─ test_main.cpp      # zentrales Unity-Setup (einziges setup()/loop())
   ├─ unit_core/
   │  └─ test_clamp_int.cpp
   ├─ unit_bluetooth/
   │  └─ (geplant)
   ├─ integration_connectivity/
   │  └─ (geplant)       # BLE/WLAN/Webserver-Integration
   └─ unit_ap/
      └─ (geplant)       # Tests für Access-Point-/WLAN-Logik
```

---

## 🔁 Workflow

1. Änderungen vornehmen (möglichst in kleinen, nachvollziehbaren Schritten).
2. Build ausführen (`pio run -e esp32s3` via PowerShell-Befehl).
3. Falls betroffen → Tests ausführen.
4. Bei Fehlern → Logs analysieren, gezielt korrigieren und erneut bauen/testen.
5. Erfolgreichen Stand dokumentieren (Dateien, Fehler vorher/nachher, Befehle, beobachtetes Laufzeitverhalten).

---

## 📌 Regeln für Änderungen (Architektur & Refactoring)

### Der Agent MUSS:

- ST7789-Pfad (`hardware/display.cpp`) nicht löschen oder brechen.
- S3-Pfad (`hardware/display_s3.cpp`) ohne ST7789-Code halten.
- QSPI-AMOLED-Pfad (SH8601, QSPI) konsistent halten, inkl. LVGL-Flush/Tick/Buffer.
- LVGL-Auflösung auf 280×456 / 16 Bit einhalten.
- PowerShell-Build-/Clean-Regeln strikt befolgen.
- Architektur (`core/hardware/ui/web`) respektieren.
- non-blocking bleiben (kein Dauersleep in zentralen Loops).
- Konfigurationssystem (Preferences/NVS) weiter nutzen, nicht umgehen.
- BLE gegen die eingebaute BLE-Implementierung des Arduino-Cores kompilieren (keine externe ESP32 BLE Arduino Lib).

### Der Agent DARF:

- interne Struktur verbessern (Refactorings, Aufräumen, Umbenennungen)
- S3-Displaytreiber refaktorisieren, inklusive Debug-Hilfen (Testpattern)
- neue Testcases hinzufügen (Unit-/Integrationstests)
- Logging verbessern und vereinheitlichen
- größere Umbauten durchführen, solange Funktionalität (ShiftLight, BLE-OBD, Web, Display) erhalten bleibt

### Der Agent DARF NICHT:

- `display.cpp` löschen oder unbrauchbar machen.
- ST7789 in `display_s3.cpp` aktivieren oder mischen.
- Board oder Pinout ändern, ohne explizite Konsistenz und Dokumentation sicherzustellen.
- Tests entfernen oder „grün patchen" (z. B. Assertions auskommentieren statt Fehler zu beheben).
- externe BLE-Libraries in `lib_deps` hinzunehmen, die mit Arduino Core 3.x kollidieren.

---

## 📌 Rückfragenpflicht

Der Agent soll Rückfragen stellen (im Report / Kommentar), wenn:

- Hardwareverhalten unklar oder widersprüchlich ist (z. B. unterschiedliche Pinouts).
- relevante Logs fehlen (z. B. Startlog vom seriellen Monitor).
- Netzwerkzugriff nötig wäre, der Nutzer aber kein „full access" erlaubt hat.
- Eingriffe außerhalb des Workspaces nötig wären (z. B. globale Systemkonfiguration).
