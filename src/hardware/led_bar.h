#ifndef LED_BAR_H
#define LED_BAR_H

#include <Adafruit_NeoPixel.h>

#include "telemetry/telemetry_manager.h"

enum class LedDiagnosticMode : uint8_t
{
    Live = 0,
    Off,
    StaticGreen,
    StaticWhite,
    PitMarkers
};

enum class LedTestSweepMode : uint8_t
{
    Expressive = 0,
    Deterministic
};

struct LedRenderEvent
{
    unsigned long timestampMs = 0;
    uint8_t writer = 0;
    ActiveTelemetrySource source = ActiveTelemetrySource::None;
    int rpm = 0;
    int maxRpm = 0;
    bool pitLimiter = false;
    uint32_t frameHash = 0;
    uint8_t brightness = 0;
    bool shown = false;
};

constexpr size_t LED_RENDER_HISTORY_LEN = 8;

struct LedRenderHistoryInfo
{
    LedRenderEvent lastEvent{};
    uint8_t count = 0;
    LedRenderEvent history[LED_RENDER_HISTORY_LEN]{};
    uint32_t externalWriteAttempts = 0;
    uint32_t snapshotChangedDuringRender = 0;
    bool deterministicSweepActive = false;
    uint8_t lastWriter = 0;
    uint8_t activeEffect = 0;
    uint8_t queuedEffect = 0;
    uint8_t lastQueuedEffect = 0;
    uint32_t sessionEffectRequests = 0;
    uint32_t sessionEffectSuppressions = 0;
};

void initLeds();
void ledBarLoop();           // kept for compatibility; no-op once the task is running
void startLedBarTask();      // spawns the ~200 Hz renderer task
void setStatusLED(bool on);
void rememberPreviewPixels();
void ledBarRefreshBrightness();
void ledBarInvalidateFrameCache();
void ledBarSetDiagnosticMode(LedDiagnosticMode mode);
LedDiagnosticMode ledBarGetDiagnosticMode();
const char *ledBarGetDiagnosticModeName();
const char *ledBarGetLastRenderModeName();
const char *ledBarGetLastWriterName();
const char *ledBarEffectNameById(uint8_t effectId);
uint8_t ledBarGetAppliedBrightness();
int ledBarGetConfiguredLedCount();
void ledBarStartTestSweep(LedTestSweepMode mode, int maxRpm);
bool ledBarTestSweepActive();
bool ledBarDeterministicSweepActive();
void ledBarRequestLogoPreview();
void ledBarRequestLogoAnimation();
void ledBarRequestLeavingAnimation();
void ledBarRequestBrightnessPreview();
void ledBarRequestSimSessionTransition(SimSessionTransitionType transition);
bool ledBarEffectActive();
LedRenderHistoryInfo ledBarGetRenderHistoryInfo();

extern Adafruit_NeoPixel strip;

#endif // LED_BAR_H
