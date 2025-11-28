# ✅ AGENTS.md – RpmCounter / ShiftLight (Codex Cloud Version)

## 1. Projektüberblick

Dieses Repository enthält die Firmware für ein ShiftLight-/RPM-Anzeige-System auf Basis eines ESP32, entwickelt mit PlatformIO und dem Arduino-Framework.

**Unterstützte Funktionen / Features:**

- BLE‑OBD‑Anbindung zu einem OBD-II Dongle
- LED-Bar als ShiftLight
- Webserver (AP/STA) mit Web‑UI und Konfiguration
- LVGL‑UI für RPM, Gang, Status, Footer, etc.
- Konfiguration via NVS/Preferences
- Mehrere Displaypfade:
  - „altes" ST7789‑Display (240×240, SPI)
  - Waveshare ESP32‑S3 Touch AMOLED 1.64" (280×456, QSPI + I²C‑Touch)

**Zielzustand für den Codex‑Agent:**

- Das Waveshare‑AMOLED‑Display zeigt eine funktionierende LVGL‑UI an (inkl. Test‑Overlay / Logo).
- BLE‑OBD funktioniert stabil, sendet RPM + Speed, Gangschätzung funktioniert.
- Webserver, WiFi AP/STA, LED‑Bar, Logo‑Animationen und State‑Handling funktionieren wie vorher oder besser.
- Das Projekt baut im Container mit `pio run -e esp32s3` fehlerfrei.
- Der Code ist konsistent refaktoriert (Architektur, Lesbarkeit, Fehlerbehandlung), ohne Features zu verlieren.

---

## 2. Arbeitsumgebung für Codex Cloud

Der Agent läuft in einem **Linux-Container** mit Zugriff auf das Git-Repository.

- Projekt-Root = Ordner, in dem `platformio.ini` liegt.
- Shell: Standard‑Shell (bash/sh).
- `pio` (PlatformIO CLI) soll verwendet werden, um Builds im Container auszuführen.
- **Keine Hardware** angeschlossen → kein Flash/Upload/Serieller Monitor im Container.
- Internetzugriff ist i. d. R. deaktiviert → keine externen `curl/wget`/HTTP‑Tests.

---

## 3. Was der Agent darf / nicht darf

### 3.1 STANDARD-MODUS

**Der Agent DARF:**

- Dateien im Workspace lesen und ändern (innerhalb des Repos).
- Shell‑Befehle im Container ausführen, insbesondere:
  - `pio run -e esp32s3`
  - `pio run -e esp32s3 -t clean`
- `platformio.ini`, `boards/`, `src/`, `include/`, `lib/`, `test/` anpassen.
- Code umfassend refaktorisieren, solange Funktionalität erhalten bleibt.
- Logging, Fehlerbehandlung, Struktur und Lesbarkeit verbessern.

**Der Agent DARF NICHT:**

- Dateien außerhalb dieses Repositories verändern.
- Upload/Flash‑Befehle ausführen, die echte Hardware voraussetzen (`pio run -t upload`, `pio device monitor`) – diese sind im Container nicht notwendig.
- Netzwerkbefehle wie `curl`, `wget` zu externen Hosts verwenden.
- Projektarchitektur zerstören (z. B. alles in eine Datei schieben).

---

## 4. Build- & Clean-Befehle (Container)

Alle Befehle werden vom **Projekt-Root** (dort, wo `platformio.ini` liegt) ausgeführt.

### 4.1 Normaler Build (Pflicht)

```sh
pio run -e esp32s3
```

Der Agent MUSS diesen Build-Befehl regelmäßig ausführen:

- nach wesentlichen Änderungen an Code/Config
- mindestens einmal, bevor er einen Task als „fertig" betrachtet

**Ziel:** Build ohne Fehler (0 Errors).

### 4.2 Clean

```sh
pio run -e esp32s3 -t clean
```

### 4.3 Full-Clean (falls nötig)

```sh
pio run -e esp32s3 -t fullclean
```

### 4.4 Verhalten bei Build-Fehlern

1. Build ausführen (`pio run -e esp32s3`).
2. Alle Fehler im Output analysieren.
3. Code/Config anpassen, um die Fehler zu beheben.
4. Erneut `pio run -e esp32s3` ausführen.
5. Diesen Zyklus wiederholen, bis ein fehlerfreier Build erreicht ist oder keine sinnvollen, sicheren Änderungen mehr möglich sind.

