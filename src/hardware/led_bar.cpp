#include "led_bar.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "core/config.h"
#include "core/state.h"
#include "core/utils.h"
#include "hardware/ambient_light.h"
#include "hardware/display.h"
#include "signal_utils.h"

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

    enum class LedFrameMode : uint8_t
    {
        None = 0,
        Linear = 1,
        Gt3 = 2,
        Gt3PitOnly = 3,
        DiagnosticOff = 4,
        DiagnosticStaticGreen = 5,
        DiagnosticStaticWhite = 6,
        DiagnosticPitMarkers = 7,
        BrightnessPreview = 8
    };

    const unsigned long PREVIEW_HOLD_MS = 2500;
    const unsigned long PREVIEW_FADE_MS = 900;
    bool previewFadeActive = false;
    unsigned long previewFadeStart = 0;
    uint32_t previewSnapshot[NUM_LEDS];
    bool previewSnapshotValid = false;
    uint32_t g_ledFrameCache[NUM_LEDS] = {};
    bool g_ledFrameCacheValid = false;
    bool g_forceNextStripShow = true;
    bool g_lastDisplayBlinkState = false;
    LedDiagnosticMode g_ledDiagnosticMode = LedDiagnosticMode::Live;
    int g_ledMedianSamples[3] = {0, 0, 0};
    uint8_t g_ledMedianCount = 0;
    uint8_t g_ledMedianIndex = 0;
    uint8_t g_appliedStripBrightness = DEFAULT_BRIGHTNESS;

    void invalidateLedFrameCacheInternal()
    {
        g_ledFrameCacheValid = false;
        g_forceNextStripShow = true;
        if (g_lastDisplayBlinkState)
        {
            displaySetShiftBlink(false);
            g_lastDisplayBlinkState = false;
        }
    }

    void setLastRenderMode(LedFrameMode mode)
    {
        g_ledRenderDebug.lastRenderMode = static_cast<uint8_t>(mode);
    }

    void applyStripBrightness(uint8_t brightness)
    {
        if (brightness == g_appliedStripBrightness)
        {
            return;
        }

        strip.setBrightness(brightness);
        g_appliedStripBrightness = brightness;
        ambientLightNoteAppliedBrightness(brightness);
        ++g_ledRenderDebug.brightnessUpdateCount;
        g_ledRenderDebug.lastAppliedBrightness = brightness;
        g_forceNextStripShow = true;
    }

    void resetLedMedianFilter(int seedRpm)
    {
        g_ledMedianSamples[0] = seedRpm;
        g_ledMedianSamples[1] = seedRpm;
        g_ledMedianSamples[2] = seedRpm;
        g_ledMedianCount = 0;
        g_ledMedianIndex = 0;
    }

    int filterLedRpm(int rpm, bool bypass)
    {
        if (bypass)
        {
            resetLedMedianFilter(rpm);
            return rpm;
        }

        g_ledMedianSamples[g_ledMedianIndex] = rpm;
        g_ledMedianIndex = static_cast<uint8_t>((g_ledMedianIndex + 1U) % 3U);
        if (g_ledMedianCount < 3)
        {
            ++g_ledMedianCount;
        }

        if (g_ledMedianCount < 3)
        {
            return rpm;
        }

        return median3Int(g_ledMedianSamples[0], g_ledMedianSamples[1], g_ledMedianSamples[2]);
    }

    uint32_t toStripColor(const RgbColor &c)
    {
        return strip.Color(c.r, c.g, c.b);
    }

    void clearFrame(uint32_t *frame)
    {
        for (int i = 0; i < NUM_LEDS; ++i)
        {
            frame[i] = 0;
        }
    }

    void pushFrameToStrip(const uint32_t *frame)
    {
        for (int i = 0; i < NUM_LEDS; ++i)
        {
            strip.setPixelColor(i, frame[i]);
            g_ledFrameCache[i] = frame[i];
        }
        strip.show();
        g_ledFrameCacheValid = true;
        g_forceNextStripShow = false;
        ++g_ledRenderDebug.frameShowCount;
        g_ledRenderDebug.lastShowMs = millis();
        g_ledRenderDebug.lastAppliedBrightness = g_appliedStripBrightness;
    }

    bool frameDiffersFromCache(const uint32_t *frame)
    {
        if (!g_ledFrameCacheValid)
        {
            return true;
        }
        for (int i = 0; i < NUM_LEDS; ++i)
        {
            if (g_ledFrameCache[i] != frame[i])
            {
                return true;
            }
        }
        return false;
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

    void overlayPitLimiterMarkers(uint32_t *frame, bool enabled)
    {
        if (!enabled)
        {
            return;
        }

        const uint32_t pitColor = toStripColor(GT3_PIT_COLOR);
        for (int offset = 0; offset < GT3_PIT_MARKER_COUNT && offset < NUM_LEDS; ++offset)
        {
            frame[offset] = pitColor;
            frame[NUM_LEDS - 1 - offset] = pitColor;
        }
    }

    void renderLinearPattern(uint32_t *frame, int ledsOn, bool aggressiveFullBlink, bool shiftBlink, bool blinkState, float greenEnd, float yellowEnd, bool &displayBlink)
    {
        g_ledRenderDebug.pitLimiterOnly = false;
        setLastRenderMode(LedFrameMode::Linear);
        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint32_t color = 0;

            if (i < ledsOn)
            {
                const float pos = (NUM_LEDS > 1) ? (float)i / (float)(NUM_LEDS - 1) : 0.0f;

                if (aggressiveFullBlink)
                {
                    color = blinkState ? strip.Color(255, 0, 0) : 0;
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
                    color = blinkState ? toStripColor(cfg.redColor) : 0;
                    displayBlink = blinkState;
                }
                else
                {
                    color = toStripColor(cfg.redColor);
                }
            }

            frame[i] = color;
        }
    }

    void renderGt3Pattern(uint32_t *frame, float fraction, float blinkStart, bool blinkState, bool &displayBlink)
    {
        if (g_pitLimiterActive)
        {
            overlayPitLimiterMarkers(frame, true);
            g_ledRenderDebug.pitLimiterOnly = true;
            setLastRenderMode(LedFrameMode::Gt3PitOnly);
            return;
        }

        const int pairCount = (NUM_LEDS + 1) / 2;
        int pairsOn = (int)round(fraction * pairCount);
        if (pairsOn < 0)
            pairsOn = 0;
        if (pairsOn > pairCount)
            pairsOn = pairCount;

        const bool finalBlink = fraction >= blinkStart;
        if (finalBlink)
        {
            const uint32_t blinkColor = blinkState ? toStripColor(cfg.redColor) : 0;
            for (int i = 0; i < NUM_LEDS; ++i)
            {
                frame[i] = blinkColor;
            }
            displayBlink = blinkState;
            g_ledRenderDebug.pitLimiterOnly = false;
            setLastRenderMode(LedFrameMode::Gt3);
            return;
        }

        for (int rank = 0; rank < pairCount; ++rank)
        {
            const bool on = rank < pairsOn;
            const float pos = (pairCount > 1) ? (float)rank / (float)(pairCount - 1) : 1.0f;
            const uint32_t color = on ? zoneColor(pos) : 0;
            const int left = rank;
            const int right = NUM_LEDS - 1 - rank;
            frame[left] = color;
            if (right != left)
            {
                frame[right] = color;
            }
        }

        g_ledRenderDebug.pitLimiterOnly = false;
        setLastRenderMode(LedFrameMode::Gt3);
    }

    void renderDiagnosticPattern(uint32_t *frame)
    {
        clearFrame(frame);
        g_ledRenderDebug.pitLimiterOnly = false;

        switch (g_ledDiagnosticMode)
        {
        case LedDiagnosticMode::Off:
            setLastRenderMode(LedFrameMode::DiagnosticOff);
            break;
        case LedDiagnosticMode::StaticGreen:
        {
            const uint32_t color = toStripColor(cfg.greenColor);
            for (int i = 0; i < NUM_LEDS; ++i)
            {
                frame[i] = color;
            }
            setLastRenderMode(LedFrameMode::DiagnosticStaticGreen);
            break;
        }
        case LedDiagnosticMode::StaticWhite:
        {
            const uint32_t color = strip.Color(255, 255, 255);
            for (int i = 0; i < NUM_LEDS; ++i)
            {
                frame[i] = color;
            }
            setLastRenderMode(LedFrameMode::DiagnosticStaticWhite);
            break;
        }
        case LedDiagnosticMode::PitMarkers:
            overlayPitLimiterMarkers(frame, true);
            g_ledRenderDebug.pitLimiterOnly = true;
            setLastRenderMode(LedFrameMode::DiagnosticPitMarkers);
            break;
        case LedDiagnosticMode::Live:
        default:
            setLastRenderMode(LedFrameMode::None);
            break;
        }
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
            invalidateLedFrameCacheInternal();
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
        setLastRenderMode(LedFrameMode::BrightnessPreview);
        invalidateLedFrameCacheInternal();
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
    g_appliedStripBrightness = static_cast<uint8_t>(clampInt(cfg.brightness, 0, 255));
    strip.setBrightness(g_appliedStripBrightness);
    ambientLightNoteAppliedBrightness(g_appliedStripBrightness);
    strip.clear();
    strip.show();
    invalidateLedFrameCacheInternal();
    resetLedMedianFilter(0);
    g_ledDiagnosticMode = LedDiagnosticMode::Live;
    g_ledRenderDebug.lastAppliedBrightness = g_appliedStripBrightness;
    g_ledRenderDebug.lastStartRpm = cfg.rpmStartRpm;
    g_ledRenderDebug.lastRenderMode = static_cast<uint8_t>(LedFrameMode::None);
}

