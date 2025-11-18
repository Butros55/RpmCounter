#include "utils.h"

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