---

## 5. PlatformIO / Toolchain / Libraries

### 5.1 Umgebung in platformio.ini

Die relevante Umgebung ist:

```ini
[env:esp32s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
framework = arduino
board = esp32s3
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
test_framework = unity
test_build_src = yes
upload_port = COM4
monitor_port = COM4

lib_deps =
  Adafruit NeoPixel
  ESP32 BLE Arduino
  moononournation/GFX Library for Arduino@1.6.3
  lvgl@^8.3.11

build_flags =
  -DLV_LVGL_H_INCLUDE_SIMPLE
  -DLV_CONF_INCLUDE_SIMPLE
  -I include
  -DCONFIG_IDF_TARGET_ESP32S3
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MODE=1
```

**Hinweis:** COM‑Ports betreffen nur die lokale Entwicklungsumgebung des Nutzers.
Im Container sind sie für den Build nicht relevant und können ignoriert werden.

### 5.2 BLE-Bibliothek (wichtig)

Der Arduino‑ESP32 Core 3.x bringt selbst eine BLE-Implementierung (`BLEDevice`, `BLEClient`, …) mit.

**Ziel:** BLE‑Code in `src/bluetooth/ble_obd.cpp/.h` soll ohne externe ESP32 BLE Arduino-Lib kompilieren, idealerweise nur mit der BLE‑Library des Cores.

**Der Agent SOLL:**

- prüfen, ob `ESP32 BLE Arduino` in `lib_deps` wirklich nötig ist.
- wenn diese externe Lib Buildfehler verursacht (fehlende `esp_gap_ble_api.h`, `esp_gatt_defs.h`, …), die BLE‑Nutzung auf die im Core enthaltene BLE‑Library migrieren:
  - `#include <BLEDevice.h>`, etc. können bleiben,
  - aber die Includes müssen aus dem Core stammen, nicht aus einer inkompatiblen externen Version.
- `ESP32 BLE Arduino` aus `lib_deps` entfernen, falls sie inkompatibel ist und die Core‑BLE‑Implementierung ausreicht.

### 5.3 Display-/GFX-Library

Für das ESP32‑S3 QSPI‑AMOLED‑Display wird die **GFX Library for Arduino** verwendet:
`moononournation/GFX Library for Arduino@1.6.3`

**QSPI-Pfad:**

- Datenbus: `Arduino_ESP32QSPI`
- Panel: `Arduino_CO5300` (oder anderer geeigneter Treiber für SH8601‑basierte AMOLED‑Panels)
- Touch-Controller FT3168 nutzt den neuen I2C-Treiber (`driver/i2c_master.h`, `i2c_master_bus_handle_t`, `i2c_master_dev_handle_t`), kein Legacy-API `driver/i2c.h`, kein Wire im S3-Pfad.

Der Agent darf, falls nötig:

- innerhalb dieser Library die korrekten Panel‑Constructor‑Parameter einsetzen,
- die Initialisierung anpassen (Brightness, Rotation, etc.), um das Panel lauffähig zu bekommen.

---

## 6. Display- & LVGL-Regeln

### 6.1 ST7789-Pfad (alte Hardware)

**Dateien:**

- `src/hardware/display.cpp`
- `src/hardware/display.h`

**Typ:**

- ST7789, 240×240, SPI

**MUSS erhalten bleiben.**

- Darf NICHT entfernt oder durch AMOLED‑Code ersetzt werden.
- Darf weiterhin ST7789/Adafruit‑Libs verwenden.

### 6.2 ESP32-S3 AMOLED Pfad (Waveshare 1.64" QSPI)

**Dateien:**

- `src/hardware/display_s3.cpp`
- `src/hardware/display_s3.h`

**Board:**

- Waveshare ESP32‑S3 Touch AMOLED 1.64" oder kompatibles Layout

**Display:**

- Typ: QSPI AMOLED (SH8601/CO5300)
- Auflösung: 280×456
- Interface: QSPI (CS, CLK, D0–D3), Reset separat

**Touch:**

- Controller: FT3168 (I²C)
- SDA: GPIO 47
- SCL: GPIO 48

#### 6.2.1 Pinbelegung (AMOLED)

