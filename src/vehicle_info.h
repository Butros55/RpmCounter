#ifndef VEHICLE_INFO_H
#define VEHICLE_INFO_H

#include <Arduino.h>

String readVehicleVin();
String readVehicleModel();
void requestVehicleInfo();

#endif // VEHICLE_INFO_H
