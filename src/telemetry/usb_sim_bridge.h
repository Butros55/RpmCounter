#pragma once

#include <Arduino.h>

void initUsbSimBridge();
void usbSimBridgeLoop();
void usbSimBridgeUpdateConfig();

bool usbSimTransportEnabled();
bool usbSimBridgeOnline();
bool usbSimTelemetryFresh(unsigned long nowMs);
bool usbSimShouldBlockObd();
bool usbSimShouldSuspendWifi();

String usbSimBuildStatusJson();
String usbSimBuildConfigJson();
