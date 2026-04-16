#pragma once

#include <Arduino.h>

void initUsbSimBridge();
void usbSimBridgeLoop();          // kept for compatibility; now a no-op when the task is running
void usbSimBridgeUpdateConfig();
void startUsbSimBridgeTask();     // launches the dedicated reader task

bool usbSimTransportEnabled();
bool usbSimBridgeOnline();
bool usbSimTelemetryFresh(unsigned long nowMs);
bool usbSimShouldBlockObd();
bool usbSimShouldSuspendWifi();

String usbSimBuildStatusJson();
String usbSimBuildConfigJson();
