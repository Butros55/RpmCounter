#include "vehicle_info.h"

#include "ble_obd.h"
#include "state.h"
#include "utils.h"

namespace
{
    String vinHexBuffer;
    String modelHexBuffer;
    bool vinComplete = false;
    bool modelComplete = false;
    bool diagComplete = false;

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

void requestVehicleInfo()
{
    // Nur starten, wenn wir verbunden sind und nicht schon ein Lauf aktiv ist
    if (!g_connected || g_vehicleInfoRequestRunning)
        return;

    vinHexBuffer = "";
    modelHexBuffer = "";
    vinComplete = false;
    modelComplete = false;
    diagComplete = false;

    g_vehicleInfoRequestRunning = true;
    g_vehicleInfoAvailable = false;

    // Initiale Platzhaltertexte
    g_vehicleVin = F("VIN wird gelesen...");
    g_vehicleModel = F("Fahrzeugdaten werden geladen...");
    g_vehicleDiagStatus = F("Diagnose wird gelesen...");
    g_vehicleInfoLastUpdate = millis();

    // Mode 09 – VIN & ECU Name (falls das Steuergerät es unterstützt)
    sendObdCommand("0902");
    delay(30);
    sendObdCommand("0904");
    delay(30);
    // Mode 01 – Diagnose-Status
    sendObdCommand("0101");
}

void handleVehicleInfoResponse(const String &compactLine)
{
    if (!g_vehicleInfoRequestRunning)
        return;

    // VIN (Mode 09 PID 02) – Antwort 49 02 ...
    if (compactLine.startsWith("4902"))
    {
        String payload = compactLine.substring(6);
        vinHexBuffer += payload;

        // Frame-Index im Byte 4 (heuristisch, kann je nach ECU anders sein)
        int frameIndex = hexByte(compactLine, 4);

        // Wenn genug Frames da sind -> VIN dekodieren
        if (frameIndex >= 3)
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

        int frameIndex = hexByte(compactLine, 4);

        if (frameIndex >= 2)
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

            // mit / ohne ⚠️ Emoji
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

        // Falls VIN/Modell vom Steuergerät NICHT kommen:
        // -> beim ersten erfolgreichen Diagnose-Frame auf sinnvolle Defaults setzen,
        // damit der Status nicht ewig auf "Abruf läuft..." hängt.
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

void handleVehicleDisconnect()
{
    g_vehicleInfoRequestRunning = false;
    vinHexBuffer = "";
    modelHexBuffer = "";
    vinComplete = false;
    modelComplete = false;
    diagComplete = false;

    // Optional: Texte zurücksetzen
    g_vehicleVin = "";
    g_vehicleModel = "";
    g_vehicleDiagStatus = "";
}
