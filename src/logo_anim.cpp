#include "logo_anim.h"

#include <Arduino.h>

#include "config.h"
#include "led_bar.h"
#include "state.h"

namespace
{
    void fadeBarToBlack(unsigned long durationMs)
    {
        const int steps = 30;
        if (durationMs < steps)
            durationMs = steps;
        unsigned long stepDelay = durationMs / steps;
        if (stepDelay == 0)
            stepDelay = 1;

        for (int s = steps; s >= 0; s--)
        {
            float t = (float)s / (float)steps;
            for (int i = 0; i < NUM_LEDS; i++)
            {
                uint32_t c = strip.getPixelColor(i);
                uint8_t r = (c >> 16) & 0xFF;
                uint8_t g = (c >> 8) & 0xFF;
                uint8_t b = c & 0xFF;
                uint8_t rr = (uint8_t)(r * t);
                uint8_t gg = (uint8_t)(g * t);
                uint8_t bb = (uint8_t)(b * t);
                strip.setPixelColor(i, strip.Color(rr, gg, bb));
            }
            strip.show();
            delay(stepDelay);
        }

        strip.clear();
        strip.show();
    }
}

void showMLogoPreview(float fadeFactor)
{
    const uint8_t lbR = 0, lbG = 120, lbB = 255;
    const uint8_t dbR = 0, dbG = 0, dbB = 120;
    const uint8_t rR = 255, rG = 0, rB = 0;

    int segLen = NUM_LEDS / 3;
    if (segLen < 2)
        segLen = 2;

    if (fadeFactor < 0.0f)
        fadeFactor = 0.0f;
    if (fadeFactor > 1.0f)
        fadeFactor = 1.0f;

    float brightnessFactor = cfg.brightness / 255.0f;
    if (brightnessFactor < 0.02f)
        brightnessFactor = 0.02f;
    brightnessFactor *= fadeFactor;

    for (int i = 0; i < NUM_LEDS; i++)
    {
        uint8_t br = 0, bg = 0, bb = 0;
        if (i < segLen)
        {
            br = lbR;
            bg = lbG;
            bb = lbB;
        }
        else if (i < 2 * segLen)
        {
            br = dbR;
            bg = dbG;
            bb = dbB;
        }
        else if (i < 3 * segLen)
        {
            br = rR;
            bg = rG;
            bb = rB;
        }
        else
        {
            br = bg = bb = 0;
        }

        uint8_t r = (uint8_t)(br * brightnessFactor);
        uint8_t g = (uint8_t)(bg * brightnessFactor);
        uint8_t b = (uint8_t)(bb * brightnessFactor);
        strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
}

void showMLogoAnimation()
{
    if (g_animationActive)
        return;
    g_animationActive = true;

    fadeBarToBlack(1200);

    Serial.println("[MLOGO] Starte BMW M Boot-Animation");

    const uint8_t lbR = 0, lbG = 120, lbB = 255;
    const uint8_t dbR = 0, dbG = 0, dbB = 120;
    const uint8_t rR = 255, rG = 0, rB = 0;

    int segLen = NUM_LEDS / 3;
    if (segLen < 2)
        segLen = 2;

    uint8_t baseR[NUM_LEDS];
    uint8_t baseG[NUM_LEDS];
    uint8_t baseB[NUM_LEDS];

    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (i < segLen)
        {
            baseR[i] = lbR;
            baseG[i] = lbG;
            baseB[i] = lbB;
        }
        else if (i < 2 * segLen)
        {
            baseR[i] = dbR;
            baseG[i] = dbG;
            baseB[i] = dbB;
        }
        else if (i < 3 * segLen)
        {
            baseR[i] = rR;
            baseG[i] = rG;
            baseB[i] = rB;
        }
        else
        {
            baseR[i] = baseG[i] = baseB[i] = 0;
        }
    }

    const int steps = 50;
    const int frameDelay = 16;

    for (int s = 0; s <= steps; s++)
    {
        float t = (float)s / (float)steps;
        float eased = t * t;
        float f = eased;

        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint8_t r = (uint8_t)(baseR[i] * f);
            uint8_t g = (uint8_t)(baseG[i] * f);
            uint8_t b = (uint8_t)(baseB[i] * f);
            strip.setPixelColor(i, strip.Color(r, g, b));
        }
        strip.show();
        delay(frameDelay);
    }

    delay(120);

    for (int s = steps; s >= 0; s--)
    {
        float t = (float)s / (float)steps;
        float eased = t * t;
        float f = eased;

        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint8_t r = (uint8_t)(baseR[i] * f);
            uint8_t g = (uint8_t)(baseG[i] * f);
            uint8_t b = (uint8_t)(baseB[i] * f);
            strip.setPixelColor(i, strip.Color(r, g, b));
        }
        strip.show();
        delay(frameDelay);
    }

    strip.clear();
    strip.show();

    Serial.println("[MLOGO] Animation fertig");

    g_animationActive = false;

    if (!g_testActive && g_currentRpm > 0)
    {
        updateRpmBar(g_currentRpm);
    }
}

void showMLogoLeavingAnimation()
{
    if (g_animationActive)
        return;
    g_animationActive = true;

    fadeBarToBlack(1200);

    Serial.println("[MLOGO] Starte Leaving-Animation");

    const uint8_t lbR = 0, lbG = 120, lbB = 255;
    const uint8_t dbR = 0, dbG = 0, dbB = 120;
    const uint8_t rR = 255, rG = 0, rB = 0;

    int segLen = NUM_LEDS / 3;
    if (segLen < 2)
        segLen = 2;

    uint8_t baseR[NUM_LEDS];
    uint8_t baseG[NUM_LEDS];
    uint8_t baseB[NUM_LEDS];

    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (i < segLen)
        {
            baseR[i] = lbR;
            baseG[i] = lbG;
            baseB[i] = lbB;
        }
        else if (i < 2 * segLen)
        {
            baseR[i] = dbR;
            baseG[i] = dbG;
            baseB[i] = dbB;
        }
        else if (i < 3 * segLen)
        {
            baseR[i] = rR;
            baseG[i] = rG;
            baseB[i] = rB;
        }
        else
        {
            baseR[i] = baseG[i] = baseB[i] = 0;
        }
    }

    const int steps = 40;
    const int frameDelay = 20;

    for (int s = 0; s <= steps; s++)
    {
        float t = (float)s / (float)steps;
        float eased = t * t;
        float f = 1.0f - eased;

        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint8_t r = (uint8_t)(baseR[i] * f);
            uint8_t g = (uint8_t)(baseG[i] * f);
            uint8_t b = (uint8_t)(baseB[i] * f);
            strip.setPixelColor(i, strip.Color(r, g, b));
        }
        strip.show();
        delay(frameDelay);
    }

    strip.clear();
    strip.show();

    Serial.println("[MLOGO] Leaving-Animation fertig");
    g_animationActive = false;
}

void logoAnimLoop()
{
    unsigned long now = millis();

    if (g_ignitionOn && (now - g_lastObdMs > IGNITION_TIMEOUT_MS))
    {
        Serial.println("[MLOGO] Zündung aus (Timeout) erkannt");
        g_ignitionOn = false;
        g_engineRunning = false;

        if (cfg.logoOnIgnitionOff && !g_leavingPlayedThisCycle)
        {
            g_leavingPlayedThisCycle = true;
            showMLogoLeavingAnimation();
        }

        g_logoPlayedThisCycle = false;
    }
}
