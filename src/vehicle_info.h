#ifndef VEHICLE_INFO_H
#define VEHICLE_INFO_H

#pragma once

#include <Arduino.h>

// Zugriff für WebUI / Settings
String readVehicleVin();
String readVehicleModel();
String readVehicleDiagStatus();

// Startet einen neuen Abruf der Fahrzeugdaten
// forceRestart = true erzwingt einen Neustart, auch wenn schon ein Request läuft
void requestVehicleInfo(bool forceRestart = false);

// Wird von processObdLine() in ble_obd.cpp aufgerufen
void handleVehicleInfoResponse(const String &compactLine);

// Wird bei BLE-Disconnect aufgerufen (ble_obd.cpp)
void handleVehicleDisconnect();

#endif // VEHICLE_INFO_H
