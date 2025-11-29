# Spark Export Contract für ShiftLight Web-UI

Dieses Dokument definiert den Vertrag zwischen Spark (Frontend-Generator) und der ESP32 ShiftLight Firmware.

## 1. Ordnerstruktur

Die von Spark exportierten Dateien MÜSSEN in folgender Struktur abgelegt werden:

```
webserver/
├── index.html           # Haupt-Dashboard
├── settings.html        # Einstellungsseite
└── assets/
    ├── style.css        # Gemeinsame Styles
    ├── main.js          # JavaScript für index.html
    └── settings.js      # JavaScript für settings.html
```

**WICHTIG:**

- Der Ordner `webserver/` ist READ-ONLY aus Sicht der Firmware-Entwicklung
- Änderungen erfolgen NUR durch Spark-Export
- Keine weiteren Unterordner außer `assets/`

## 2. URL/Pfad-Mapping (LittleFS → HTTP)

| LittleFS-Pfad         | HTTP-URL                         | Handler                |
| --------------------- | -------------------------------- | ---------------------- |
| `/index.html`         | `http://<ip>/`                   | `handleRoot()`         |
| `/index.html`         | `http://<ip>/index.html`         | `handleRoot()`         |
| `/settings.html`      | `http://<ip>/settings`           | `handleSettingsGet()`  |
| `/settings.html`      | `http://<ip>/settings.html`      | Redirect → `/settings` |
| `/assets/style.css`   | `http://<ip>/assets/style.css`   | `serveStatic()`        |
| `/assets/main.js`     | `http://<ip>/assets/main.js`     | `serveStatic()`        |
| `/assets/settings.js` | `http://<ip>/assets/settings.js` | `serveStatic()`        |

## 3. API-Endpunkte (Backend)

Die folgenden Endpunkte werden vom ESP32 bereitgestellt und sind vom Frontend zu nutzen:

### 3.1 Konfiguration & Status

| Methode | Endpunkt      | Beschreibung                   | Request-Body | Response           |
| ------- | ------------- | ------------------------------ | ------------ | ------------------ |
| GET     | `/api/config` | Initiale Konfiguration laden   | -            | JSON (siehe unten) |
| GET     | `/status`     | Live-Status (RPM, Speed, etc.) | -            | JSON               |
| POST    | `/save`       | LED-Konfiguration speichern    | JSON         | `"OK"` / Error     |

### 3.2 LED/Display-Steuerung

| Methode | Endpunkt      | Beschreibung                | Request-Body            | Response |
| ------- | ------------- | --------------------------- | ----------------------- | -------- |
| POST    | `/test`       | LED-Test-Animation auslösen | -                       | `"OK"`   |
| POST    | `/brightness` | Helligkeit setzen           | `{"brightness": 0-100}` | `"OK"`   |

### 3.3 OBD/BLE-Verbindung

| Methode | Endpunkt       | Beschreibung             | Request-Body | Response   |
| ------- | -------------- | ------------------------ | ------------ | ---------- |
| POST    | `/connect`     | Mit OBD-Dongle verbinden | -            | JSON       |
| POST    | `/disconnect`  | OBD-Verbindung trennen   | -            | `"OK"`     |
| GET     | `/ble/status`  | BLE-Verbindungsstatus    | -            | JSON       |
| GET     | `/ble/devices` | Verfügbare BLE-Geräte    | -            | JSON Array |

### 3.4 WiFi-Konfiguration

| Methode | Endpunkt         | Beschreibung                 | Request-Body | Response       |
| ------- | ---------------- | ---------------------------- | ------------ | -------------- |
| GET     | `/wifi/status`   | WiFi-Status                  | -            | JSON           |
| GET     | `/wifi/networks` | Verfügbare Netzwerke scannen | -            | JSON Array     |
| POST    | `/wifi/connect`  | Mit Netzwerk verbinden       | JSON         | `"OK"` / Error |

### 3.5 Einstellungen

| Methode | Endpunkt    | Beschreibung            | Request-Body | Response       |
| ------- | ----------- | ----------------------- | ------------ | -------------- |
| GET     | `/settings` | Settings-Seite (HTML)   | -            | HTML           |
| POST    | `/settings` | Einstellungen speichern | JSON         | `"OK"` / Error |

### 3.6 Entwickler/Debug

| Methode | Endpunkt         | Beschreibung                | Request-Body     | Response  |
| ------- | ---------------- | --------------------------- | ---------------- | --------- |
| POST    | `/dev/set-rpm`   | RPM manuell setzen (Test)   | `{"rpm": N}`     | `"OK"`    |
| POST    | `/dev/set-speed` | Speed manuell setzen (Test) | `{"speed": N}`   | `"OK"`    |
| POST    | `/dev/obd-send`  | OBD-Befehl direkt senden    | `{"cmd": "..."}` | Response  |
| GET     | `/dev/logs`      | Debug-Logs abrufen          | -                | Text/JSON |

## 4. JSON-Schemas

### 4.1 `/api/config` Response

```json
{
  "ledCount": 30,
  "ledPin": 5,
  "wifiSSID": "ShiftLight",
  "wifiPassword": "shiftlight123",
  "bleDeviceName": "OBDII",
  "rpmMin": 1000,
  "rpmMax": 7000,
  "shiftRpm": 6500,
  "brightness": 80,
  "colors": {
    "low": "#00FF00",
    "mid": "#FFFF00",
    "high": "#FF0000"
  }
}
```

### 4.2 `/status` Response

