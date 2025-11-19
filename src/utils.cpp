#include "utils.h"

#include <stdio.h>

int hexByte(const String &s, int idx)
{
    if (idx + 2 > s.length())
        return -1;
    char buf[3];
    buf[0] = s[idx];
    buf[1] = s[idx + 1];
    buf[2] = '\0';
    return (int)strtol(buf, nullptr, 16);
}

int clampInt(int value, int minValue, int maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

String colorToHtmlHex(uint32_t color)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", (unsigned int)((color >> 16) & 0xFF), (unsigned int)((color >> 8) & 0xFF), (unsigned int)(color & 0xFF));
    return String(buf);
}

uint32_t parseHtmlColor(const String &value, uint32_t fallback)
{
    String hex = value;
    hex.trim();
    if (!hex.startsWith("#"))
        return fallback;

    String payload = hex.substring(1);
    if (payload.length() != 6)
        return fallback;

    long parsed = strtol(payload.c_str(), nullptr, 16);
    if (parsed < 0)
        parsed = 0;
    if (parsed > 0xFFFFFF)
        parsed = 0xFFFFFF;
    return (uint32_t)parsed;
}
