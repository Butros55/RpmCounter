#ifndef VEHICLE_INFO_H
#define VEHICLE_INFO_H

#include <Arduino.h>

String readVehicleVin();
String readVehicleModel();
String readVehicleDiagStatus();
void requestVehicleInfo();
void handleVehicleInfoResponse(const String &compactLine);
void handleVehicleDisconnect();

#endif // VEHICLE_INFO_H
