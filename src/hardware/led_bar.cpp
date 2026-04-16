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
    TaskHandle_t g_ledBarTaskHandle = nullptr;
    volatile bool g_ledBarTaskRunning = false;

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
        BrightnessPreview = 8,
        TestDeterministic = 9,
        EffectSimReady = 10,
        EffectSimStandby = 11,
        EffectSimError = 12,
        EffectLogo = 13,
        EffectLogoLeave = 14
    };

    enum class LedRenderWriter : uint8_t
    {
        None = 0,
        Live = 1,
        Diagnostic = 2,
        SweepExpressive = 3,
        SweepDeterministic = 4,
        EffectBrightnessPreview = 5,
        EffectLogo = 6,
        EffectLogoLeaving = 7,
        EffectSimReady = 8,
        EffectSimStandby = 9,
        EffectSimError = 10
    };

    enum class LedEffectType : uint8_t
    {
        None = 0,
        BrightnessPreview = 1,
        Logo = 2,
        LogoLeaving = 3,
        SimReady = 4,
        SimStandby = 5,
        SimError = 6
    };

    struct LedEffectState
    {
        LedEffectType type = LedEffectType::None;
        uint32_t requestSerial = 0;
        unsigned long startMs = 0;
        bool baseFrameValid = false;
        uint32_t baseFrame[NUM_LEDS]{};
    };

    const unsigned long PREVIEW_HOLD_MS = 2500;
    const unsigned long PREVIEW_FADE_MS = 900;
    constexpr unsigned long LOGO_EFFECT_MS = 1800;
    constexpr unsigned long LEAVING_FADE_MS = 600;
    constexpr unsigned long LEAVING_HOLD_MS = 120;
    constexpr unsigned long SIM_READY_EFFECT_MS = 900;
    constexpr unsigned long SIM_STANDBY_EFFECT_MS = 700;
    constexpr unsigned long SIM_ERROR_EFFECT_MS = 720;
    uint32_t g_ledFrameCache[NUM_LEDS] = {};
    bool g_ledFrameCacheValid = false;
    bool g_forceNextStripShow = true;
    bool g_lastDisplayBlinkState = false;
    LedDiagnosticMode g_ledDiagnosticMode = LedDiagnosticMode::Live;
    int g_ledMedianSamples[3] = {0, 0, 0};
    uint8_t g_ledMedianCount = 0;
    uint8_t g_ledMedianIndex = 0;
    uint8_t g_appliedStripBrightness = DEFAULT_BRIGHTNESS;
    portMUX_TYPE g_ledEffectMux = portMUX_INITIALIZER_UNLOCKED;
    LedEffectType g_pendingEffectType = LedEffectType::None;
    uint32_t g_pendingEffectSerial = 0;
    LedEffectState g_effectState{};
    LedRenderEvent g_renderHistory[LED_RENDER_HISTORY_LEN]{};
    uint8_t g_renderHistoryCount = 0;
    uint8_t g_renderHistoryHead = 0;
    LedRenderEvent g_lastRenderEvent{};
    uint32_t g_ledExternalWriteAttempts = 0;
    uint32_t g_ledSnapshotChangedDuringRender = 0;
    LedRenderWriter g_lastWriter = LedRenderWriter::None;
    LedTestSweepMode g_testSweepMode = LedTestSweepMode::Expressive;

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

    void setLastWriter(LedRenderWriter writer)
    {
        g_lastWriter = writer;
    }

    const char *writerName(LedRenderWriter writer)
    {
        switch (writer)
        {
        case LedRenderWriter::Live:
            return "live";
        case LedRenderWriter::Diagnostic:
            return "diagnostic";
        case LedRenderWriter::SweepExpressive:
            return "sweep-show";
        case LedRenderWriter::SweepDeterministic:
            return "sweep-diagnostic";
        case LedRenderWriter::EffectBrightnessPreview:
            return "effect-brightness";
        case LedRenderWriter::EffectLogo:
            return "effect-logo";
        case LedRenderWriter::EffectLogoLeaving:
            return "effect-leaving";
        case LedRenderWriter::EffectSimReady:
            return "effect-sim-ready";
        case LedRenderWriter::EffectSimStandby:
            return "effect-sim-standby";
        case LedRenderWriter::EffectSimError:
            return "effect-sim-error";
        case LedRenderWriter::None:
        default:
            return "none";
        }
    }

    void pushRenderHistory(const LedRenderEvent &event)
    {
        g_lastRenderEvent = event;
        g_renderHistory[g_renderHistoryHead] = event;
        g_renderHistoryHead = static_cast<uint8_t>((g_renderHistoryHead + 1U) % LED_RENDER_HISTORY_LEN);
        if (g_renderHistoryCount < LED_RENDER_HISTORY_LEN)
        {
            ++g_renderHistoryCount;
        }
    }

    uint32_t hashFrame(const uint32_t *frame)
    {
        uint32_t hash = 2166136261UL;
        for (int i = 0; i < NUM_LEDS; ++i)
        {
            hash ^= frame[i];
            hash *= 16777619UL;
        }
        return hash;
    }

    void recordRenderEvent(LedRenderWriter writer,
                           const TelemetryRenderSnapshot &snapshot,
                           const uint32_t *frame,
                           bool shown)
    {
        LedRenderEvent event{};
        event.timestampMs = millis();
        event.writer = static_cast<uint8_t>(writer);
        event.source = snapshot.source;
        event.rpm = snapshot.rpm;
        event.maxRpm = snapshot.maxSeenRpm;
        event.pitLimiter = snapshot.pitLimiter;
        event.frameHash = hashFrame(frame);
        event.brightness = g_appliedStripBrightness;
        event.shown = shown;
        portENTER_CRITICAL(&g_ledEffectMux);
        pushRenderHistory(event);
        portEXIT_CRITICAL(&g_ledEffectMux);
        setLastWriter(writer);
    }

    void copyFrame(uint32_t *dest, const uint32_t *src)
    {
        for (int i = 0; i < NUM_LEDS; ++i)
        {
            dest[i] = src[i];
        }
    }

    void queueEffect(LedEffectType type, bool animationActive, bool brightnessPreview)
    {
        portENTER_CRITICAL(&g_ledEffectMux);
        g_pendingEffectType = type;
        ++g_pendingEffectSerial;
        portEXIT_CRITICAL(&g_ledEffectMux);
        g_animationActive = animationActive;
        g_brightnessPreviewActive = brightnessPreview;
        invalidateLedFrameCacheInternal();
    }

    bool startPendingEffectIfAny(unsigned long nowMs)
    {
        LedEffectType pendingType = LedEffectType::None;
        uint32_t pendingSerial = 0;
        portENTER_CRITICAL(&g_ledEffectMux);
        pendingType = g_pendingEffectType;
        pendingSerial = g_pendingEffectSerial;
        portEXIT_CRITICAL(&g_ledEffectMux);

        if (pendingType == LedEffectType::None || pendingSerial == g_effectState.requestSerial)
        {
            return false;
        }

        g_effectState = LedEffectState{};
        g_effectState.type = pendingType;
        g_effectState.requestSerial = pendingSerial;
        g_effectState.startMs = nowMs;
        g_effectState.baseFrameValid = g_ledFrameCacheValid;
        if (g_effectState.baseFrameValid)
        {
            copyFrame(g_effectState.baseFrame, g_ledFrameCache);
        }
        portENTER_CRITICAL(&g_ledEffectMux);
        // Consume the queued request so a finished one-shot effect does not
        // restart forever on the next render tick.
        if (g_pendingEffectSerial == pendingSerial)
        {
            g_pendingEffectType = LedEffectType::None;
        }
        portEXIT_CRITICAL(&g_ledEffectMux);
        g_forceNextStripShow = true;
        return true;
    }

    void clearEffectIfFinished()
    {
        if (g_effectState.type == LedEffectType::None)
        {
            g_animationActive = false;
            g_brightnessPreviewActive = false;
            return;
        }

        g_effectState = LedEffectState{};
        g_animationActive = false;
        g_brightnessPreviewActive = false;
        g_forceNextStripShow = true;
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
        const TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        if (g_ledBarTaskRunning && g_ledBarTaskHandle != nullptr && currentTask != g_ledBarTaskHandle)
        {
            ++g_ledExternalWriteAttempts;
        }
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

    uint32_t zoneColor(float pos, float greenEnd, float yellowEnd)
    {
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

    void renderLinearPattern(uint32_t *frame,
                             int ledsOn,
                             bool aggressiveFullBlink,
                             bool shiftBlink,
                             bool blinkState,
                             float greenEnd,
                             float yellowEnd,
                             bool &displayBlink)
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

    void renderGt3Pattern(uint32_t *frame,
                          float fraction,
                          float greenEnd,
                          float yellowEnd,
                          float blinkStart,
                          bool blinkState,
                          bool pitLimiterActive,
                          bool &displayBlink)
    {
        if (pitLimiterActive)
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
            const uint32_t color = on ? zoneColor(pos, greenEnd, yellowEnd) : 0;
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

    float computeDeterministicSweepPct(float t)
    {
        t = constrain(t, 0.0f, 1.0f);
        if (t < 0.45f)
        {
            const float tt = t / 0.45f;
            return tt * tt * (3.0f - 2.0f * tt);
        }
        if (t < 0.65f)
        {
            return 1.0f;
        }
        const float tt = (t - 0.65f) / 0.35f;
        return 1.0f - (tt * tt * (3.0f - 2.0f * tt));
    }

    float smoothStep(float t)
    {
        t = constrain(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void renderLogoIntensityFrame(uint32_t *frame, float intensity)
    {
        constexpr uint8_t lbR = 0, lbG = 120, lbB = 255;
        constexpr uint8_t dbR = 0, dbG = 0, dbB = 120;
        constexpr uint8_t rR = 255, rG = 0, rB = 0;

        intensity = constrain(intensity, 0.0f, 1.0f);
        clearFrame(frame);

        const int segLen = max(2, NUM_LEDS / 3);
        for (int i = 0; i < NUM_LEDS; ++i)
        {
            uint8_t br = 0;
            uint8_t bg = 0;
            uint8_t bb = 0;
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
            frame[i] = strip.Color(static_cast<uint8_t>(br * intensity),
                                   static_cast<uint8_t>(bg * intensity),
                                   static_cast<uint8_t>(bb * intensity));
        }
    }

    void scaleFrame(uint32_t *frame, const uint32_t *source, float scale)
    {
        scale = constrain(scale, 0.0f, 1.0f);
        for (int i = 0; i < NUM_LEDS; ++i)
        {
            const uint32_t col = source[i];
            const uint8_t gVal = (col >> 16) & 0xFF;
            const uint8_t rVal = (col >> 8) & 0xFF;
            const uint8_t bVal = col & 0xFF;
            frame[i] = strip.Color(static_cast<uint8_t>(rVal * scale),
                                   static_cast<uint8_t>(gVal * scale),
                                   static_cast<uint8_t>(bVal * scale));
        }
    }

    bool renderActiveEffect(unsigned long nowMs, uint32_t *frame, LedRenderWriter &writer, LedFrameMode &mode)
    {
        if (g_effectState.type == LedEffectType::None)
        {
            return false;
        }

        clearFrame(frame);
        const unsigned long elapsed = nowMs - g_effectState.startMs;

        switch (g_effectState.type)
        {
        case LedEffectType::BrightnessPreview:
        {
            float intensity = max(0.02f, g_appliedStripBrightness / 255.0f);
            if (elapsed >= PREVIEW_HOLD_MS)
            {
                const float fade = 1.0f - ((elapsed - PREVIEW_HOLD_MS) / static_cast<float>(PREVIEW_FADE_MS));
                intensity *= constrain(fade, 0.0f, 1.0f);
            }
            renderLogoIntensityFrame(frame, intensity);
            writer = LedRenderWriter::EffectBrightnessPreview;
            mode = LedFrameMode::BrightnessPreview;
            if (elapsed >= (PREVIEW_HOLD_MS + PREVIEW_FADE_MS))
            {
                clearEffectIfFinished();
            }
            return true;
        }
        case LedEffectType::Logo:
        {
            const float t = min(1.0f, elapsed / static_cast<float>(LOGO_EFFECT_MS));
            const float intensity = (t < 0.5f) ? smoothStep(t / 0.5f) : (1.0f - smoothStep((t - 0.5f) / 0.5f));
            renderLogoIntensityFrame(frame, intensity);
            writer = LedRenderWriter::EffectLogo;
            mode = LedFrameMode::EffectLogo;
            if (elapsed >= LOGO_EFFECT_MS)
            {
                clearEffectIfFinished();
            }
            return true;
        }
        case LedEffectType::LogoLeaving:
        {
            if (elapsed < LEAVING_FADE_MS)
            {
                if (g_effectState.baseFrameValid)
                {
                    const float scale = 1.0f - smoothStep(elapsed / static_cast<float>(LEAVING_FADE_MS));
                    scaleFrame(frame, g_effectState.baseFrame, scale);
                }
                writer = LedRenderWriter::EffectLogoLeaving;
                mode = LedFrameMode::EffectLogoLeave;
                return true;
            }
            if (elapsed < (LEAVING_FADE_MS + LEAVING_HOLD_MS))
            {
                writer = LedRenderWriter::EffectLogoLeaving;
                mode = LedFrameMode::EffectLogoLeave;
                return true;
            }

            const unsigned long logoElapsed = elapsed - LEAVING_FADE_MS - LEAVING_HOLD_MS;
            const float t = min(1.0f, logoElapsed / static_cast<float>(LOGO_EFFECT_MS));
            const float intensity = (t < 0.5f) ? smoothStep(t / 0.5f) : (1.0f - smoothStep((t - 0.5f) / 0.5f));
            renderLogoIntensityFrame(frame, intensity);
            writer = LedRenderWriter::EffectLogoLeaving;
            mode = LedFrameMode::EffectLogoLeave;
            if (logoElapsed >= LOGO_EFFECT_MS)
            {
                clearEffectIfFinished();
            }
            return true;
        }
        case LedEffectType::SimReady:
        {
            const float t = min(1.0f, elapsed / static_cast<float>(SIM_READY_EFFECT_MS));
            const int ledsOn = static_cast<int>(round(smoothStep(min(1.0f, t / 0.55f)) * NUM_LEDS));
            const float fade = (t < 0.55f) ? 1.0f : (1.0f - smoothStep((t - 0.55f) / 0.45f));
            const uint32_t color = strip.Color(0, static_cast<uint8_t>(180 * fade), static_cast<uint8_t>(255 * fade));
            for (int i = 0; i < ledsOn && i < NUM_LEDS; ++i)
            {
                frame[i] = color;
            }
            writer = LedRenderWriter::EffectSimReady;
            mode = LedFrameMode::EffectSimReady;
            if (elapsed >= SIM_READY_EFFECT_MS)
            {
                clearEffectIfFinished();
            }
            return true;
        }
        case LedEffectType::SimStandby:
        {
            const float t = min(1.0f, elapsed / static_cast<float>(SIM_STANDBY_EFFECT_MS));
            if (g_effectState.baseFrameValid)
            {
                scaleFrame(frame, g_effectState.baseFrame, 1.0f - smoothStep(t));
                const int keep = static_cast<int>(round((1.0f - t) * NUM_LEDS));
                const int left = max(0, (NUM_LEDS - keep) / 2);
                const int right = min(NUM_LEDS, left + keep);
                for (int i = 0; i < NUM_LEDS; ++i)
                {
                    if (i < left || i >= right)
                    {
                        frame[i] = 0;
                    }
                }
            }
            writer = LedRenderWriter::EffectSimStandby;
            mode = LedFrameMode::EffectSimStandby;
            if (elapsed >= SIM_STANDBY_EFFECT_MS)
            {
                clearEffectIfFinished();
            }
            return true;
        }
        case LedEffectType::SimError:
        {
            const bool on = ((elapsed / 120UL) % 2UL) == 0UL;
            const uint32_t color = on ? strip.Color(255, 0, 0) : strip.Color(30, 0, 0);
            for (int i = 0; i < NUM_LEDS; ++i)
            {
                frame[i] = color;
            }
            writer = LedRenderWriter::EffectSimError;
            mode = LedFrameMode::EffectSimError;
            if (elapsed >= SIM_ERROR_EFFECT_MS)
            {
                clearEffectIfFinished();
            }
            return true;
        }
        case LedEffectType::None:
        default:
            break;
        }

        return false;
    }

}

void rememberPreviewPixels()
{
    // Preview frames are now rendered exclusively by the LED task.
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
    g_lastWriter = LedRenderWriter::None;
    g_testSweepMode = LedTestSweepMode::Expressive;
    g_effectState = LedEffectState{};
    g_pendingEffectType = LedEffectType::None;
    g_pendingEffectSerial = 0;
    g_renderHistoryCount = 0;
    g_renderHistoryHead = 0;
    g_lastRenderEvent = LedRenderEvent{};
    g_ledExternalWriteAttempts = 0;
    g_ledSnapshotChangedDuringRender = 0;
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
    g_animationActive = false;
    portENTER_CRITICAL(&g_ledEffectMux);
    g_pendingEffectType = LedEffectType::None;
    g_effectState = LedEffectState{};
    portEXIT_CRITICAL(&g_ledEffectMux);
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
    case LedFrameMode::TestDeterministic:
        return "test-deterministic";
    case LedFrameMode::EffectSimReady:
        return "sim-ready";
    case LedFrameMode::EffectSimStandby:
        return "sim-standby";
    case LedFrameMode::EffectSimError:
        return "sim-error";
    case LedFrameMode::EffectLogo:
        return "logo";
    case LedFrameMode::EffectLogoLeave:
        return "logo-leave";
    case LedFrameMode::None:
    default:
        return "none";
    }
}

uint8_t ledBarGetAppliedBrightness()
{
    return g_appliedStripBrightness;
}

const char *ledBarGetLastWriterName()
{
    return writerName(g_lastWriter);
}

void ledBarStartTestSweep(LedTestSweepMode mode, int maxRpm)
{
    g_testSweepMode = mode;
    g_testActive = true;
    g_testStartMs = millis();
    g_testMaxRpm = max(2000, maxRpm);
    g_brightnessPreviewActive = false;
    g_animationActive = false;
    portENTER_CRITICAL(&g_ledEffectMux);
    g_pendingEffectType = LedEffectType::None;
    g_effectState = LedEffectState{};
    portEXIT_CRITICAL(&g_ledEffectMux);
    invalidateLedFrameCacheInternal();
}

bool ledBarTestSweepActive()
{
    return g_testActive;
}

bool ledBarDeterministicSweepActive()
{
    return g_testActive && g_testSweepMode == LedTestSweepMode::Deterministic;
}

void ledBarRequestLogoPreview()
{
    queueEffect(LedEffectType::BrightnessPreview, false, true);
}

void ledBarRequestBrightnessPreview()
{
    ledBarRequestLogoPreview();
}

void ledBarRequestLogoAnimation()
{
    queueEffect(LedEffectType::Logo, true, false);
}

void ledBarRequestLeavingAnimation()
{
    queueEffect(LedEffectType::LogoLeaving, true, false);
}

void ledBarRequestSimSessionTransition(SimSessionTransitionType transition)
{
    switch (transition)
    {
    case SimSessionTransitionType::BecameLive:
        queueEffect(LedEffectType::SimReady, false, false);
        return;
    case SimSessionTransitionType::BecameWaiting:
        queueEffect(LedEffectType::SimStandby, false, false);
        return;
    case SimSessionTransitionType::BecameError:
        queueEffect(LedEffectType::SimError, false, false);
        return;
    case SimSessionTransitionType::None:
    default:
        return;
    }
}

bool ledBarEffectActive()
{
    LedEffectType pendingType = LedEffectType::None;
    uint32_t pendingSerial = 0;
    portENTER_CRITICAL(&g_ledEffectMux);
    pendingType = g_pendingEffectType;
    pendingSerial = g_pendingEffectSerial;
    const bool active = g_effectState.type != LedEffectType::None || (pendingType != LedEffectType::None && pendingSerial != g_effectState.requestSerial);
    portEXIT_CRITICAL(&g_ledEffectMux);
    return active;
}

LedRenderHistoryInfo ledBarGetRenderHistoryInfo()
{
    LedRenderHistoryInfo info{};
    portENTER_CRITICAL(&g_ledEffectMux);
    info.lastEvent = g_lastRenderEvent;
    info.count = g_renderHistoryCount;
    info.externalWriteAttempts = g_ledExternalWriteAttempts;
    info.snapshotChangedDuringRender = g_ledSnapshotChangedDuringRender;
    info.deterministicSweepActive = ledBarDeterministicSweepActive();
    info.lastWriter = static_cast<uint8_t>(g_lastWriter);
    for (uint8_t i = 0; i < g_renderHistoryCount; ++i)
    {
        const uint8_t index = static_cast<uint8_t>((g_renderHistoryHead + LED_RENDER_HISTORY_LEN - g_renderHistoryCount + i) % LED_RENDER_HISTORY_LEN);
        info.history[i] = g_renderHistory[index];
    }
    portEXIT_CRITICAL(&g_ledEffectMux);
    return info;
}

namespace
{
    constexpr uint32_t LED_TASK_TICK_MS = 10;

    void finalizeFrame(const uint32_t *frame,
                       LedRenderWriter writer,
                       const TelemetryRenderSnapshot &snapshot,
                       bool displayBlink)
    {
        const bool frameChanged = frameDiffersFromCache(frame);
        const bool blinkChanged = displayBlink != g_lastDisplayBlinkState;
        const uint32_t currentSnapshotVersion = telemetryGetRenderSnapshotVersion();
        if (snapshot.version > 0 && currentSnapshotVersion != snapshot.version)
        {
            ++g_ledSnapshotChangedDuringRender;
        }

        if (frameChanged || g_forceNextStripShow)
        {
            pushFrameToStrip(frame);
            recordRenderEvent(writer, snapshot, frame, true);
        }
        else
        {
            ++g_ledRenderDebug.frameSkipCount;
            recordRenderEvent(writer, snapshot, frame, false);
        }

        if (blinkChanged)
        {
            displaySetShiftBlink(displayBlink);
            g_lastDisplayBlinkState = displayBlink;
        }
    }

    void renderRpmBar(const TelemetryRenderSnapshot &snapshot, int rpm)
    {
        ++g_ledRenderDebug.renderCalls;

        rpm = max(0, rpm);
        const bool bypassFilter = g_testActive;
        const int filteredRpm = filterLedRpm(rpm, bypassFilter);
        g_ledRenderDebug.lastRawRpm = rpm;
        g_ledRenderDebug.lastFilteredRpm = filteredRpm;
        if (filteredRpm != rpm)
        {
            ++g_ledRenderDebug.filterAdjustCount;
        }

        int maxRpmForBar = 2000;
        if (cfg.autoScaleMaxRpm)
        {
            maxRpmForBar = max(snapshot.maxSeenRpm, 2000);
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
            fraction = (filteredRpm - startRpm) / static_cast<float>(maxRpmForBar - startRpm);
            fraction = constrain(fraction, 0.0f, 1.0f);
        }

        float greenEnd = constrain(cfg.greenEndPct / 100.0f, 0.0f, 1.0f);
        float yellowEnd = constrain(cfg.yellowEndPct / 100.0f, greenEnd, 1.0f);
        float redEnd = constrain(cfg.redEndPct / 100.0f, yellowEnd, 1.0f);
        float blinkStart = constrain(cfg.blinkStartPct / 100.0f, 0.0f, 1.0f);
        const float blinkTrigger = max(redEnd, blinkStart);

        int ledsOn = static_cast<int>(round(fraction * NUM_LEDS));
        g_ledRenderDebug.lastDisplayedLeds = ledsOn;

        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        const unsigned long now = millis();
        const bool shiftBlink = ((cfg.mode == LED_MODE_F1 || cfg.mode == LED_MODE_AGGRESSIVE || cfg.mode == LED_MODE_GT3) && fraction >= blinkTrigger);
        g_ledRenderDebug.lastShiftBlink = shiftBlink;
        if (shiftBlink && now - lastBlink > 100)
        {
            lastBlink = now;
            blinkState = !blinkState;
        }
        else if (!shiftBlink)
        {
            blinkState = false;
        }

        bool displayBlink = false;
        if (cfg.mode == LED_MODE_AGGRESSIVE && fraction >= blinkTrigger)
        {
            ledsOn = NUM_LEDS;
            displayBlink = blinkState;
        }

        uint32_t frame[NUM_LEDS];
        clearFrame(frame);
        if (cfg.mode == LED_MODE_GT3)
        {
            renderGt3Pattern(frame, fraction, greenEnd, yellowEnd, blinkTrigger, blinkState, snapshot.pitLimiter, displayBlink);
        }
        else
        {
            renderLinearPattern(frame,
                                ledsOn,
                                cfg.mode == LED_MODE_AGGRESSIVE && fraction >= blinkTrigger,
                                shiftBlink,
                                blinkState,
                                greenEnd,
                                yellowEnd,
                                displayBlink);
        }

        TelemetryRenderSnapshot debugSnapshot = snapshot;
        debugSnapshot.rpm = filteredRpm;
        debugSnapshot.maxSeenRpm = maxRpmForBar;
        finalizeFrame(frame, g_testActive ? (g_testSweepMode == LedTestSweepMode::Deterministic ? LedRenderWriter::SweepDeterministic : LedRenderWriter::SweepExpressive) : LedRenderWriter::Live, debugSnapshot, displayBlink);
    }

    void ledRenderOnce()
    {
        ledBarRefreshBrightness();
        const unsigned long now = millis();
        startPendingEffectIfAny(now);

        TelemetryRenderSnapshot snapshot = telemetryCopyRenderSnapshot();
        if (snapshot.version == 0)
        {
            snapshot.source = ActiveTelemetrySource::None;
        }

        if (g_effectState.type != LedEffectType::None)
        {
            ++g_ledRenderDebug.renderCalls;
            uint32_t frame[NUM_LEDS];
            LedRenderWriter writer = LedRenderWriter::None;
            LedFrameMode mode = LedFrameMode::None;
            if (renderActiveEffect(now, frame, writer, mode))
            {
                g_ledRenderDebug.pitLimiterOnly = false;
                setLastRenderMode(mode);
                g_ledRenderDebug.lastShiftBlink = false;
                finalizeFrame(frame, writer, snapshot, false);
                return;
            }
        }

        if (g_ledDiagnosticMode != LedDiagnosticMode::Live)
        {
            ++g_ledRenderDebug.renderCalls;
            uint32_t frame[NUM_LEDS];
            renderDiagnosticPattern(frame);
            g_ledRenderDebug.lastShiftBlink = false;
            finalizeFrame(frame, LedRenderWriter::Diagnostic, snapshot, false);
            return;
        }

        if (g_testActive)
        {
            const unsigned long elapsed = now - g_testStartMs;
            if (elapsed >= TEST_SWEEP_DURATION)
            {
                g_testActive = false;
                invalidateLedFrameCacheInternal();
            }
            else
            {
                const float t = elapsed / static_cast<float>(TEST_SWEEP_DURATION);
                const float pct = (g_testSweepMode == LedTestSweepMode::Deterministic) ? computeDeterministicSweepPct(t) : computeTestSweepPct(t);
                TelemetryRenderSnapshot sweepSnapshot = snapshot;
                sweepSnapshot.rpm = static_cast<int>(pct * g_testMaxRpm);
                renderRpmBar(sweepSnapshot, sweepSnapshot.rpm);
                setLastRenderMode(g_testSweepMode == LedTestSweepMode::Deterministic ? LedFrameMode::TestDeterministic : g_ledRenderDebug.lastRenderMode == static_cast<uint8_t>(LedFrameMode::Gt3) ? LedFrameMode::Gt3 : LedFrameMode::Linear);
                return;
            }
        }

        renderRpmBar(snapshot, snapshot.rpm);
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
