#pragma once

#include <Arduino.h>

enum class GestureDirection : uint8_t
{
    None = 0,
    Left,
    Right
};

struct GestureSensorDebugInfo
{
    bool enabled = false;
    bool sensorDetected = false;
    bool sensorActive = false;
    bool busInitialized = false;
    bool deviceResponding = false;
    bool ackResponding = false;
    bool idReadOk = false;
    bool configApplied = false;
    bool intConfigured = false;
    bool intEnabled = false;
    bool intPending = false;
    bool intLineLow = false;
    bool usingSharedBus = false;
    int sdaPin = -1;
    int sclPin = -1;
    int intPin = -1;
    uint8_t deviceId = 0;
    uint8_t lastStatusReg = 0;
    uint8_t lastFifoLevel = 0;
    uint32_t initAttempts = 0;
    uint32_t initSuccessCount = 0;
    uint32_t probeCount = 0;
    uint32_t pollCount = 0;
    uint32_t readErrorCount = 0;
    uint32_t intTriggerCount = 0;
    uint32_t gestureCount = 0;
    uint32_t modeSwitchCount = 0;
    unsigned long lastInitMs = 0;
    unsigned long lastProbeMs = 0;
    unsigned long lastReadMs = 0;
    unsigned long lastIntMs = 0;
    unsigned long lastGestureMs = 0;
    String lastError;
    GestureDirection lastGesture = GestureDirection::None;
};

constexpr int GESTURE_LED_MODE_MIN = 0;
constexpr int GESTURE_LED_MODE_MAX = 3;

inline int gestureSensorClampMode(int mode)
{
    if (mode < GESTURE_LED_MODE_MIN)
    {
        return GESTURE_LED_MODE_MIN;
    }
    if (mode > GESTURE_LED_MODE_MAX)
    {
        return GESTURE_LED_MODE_MAX;
    }
    return mode;
}

inline int gestureSensorNextMode(int mode)
{
    const int clamped = gestureSensorClampMode(mode);
    return (clamped >= GESTURE_LED_MODE_MAX) ? GESTURE_LED_MODE_MIN : (clamped + 1);
}

inline int gestureSensorPreviousMode(int mode)
{
    const int clamped = gestureSensorClampMode(mode);
    return (clamped <= GESTURE_LED_MODE_MIN) ? GESTURE_LED_MODE_MAX : (clamped - 1);
}

inline int gestureSensorModeAfterDirection(int mode, GestureDirection direction)
{
    switch (direction)
    {
    case GestureDirection::Right:
        return gestureSensorNextMode(mode);
    case GestureDirection::Left:
        return gestureSensorPreviousMode(mode);
    case GestureDirection::None:
    default:
        return gestureSensorClampMode(mode);
    }
}

inline bool gestureSensorCooldownReady(unsigned long nowMs, unsigned long lastGestureMs, unsigned long cooldownMs)
{
    return lastGestureMs == 0 || nowMs < lastGestureMs || (nowMs - lastGestureMs) >= cooldownMs;
}

inline GestureDirection gestureSensorClassifyDeltas(int horizontalDelta,
                                                    int verticalDelta,
                                                    int sensitivityThreshold,
                                                    int dominanceMargin = 10)
{
    const int absHorizontal = abs(horizontalDelta);
    const int absVertical = abs(verticalDelta);
    if (absHorizontal < sensitivityThreshold)
    {
        return GestureDirection::None;
    }
    if (absHorizontal < (absVertical + dominanceMargin))
    {
        return GestureDirection::None;
    }
    return horizontalDelta > 0 ? GestureDirection::Right : GestureDirection::Left;
}

const char *gestureSensorDirectionName(GestureDirection direction);
const char *gestureSensorModeName(int mode);
void initGestureSensor();
void gestureSensorLoop();
void gestureSensorOnConfigChanged();
void gestureSensorForceProbe();
GestureSensorDebugInfo gestureSensorGetDebugInfo();
