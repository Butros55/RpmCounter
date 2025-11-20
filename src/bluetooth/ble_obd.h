#ifndef BLE_OBD_H
#define BLE_OBD_H

#include <Arduino.h>

#include "core/state.h"

bool connectToObd();
bool connectToObd(const String &address, const String &name = "");
void sendObdCommand(const String &cmd);
void initBle();
void bleObdLoop();
bool startBleScan(uint32_t durationSeconds = 4);
bool isBleScanRunning();
unsigned long lastBleScanFinished();
const std::vector<BleDeviceInfo> &getBleScanResults();
void requestManualConnect(const String &address, const String &name, int attempts = MANUAL_CONNECT_RETRY_COUNT);

#endif // BLE_OBD_H
