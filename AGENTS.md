# ✅ AGENTS.md – RpmCounter / ShiftLight (Aktueller funktionaler Stand)

## 1. Projektüberblick

Dieses Repository enthält die Firmware für ein ShiftLight-/RPM-Anzeige-System auf Basis eines ESP32-S3, entwickelt mit PlatformIO und dem Arduino-Framework.

**⚠️ WICHTIG: Dieses Projekt ist FUNKTIONAL und GETESTET! Änderungen nur mit Vorsicht!**

**Unterstützte Funktionen / Features (alle funktionieren!):**

- ✅ BLE‑OBD‑Anbindung zu einem OBD-II Dongle (NimBLE oder Core-BLE)
- ✅ LED-Bar als ShiftLight (30 LEDs, Adafruit NeoPixel)
- ✅ Webserver (AP/STA) mit Web‑UI und Konfiguration
- ✅ LVGL‑UI für RPM, Gang, Status, Footer, etc.
- ✅ Touch-Input funktioniert (FT3168 via neuer I2C-API)
- ✅ Konfiguration via NVS/Preferences
- ✅ WiFi AP-Mode ("ShiftLight" Netzwerk) + STA-Mode
- ✅ Displaypfade:
  - „altes" ST7789‑Display (240×240, SPI) - für andere Hardware
  - **Waveshare ESP32‑S3 Touch AMOLED 1.64" (280×456, QSPI + I²C‑Touch)** - HAUPTZIEL

**Aktueller Status (November 2025): ALLES FUNKTIONIERT!**

- Das Waveshare‑AMOLED‑Display zeigt eine funktionierende LVGL‑UI an.
- Touch funktioniert stabil (FT3168, neuer I2C-Treiber, synchroner Modus).
- BLE‑OBD funktioniert, sendet RPM + Speed, Gangschätzung funktioniert.
- WiFi AP-Mode funktioniert stabil (Channel 6, ESP-IDF Config).
- Webserver erreichbar unter http://192.168.4.1
- LED‑Bar, Logo‑Animationen und State‑Handling funktionieren.

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

### 5.1 Umgebung in platformio.ini (AKTUELL - NICHT ÄNDERN!)

```ini
[env:esp32s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
framework = arduino
board = esp32s3
monitor_speed = 115200
build_type = debug
upload_speed = 921600

lib_deps =
  Adafruit NeoPixel
  moononournation/GFX Library for Arduino@1.6.3
  lvgl@^8.3.11

build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -D LV_CONF_PATH="${PROJECT_DIR}/include/lv_conf.h"
```

**WICHTIG:**

- **KEINE** `ESP32 BLE Arduino` in lib_deps! Der Arduino-ESP32 Core 3.x bringt BLE bereits mit.
- Falls NimBLE verfügbar ist, wird es automatisch verwendet (siehe `ble_obd.cpp`).
- COM‑Ports betreffen nur die lokale Windows-Entwicklungsumgebung.

### 5.2 BLE-Bibliothek (FUNKTIONIERT - NICHT ÄNDERN!)

**Aktueller Stand:**

- Der Code in `src/bluetooth/ble_obd.cpp` unterstützt BEIDE Varianten:
  - **NimBLE** (wenn `<NimBLEDevice.h>` verfügbar ist) - bevorzugt
  - **Core-BLE** (`<BLEDevice.h>` aus dem Arduino-ESP32 Core)
- Automatische Erkennung via `#if __has_include(<NimBLEDevice.h>)`

**Der Agent DARF NICHT:**

- `ESP32 BLE Arduino` zu lib_deps hinzufügen (verursacht Konflikte!)
- Die BLE-Abstraktionsschicht in `ble_obd.cpp` ändern
- Die NimBLE/Core-BLE-Kompatibilitätsschicht entfernen

### 5.3 Display-/GFX-Library (FUNKTIONIERT - NICHT ÄNDERN!)

Für das ESP32‑S3 QSPI‑AMOLED‑Display wird verwendet:
`moononournation/GFX Library for Arduino@1.6.3`

**QSPI-Stack (funktioniert!):**

- Datenbus: `Arduino_ESP32QSPI`
- Panel: `Arduino_CO5300`
- Auflösung: 280×456, 16-bit Farbe

