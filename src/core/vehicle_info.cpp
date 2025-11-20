#include "vehicle_info.h"

#include "bluetooth/ble_obd.h"
#include "state.h"
#include "utils.h"

namespace
{
    String vinHexBuffer;
    String modelHexBuffer;
    bool vinComplete = false;
    bool modelComplete = false;
    bool diagComplete = false;

    // optionaler Zeitstempel, falls du später noch Timeouts nutzen willst
    unsigned long requestStartMs = 0;

    String hexToAscii(const String &hex)
    {
        String out;
        for (int i = 0; i + 1 < hex.length(); i += 2)
        {
            int value = (int)strtol(hex.substring(i, i + 2).c_str(), nullptr, 16);
            if (value == 0)
                continue;
            if (value < 32 || value > 126)
                continue;
            out += (char)value;
        }
        out.trim();
        return out;
    }

    void markCompleteIfReady()
    {
        if (vinComplete && modelComplete && diagComplete)
        {
            g_vehicleInfoRequestRunning = false;
            g_vehicleInfoAvailable = true;
            g_vehicleInfoLastUpdate = millis();
        }
    }
}

// -----------------------------------------------------------------------------
// Getter für WebUI / Settings
// -----------------------------------------------------------------------------

String readVehicleVin()
{
    return g_vehicleVin;
}

String readVehicleModel()
{
    return g_vehicleModel;
}

String readVehicleDiagStatus()
{
    return g_vehicleDiagStatus;
}

// -----------------------------------------------------------------------------
// Abruf starten (bei Connect oder über Settings-Button)
// -----------------------------------------------------------------------------

void requestVehicleInfo(bool forceRestart)
{
    if (!g_connected)
        return;

    if (g_vehicleInfoRequestRunning && !forceRestart)
        return;

    vinHexBuffer = "";
    modelHexBuffer = "";
    vinComplete = false;
    modelComplete = false;
    diagComplete = false;

    g_vehicleInfoRequestRunning = true;
    g_vehicleInfoAvailable = false;

    requestStartMs = millis();

    // Platzhaltertexte, damit UI sofort etwas hat
    g_vehicleVin = F("VIN wird gelesen...");
    g_vehicleModel = F("Fahrzeugdaten werden geladen...");
    g_vehicleDiagStatus = F("Diagnose wird gelesen...");
    g_vehicleInfoLastUpdate = millis();

    // Mode 09 – VIN & ECU Name (falls Steuergerät das unterstützt)
    const int RETRIES = 3;
    for (int i = 0; i < RETRIES; ++i)
    {
        sendObdCommand("0902"); // VIN
        delay(30);
    }

    for (int i = 0; i < RETRIES; ++i)
    {
        sendObdCommand("0904"); // ECU / Modellname
        delay(30);
    }

    // Mode 01 – Diagnose-Status
    for (int i = 0; i < RETRIES; ++i)
    {
        sendObdCommand("0101");
        delay(30);
    }
}

// -----------------------------------------------------------------------------
// Wird aus processObdLine() aufgerufen
// compactLine = ohne Leerzeichen, Großbuchstaben (machst du bereits so)
// -----------------------------------------------------------------------------

void handleVehicleInfoResponse(const String &compactLine)
{
    if (!g_vehicleInfoRequestRunning)
        return;

    // VIN (Mode 09 PID 02) – Antwort 49 02 ...
    if (compactLine.startsWith("4902"))
    {
        // alles nach "4902xx" ab Byte 6 ist Nutzlast
        String payload = compactLine.substring(6);
        vinHexBuffer += payload;

        // VIN hat 17 Zeichen → 17 * 2 = 34 Hexzeichen
        if (!vinComplete && vinHexBuffer.length() >= 34)
        {
            String vin = hexToAscii(vinHexBuffer);
            if (vin.length() > 0)
            {
                g_vehicleVin = vin;
            }
            else
            {
                g_vehicleVin = F("VIN nicht verfügbar");
            }
            vinComplete = true;
            g_vehicleInfoLastUpdate = millis();
            markCompleteIfReady();
        }
    }
    // ECU-Beschreibung / Modell (Mode 09 PID 04) – Antwort 49 04 ...
    else if (compactLine.startsWith("4904"))
    {
        String payload = compactLine.substring(6);
        modelHexBuffer += payload;

        // Heuristik: nach zweitem Frame ist meistens alles da,
        // zusätzlich auf ausreichend Länge prüfen
        static int modelFrameCount = 0;
        modelFrameCount++;

        if (!modelComplete && (modelFrameCount >= 2 || modelHexBuffer.length() >= 32))
        {
            String model = hexToAscii(modelHexBuffer);
            if (model.length() > 0)
            {
                g_vehicleModel = model;
            }
            else
            {
                g_vehicleModel = F("Modell unbekannt");
            }
            modelComplete = true;
            g_vehicleInfoLastUpdate = millis();
            markCompleteIfReady();
        }
    }
    // Diagnose-Status (Mode 01 PID 01) – Antwort 41 01 ...
    else if (compactLine.startsWith("4101"))
    {
        int statusByte = hexByte(compactLine, 4);
        if (statusByte >= 0)
        {
            bool milOn = (statusByte & 0x80) != 0;
            int dtcCount = statusByte & 0x7F;

            if (milOn)
            {
                g_vehicleDiagStatus = String("⚠️ MIL an, DTCs: ") + String(dtcCount);
            }
            else
            {
                g_vehicleDiagStatus = String("Keine Warnlampe (DTCs: ") + String(dtcCount) + String(")");
            }
        }
        else
        {
            g_vehicleDiagStatus = F("Diagnose nicht lesbar");
        }

        diagComplete = true;
        g_vehicleInfoLastUpdate = millis();

        // Fallback: falls das Steuergerät keine 09-Frames liefert,
        // verhindern wir "hängt auf 'wird gelesen...'"
        if (!vinComplete)
        {
            if (g_vehicleVin == F("VIN wird gelesen..."))
            {
                g_vehicleVin = F("VIN nicht verfügbar");
            }
            vinComplete = true;
        }

        if (!modelComplete)
        {
            if (g_vehicleModel == F("Fahrzeugdaten werden geladen..."))
            {
                g_vehicleModel = F("Modell unbekannt");
            }
            modelComplete = true;
        }

        markCompleteIfReady();
    }
}

// -----------------------------------------------------------------------------
// Reset bei BLE-Disconnect
// -----------------------------------------------------------------------------

void handleVehicleDisconnect()
{
    g_vehicleInfoRequestRunning = false;
    g_vehicleInfoAvailable = false;
    g_vehicleInfoLastUpdate = 0;

    vinHexBuffer = "";
    modelHexBuffer = "";

    vinComplete = false;
    modelComplete = false;
    diagComplete = false;
    requestStartMs = 0;

    // Texte zurücksetzen – wird in Settings & /status direkt angezeigt
    g_vehicleVin = F("Noch nicht gelesen");
    g_vehicleModel = F("Unbekannt");
    g_vehicleDiagStatus = F("Keine Daten");
}
