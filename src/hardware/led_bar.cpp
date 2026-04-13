#include "led_bar.h"

#include <Arduino.h>
#include <math.h>

#include "core/config.h"
#include "core/state.h"
#include "hardware/display.h"

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
// Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_RGB + NEO_KHZ800);

namespace
{
    constexpr int LED_MODE_CASUAL = 0;
    constexpr int LED_MODE_F1 = 1;
    constexpr int LED_MODE_AGGRESSIVE = 2;
    constexpr int LED_MODE_GT3 = 3;
    constexpr int GT3_PIT_MARKER_COUNT = 2;
    const RgbColor GT3_PIT_COLOR = {255, 0, 170};

    const unsigned long PREVIEW_HOLD_MS = 2500;
    const unsigned long PREVIEW_FADE_MS = 900;
    bool previewFadeActive = false;
    unsigned long previewFadeStart = 0;
    uint32_t previewSnapshot[NUM_LEDS];
    bool previewSnapshotValid = false;

    uint32_t toStripColor(const RgbColor &c)
    {
        return strip.Color(c.r, c.g, c.b);
    }

    uint32_t zoneColor(float pos)
    {
        const float greenEnd = cfg.greenEndPct / 100.0f;
        const float yellowEnd = cfg.yellowEndPct / 100.0f;
        if (pos < greenEnd)
        {
            return toStripColor(cfg.greenColor);
        }
        if (pos < yellowEnd)
        {
            return toStripColor(cfg.yellowColor);
        }
        return toStripColor(cfg.redColor);
    }

    void overlayPitLimiterMarkers(bool enabled)
    {
        if (!enabled)
        {
            return;
        }

        const uint32_t pitColor = toStripColor(GT3_PIT_COLOR);
        for (int offset = 0; offset < GT3_PIT_MARKER_COUNT && offset < NUM_LEDS; ++offset)
        {
            strip.setPixelColor(offset, pitColor);
            strip.setPixelColor(NUM_LEDS - 1 - offset, pitColor);
        }
    }