**Touch-Controller FT3168:**

- **MUSS** den neuen I2C-Treiber nutzen: `driver/i2c_master.h`
- **NICHT** die Legacy-API `driver/i2c.h`
- **NICHT** Wire-Library im S3-Pfad
- **KRITISCH:** `trans_queue_depth = 0` (synchroner Modus, verhindert Queue-Overflow!)
- **KRITISCH:** Timeout `-1` (blockierend, verhindert Race Conditions!)

---

## 6. Display- & LVGL-Regeln (FUNKTIONIERT - KRITISCHE DETAILS!)

### 6.1 ST7789-Pfad (alte Hardware)

**Dateien:**

- `src/hardware/display.cpp`
- `src/hardware/display.h`

**Typ:**

- ST7789, 240×240, SPI

**MUSS erhalten bleiben.**

- Darf NICHT entfernt oder durch AMOLED‑Code ersetzt werden.
- Wird kompiliert wenn NICHT `CONFIG_IDF_TARGET_ESP32S3` definiert ist.

### 6.2 ESP32-S3 AMOLED Pfad (Waveshare 1.64" QSPI) - FUNKTIONIERT!

**Dateien:**

- `src/hardware/display_s3.cpp` - Hauptdatei für Display & Touch
- `src/hardware/display_s3.h`
- `src/ui/ui_s3_main.cpp` - LVGL UI Komponenten
- `src/ui/ui_s3_main.h`

**Board:**

- Waveshare ESP32‑S3 Touch AMOLED 1.64"

**Display:**

- Typ: QSPI AMOLED (CO5300-Treiber für SH8601-basiertes Panel)
- Auflösung: 280×456
- Interface: QSPI (CS, CLK, D0–D3), Reset separat
- Helligkeit: Über `Arduino_CO5300::setBrightness()` steuerbar

**Touch:**

- Controller: FT3168 (I²C)
- SDA: GPIO 47
- SCL: GPIO 48
- Adresse: 0x38
- Geschwindigkeit: 100kHz (für Stabilität!)

#### 6.2.1 Pinbelegung (AMOLED) - NICHT ÄNDERN!

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

#### 6.2.2 LVGL-Konfiguration (include/lv_conf.h) - NICHT ÄNDERN!

```c
#define LV_HOR_RES_MAX (280)
#define LV_VER_RES_MAX (456)
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1  // WICHTIG: 1, nicht 0!
#define LV_MEM_SIZE (48U * 1024U)
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30
```

#### 6.2.3 Treiber-Stack in display_s3.cpp (FUNKTIONIERT!)

**Datenbus:**

```cpp
g_bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_CLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
```

**Panel:**

```cpp
g_gfx = new Arduino_CO5300(
    g_bus,
    PIN_LCD_RST,
    0,                  // rotation
    LCD_H_RES,          // 280
    LCD_V_RES,          // 456
    LCD_COL_OFFSET1,    // 20
    LCD_ROW_OFFSET1,    // 0
    LCD_COL_OFFSET2,    // 0
    LCD_ROW_OFFSET2);   // 0
```

**LVGL‑Integration:**

- Zwei DMA‑fähige Buffers (`heap_caps_malloc(..., MALLOC_CAP_DMA)` mit Fallback)
- `lv_disp_draw_buf_init(...)` mit Doppelpuffer
- `rounder_cb` für korrekte Ausrichtung (gerade Pixelwerte)
- LVGL‑Tick via `esp_timer` (2ms Periode)

#### 6.2.4 Touch-Controller FT3168 (KRITISCHE IMPLEMENTIERUNG!)

**I2C-Bus Konfiguration (NICHT ÄNDERN!):**

```cpp
i2c_master_bus_config_t bus_cfg = {};
bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
bus_cfg.i2c_port = I2C_NUM_0;
bus_cfg.scl_io_num = GPIO_NUM_48;
bus_cfg.sda_io_num = GPIO_NUM_47;
bus_cfg.glitch_ignore_cnt = 7;
bus_cfg.trans_queue_depth = 0;  // KRITISCH: Synchroner Modus!
bus_cfg.flags.enable_internal_pullup = true;
```

**I2C-Device Konfiguration:**