```json
{
  "rpm": 3500,
  "speed": 85,
  "gear": 4,
  "connected": true,
  "bleConnected": true,
  "wifiConnected": true,
  "wifiClients": 1
}
```

### 4.3 `/save` Request

```json
{
  "rpmMin": 1000,
  "rpmMax": 7000,
  "shiftRpm": 6500,
  "brightness": 80,
  "colors": {
    "low": "#00FF00",
    "mid": "#FFFF00",
    "high": "#FF0000"
  }
}
```

## 5. HTML-Anforderungen

### 5.1 index.html

```html
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>ShiftLight Dashboard</title>
    <link rel="stylesheet" href="/assets/style.css" />
  </head>
  <body>
    <!-- Dashboard Content -->
    <script src="/assets/main.js"></script>
  </body>
</html>
```

**Anforderungen:**

- MUSS `/assets/style.css` laden (relativer Pfad mit führendem `/`)
- MUSS `/assets/main.js` am Ende des `<body>` laden
- Responsive Design für mobile Geräte (min-width: 280px)

### 5.2 settings.html

```html
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>ShiftLight Settings</title>
    <link rel="stylesheet" href="/assets/style.css" />
  </head>
  <body>
    <!-- Settings Content -->
    <script src="/assets/settings.js"></script>
  </body>
</html>
```

**Anforderungen:**

- MUSS `/assets/style.css` laden
- MUSS `/assets/settings.js` am Ende des `<body>` laden
- Links zu `/` für "Zurück zum Dashboard"

## 6. JavaScript-Anforderungen

### 6.1 main.js

**Erforderliche Funktionalität:**

- Initiale Konfiguration über `GET /api/config` laden
- Status-Polling über `GET /status` (alle 500ms–1000ms)
- Event-Handler für UI-Interaktionen
- POST-Requests zu `/save`, `/test`, `/brightness`, `/connect`, `/disconnect`

**Beispiel-Pattern:**

```javascript
// Initiale Konfiguration laden
async function loadConfig() {
  const response = await fetch("/api/config");
  const config = await response.json();
  // UI initialisieren
}

// Status-Polling
function startStatusPolling() {
  setInterval(async () => {
    const response = await fetch("/status");
    const status = await response.json();
    updateUI(status);
  }, 500);
}

// Bei Seitenstart
document.addEventListener("DOMContentLoaded", () => {
  loadConfig().then(startStatusPolling);
});
```

### 6.2 settings.js

**Erforderliche Funktionalität:**

- Initiale Konfiguration über `GET /api/config` laden
- WiFi-Netzwerke über `GET /wifi/networks` scannen
- BLE-Geräte über `GET /ble/devices` auflisten
- Einstellungen über `POST /settings` speichern

## 7. CSS-Anforderungen

### 7.1 style.css

**Anforderungen:**

- Mobile-First Design
- Dark Theme (empfohlen für Automotive-Kontext)
- Keine externen Fonts (LittleFS-Größenbeschränkung)
- Keine externen Images (als Data-URI einbetten falls nötig)

**Empfohlene Farben:**

- Background: `#1a1a1a`
- Text: `#ffffff`
- Accent: `#ff5722` (Orange/Rot für ShiftLight)
- Success: `#4caf50`
- Warning: `#ff9800`
- Error: `#f44336`

## 8. Dateigrößen-Limits

| Datei         | Max. Größe  | Empfohlen    |
| ------------- | ----------- | ------------ |
| index.html    | 50 KB       | < 20 KB      |
| settings.html | 50 KB       | < 20 KB      |
| style.css     | 100 KB      | < 30 KB      |
| main.js       | 100 KB      | < 50 KB      |
| settings.js   | 100 KB      | < 50 KB      |
| **Gesamt**    | **~400 KB** | **< 150 KB** |

**Hinweis:** Die LittleFS-Partition ist begrenzt. Minimieren/Komprimieren der Dateien wird empfohlen.

## 9. Build & Deploy Workflow

### 9.1 Spark → Firmware

1. **Spark Export:** Dateien in `webserver/` exportieren
2. **Filesystem bauen:** `pio run -t buildfs -e esp32s3`
3. **Filesystem flashen:** `pio run -t uploadfs -e esp32s3`
4. **Firmware flashen:** `pio run -t upload -e esp32s3`

### 9.2 Vollständiger Flash (Beide)

```bash
# Erst Filesystem, dann Firmware
pio run -t uploadfs -e esp32s3
pio run -t upload -e esp32s3
```

## 10. Fehlerbehandlung

### 10.1 Filesystem nicht gefunden

Wenn LittleFS nicht gemountet werden kann, liefert der Server:

- HTTP 503 für alle statischen Dateien
- Fallback-HTML mit Fehlermeldung

### 10.2 Datei nicht gefunden

- HTTP 404 für nicht existierende Dateien
- Keine automatischen Redirects außer den definierten

### 10.3 API-Fehler

- HTTP 400 für ungültige Requests
- HTTP 500 für interne Fehler
- Fehler-Response immer als JSON: `{"error": "message"}`

## 11. Versionierung

Änderungen an diesem Contract MÜSSEN folgende Regeln befolgen:

1. **Neue Endpunkte:** Dürfen jederzeit hinzugefügt werden
2. **Bestehende Endpunkte:** Dürfen nicht ohne Deprecation-Phase entfernt werden
3. **JSON-Schemas:** Neue Felder sind erlaubt, bestehende Felder dürfen nicht geändert werden
4. **Breaking Changes:** Erfordern Inkrement der Major-Version

**Aktuelle Version:** 1.0.0