    void renderLinearPattern(int ledsOn, bool aggressiveFullBlink, bool shiftBlink, bool blinkState, float greenEnd, float yellowEnd, bool &displayBlink)
    {
        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint32_t color = strip.Color(0, 0, 0);

            if (i < ledsOn)
            {
                const float pos = (NUM_LEDS > 1) ? (float)i / (float)(NUM_LEDS - 1) : 0.0f;

                if (aggressiveFullBlink)
                {
                    color = blinkState ? strip.Color(255, 0, 0) : strip.Color(0, 0, 0);
                }
                else if (pos < greenEnd)
                {
                    color = toStripColor(cfg.greenColor);
                }
                else if (pos < yellowEnd)
                {
                    color = toStripColor(cfg.yellowColor);
                }
                else if (cfg.mode == LED_MODE_F1 && shiftBlink)
                {
                    color = blinkState ? toStripColor(cfg.redColor) : strip.Color(0, 0, 0);
                    displayBlink = blinkState;
                }
                else
                {
                    color = toStripColor(cfg.redColor);
                }
            }

            strip.setPixelColor(i, color);
        }
    }

    void renderGt3Pattern(float fraction, float blinkStart, bool blinkState, bool &displayBlink)
    {
        const int pairCount = (NUM_LEDS + 1) / 2;
        int pairsOn = (int)round(fraction * pairCount);
        if (pairsOn < 0)
            pairsOn = 0;
        if (pairsOn > pairCount)
            pairsOn = pairCount;

        const bool finalBlink = fraction >= blinkStart;
        if (finalBlink)
        {
            const uint32_t blinkColor = blinkState ? toStripColor(cfg.redColor) : strip.Color(0, 0, 0);
            for (int i = 0; i < NUM_LEDS; ++i)
            {
                strip.setPixelColor(i, blinkColor);
            }
            displayBlink = blinkState;
            return;
        }

        for (int rank = 0; rank < pairCount; ++rank)
        {
            const bool on = rank < pairsOn;
            const float pos = (pairCount > 1) ? (float)rank / (float)(pairCount - 1) : 1.0f;
            const uint32_t color = on ? zoneColor(pos) : strip.Color(0, 0, 0);
            const int left = rank;
            const int right = NUM_LEDS - 1 - rank;
            strip.setPixelColor(left, color);
            if (right != left)
            {
                strip.setPixelColor(right, color);
            }
        }

        overlayPitLimiterMarkers(g_pitLimiterActive);
    }

    // Gleiche Kurve wie computeSimFraction() im HTML
    float computeTestSweepPct(float t)
    {
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;

        float pct = 0.0f;

        if (t < 0.30f)
        {
            // 0 → 100% (smoothstep)
            float tt = t / 0.30f;
            pct = tt * tt * (3.0f - 2.0f * tt);
        }
        else if (t < 0.60f)
        {
            // 100% → 40% mit Gasstößen
            float tt = (t - 0.30f) / 0.30f;                    // 0..1
            float base = 1.0f - 0.6f * tt;                     // 1.0 -> 0.4
            float wobble = 0.10f * sinf(tt * 3.14159f * 4.0f); // Wackler
            pct = base + wobble;

            if (pct < 0.4f)
                pct = 0.4f;
            if (pct > 1.0f)
                pct = 1.0f;
        }
        else if (t < 0.85f)
        {
            // 40% → 100% (zweiter Gasstoß)
            float tt = (t - 0.60f) / 0.25f;                            // 0..1
            float base = 0.4f + 0.6f * (tt * tt * (3.0f - 2.0f * tt)); // 0.4 -> 1.0
            float wobble = 0.05f * sinf(tt * 3.14159f * 2.0f);
            pct = base + wobble;

            if (pct < 0.4f)
                pct = 0.4f;
            if (pct > 1.0f)
                pct = 1.0f;
        }
        else
        {
            // zum Schluss von 100% wieder runter
            float tt = (t - 0.85f) / 0.15f; // 0..1
            float base = 1.0f - tt;         // 1.0 -> 0.0
            float wobble = 0.05f * sinf(tt * 3.14159f * 2.0f);
            pct = base + wobble;

            if (pct < 0.0f)
                pct = 0.0f;
            if (pct > 1.0f)
                pct = 1.0f;
        }

        return pct;
    }

    void renderPreviewFade()
    {
        if (!g_brightnessPreviewActive)
        {
            previewFadeActive = false;
            return;
        }

        unsigned long now = millis();
        if (!previewFadeActive)
        {
            if (!previewSnapshotValid)
            {
                g_brightnessPreviewActive = false;
                if (!g_testActive)
                {
                    updateRpmBar(g_currentRpm);
                }
                return;
            }

            if (now - g_lastBrightnessChangeMs < PREVIEW_HOLD_MS)
            {
                return;
            }

            previewFadeActive = true;
            previewFadeStart = now;
        }

        float t = (float)(now - previewFadeStart) / (float)PREVIEW_FADE_MS;
        if (t >= 1.0f)
        {
            previewFadeActive = false;
            g_brightnessPreviewActive = false;
            strip.clear();
            strip.show();
            if (!g_testActive)
            {
                updateRpmBar(g_currentRpm);
            }
            return;
        }

        float scale = 1.0f - t;
        if (scale < 0.0f)
            scale = 0.0f;

        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint32_t col = previewSnapshot[i];
            uint8_t gVal = (col >> 16) & 0xFF;
            uint8_t rVal = (col >> 8) & 0xFF;
            uint8_t bVal = col & 0xFF;

            uint8_t r = (uint8_t)(rVal * scale);
            uint8_t g = (uint8_t)(gVal * scale);
            uint8_t b = (uint8_t)(bVal * scale);
            strip.setPixelColor(i, strip.Color(r, g, b));
        }

        strip.show();
    }
}

void rememberPreviewPixels()
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        previewSnapshot[i] = strip.getPixelColor(i);
    }
    previewSnapshotValid = true;
    previewFadeActive = false;
}

