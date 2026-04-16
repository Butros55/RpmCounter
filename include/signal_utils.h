#pragma once

#include <stdlib.h>

inline int median3Int(int a, int b, int c)
{
    if ((a <= b && b <= c) || (c <= b && b <= a))
    {
        return b;
    }
    if ((b <= a && a <= c) || (c <= a && a <= b))
    {
        return a;
    }
    return c;
}

inline bool isShortGapSpike(int previousValue,
                            int nextValue,
                            unsigned long dtMs,
                            int deltaLimit,
                            unsigned long shortGapLimitMs)
{
    if (dtMs == 0 || dtMs > shortGapLimitMs)
    {
        return false;
    }
    return abs(nextValue - previousValue) >= deltaLimit;
}
