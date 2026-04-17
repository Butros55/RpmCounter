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
    constexpr float LED_LEVEL_HYSTERESIS_UP = 0.38f;
    constexpr float LED_LEVEL_HYSTERESIS_DOWN = 0.18f;
    constexpr unsigned long SESSION_EFFECT_COOLDOWN_MS = 5000;
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
    int g_stableDisplayLevel = 0;
    int g_stableDisplayLevelCount = 0;
    uint8_t g_stableDisplayMode = 0;
    float g_stableDisplayRawLevel = 0.0f;
    LedEffectType g_lastQueuedEffectType = LedEffectType::None;
    uint32_t g_sessionEffectRequestCount = 0;
    uint32_t g_sessionEffectSuppressedCount = 0;
    unsigned long g_lastSessionEffectMs = 0;

    constexpr uint8_t DISPLAY_MODE_LINEAR = 1;
    constexpr uint8_t DISPLAY_MODE_GT3 = 2;

    uint32_t toStripColor(const RgbColor &c);

    bool ledFastResponseForSource(ActiveTelemetrySource source)
    {
        return source != ActiveTelemetrySource::None;
    }

    uint32_t redZoneColor()
    {
        return toStripColor(effectiveRedColor());
    }

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

    void resetDisplayLevelState()
    {
        g_stableDisplayLevel = 0;
        g_stableDisplayLevelCount = 0;
        g_stableDisplayMode = 0;
        g_stableDisplayRawLevel = 0.0f;
        g_ledRenderDebug.lastDesiredLevel = 0;
        g_ledRenderDebug.lastDisplayedLevel = 0;
        g_ledRenderDebug.lastLevelCount = 0;
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

    const char *effectName(LedEffectType effect)
    {
        switch (effect)
        {
        case LedEffectType::BrightnessPreview:
            return "brightness-preview";
        case LedEffectType::Logo:
            return "logo";
        case LedEffectType::LogoLeaving:
            return "logo-leaving";
        case LedEffectType::SimReady:
            return "sim-ready";
        case LedEffectType::SimStandby:
            return "sim-standby";
        case LedEffectType::SimError:
            return "sim-error";
        case LedEffectType::None:
        default:
            return "none";
        }
    }

    int activeLedCount()
    {
        return clampInt(cfg.activeLedCount, 1, NUM_LEDS);
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
        g_lastQueuedEffectType = type;
        portEXIT_CRITICAL(&g_ledEffectMux);
        g_animationActive = animationActive;
        g_brightnessPreviewActive = brightnessPreview;
        invalidateLedFrameCacheInternal();
    }

    bool isSimSessionEffect(LedEffectType type)
    {
        return type == LedEffectType::SimReady || type == LedEffectType::SimStandby || type == LedEffectType::SimError;
    }

    void cancelSimSessionEffects()
    {
        bool changed = false;
        portENTER_CRITICAL(&g_ledEffectMux);
        if (isSimSessionEffect(g_pendingEffectType))
        {
            g_pendingEffectType = LedEffectType::None;
            changed = true;
        }
        if (isSimSessionEffect(g_effectState.type))
        {
            g_effectState = LedEffectState{};
            changed = true;
        }
        portEXIT_CRITICAL(&g_ledEffectMux);
        if (changed)
        {
            g_forceNextStripShow = true;
        }
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

    int filterLedRpm(int rpm, bool bypass, bool fastResponse)
    {
        if (bypass || fastResponse)
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

    uint32_t scalePackedColor(uint32_t color, float scale)
    {
        scale = constrain(scale, 0.0f, 1.0f);
        const uint8_t gVal = (color >> 16) & 0xFF;
        const uint8_t rVal = (color >> 8) & 0xFF;
        const uint8_t bVal = color & 0xFF;
        return strip.Color(static_cast<uint8_t>(rVal * scale),
                           static_cast<uint8_t>(gVal * scale),
                           static_cast<uint8_t>(bVal * scale));
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
        return redZoneColor();
    }

    struct LedZoneProfile
    {
        float greenEnd = 0.0f;
        float yellowEnd = 0.0f;
        float redFillEnd = 1.0f;
        float blinkStart = 1.0f;
    };

    LedZoneProfile buildLedZoneProfile()
    {
        const float greenPct = constrain(cfg.greenEndPct / 100.0f, 0.0f, 1.0f);
        const float yellowPct = constrain(cfg.yellowEndPct / 100.0f, greenPct, 1.0f);
        const float redPct = constrain(cfg.redEndPct / 100.0f, yellowPct, 1.0f);
        const float blinkPct = constrain(cfg.blinkStartPct / 100.0f, 0.0f, 1.0f);
        const float safeRedPct = redPct > 0.01f ? redPct : 0.01f;

        LedZoneProfile profile;
        profile.greenEnd = constrain(greenPct / safeRedPct, 0.0f, 1.0f);
        profile.yellowEnd = constrain(yellowPct / safeRedPct, profile.greenEnd, 1.0f);
        profile.redFillEnd = constrain(blinkPct * safeRedPct, 0.0f, 1.0f);
        profile.blinkStart = blinkPct;
        return profile;
    }

    float computeLedDisplayFraction(float actualFraction, const LedZoneProfile &profile)
    {
        if (actualFraction <= 0.0f)
        {
            return 0.0f;
        }
        if (profile.redFillEnd <= 0.001f)
        {
            return 1.0f;
        }
        return constrain(actualFraction / profile.redFillEnd, 0.0f, 1.0f);
    }

    unsigned long blinkIntervalMsFromPct(int blinkSpeedPct)
    {
        const int clampedPct = clampInt(blinkSpeedPct, 0, 100);
        if (clampedPct <= 0)
        {
            return 0;
        }
        if (clampedPct >= 100)
        {
            return 1;
        }

        const float normalized = clampedPct / 99.0f;
        const float intervalMs = 480.0f - (normalized * 440.0f);
        return static_cast<unsigned long>(lroundf(constrain(intervalMs, 40.0f, 480.0f)));
    }

    int directDisplayedLevel(float rawLevel, int levelCount)
    {
        if (rawLevel <= 0.0f)
        {
            return 0;
        }
        return clampInt(static_cast<int>(ceilf(rawLevel)), 0, levelCount);
    }

    int computeStableDisplayedLevel(float displayFraction, int levelCount, uint8_t displayMode, bool bypass, bool fastResponse)
    {
        if (levelCount <= 0)
        {
            resetDisplayLevelState();
            return 0;
        }

        const float rawLevel = constrain(displayFraction, 0.0f, 1.0f) * static_cast<float>(levelCount);
        const int desiredLevel = fastResponse ? directDisplayedLevel(rawLevel, levelCount)
                                              : clampInt(static_cast<int>(lroundf(rawLevel)), 0, levelCount);
        g_stableDisplayRawLevel = rawLevel;

        if (bypass || fastResponse || g_stableDisplayMode != displayMode || g_stableDisplayLevelCount != levelCount)
        {
            g_stableDisplayLevel = desiredLevel;
            g_stableDisplayLevelCount = levelCount;
            g_stableDisplayMode = displayMode;
        }
        else
        {
            g_stableDisplayLevel =
                applyDisplayLevelHysteresis(g_stableDisplayLevel, rawLevel, levelCount, LED_LEVEL_HYSTERESIS_UP, LED_LEVEL_HYSTERESIS_DOWN);
        }

        g_ledRenderDebug.lastDesiredLevel = desiredLevel;
        g_ledRenderDebug.lastDisplayedLevel = g_stableDisplayLevel;
        g_ledRenderDebug.lastLevelCount = levelCount;
        return g_stableDisplayLevel;
    }

    void overlayPitLimiterMarkers(uint32_t *frame, bool enabled)
    {
        if (!enabled)
        {
            return;
        }

        const int ledCount = activeLedCount();
        const uint32_t pitColor = toStripColor(GT3_PIT_COLOR);
        for (int offset = 0; offset < GT3_PIT_MARKER_COUNT && offset < ledCount; ++offset)
        {
            frame[offset] = pitColor;
            frame[ledCount - 1 - offset] = pitColor;
        }
    }

    void renderLinearPattern(uint32_t *frame,
                             int stableLevel,
                             float rawLevel,
                             bool aggressiveFullBlink,
                             bool shiftBlink,
                             bool blinkState,
                             float greenEnd,
                             float yellowEnd,
                             bool &displayBlink)
    {
        g_ledRenderDebug.pitLimiterOnly = false;
        setLastRenderMode(LedFrameMode::Linear);
        const int ledCount = activeLedCount();
        const int tailIndex = (aggressiveFullBlink || shiftBlink) ? -1 : displayLevelTailIndex(stableLevel, rawLevel, ledCount);
        const float tailIntensity = (tailIndex >= 0) ? displayLevelTailIntensity(stableLevel, rawLevel, ledCount) : 0.0f;
        for (int i = 0; i < ledCount; i++)
        {
            uint32_t color = 0;
            const bool fullyLit = i < stableLevel;
            const bool partiallyLit = !fullyLit && i == tailIndex && tailIntensity > 0.02f;

            if (fullyLit || partiallyLit)
            {
                const float pos = (ledCount > 1) ? (float)i / (float)(ledCount - 1) : 0.0f;

                if (aggressiveFullBlink)
                {
                    color = blinkState ? redZoneColor() : 0;
                }
                else
                {
                    if (pos < greenEnd)
                    {
                        color = toStripColor(cfg.greenColor);
                    }
                    else if (pos < yellowEnd)
                    {
                        color = toStripColor(cfg.yellowColor);
                    }
                    else if (cfg.mode == LED_MODE_F1 && shiftBlink)
                    {
                        color = blinkState ? redZoneColor() : 0;
                        displayBlink = blinkState;
                    }
                    else
                    {
                        color = redZoneColor();
                    }

                    if (partiallyLit)
                    {
                        color = scalePackedColor(color, tailIntensity);
                    }
                }
            }

            frame[i] = color;
        }
    }

    void renderGt3Pattern(uint32_t *frame,
                          int stableLevel,
                          float rawLevel,
                          bool finalBlink,
                          float greenEnd,
                          float yellowEnd,
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

        const int ledCount = activeLedCount();
        const int pairCount = (ledCount + 1) / 2;
        stableLevel = clampInt(stableLevel, 0, pairCount);

        if (finalBlink)
        {
            const uint32_t blinkColor = blinkState ? redZoneColor() : 0;
            for (int i = 0; i < ledCount; ++i)
            {
                frame[i] = blinkColor;
            }
            displayBlink = blinkState;
            g_ledRenderDebug.pitLimiterOnly = false;
            setLastRenderMode(LedFrameMode::Gt3);
            return;
        }

        const int tailIndex = displayLevelTailIndex(stableLevel, rawLevel, pairCount);
        const float tailIntensity = displayLevelTailIntensity(stableLevel, rawLevel, pairCount);
        for (int rank = 0; rank < pairCount; ++rank)
        {
            const bool on = rank < stableLevel;
            const bool partial = !on && rank == tailIndex && tailIntensity > 0.02f;
            const float pos = (pairCount > 1) ? (float)rank / (float)(pairCount - 1) : 1.0f;
            uint32_t color = on ? zoneColor(pos, greenEnd, yellowEnd) : 0;
            if (partial)
            {
                color = scalePackedColor(zoneColor(pos, greenEnd, yellowEnd), tailIntensity);
            }
            const int left = rank;
            const int right = ledCount - 1 - rank;
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
            const int ledCount = activeLedCount();
            for (int i = 0; i < ledCount; ++i)
            {
                frame[i] = color;
            }
            setLastRenderMode(LedFrameMode::DiagnosticStaticGreen);
            break;
        }
        case LedDiagnosticMode::StaticWhite:
        {
            const uint32_t color = strip.Color(255, 255, 255);
            const int ledCount = activeLedCount();
            for (int i = 0; i < ledCount; ++i)
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

        const int ledCount = activeLedCount();
        const int segLen = max(2, ledCount / 3);
        for (int i = 0; i < ledCount; ++i)
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
        const int ledCount = activeLedCount();
        for (int i = 0; i < ledCount; ++i)
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
            const int ledCount = activeLedCount();
            const int ledsOn = static_cast<int>(round(smoothStep(min(1.0f, t / 0.55f)) * ledCount));
            const float fade = (t < 0.55f) ? 1.0f : (1.0f - smoothStep((t - 0.55f) / 0.45f));
            const uint32_t color = strip.Color(0, static_cast<uint8_t>(180 * fade), static_cast<uint8_t>(255 * fade));
            for (int i = 0; i < ledsOn && i < ledCount; ++i)
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
                const int ledCount = activeLedCount();
                scaleFrame(frame, g_effectState.baseFrame, 1.0f - smoothStep(t));
                const int keep = static_cast<int>(round((1.0f - t) * ledCount));
                const int left = max(0, (ledCount - keep) / 2);
                const int right = min(ledCount, left + keep);
                for (int i = 0; i < ledCount; ++i)
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
            const int ledCount = activeLedCount();
            const bool on = ((elapsed / 120UL) % 2UL) == 0UL;
            const uint32_t color = on ? strip.Color(255, 0, 0) : strip.Color(30, 0, 0);
            for (int i = 0; i < ledCount; ++i)
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
    g_ledRenderDebug.fastResponseActive = false;
    g_ledRenderDebug.redFallbackActive = redColorFallbackActive();
    g_lastWriter = LedRenderWriter::None;
    g_testSweepMode = LedTestSweepMode::Expressive;
    g_effectState = LedEffectState{};
    g_pendingEffectType = LedEffectType::None;
    g_pendingEffectSerial = 0;
    g_lastQueuedEffectType = LedEffectType::None;
    g_sessionEffectRequestCount = 0;
    g_sessionEffectSuppressedCount = 0;
    g_lastSessionEffectMs = 0;
    g_renderHistoryCount = 0;
    g_renderHistoryHead = 0;
    g_lastRenderEvent = LedRenderEvent{};
    g_ledExternalWriteAttempts = 0;
    g_ledSnapshotChangedDuringRender = 0;
    resetDisplayLevelState();
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
    resetDisplayLevelState();
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

int ledBarGetConfiguredLedCount()
{
    return activeLedCount();
}

const char *ledBarGetLastWriterName()
{
    return writerName(g_lastWriter);
}

const char *ledBarEffectNameById(uint8_t effectId)
{
    return effectName(static_cast<LedEffectType>(effectId));
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
    resetDisplayLevelState();
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
    ++g_sessionEffectRequestCount;
    cancelSimSessionEffects();
    if (!cfg.simSessionLedEffectsEnabled)
    {
        ++g_sessionEffectSuppressedCount;
        return;
    }

    const unsigned long nowMs = millis();
    if (!cooldownElapsed(nowMs, g_lastSessionEffectMs, SESSION_EFFECT_COOLDOWN_MS))
    {
        ++g_sessionEffectSuppressedCount;
        return;
    }

    switch (transition)
    {
    case SimSessionTransitionType::BecameLive:
        queueEffect(LedEffectType::SimReady, false, false);
        g_lastSessionEffectMs = nowMs;
        return;
    case SimSessionTransitionType::BecameWaiting:
        queueEffect(LedEffectType::SimStandby, false, false);
        g_lastSessionEffectMs = nowMs;
        return;
    case SimSessionTransitionType::BecameError:
        queueEffect(LedEffectType::SimError, false, false);
        g_lastSessionEffectMs = nowMs;
        return;
    case SimSessionTransitionType::None:
    default:
        ++g_sessionEffectSuppressedCount;
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
    info.activeEffect = static_cast<uint8_t>(g_effectState.type);
    info.queuedEffect = static_cast<uint8_t>(g_pendingEffectType);
    info.lastQueuedEffect = static_cast<uint8_t>(g_lastQueuedEffectType);
    info.sessionEffectRequests = g_sessionEffectRequestCount;
    info.sessionEffectSuppressions = g_sessionEffectSuppressedCount;
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
    constexpr uint32_t LED_TASK_TICK_MS = 5;

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
        const bool fastResponse = ledFastResponseForSource(snapshot.source) && !g_testActive;
        const bool bypassFilter = g_testActive;
        const int filteredRpm = filterLedRpm(rpm, bypassFilter, fastResponse);
        g_ledRenderDebug.lastRawRpm = rpm;
        g_ledRenderDebug.lastFilteredRpm = filteredRpm;
        g_ledRenderDebug.fastResponseActive = fastResponse;
        g_ledRenderDebug.redFallbackActive = redColorFallbackActive();
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

        const LedZoneProfile zoneProfile = buildLedZoneProfile();
        const float displayFraction = computeLedDisplayFraction(fraction, zoneProfile);

        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        const unsigned long now = millis();
        const int blinkSpeedPct = clampInt(cfg.blinkSpeedPct, 0, 100);
        const bool shiftBlink =
            ((cfg.mode == LED_MODE_F1 || cfg.mode == LED_MODE_AGGRESSIVE || cfg.mode == LED_MODE_GT3) &&
             fraction >= zoneProfile.blinkStart);
        g_ledRenderDebug.lastShiftBlink = shiftBlink;
        if (!shiftBlink)
        {
            blinkState = false;
            lastBlink = now;
        }
        else if (blinkSpeedPct >= 100)
        {
            blinkState = true;
            lastBlink = now;
        }
        else if (blinkSpeedPct <= 0)
        {
            blinkState = false;
            lastBlink = now;
        }
        else
        {
            const unsigned long blinkIntervalMs = blinkIntervalMsFromPct(blinkSpeedPct);
            if (blinkIntervalMs > 0 && (now < lastBlink || (now - lastBlink) >= blinkIntervalMs))
            {
                lastBlink = now;
                blinkState = !blinkState;
            }
        }

        bool displayBlink = false;
        if (cfg.mode == LED_MODE_AGGRESSIVE && shiftBlink)
        {
            displayBlink = blinkState;
        }

        uint32_t frame[NUM_LEDS];
        clearFrame(frame);
        const int ledCount = activeLedCount();
        if (cfg.mode == LED_MODE_GT3)
        {
            const int pairCount = (ledCount + 1) / 2;
            const int pairsOn = computeStableDisplayedLevel(displayFraction, pairCount, DISPLAY_MODE_GT3, bypassFilter, fastResponse);
            g_ledRenderDebug.lastDisplayedLeds = min(ledCount, pairsOn * 2);
            renderGt3Pattern(frame,
                             pairsOn,
                             g_stableDisplayRawLevel,
                             shiftBlink,
                             zoneProfile.greenEnd,
                             zoneProfile.yellowEnd,
                             blinkState,
                             snapshot.pitLimiter,
                             displayBlink);
        }
        else
        {
            int ledsOn = computeStableDisplayedLevel(displayFraction,
                                                     ledCount,
                                                     DISPLAY_MODE_LINEAR,
                                                     bypassFilter,
                                                     fastResponse);
            if (cfg.mode == LED_MODE_AGGRESSIVE && shiftBlink)
            {
                ledsOn = ledCount;
                g_ledRenderDebug.lastDisplayedLevel = ledCount;
                g_ledRenderDebug.lastDesiredLevel = ledCount;
                g_ledRenderDebug.lastLevelCount = ledCount;
            }
            g_ledRenderDebug.lastDisplayedLeds = ledsOn;
            renderLinearPattern(frame,
                                ledsOn,
                                g_stableDisplayRawLevel,
                                cfg.mode == LED_MODE_AGGRESSIVE && shiftBlink,
                                shiftBlink,
                                blinkState,
                                zoneProfile.greenEnd,
                                zoneProfile.yellowEnd,
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
        g_ledRenderDebug.redFallbackActive = redColorFallbackActive();

        TelemetryRenderSnapshot snapshot = telemetryCopyRenderSnapshot();
        if (snapshot.version == 0)
        {
            snapshot.source = ActiveTelemetrySource::None;
        }

        if (g_effectState.type != LedEffectType::None)
        {
            ++g_ledRenderDebug.renderCalls;
            g_ledRenderDebug.fastResponseActive = false;
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
            g_ledRenderDebug.fastResponseActive = false;
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
    // loop() (which is priority 1). Priority 3 keeps render jitter low
    // while the renderer ticks at ~200 Hz, still leaving room for the USB
    // reader and WiFi tasks.
    xTaskCreatePinnedToCore(
        ledBarTaskBody,
        "ledBar",
        4096,
        nullptr,
        3,
        &g_ledBarTaskHandle,
        1);
}
