#include "led_bar.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"
#include "state.h"
#include "logo_anim.h"

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

namespace
{
    int s_targetBrightness = DEFAULT_BRIGHTNESS;
    int s_currentBrightness = DEFAULT_BRIGHTNESS;

    uint32_t colorFromConfig(uint32_t cfgColor)
    {
        uint8_t r = (cfgColor >> 16) & 0xFF;
        uint8_t g = (cfgColor >> 8) & 0xFF;
        uint8_t b = cfgColor & 0xFF;
        return strip.Color(r, g, b);
    }
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
    s_targetBrightness = cfg.brightness;
    s_currentBrightness = cfg.brightness;
    strip.setBrightness(s_currentBrightness);
    strip.clear();
    strip.show();
}

void setLedTargetBrightness(int value)
{
    if (value < 0)
        value = 0;
    if (value > 255)
        value = 255;
    s_targetBrightness = value;
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

    if (g_brightnessPreviewActive && (now - g_lastBrightnessChangeMs > 1000))
    {
        g_brightnessPreviewActive = false;

        if (!g_testActive)
        {
            if (g_currentRpm > 0)
            {
                updateRpmBar(g_currentRpm);
            }
            else
            {
                strip.clear();
                strip.show();
            }
        }
    }

    bool shiftBlink = ((cfg.mode == 1 || cfg.mode == 2) && fraction >= blinkStart);

    if (shiftBlink && now - lastBlink > 100)
    {
        lastBlink = now;
        blinkState = !blinkState;
    }

    if (cfg.mode == 2 && fraction >= blinkStart)
    {
        ledsOn = NUM_LEDS;
    }

    strip.clear();

    uint32_t colorGreen = colorFromConfig(cfg.color1);
    uint32_t colorYellow = colorFromConfig(cfg.color2);
    uint32_t colorRed = colorFromConfig(cfg.color3);

    for (int i = 0; i < NUM_LEDS; i++)
    {
        uint32_t color = strip.Color(0, 0, 0);

        if (i < ledsOn)
        {
            float pos = (float)i / (float)(NUM_LEDS - 1);

            if (cfg.mode == 2 && fraction >= blinkStart)
            {
                color = blinkState ? strip.Color(255, 0, 0) : strip.Color(0, 0, 0);
            }
            else
            {
                if (pos < greenEnd)
                {
                    color = colorGreen;
                }
                else if (pos < yellowEnd)
                {
                    color = colorYellow;
                }
                else
                {
                    if (cfg.mode == 1 && shiftBlink)
                    {
                        color = blinkState ? colorRed : strip.Color(0, 0, 0);
                    }
                    else
                    {
                        color = colorRed;
                    }
                }
            }
        }

        strip.setPixelColor(i, color);
    }

    strip.setBrightness(s_currentBrightness);
    strip.show();

    Serial.print("[LED] rpm=");
    Serial.print(rpm);
    Serial.print(" fraction=");
    Serial.print(fraction, 2);
    Serial.print(" ledsOn=");
    Serial.println(ledsOn);
}

void ledBarLoop()
{
    static const unsigned long PREVIEW_HOLD_MS = 2500;
    static const unsigned long PREVIEW_FADE_MS = 1000;

    if (g_brightnessPreviewActive)
    {
        unsigned long now = millis();
        unsigned long elapsed = now - g_lastBrightnessChangeMs;

        if (!g_brightnessPreviewFading && elapsed > PREVIEW_HOLD_MS)
        {
            g_brightnessPreviewFading = true;
            g_brightnessPreviewFadeStartMs = now;
        }

        if (g_brightnessPreviewFading)
        {
            unsigned long fadeElapsed = now - g_brightnessPreviewFadeStartMs;
            float t = (float)fadeElapsed / (float)PREVIEW_FADE_MS;
            if (t >= 1.0f)
            {
                g_brightnessPreviewActive = false;
                g_brightnessPreviewFading = false;
                strip.clear();
                strip.show();

                if (!g_testActive)
                {
                    updateRpmBar(g_currentRpm);
                }
            }
            else
            {
                float factor = 1.0f - t;
                if (factor < 0.0f)
                    factor = 0.0f;
                showMLogoPreview(factor);
            }
        }

        return;
    }

    if (s_currentBrightness != s_targetBrightness)
    {
        int diff = s_targetBrightness - s_currentBrightness;
        int step = (abs(diff) > 20) ? 6 : 2;
        if (abs(diff) <= step)
        {
            s_currentBrightness = s_targetBrightness;
        }
        else
        {
            s_currentBrightness += (diff > 0) ? step : -step;
        }

        if (s_currentBrightness < 0)
            s_currentBrightness = 0;
        if (s_currentBrightness > 255)
            s_currentBrightness = 255;

        strip.setBrightness(s_currentBrightness);
        strip.show();
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
            float t = (float)elapsed / (float)TEST_SWEEP_DURATION;
            int simRpm = 0;

            if (t < 0.25f)
            {
                float tt = t / 0.25f;
                float pct = sinf(tt * 3.14159f);
                if (pct < 0.0f)
                    pct = 0.0f;
                simRpm = (int)(pct * g_testMaxRpm);
            }
            else if (t < 0.70f)
            {
                float tt = (t - 0.25f) / 0.45f;
                if (tt < 0.0f)
                    tt = 0.0f;
                if (tt > 1.0f)
                    tt = 1.0f;

                float pct = tt * tt * (3.0f - 2.0f * tt);
                if (pct < 0.0f)
                    pct = 0.0f;
                if (pct > 1.0f)
                    pct = 1.0f;

                simRpm = (int)(pct * g_testMaxRpm);
            }
            else
            {
                float tt = (t - 0.70f) / 0.30f;
                if (tt < 0.0f)
                    tt = 0.0f;
                if (tt > 1.0f)
                    tt = 1.0f;

                float base = 1.0f - tt;
                float wobble = 0.05f * sinf(tt * 3.14159f * 4.0f);
                float pct = base + wobble;

                if (pct < 0.0f)
                    pct = 0.0f;
                if (pct > 1.0f)
                    pct = 1.0f;

                simRpm = (int)(pct * g_testMaxRpm);
            }

            updateRpmBar(simRpm);
        }
    }
}