| AMOLED Pin   | ESP32-S3 GPIO | Verwendung    |
| ------------ | ------------- | ------------- |
| QSPI_CS      | 9             | CS            |
| QSPI_CLK     | 10            | Clock         |
| QSPI_D0      | 11            | Data0         |
| QSPI_D1      | 12            | Data1         |
| QSPI_D2      | 13            | Data2         |
| QSPI_D3      | 14            | Data3         |
| AMOLED_RESET | 21            | Reset         |
| TP_SDA       | 47            | Touch I²C SDA |
| TP_SCL       | 48            | Touch I²C SCL |

#### 6.2.2 LVGL-Konfiguration

`include/lv_conf.h` MUSS für den S3‑Pfad konsistent sein:

```c
#define LV_HOR_RES_MAX 280
#define LV_VER_RES_MAX 456
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
```

Rotation/Orientierung muss mit dem Displaytreiber übereinstimmen.

#### 6.2.3 Treiber-Stack (Sollzustand für display_s3.cpp)

**Datenbus:**

- `Arduino_ESP32QSPI` mit den oben genannten Pins.

**Panel:**

- `Arduino_CO5300` (oder ein anderer, passender Arduino_GFX‑Treiber für SH8601).

**LVGL‑Integration:**

- Zwei DMA‑fähige Buffers (`heap_caps_malloc(..., MALLOC_CAP_DMA)` mit Fallback).
- `lv_disp_draw_buf_init(...)`.
- `lv_disp_drv_init(...)` mit:
  - `flush_cb` → `g_gfx->draw16bitRGBBitmap(...)`
  - `rounder_cb` zur korrekten Ausrichtung.
- LVGL‑Tick via `esp_timer` oder Fallback im `display_s3_loop()`.

**Strikte Regel:**
In `display_s3.cpp` darf KEIN ST7789‑Code mehr enthalten sein und keine parallele `esp_lcd_*`‑Panelimplementation, wenn der Weg über Arduino_GFX genutzt wird.

---

## 7. UI- und Anwendungslogik

### 7.1 LVGL-UI

**Zentrale UI-Dateien:**

- `src/ui/ui_main.cpp`
- `src/ui/ui_main.h`

**Aufgaben:**

- Status-Leiste für WiFi/BLE
- Labels für RPM, Gang, Speed
- Activity-Bar
- Footer für ShiftLight‑Status
- Overlay „AMOLED TEST / Waveshare 1.64""

**S3‑Display-Datenfluss (Soll):**

`display_s3_init()`:

- LVGL initialisieren.
- GFX‑Panel initialisieren (QSPI, Panel, Helligkeit, Clear-Screen).
- LVGL‑Display registrieren (inkl. flush_cb, rounder_cb).
- Touch‑Eingabegerät (FT3168) registrieren.
- `ui_main_init(g_disp)` aufrufen.

`display_s3_loop()`:

- LVGL‑Tick aktualisieren (`lv_tick_inc` via Timer oder Fallback).
- `lv_timer_handler()` aufrufen.
- `ui_main_update_status(...)` für WiFi/BLE‑Status nutzen.
- `ui_main_loop()` aufrufen (Activity-Bar, Overlay‑Timer etc.).

### 7.2 BLE, WiFi, Webserver, LED-Bar

**BLE / OBD:**

- `src/bluetooth/ble_obd.cpp`
- `src/bluetooth/ble_obd.h`
- Verwendet BLE‑API (`BLEDevice`, `BLEClient`, …).
- Verarbeitet OBD‑Daten, berechnet Gangschätzung, ruft `displaySetGear(...)` und `displaySetShiftBlink(...)` auf.

**WiFi / Webserver:**

- `src/core/wifi.cpp`, `src/core/wifi.h`
- `src/web/web_ui.cpp`, `src/web/web_ui.h`
- `src/web/web_helpers.cpp`, `src/web/web_helpers.h`

**LED-Bar / Logo:**

- `src/hardware/led_bar.cpp`, `src/hardware/led_bar.h`
- `src/hardware/logo_anim.cpp`, `src/hardware/logo_anim.h`

**Main:**
`src/main.cpp`:

`setup()`:

- Config laden
- State initialisieren
- LED‑Bar, WiFi, WebUI, BLE, Display initialisieren.

`loop()`:

