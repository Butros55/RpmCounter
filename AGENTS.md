# ✅ AGENTS.md – RpmCounter / ShiftLight (Architektur, Regeln, Display-Pfade)

## 🌟 Projektüberblick

Dieses Repository enthält die Firmware für ein ShiftLight-/RPM-Anzeige-System auf Basis eines ESP32 (PlatformIO, Arduino).

Der Code unterstützt mehrere Hardwarevarianten, insbesondere:

- ältere Boards mit ST7789-Display (240×240)
- neue Waveshare ESP32-S3 Boards mit 1.64" QSPI AMOLED (280×456)
- BLE-OBD Anbindung
- LED-Bar
- WLAN-Webserver (AP/STA)
- LVGL-UI
- NVS-Konfiguration

**Ziel:**  
Eine stabile, nicht-blockierende Firmware, bei der BLE, WLAN/AP/STA, LED-Bar, Webserver und Display parallel funktionieren.

---

## 🤖 Agent-Modi

### 🟦 STANDARD-MODUS

**Der Agent darf:**

- Dateien im Workspace lesen/schreiben
- Build/Test-Befehle in PowerShell ausführen
- Logs analysieren
- architekturgemäße Änderungen vornehmen

**Der Agent darf NICHT:**

- Netzwerkbefehle (curl/wget) ausführen
- Linux/WSL-Syntax verwenden
- Dateien außerhalb des Repositories verändern

### 🟩 NETZWERK-DEBUG („FULL ACCESS”)

Nur erlaubt, wenn der Nutzer explizit schreibt:

> „Nutze bitte Netzwerk-Debug / full access“

Dann darf der Agent zusätzlich Netzwerkbefehle im LAN ausführen.

---

### ✔ PowerShell-Befehle (Python 3.11, PlatformIO)

# Build

'C:\Program Files\PowerShell\7\pwsh.exe' -Command 'Set-Location "c:\dev\RpmCounter"; $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home"; pio run -e esp32s3'

# Flash

'C:\Program Files\PowerShell\7\pwsh.exe' -Command 'Set-Location "c:\dev\RpmCounter"; $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home"; pio run -e esp32s3 -t upload'

# Serieller Monitor

'C:\Program Files\PowerShell\7\pwsh.exe' -Command 'Set-Location "c:\dev\RpmCounter"; $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home"; pio device monitor'

# Clean

'C:\Program Files\PowerShell\7\pwsh.exe' -Command 'Set-Location "c:\dev\RpmCounter"; $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home"; pio run -e esp32s3 -t clean'

# Full-Clean (falls nötig)

'C:\Program Files\PowerShell\7\pwsh.exe' -Command 'Set-Location "c:\dev\RpmCounter"; $env:PLATFORMIO_HOME_DIR = "$PWD\.pio-home"; pio run -e esp32s3 -t fullclean'

````

---

## 🧹 Build-Clean & .pio-Probleme

### Typische Fehler

- WinError 5 – Zugriff verweigert (In diesem fall mich nach permission prompten!! nachdem ich allow for this session angeklick habe gehts)
- „Can not remove temporary directory .pio\build …“


### Eskalationsstufen

#### 1) Normaler Clean

```powershell
Set-Location "C:\dev\RpmCounter"
py -3.11 -m platformio run -e esp32s3 -t clean
```

#### 2) Full-Clean

```powershell
Set-Location "C:\dev\RpmCounter"
py -3.11 -m platformio run -e esp32s3 -t fullclean
```

#### 3) .pio\build hart löschen (nur wenn PlatformIO ausdrücklich scheitert!)

```powershell
Set-Location "C:\dev\RpmCounter"

if (Test-Path ".pio\build") {
    Get-ChildItem ".pio\build" -Recurse -Force | ForEach-Object {
        if (-not $_.PSIsContainer) {
            try { $_.IsReadOnly = $false } catch {}
        }
    }
    Remove-Item ".pio\build" -Recurse -Force
}
```

**Wenn immer noch „Access Denied“:**
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
- ❗ Dieses Display ist ein QSPI AMOLED (280×456)
- ❗ Daher darf in display_s3.cpp KEIN ST7789-Code mehr existieren

#### ✔ Anforderungen an den ESP32-S3-Pfad

- **Displaytyp:** Waveshare 1.64" AMOLED
- **Interface:** QSPI (6-Pin: CLK, CS, D0–D3)
- **Auflösung:** 280 × 456 Pixel
- **Farbtiefe:** 16 Bit
- **Touch:** I²C (GPIO 47/48)

#### ✔ Pinbelegung (laut Waveshare-Pinout)

| AMOLED Pin   | ESP32-S3 GPIO | Verwendung       |
| ------------ | ------------- | ---------------- |
| QSPI_CS      | GPIO 9        | DC / Host CS     |
| QSPI_CLK     | GPIO 10       | Clock            |
| QSPI_D0      | GPIO 11       | Data0            |
| QSPI_D1      | GPIO 12       | Data1            |
| QSPI_D2      | GPIO 13       | Data2            |
| QSPI_D3      | GPIO 14       | Data3 / RST      |
| AMOLED_RESET | GPIO 21       | Reset (optional) |
| TP_SDA       | GPIO 47       | Touch I²C SDA    |
| TP_SCL       | GPIO 48       | Touch I²C SCL    |

#### ✔ LVGL-Konfiguration

`include/lv_conf.h` MUSS für den S3-Pfad diese Werte widerspiegeln:

```c
#define LV_HOR_RES_MAX 280
#define LV_VER_RES_MAX 456
#define LV_COLOR_DEPTH 16
```

Rotation/Orientierung muss konsistent mit dem Displaytreiber sein.

---

## 🧩 UI-Struktur (nur Architekturregeln)

- `src/ui/ui_main.cpp` ist die zentrale Stelle für LVGL-Screens
- Display-Update-Loop fließt über `display_s3_loop()`
- Keine blockierenden Operationen
- Statusanzeigen (WiFi, BLE) laufen über `state.*`
- Keine inhaltlichen Anweisungen hier – nur Struktur.

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

1. Änderungen vornehmen
2. Build ausführen
3. Falls betroffen → Tests ausführen
4. Bei Fehlern → korrigieren und erneut bauen/testen
5. Erfolgreichen Stand dokumentieren (Dateien, Fehler vorher/nachher, Befehle)

---

## 📌 Regeln für Änderungen (Architektur, keine Aufgaben)

### Der Agent MUSS:

- ST7789-Pfad (`display.cpp`) nicht löschen oder brechen
- S3-Pfad (`display_s3.cpp`) ohne ST7789-Code halten
- QSPI-AMOLED-Pfad konsistent halten
- LVGL-Auflösung auf 280×456 einhalten
- PowerShell-Regeln strikt befolgen
- Architektur (core/hardware/ui/web) einhalten
- non-blocking bleiben
- Konfigurationssystem (Preferences/NVS) weiter nutzen, nicht umgehen

### Der Agent DARF:

- interne Struktur verbessern
- S3-Displaytreiber refaktorisieren
- neue Testcases hinzufügen
- Logging verbessern

### Der Agent DARF NICHT:

- `display.cpp` löschen
- ST7789 in `display_s3.cpp` aktivieren
- Board oder Pinout ändern, ohne Konsistenz sicherzustellen
- Tests entfernen oder „grün patchen“

---

## 📌 Rückfragenpflicht

Der Agent soll Fragen stellen, wenn:

- Hardwareverhalten unklar oder widersprüchlich ist
- relevante Logs fehlen
- Netzwerkzugriff nötig wäre
- Eingriffe außerhalb des Workspaces nötig wären
````
