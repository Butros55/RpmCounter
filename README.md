# 🚀 RpmCounter / ShiftLight – ESP32‑S3 Firmware

Ein hochmodernes, vollmodulares **Shiftlight‑ und RPM‑Anzeige‑System** für Fahrzeuge – entwickelt für den **ESP32‑S3 (16 MB Flash & LVGL‑Display)**.

Perfekt geeignet für OBD‑II‑Live-Daten, LED‑RPM‑Indikatoren (F1‑Style), Touch‑Display‑UI und Web‑Konfiguration über WLAN.

---

# 🌟 Features

## 🔧 **Hardware‑Features**

- ESP32‑S3 mit 16 MB Flash
- LVGL Display (ST7789, Touch)
- RGB‑LED‑Bar (NeoPixel)
- Bluetooth BLE (OBD‑II Dongle)
- WLAN AP/STA für Web‑Konfiguration
- Persistente Einstellungen via NVS

## 📡 **Live‑Daten & OBD‑II**

- RPM (Engine Speed)
- Vehicle Speed
- Throttle Position
- Gangauswertung über Formel (Wheel Speed / RPM)
- Fehlerrobuste BLE‑Reconnect‑Logik

## 🎨 **Display (LVGL)**

- Custom UI (Speed, RPM, Status)
- Touch-Unterstützung
- Performante Rendering-Pipeline
- Non-Blocking Update‑Loop

## 💡 **LED‑Shiftlight**

- F1‑Modus: Blau→Grün→Gelb→Rot + Blinken
- Manuelle Farbkarten
- Start‑Animation
- Dynamische Preview in WebUI

## 🌐 **WebUI über WLAN**

- ESP32 startet eigenen Access Point **ShiftLight**
- Live‑Status
- WLAN‑Scan & Connect
- LED‑Einstellungen
- OBD‑Einstellungen
- Fahrzeugprofile (geplant)

---

# 🧩 Architektur

Die Firmware folgt einem klaren, modularen Aufbau:

```
src/
├── bluetooth/        # BLE OBD-II Kommunikation
├── core/             # State-Machine, Config, Utils, Vehicle Logic
├── hardware/         # Display, LEDs, Logos
├── ui/               # LVGL View Layer
├── web/              # Webserver, HTML Generator, Routes
└── main.cpp          # Entry Point
```

Alles ist **non-blocking** aufgebaut, damit BLE, WLAN, Display und LED‑Bar parallel laufen.

---

# ⚙️ PlatformIO Konfiguration

Die Firmware verwendet **PlatformIO + Arduino Framework**.

Wesentliche Bibliotheken:

- ESP32 BLE Arduino
- Adafruit NeoPixel
- Adafruit ST7789
- LVGL 8.4
- Preferences (NVS)
- WebServer (esp32)

---

# 🔄 Entwicklungs‑Workflow

## 🧱 Build (unter WSL)

```bash
cd /home/RpmCounter
pio run -e esp32s3
```

## 🔥 Flash (unter Windows PowerShell)

```powershell
pwsh -Command "cd 'C:\dev\RpmCounter'; pio run -e esp32s3 -t upload"
```

## 🖥️ Serieller Monitor

```powershell
pwsh -Command "cd 'C:\dev\RpmCounter'; pio device monitor"
```

---

# 🧪 Tests

Unit‑Tests laufen unter WSL:

```bash
pio test -e native
```

Für Board‑nahe Tests:

```bash
pio test -e esp32s3
```

---

# 📷 Screenshots (optional)

_(Platzhalter – kann später ergänzt werden)_

---

# 🛣️ Roadmap

- 🔜 Performance-Optimierung LVGL
- 🔜 Neues UI‑Layout (Dark Mode)
- 🔜 OBD‑Datencaching für schnellere Anzeige
- 🔜 Fahrzeugprofile
- 🔜 Logging-System über WLAN
- 🔜 Over-the-Air Updates (OTA)

---

# 🤝 Mitwirken

Pull Requests sind willkommen!
Bitte beachte die Struktur unter `src/` und halte dich an non-blocking Patterns.

---

# 📄 Lizenz

MIT License – frei nutzbar für alle Projekte.

---

# ❤️ Credits

Dieses Projekt wird von **Geret** entwickelt – mit Fokus auf Technik, Qualität und realistische OBD‑Datenanzeige.

Die Dokumentation & Tools werden von **Codex** begleitet, optimiert & automatisiert.

---

👋 Viel Spaß beim Entwickeln – und volle Drehzahl voraus! 🚗💨