```cpp
i2c_device_config_t dev_cfg = {};
dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
dev_cfg.device_address = 0x38;
dev_cfg.scl_speed_hz = 100000;  // 100kHz für Stabilität
```

**I2C Lese/Schreib-Operationen:**

```cpp
// Timeout -1 = blockierend (KRITISCH!)
i2c_master_transmit(g_touchDev, buf, len, -1);
i2c_master_transmit_receive(g_touchDev, &reg, 1, data, len, -1);
```

**WARUM `trans_queue_depth = 0` und `timeout = -1`?**

- Verhindert "i2c_ll_master_get_event I2C Software buffer overflow" Fehler
- Synchroner Modus = kein interner Queue-Buffer = keine Überläufe
- Blockierendes Timeout = keine Race Conditions bei Touch-Abfragen

**Strikte Regel:**
In `display_s3.cpp` darf KEIN ST7789‑Code enthalten sein und KEINE Legacy-I2C-API!

---

## 7. UI- und Anwendungslogik (FUNKTIONIERT!)

### 7.1 LVGL-UI

**Zentrale UI-Dateien für S3:**

- `src/ui/ui_s3_main.cpp` - UI-Komponenten, Labels, Status-Icons
- `src/ui/ui_s3_main.h`

**Aufgaben:**

- Status-Icons für WiFi (grün/rot/blinkend) und BLE (blau/rot)
- Labels für RPM, Gang, Speed
- Activity-Bar
- Footer für ShiftLight‑Status
- Test-Overlay „AMOLED TEST / Waveshare 1.64""

**S3‑Display-Datenfluss (FUNKTIONIERT!):**

`display_s3_init()`:

1. LVGL initialisieren (`lv_init()`)
2. LVGL-Buffers allokieren (DMA oder Fallback)
3. GFX‑Panel initialisieren (QSPI-Bus, CO5300, Helligkeit)
4. LVGL‑Display registrieren (flush_cb, rounder_cb)
5. Touch‑Eingabegerät (FT3168) initialisieren und registrieren
6. `ui_s3_main_init(g_disp)` aufrufen

`display_s3_loop()`:

1. LVGL‑Tick aktualisieren (via esp_timer oder Fallback)
2. `lv_timer_handler()` aufrufen
3. `ui_s3_update_status(...)` für WiFi/BLE‑Status
4. `ui_s3_main_loop()` aufrufen (Activity-Bar, etc.)

### 7.2 WiFi (FUNKTIONIERT!)

**Dateien:**

- `src/core/wifi.cpp`
- `src/core/wifi.h`

**Implementierung:**

- **SoftAP-Mode:** SSID "ShiftLight", Passwort "shiftlight123"
- **Channel 6** für bessere Kompatibilität
- **ESP-IDF Level Konfiguration** für Stabilität:
  ```cpp
  wifi_config_t wifi_config = {};
  // ... Konfiguration ...
  esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
  ```
- IP: 192.168.4.1 im AP-Mode
- STA-Mode funktioniert parallel (falls konfiguriert)

**WiFi-Icon Status:**

- Grün blinkend: AP aktiv, wartet auf Clients
- Grün solid: Client verbunden
- Rot: Fehler oder nicht aktiv

### 7.3 BLE/OBD (FUNKTIONIERT!)

**Dateien:**

- `src/bluetooth/ble_obd.cpp`
- `src/bluetooth/ble_obd.h`

**Implementierung:**

- **Automatische BLE-Library Erkennung:**
  ```cpp
  #if __has_include(<NimBLEDevice.h>)
  #define RPMCOUNTER_USE_NIMBLE 1
  #include <NimBLEDevice.h>
  #else
  #include <BLEDevice.h>
  // ... Core-BLE includes ...
  #endif
  ```
- **Type-Aliasing** für NimBLE/Core-BLE Kompatibilität
- OBD-PID Abfragen für RPM und Speed
- **Gangschätzung** via Verhältnis RPM/Speed mit Lookup-Tabelle
- Callbacks für UI-Updates (`displaySetGear`, `displaySetShiftBlink`)

**BLE-Icon Status:**

- Blau: Verbunden mit OBD-Dongle
- Rot: Nicht verbunden

### 7.4 Webserver (FUNKTIONIERT!)

