#include "ambient_light_algo.h"

#include <math.h>

namespace
{
    template <typename T>
    T clampValue(T value, T minValue, T maxValue)
    {
        if (value < minValue)
        {
            return minValue;
        }
        if (value > maxValue)
        {
            return maxValue;
        }
        return value;
    }
}

float ambientNormalizeLux(float lux, float luxMin, float luxMax)
{
    const float safeLuxMin = (luxMin < 0.0f) ? 0.0f : luxMin;
    const float safeLuxMax = (luxMax <= safeLuxMin) ? (safeLuxMin + 1.0f) : luxMax;
    const float clampedLux = clampValue(lux, safeLuxMin, safeLuxMax);
    const float logMin = log10f(safeLuxMin + 1.0f);
    const float logMax = log10f(safeLuxMax + 1.0f);
    const float logValue = log10f(clampedLux + 1.0f);
    const float denom = logMax - logMin;
    if (denom <= 0.0f)
    {
        return 0.0f;
    }

    return clampValue((logValue - logMin) / denom, 0.0f, 1.0f);
}

float ambientComputeResponseAlpha(int responsePct)
{
    const float normalized = clampValue(static_cast<float>(responsePct), 1.0f, 100.0f) / 100.0f;
    return 0.03f + normalized * 0.27f;
}

float ambientApplySmoothing(float current, float target, float alpha)
{
    const float clampedAlpha = clampValue(alpha, 0.0f, 1.0f);
    return current + ((target - current) * clampedAlpha);
}

int ambientComputeTargetBrightness(float lux, const AutoBrightnessCurveConfig &config)
{
    const int manualMax = clampValue(config.manualMax, 0, 255);
    const int minBrightness = clampValue(config.minBrightness, 0, manualMax);
    const float normalizedLux =
        ambientNormalizeLux(lux, static_cast<float>(config.luxMin), static_cast<float>(config.luxMax));
    const float strength = clampValue(static_cast<float>(config.strengthPct), 25.0f, 200.0f) / 100.0f;
    const float adjustedLux = clampValue(normalizedLux * strength, 0.0f, 1.0f);
    const float target =
        static_cast<float>(minBrightness) + adjustedLux * static_cast<float>(manualMax - minBrightness);
    const int rounded = static_cast<int>(lroundf(target));
    return clampValue(rounded, 0, manualMax);
}