void ledBarRefreshBrightness()
{
    applyStripBrightness(ambientLightGetLedBrightness());
}

void ledBarInvalidateFrameCache()
{
    invalidateLedFrameCacheInternal();
}

void ledBarSetDiagnosticMode(LedDiagnosticMode mode)
{
    if (g_ledDiagnosticMode == mode)
    {
        return;
    }

    g_ledDiagnosticMode = mode;
    g_testActive = false;
    g_brightnessPreviewActive = false;
    invalidateLedFrameCacheInternal();
}

LedDiagnosticMode ledBarGetDiagnosticMode()
{
    return g_ledDiagnosticMode;
}

const char *ledBarGetDiagnosticModeName()
{
    switch (g_ledDiagnosticMode)
    {
    case LedDiagnosticMode::Off:
        return "off";
    case LedDiagnosticMode::StaticGreen:
        return "static-green";
    case LedDiagnosticMode::StaticWhite:
        return "static-white";
    case LedDiagnosticMode::PitMarkers:
        return "pit-markers";
    case LedDiagnosticMode::Live:
    default:
        return "live";
    }
}

const char *ledBarGetLastRenderModeName()
{
    switch (static_cast<LedFrameMode>(g_ledRenderDebug.lastRenderMode))
    {
    case LedFrameMode::Linear:
        return "linear";
    case LedFrameMode::Gt3:
        return "gt3";
    case LedFrameMode::Gt3PitOnly:
        return "gt3-pit-only";
    case LedFrameMode::DiagnosticOff:
        return "diag-off";
    case LedFrameMode::DiagnosticStaticGreen:
        return "diag-green";
    case LedFrameMode::DiagnosticStaticWhite:
        return "diag-white";
    case LedFrameMode::DiagnosticPitMarkers:
        return "diag-pit";
    case LedFrameMode::BrightnessPreview:
        return "brightness-preview";
    case LedFrameMode::None:
    default:
        return "none";
    }
}