**Dateien:**

- `src/web/web_ui.cpp`, `src/web/web_ui.h`
- `src/web/web_helpers.cpp`, `src/web/web_helpers.h`

**Implementierung:**

- AsyncWebServer auf Port 80
- Konfigurationsseite für LED-Bar, WiFi, etc.
- API-Endpunkte für Live-Daten
- URL: http://192.168.4.1 (AP-Mode) oder dynamische IP (STA-Mode)

### 7.5 LED-Bar (FUNKTIONIERT!)

**Dateien:**

- `src/hardware/led_bar.cpp`
- `src/hardware/led_bar.h`

**Implementierung:**

- **30s** via Adafruit NeoPixel
- **Pin:** GPIO 5
- **Farbzonen:** Grün → Gelb → Rot basierend auf RPM
- **Blink-Modus** bei Schaltdrehzahl
- Konfigurierbare Farben und Schwellwerte

### 7.6 Logo-Animation (FUNKTIONIERT!)

**Dateien:**

- `src/hardware/logo_anim.cpp`
- `src/hardware/logo_anim.h`

**Implementierung:**

- Test-Sweep Animation auf LED-Bar
- Triggerbar bei Zündung an/aus, Motorstart

### 7.7 Logging-System

**Dateien:**

- `src/core/logging.cpp`
- `src/core/logging.h`

**Log-Level:**

```cpp
enum class LogLevel : uint8_t {
    None = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4
};
```

**Makros:**

- `LOG_ERROR(source, code, message)`
- `LOG_WARN(source, code, message)`
- `LOG_INFO(source, code, message)`
- `LOG_DEBUG(source, code, message)`

### 7.8 Main (FUNKTIONIERT!)

**Datei:** `src/main.cpp`

```cpp
void setup() {
    // 1. Serial + Boot-Banner
    // 2. Config laden (NVS)
    // 3. State initialisieren
    // 4. LEDs initialisieren
    // 5. WiFi starten
    // 6. WebServer starten
    // 7. BLE initialisieren
    // 8. Display initialisieren (S3 oder ST7789)
}

void loop() {
    // S3: display_s3_loop()
    // webUiLoop()
    // wifiLoop()
    // bleObdLoop()
    // ledBarLoop()
    // logoAnimLoop()
}
```

**WICHTIG:** Alle Loop-Funktionen sind non-blocking!

---

## 8. Projektstruktur

```
.
├─ platformio.ini              # Build-Konfiguration
├─ AGENTS.md                   # Diese Datei - DOKUMENTATION!
├─ boards/
│  └─ esp32s3.json             # Board-Definition
├─ include/
│  └─ lv_conf.h                # LVGL Konfiguration (280x456, 16bit, SWAP=1)
├─ src/
│  ├─ main.cpp                 # Setup/Loop Entry Point
│  ├─ bluetooth/
│  │  ├─ ble_obd.cpp           # BLE OBD-II Kommunikation (NimBLE/Core-BLE)
│  │  └─ ble_obd.h
│  ├─ core/
│  │  ├─ config.cpp/.h         # NVS Konfiguration
│  │  ├─ logging.cpp/.h        # Zentrales Logging (Error/Warn/Info/Debug)
│  │  ├─ state.cpp/.h          # Globaler State
│  │  ├─ utils.cpp/.h          # Hilfsfunktionen
│  │  ├─ vehicle_info.cpp/.h   # Fahrzeugdaten
│  │  └─ wifi.cpp/.h           # WiFi AP/STA (ESP-IDF Level Config!)
│  ├─ hardware/
│  │  ├─ display_s3.cpp/.h     # S3 AMOLED + FT3168 Touch (HAUPTDATEI!)
│  │  ├─ display.cpp/.h        # ST7789 Fallback (nicht S3)
│  │  ├─ esp_lcd_sh8601.cpp/.h # Optionaler SH8601 Treiber
│  │  ├─ led_bar.cpp/.h        # 8x NeoPixel LED-Bar
│  │  └─ logo_anim.cpp/.h      # Logo/Test Animation
│  ├─ ui/
│  │  ├─ ui_s3_main.cpp/.h     # LVGL UI für S3 Display
│  ├─ web/
│  │  ├─ web_ui.cpp/.h         # AsyncWebServer
│  │  └─ web_helpers.cpp/.h    # Web Hilfsfunktionen
├─ lib/                        # Externe Libraries (via lib_deps)
└─ test/
   ├─ test_main.cpp
   └─ unit_core/
      ├─ test_clamp_int.cpp
      └─ test_state_retry.cpp
```

