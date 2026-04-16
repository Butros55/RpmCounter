#pragma once

struct AutoBrightnessCurveConfig
{
    int manualMax = 0;
    int minBrightness = 0;
    int strengthPct = 100;
    int luxMin = 1;
    int luxMax = 1000;
};

float ambientNormalizeLux(float lux, float luxMin, float luxMax);
float ambientComputeResponseAlpha(int responsePct);
float ambientApplySmoothing(float current, float target, float alpha);
float ambientComputeTargetBrightnessFloat(float lux, const AutoBrightnessCurveConfig &config);
int ambientComputeTargetBrightness(float lux, const AutoBrightnessCurveConfig &config);
