#ifndef BLE_OBD_H
#define BLE_OBD_H

#include <Arduino.h>

bool connectToObd();
void sendObdCommand(const String &cmd);
void initBle();
void bleObdLoop();
void scheduleConnectLoop(int attempts, bool manual);
void cancelConnectLoop();

#endif // BLE_OBD_H
