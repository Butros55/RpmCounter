#ifndef LED_BAR_H
#define LED_BAR_H

#include <Adafruit_NeoPixel.h>

enum class LedDiagnosticMode : uint8_t
{
    Live = 0,
    Off,
    StaticGreen,
    StaticWhite,
    PitMarkers
};

void initLeds();
void ledBarLoop();           // kept for compatibility; no-op once the task is running
void startLedBarTask();      // spawns the 100 Hz renderer task
void updateRpmBar(int rpm);
void setStatusLED(bool on);
void rememberPreviewPixels();
void ledBarRefreshBrightness();
void ledBarInvalidateFrameCache();
void ledBarSetDiagnosticMode(LedDiagnosticMode mode);
LedDiagnosticMode ledBarGetDiagnosticMode();
const char *ledBarGetDiagnosticModeName();
const char *ledBarGetLastRenderModeName();
uint8_t ledBarGetAppliedBrightness();

extern Adafruit_NeoPixel strip;

#endif // LED_BAR_H
