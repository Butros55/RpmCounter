#pragma once

#include <cstdint>

struct UiSessionData
{
    bool hasAnyData = false;

    bool hasDelta = false;
    float deltaSeconds = 0.0f;

    bool hasPredictedLap = false;
    uint32_t predictedLapMs = 0;

    bool hasLastLap = false;
    uint32_t lastLapMs = 0;

    bool hasBestLap = false;
    uint32_t bestLapMs = 0;

    bool hasSessionClock = false;
    uint32_t sessionClockMs = 0;

    bool hasPosition = false;
    int position = 0;

    bool hasTotalPositions = false;
    int totalPositions = 0;

    bool hasLap = false;
    int lap = 0;

    bool hasTotalLaps = false;
    int totalLaps = 0;

    bool hasFuelLiters = false;
    float fuelLiters = 0.0f;

    bool hasFuelAvgPerLap = false;
    float fuelAvgPerLap = 0.0f;

    bool hasFuelLapsRemaining = false;
    float fuelLapsRemaining = 0.0f;

    bool hasOilTemp = false;
    float oilTempC = 0.0f;

    bool hasOilPressure = false;
    float oilPressureBar = 0.0f;

    bool hasOilLevel = false;
    float oilLevel = 0.0f;

    bool hasFuelPressure = false;
    float fuelPressureBar = 0.0f;

    bool hasWaterTemp = false;
    float waterTempC = 0.0f;

    bool hasBatteryVolts = false;
    float batteryVolts = 0.0f;

    bool hasTractionControl = false;
    int tractionControl = 0;

    bool hasTractionCut = false;
    int tractionCut = 0;

    bool hasAbs = false;
    int absLevel = 0;

    bool hasBrakeBias = false;
    float brakeBias = 0.0f;

    bool hasEngineMap = false;
    int engineMap = 0;
};
