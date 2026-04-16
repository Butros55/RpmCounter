#pragma once

#include <Arduino.h>

#include "telemetry/telemetry_manager.h"

void displayInit();
void displayClear();
void displayShowTestLogo();
void displaySetGear(int gear);
void displaySetShiftBlink(bool active);
void displayShowSimSessionTransition(SimSessionTransitionType transition);

enum class DisplayDebugPattern
{
    ColorBars,
    Grid,
    UiLabel
};

struct DisplayDebugInfo
{
    bool initAttempted = false;
    bool ready = false;
    bool buffersAllocated = false;
    bool panelInitialized = false;
    bool touchReady = false;
    bool tickFallback = false;
    bool debugSimpleUi = false;
    uint32_t lastLvglRunMs = 0;
    String lastError;
};

DisplayDebugInfo displayGetDebugInfo();
void displayShowDebugPattern(DisplayDebugPattern pattern);
