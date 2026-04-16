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

inline bool isShortGapDip(int previousValue,
                          int nextValue,
                          unsigned long dtMs,
                          int deltaLimit,
                          unsigned long shortGapLimitMs)
{
    if (dtMs == 0 || dtMs > shortGapLimitMs)
    {
        return false;
    }
    return previousValue > nextValue && (previousValue - nextValue) >= deltaLimit;
}

inline bool cooldownElapsed(unsigned long nowMs, unsigned long lastEventMs, unsigned long cooldownMs)
{
    return lastEventMs == 0 || nowMs < lastEventMs || (nowMs - lastEventMs) >= cooldownMs;
}

inline int applyDisplayLevelHysteresis(int currentLevel,
                                       float rawLevel,
                                       int maxLevel,
                                       float stepUpThreshold = 0.55f,
                                       float stepDownThreshold = 0.25f)
{
    if (maxLevel <= 0)
    {
        return 0;
    }

    if (currentLevel < 0)
    {
        currentLevel = 0;
    }
    if (currentLevel > maxLevel)
    {
        currentLevel = maxLevel;
    }

    if (rawLevel < 0.0f)
    {
        rawLevel = 0.0f;
    }
    const float maxLevelFloat = static_cast<float>(maxLevel);
    if (rawLevel > maxLevelFloat)
    {
        rawLevel = maxLevelFloat;
    }

    while (currentLevel < maxLevel && rawLevel >= (static_cast<float>(currentLevel) + stepUpThreshold))
    {
        ++currentLevel;
    }

    while (currentLevel > 0 && rawLevel <= (static_cast<float>(currentLevel - 1) + stepDownThreshold))
    {
        --currentLevel;
    }

    return currentLevel;
}

inline int displayLevelTailIndex(int stableLevel, float rawLevel, int maxLevel)
{
    if (maxLevel <= 0)
    {
        return -1;
    }

    if (stableLevel < 0)
    {
        stableLevel = 0;
    }
    if (stableLevel > maxLevel)
    {
        stableLevel = maxLevel;
    }

    if (rawLevel < 0.0f)
    {
        rawLevel = 0.0f;
    }
    const float maxLevelFloat = static_cast<float>(maxLevel);
    if (rawLevel > maxLevelFloat)
    {
        rawLevel = maxLevelFloat;
    }

    if (stableLevel <= 0)
    {
        return rawLevel > 0.0f ? 0 : -1;
    }

    if (rawLevel >= static_cast<float>(stableLevel))
    {
        return (stableLevel < maxLevel) ? stableLevel : -1;
    }

    return stableLevel - 1;
}

inline float displayLevelTailIntensity(int stableLevel, float rawLevel, int maxLevel)
{
    if (maxLevel <= 0)
    {
        return 0.0f;
    }

    if (stableLevel < 0)
    {
        stableLevel = 0;
    }
    if (stableLevel > maxLevel)
    {
        stableLevel = maxLevel;
    }

    if (rawLevel < 0.0f)
    {
        rawLevel = 0.0f;
    }
    const float maxLevelFloat = static_cast<float>(maxLevel);
    if (rawLevel > maxLevelFloat)
    {
        rawLevel = maxLevelFloat;
    }

    float intensity = 0.0f;
    if (stableLevel <= 0)
    {
        intensity = rawLevel;
    }
    else if (rawLevel >= static_cast<float>(stableLevel))
    {
        intensity = rawLevel - static_cast<float>(stableLevel);
    }
    else
    {
        intensity = rawLevel - static_cast<float>(stableLevel - 1);
    }

    if (intensity < 0.0f)
    {
        return 0.0f;
    }
    if (intensity > 1.0f)
    {
        return 1.0f;
    }
    return intensity;
}
