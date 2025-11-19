#include "vehicle_info.h"

#include <Arduino.h>

#include "state.h"

String readVehicleVin()
{
    return g_vehicleVin;
}

String readVehicleModel()
{
    return g_vehicleModel;
}

String readVehicleBrand()
{
    return g_vehicleBrand;
}

bool readVehicleDiagOk()
{
    return g_vehicleDiagOk;
}

void requestVehicleInfo()
{
    if (g_vehicleInfoLoaded)
    {
        return;
    }

    Serial.println("[VEH] Lese Fahrzeugdaten vom OBD-Gerät...");

    // In einer echten Implementierung würden hier OBD-Befehle abgesetzt.
    // Für die Firmware speichern wir exemplarische Daten, sobald eine Verbindung steht.
    g_vehicleVin = "WBS0M9C57FG123456";
    g_vehicleModel = "M4 Competition (S58)";
    g_vehicleBrand = "BMW M GmbH";
    g_vehicleDiagOk = true;
    g_vehicleInfoLoaded = true;

    Serial.println("[VEH] Fahrzeugdaten initialisiert.");
}
