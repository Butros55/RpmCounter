#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

int hexByte(const String &s, int idx);
int clampInt(int value, int minValue, int maxValue);
String colorToHtmlHex(uint32_t color);
uint32_t parseHtmlColor(const String &value, uint32_t fallback);

#endif // UTILS_H