void setStatusLED(bool on)
{
    digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
}

void initLeds()
{
    pinMode(STATUS_LED_PIN, OUTPUT);
    setStatusLED(false);

    strip.begin();
    strip.setBrightness(cfg.brightness);
    strip.clear();
    strip.show();
}

void updateRpmBar(int rpm)
{
    if (g_animationActive || g_brightnessPreviewActive)
    {
        return;
    }

    if (rpm < 0)
    {
        rpm = 0;
    }

    int maxRpmForBar;
    if (cfg.autoScaleMaxRpm)
    {
        maxRpmForBar = g_maxSeenRpm;
        if (maxRpmForBar < 2000)
        {
            maxRpmForBar = 2000;
        }
    }
    else
    {
        maxRpmForBar = (cfg.fixedMaxRpm > 1000) ? cfg.fixedMaxRpm : 2000;
    }

    float fraction = (float)rpm / (float)maxRpmForBar;
    if (fraction > 1.0f)
    {
        fraction = 1.0f;
    }

    float greenEnd = cfg.greenEndPct / 100.0f;
    float yellowEnd = cfg.yellowEndPct / 100.0f;
    float blinkStart = cfg.blinkStartPct / 100.0f;

    if (greenEnd < 0.0f)
        greenEnd = 0.0f;
    if (greenEnd > 1.0f)
        greenEnd = 1.0f;
    if (yellowEnd < greenEnd)
        yellowEnd = greenEnd;
    if (yellowEnd > 1.0f)
        yellowEnd = 1.0f;
    if (blinkStart < yellowEnd)
        blinkStart = yellowEnd;
    if (blinkStart > 1.0f)
        blinkStart = 1.0f;

    int ledsOn = (int)round(fraction * NUM_LEDS);

    static unsigned long lastBlink = 0;
    static bool blinkState = false;
    unsigned long now = millis();

    bool shiftBlink = ((cfg.mode == LED_MODE_F1 || cfg.mode == LED_MODE_AGGRESSIVE || cfg.mode == LED_MODE_GT3) && fraction >= blinkStart);

    if (shiftBlink && now - lastBlink > 100)
    {
        lastBlink = now;
        blinkState = !blinkState;
    }

    bool displayBlink = false;

    if (cfg.mode == LED_MODE_AGGRESSIVE && fraction >= blinkStart)
    {
        ledsOn = NUM_LEDS;
        displayBlink = blinkState;
    }

    strip.clear();

    if (cfg.mode == LED_MODE_GT3)
    {
        renderGt3Pattern(fraction, blinkStart, blinkState, displayBlink);
    }
    else
    {
        renderLinearPattern(ledsOn, cfg.mode == LED_MODE_AGGRESSIVE && fraction >= blinkStart, shiftBlink, blinkState, greenEnd, yellowEnd, displayBlink);
    }

    strip.show();
    displaySetShiftBlink(displayBlink);

    static unsigned long lastLogMs = 0;
    if (now - lastLogMs > 500)
    {
        lastLogMs = now;
        Serial.print("[LED] rpm=");
        Serial.print(rpm);
        Serial.print(" fraction=");
        Serial.print(fraction, 2);
        Serial.print(" ledsOn=");
        Serial.println(ledsOn);
    }
}

void ledBarLoop()
{
    if (g_brightnessPreviewActive)
    {
        renderPreviewFade();
        return;
    }

    if (g_testActive)
    {
        unsigned long now = millis();
        unsigned long elapsed = now - g_testStartMs;
        if (elapsed >= TEST_SWEEP_DURATION)
        {
            g_testActive = false;
            updateRpmBar(g_currentRpm);
        }
        else
        {
            float t = (float)elapsed / (float)TEST_SWEEP_DURATION; // 0..1
            float pct = computeTestSweepPct(t);
            int simRpm = (int)(pct * g_testMaxRpm);
            updateRpmBar(simRpm);
        }
        return;
    }

    if (!g_animationActive)
    {
        updateRpmBar(g_currentRpm);
    }
}