---

## 9. Konkrete Aufgaben für den Codex-Agenten

**⚠️ ACHTUNG: Das Projekt funktioniert bereits! Sei SEHR vorsichtig mit Änderungen!**

Der Agent soll nur dann ändern, wenn:

1. Ein echter Bug vorliegt
2. Ein neues Feature explizit angefragt wird
3. Refactoring ohne Funktionsverlust möglich ist

**PFLICHT vor jeder Änderung:**

1. `pio run -e esp32s3` ausführen, um aktuellen Build-Status zu prüfen
2. Verstehen WAS funktioniert und WARUM
3. Diese AGENTS.md durchlesen!

**Bei Build-Fehlern:**

1. Build ausführen (`pio run -e esp32s3`).
2. Alle Fehler im Output analysieren.
3. Code/Config anpassen, um die Fehler zu beheben.
4. Erneut `pio run -e esp32s3` ausführen.
5. Diesen Zyklus wiederholen, bis ein fehlerfreier Build erreicht ist.

**NICHT anfassen ohne guten Grund:**

- Touch-Controller I2C Konfiguration (`trans_queue_depth = 0`, `timeout = -1`)
- WiFi ESP-IDF Level Config (Channel 6, AP-Config)
- BLE NimBLE/Core-BLE Abstraktionsschicht
- LVGL Buffer Allokation (DMA + Fallback)
- Pinbelegung für Display und Touch

---

## 10. Regeln für Änderungen

### 10.1 Der Agent MUSS

- Den ST7789‑Pfad (`src/hardware/display.cpp`) funktionsfähig lassen.
- Den S3‑Pfad (`src/hardware/display_s3.cpp`) ST7789‑frei halten (AMOLED only).
- QSPI‑Pfad laut Pinout beibehalten.
- LVGL‑Auflösung 280×456 / 16 Bit / **LV_COLOR_16_SWAP=1** einhalten.
- Nur den neuen I2C-Treiber (`driver/i2c_master.h`) für den FT3168 nutzen.
- **KRITISCH:** `trans_queue_depth = 0` und `timeout = -1` für Touch I2C beibehalten!
- `pio run -e esp32s3` regelmäßig ausführen.
- Non‑blocking‑Design respektieren (keine langen Delays in zentralen Loops).
- NVS/Preferences weiterhin nutzen (Konfigurationssystem nicht umgehen).
- BLE gegen die BLE‑Implementierung des Arduino‑Cores 3.x kompilieren.

### 10.2 Der Agent DARF

- Code umfassend refaktorisieren (auch mehrere Dateien).
- Neue Helper-Funktionen/Typen/Namespaces einführen.
- Logging vereinheitlichen und verbessern.
- Neue Tests hinzufügen.

### 10.3 Der Agent DARF NICHT

- `display.cpp` bzw. den ST7789‑Pfad entfernen oder brechen.
- Das Pinout eigenmächtig ändern.
- `ESP32 BLE Arduino` zu lib_deps hinzufügen!
- Touch I2C Konfiguration ändern (`trans_queue_depth`, `timeout`).
- WiFi ESP-IDF Konfiguration ändern (Channel, Auth-Mode).
- Tests löschen oder einfach „grün patchen".
- Funktionen stubben, so dass der Build ok ist aber Features deaktiviert sind.
- `LV_COLOR_16_SWAP` ändern (muss 1 bleiben!).

---

## 11. Rückfragenpflicht

Der Agent soll (im Kommentar/Report) Rückfragen signalisieren, wenn:

- Hardwareangaben im Code/Boardfile widersprüchlich sind.
- Crashlogs/Backtraces erwähnt werden, aber der konkrete Log fehlt.
- ein fundamentaler Wechsel der BLE‑Library oder Plattform nötig wäre.
- Build‑Fehler nur durch potenziell riskante, nicht-triviale Änderungen lösbar erscheinen.
