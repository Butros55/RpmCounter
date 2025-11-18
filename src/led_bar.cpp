#include "led_bar.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"
#include "state.h"

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

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
    if (g_animationActive)
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
                    color = strip.Color(0, 255, 0);
                }
                else if (pos < yellowEnd)
                {
                    color = strip.Color(255, 180, 0);
                }
                else
                {
                    if (cfg.mode == 1 && shiftBlink)
                    {
                        color = blinkState ? strip.Color(255, 0, 0) : strip.Color(0, 0, 0);
                    }
                    else
                    {
                        color = strip.Color(255, 0, 0);
                    }
                }
            }
        }

        strip.setPixelColor(i, color);
    }

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