- S3: `display_s3_loop()`
- `webUiLoop()`, `wifiLoop()`, `bleObdLoop()`, `ledBarLoop()`, `logoAnimLoop()`
- non‑blocking.

---

## 8. Projektstruktur

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
│  │  ├─ wifi.cpp
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
│  │  ├─ web_ui.cpp
│  │  ├─ web_ui.h
│  │  ├─ web_helpers.cpp
│  │  └─ web_helpers.h
│  └─ main.cpp
├─ lib/
│  └─ ...
└─ test/
   ├─ test_main.cpp
   ├─ unit_core/
   │  └─ test_clamp_int.cpp
   ├─ unit_bluetooth/
   │  └─ (geplant)
   ├─ integration_connectivity/
   │  └─ (geplant)
   └─ unit_ap/
      └─ (geplant)
```

---

## 9. Konkrete Aufgaben für den Codex-Agenten

Der Agent soll in iterativen Schritten:

1. `pio run -e esp32s3` im Container ausführen, alle Buildfehler sammeln.
2. Code/Config anpassen, um:
   - das S3‑Display (AMOLED, LVGL, QSPI) funktionsfähig zu machen,
   - BLE‑OBD mit der BLE‑Implementierung des Arduino‑Cores kompatibel zu halten,
   - Webserver/WiFi/LED‑Bar/Logo‑Animationen beizubehalten.
3. Nach Änderungen wieder `pio run -e esp32s3` ausführen.
4. Diesen Zyklus wiederholen, bis der Build fehlerfrei ist.
5. Parallel dazu refaktorieren:
   - lange Funktionen aufteilen,
   - Duplikate entfernen,
   - klarere Struktur in `display_s3.cpp`, `ble_obd.cpp`, `web_ui.cpp`, etc.
6. Am Ende einen kurzen Bericht hinterlassen:
   - Geänderte Dateien,
   - Ursprüngliche Buildfehler (kurz),
   - Wie sie gelöst wurden,
   - Status (Display, BLE, Web, LED‑Bar),
   - Offene TODOs.

---

## 10. Regeln für Änderungen

### 10.1 Der Agent MUSS

- Den ST7789‑Pfad (`src/hardware/display.cpp`) funktionsfähig lassen.
- Den S3‑Pfad (`src/hardware/display_s3.cpp`) ST7789‑frei halten (AMOLED only).
- QSPI‑Pfad laut Pinout beibehalten.
- LVGL‑Auflösung 280×456 / 16 Bit einhalten.
- Nur den neuen I2C-Treiber (`driver/i2c_master.h`) f?r den FT3168 nutzen - keine Legacy-API `driver/i2c.h`, kein Wire im S3-Pfad.
- `pio run -e esp32s3` regelmäßig ausführen, bis ein fehlerfreier Build erreicht ist (oder keine sinnvollen Änderungen mehr möglich sind).
- Non‑blocking‑Design respektieren (keine langen Delays in zentralen Loops).
- NVS/Preferences weiterhin nutzen (Konfigurationssystem nicht umgehen).
- BLE gegen die BLE‑Implementierung des Arduino‑Cores 3.x kompilieren (keine inkompatiblen externen BLE‑Libs reinziehen).

### 10.2 Der Agent DARF

- Code umfassend refaktorisieren (auch mehrere Dateien).
- neue Helper-Funktionen/Typen/Namespaces einführen.
- Logging vereinheitlichen und verbessern.
- neue Tests hinzufügen.

### 10.3 Der Agent DARF NICHT

- `display.cpp` bzw. den ST7789‑Pfad entfernen oder brechen.
- das Pinout eigenmächtig ändern, ohne klaren Grund.
- Tests löschen oder einfach „grün patchen".
- Funktionen stubben, so dass zwar der Build ok ist, aber das Feature faktisch deaktiviert ist.

---

## 11. Rückfragenpflicht

Der Agent soll (im Kommentar/Report) Rückfragen signalisieren, wenn:

- Hardwareangaben im Code/Boardfile widersprüchlich sind.
- Crashlogs/Backtraces erwähnt werden, aber der konkrete Log fehlt.
- ein fundamentaler Wechsel der BLE‑Library oder Plattform nötig wäre.
- Build‑Fehler nur durch potenziell riskante, nicht-triviale Änderungen lösbar erscheinen.
