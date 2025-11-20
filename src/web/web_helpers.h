#pragma once

#include <Arduino.h>

#include "core/config.h"

String colorToHex(const RgbColor &color);
RgbColor parseHexColor(const String &value, const RgbColor &fallback);
String jsonEscape(const String &input);
String htmlEscape(const String &input);
String safeLabel(const String &value, const String &fallback);
int clampInt(int v, int lo, int hi);
void enforceOrder(int &g, int &y, int &b);
