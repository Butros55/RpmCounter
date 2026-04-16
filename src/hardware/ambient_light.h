#pragma once

#include <Arduino.h>

struct AmbientLightDebugInfo
{
    bool autoEnabled = false;
    bool sensorDetected = false;
    bool sensorActive = false;
    bool busInitialized = false;
    bool deviceResponding = false;
    bool usingSharedBus = false;
    int sdaPin = -1;
    int sclPin = -1;
    float rawLux = 0.0f;
    float filteredLux = 0.0f;
    uint16_t rawAls = 0;
    uint16_t rawWhite = 0;
    uint16_t configReg = 0;
    int targetBrightness = 0;
    int desiredBrightness = 0;
    int appliedBrightness = 0;
    uint32_t initAttempts = 0;
    uint32_t initSuccessCount = 0;
    uint32_t readCount = 0;
    uint32_t readErrorCount = 0;
    unsigned long lastInitMs = 0;
    unsigned long lastReadMs = 0;
    unsigned long lastApplyMs = 0;
    String lastError;
};

void initAmbientLight();
void ambientLightLoop();
void ambientLightOnConfigChanged();
void ambientLightForceProbe();
uint8_t ambientLightGetLedBrightness();
void ambientLightNoteAppliedBrightness(uint8_t brightness);
AmbientLightDebugInfo ambientLightGetDebugInfo();
