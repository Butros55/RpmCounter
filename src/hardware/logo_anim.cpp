#include "logo_anim.h"

#include <Arduino.h>

#include "core/config.h"
#include "led_bar.h"
#include "core/state.h"

namespace
{
    const uint8_t lbR = 0, lbG = 120, lbB = 255;
    const uint8_t dbR = 0, dbG = 0, dbB = 120;
    const uint8_t rR = 255, rG = 0, rB = 0;

    int segmentLength()
    {
        int segLen = NUM_LEDS / 3;
        return (segLen < 2) ? 2 : segLen;
    }

    void drawLogoFrame(float intensity)
    {
        if (intensity < 0.0f)
            intensity = 0.0f;
        if (intensity > 1.0f)
            intensity = 1.0f;

        int segLen = segmentLength();
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

            uint8_t r = (uint8_t)(br * intensity);
            uint8_t g = (uint8_t)(bg * intensity);
            uint8_t b = (uint8_t)(bb * intensity);
            strip.setPixelColor(i, strip.Color(r, g, b));
        }
        strip.show();
    }

    void fadeCurrentBarToBlack(unsigned long durationMs)
    {
        uint32_t snapshot[NUM_LEDS];
        for (int i = 0; i < NUM_LEDS; i++)
        {
            snapshot[i] = strip.getPixelColor(i);
        }

        const int steps = 24;
        unsigned long perStep = (durationMs > 0) ? durationMs / steps : 0;
        for (int s = 0; s <= steps; s++)
        {
            float t = (float)s / (float)steps;
            float scale = 1.0f - t;
            if (scale < 0.0f)
                scale = 0.0f;

            for (int i = 0; i < NUM_LEDS; i++)
            {
                uint32_t col = snapshot[i];
                uint8_t gVal = (col >> 16) & 0xFF;
                uint8_t rVal = (col >> 8) & 0xFF;
                uint8_t bVal = col & 0xFF;
                uint8_t r = (uint8_t)(rVal * scale);
                uint8_t g = (uint8_t)(gVal * scale);
                uint8_t b = (uint8_t)(bVal * scale);
                strip.setPixelColor(i, strip.Color(r, g, b));
            }
            strip.show();
            if (perStep > 0)
            {
                delay(perStep);
            }
        }
    }

    void playLogoSequence(int steps, int frameDelay, bool fadeOutSlow)
    {
        for (int s = 0; s <= steps; s++)
        {
            float t = (float)s / (float)steps;
            float eased = t * t * (3.0f - 2.0f * t);
            drawLogoFrame(eased);
            delay(frameDelay);
        }

        int fadeSteps = fadeOutSlow ? steps : (steps / 2);
        for (int s = fadeSteps; s >= 0; s--)
        {
            float t = (float)s / (float)fadeSteps;
            float eased = t * t;
            drawLogoFrame(eased);
            delay(frameDelay);
        }

        strip.clear();
        strip.show();
    }
}

void showMLogoPreview()
{
    float brightnessFactor = cfg.brightness / 255.0f;
    if (brightnessFactor < 0.02f)
        brightnessFactor = 0.02f;

    drawLogoFrame(brightnessFactor);
    rememberPreviewPixels();
}

void showMLogoAnimation()
{
    if (g_animationActive)
        return;
    g_animationActive = true;

    Serial.println("[MLOGO] Starte BMW M Boot-Animation");

    playLogoSequence(60, 15, true);

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

    Serial.println("[MLOGO] Starte Leaving-Animation");

    fadeCurrentBarToBlack(1200);
    delay(100);
    playLogoSequence(50, 20, true);

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
        g_engineStartLogoShown = false;
        g_ignitionLogoShown = false;
    }
}
