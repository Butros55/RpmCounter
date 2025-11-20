#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

int hexByte(const String &s, int idx);

#pragma once
int clampInt(int value, int minValue, int maxValue);

#endif // UTILS_H