uint8_t ledBarGetAppliedBrightness()
{
    return g_appliedStripBrightness;
}

void updateRpmBar(int rpm)
{
    if (g_animationActive || g_brightnessPreviewActive)
    {
        return;
    }

    ++g_ledRenderDebug.renderCalls;

    if (rpm < 0)
    {
        rpm = 0;
    }

    const int filteredRpm = filterLedRpm(rpm, g_testActive);
    g_ledRenderDebug.lastRawRpm = rpm;
    g_ledRenderDebug.lastFilteredRpm = filteredRpm;
    if (filteredRpm != rpm)
    {
        ++g_ledRenderDebug.filterAdjustCount;
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

    int startRpm = clampInt(cfg.rpmStartRpm, 0, 12000);
    if (maxRpmForBar <= startRpm)
    {
        startRpm = max(0, maxRpmForBar - 1);
    }
    g_ledRenderDebug.lastStartRpm = startRpm;

    float fraction = 0.0f;
    if (filteredRpm > startRpm && maxRpmForBar > startRpm)
    {
        fraction = (float)(filteredRpm - startRpm) / (float)(maxRpmForBar - startRpm);
        if (fraction > 1.0f)
        {
            fraction = 1.0f;
        }
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
    g_ledRenderDebug.lastDisplayedLeds = ledsOn;

    static unsigned long lastBlink = 0;
    static bool blinkState = false;
    unsigned long now = millis();

    bool shiftBlink = ((cfg.mode == LED_MODE_F1 || cfg.mode == LED_MODE_AGGRESSIVE || cfg.mode == LED_MODE_GT3) && fraction >= blinkStart);
    g_ledRenderDebug.lastShiftBlink = shiftBlink;

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
    uint32_t frame[NUM_LEDS];
    clearFrame(frame);

    if (cfg.mode == LED_MODE_GT3)
    {
        renderGt3Pattern(frame, fraction, blinkStart, blinkState, displayBlink);
    }
    else
    {
        renderLinearPattern(frame, ledsOn, cfg.mode == LED_MODE_AGGRESSIVE && fraction >= blinkStart, shiftBlink, blinkState, greenEnd, yellowEnd, displayBlink);
    }

    const bool frameChanged = frameDiffersFromCache(frame);
    const bool blinkChanged = displayBlink != g_lastDisplayBlinkState;

    if (frameChanged || g_forceNextStripShow)
    {
        pushFrameToStrip(frame);
    }
    else
    {
        ++g_ledRenderDebug.frameSkipCount;
    }

    if (blinkChanged)
    {
        displaySetShiftBlink(displayBlink);
        g_lastDisplayBlinkState = displayBlink;
    }
}

namespace
{
    TaskHandle_t g_ledBarTaskHandle = nullptr;
    volatile bool g_ledBarTaskRunning = false;

    // Target 100 Hz. At fewer than 10 ms per frame we'd just be refreshing the
    // NeoPixel bus faster than the eye can see without any data win; 10 ms is
    // also comfortably above the WS2812 latch time so we never race the strip.
    constexpr uint32_t LED_TASK_TICK_MS = 10;

    void ledRenderOnce()
    {
        ledBarRefreshBrightness();

        if (g_brightnessPreviewActive)
        {
            renderPreviewFade();
            return;
        }

        if (g_ledDiagnosticMode != LedDiagnosticMode::Live)
        {
            ++g_ledRenderDebug.renderCalls;
            uint32_t frame[NUM_LEDS];
            renderDiagnosticPattern(frame);
            const bool frameChanged = frameDiffersFromCache(frame);
            if (frameChanged || g_forceNextStripShow)
            {
                pushFrameToStrip(frame);
            }
            else
            {
                ++g_ledRenderDebug.frameSkipCount;
            }

            if (g_lastDisplayBlinkState)
            {
                displaySetShiftBlink(false);
                g_lastDisplayBlinkState = false;
            }
            g_ledRenderDebug.lastShiftBlink = false;
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
                float t = (float)elapsed / (float)TEST_SWEEP_DURATION;
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

    void ledBarTaskBody(void *)
    {
        TickType_t lastWake = xTaskGetTickCount();
        for (;;)
        {
            ledRenderOnce();
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(LED_TASK_TICK_MS));
        }
    }
}

void ledBarLoop()
{
    // When the dedicated task has taken over we must NOT also render from
    // loop() — two renderers would fight over the strip buffer.
    if (g_ledBarTaskRunning)
    {
        return;
    }
    ledRenderOnce();
}

void startLedBarTask()
{
    if (g_ledBarTaskRunning)
    {
        return;
    }
    g_ledBarTaskRunning = true;

    // Pinned to Core 1 so it shares a cache with the Arduino loop (config
    // reads etc.) but runs as its own task with a higher priority than
    // loop() (which is priority 1). Priority 2 is high enough that render
    // jitter stays under a frame time, low enough that WiFi + BLE are not
    // starved.
    xTaskCreatePinnedToCore(
        ledBarTaskBody,
        "ledBar",
        4096,
        nullptr,
        2,
        &g_ledBarTaskHandle,
        1);
}
