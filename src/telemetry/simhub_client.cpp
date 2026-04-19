#include "simhub_client.h"

#include <Arduino.h>
#include <cmath>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/config.h"
#include "core/logging.h"
#include "core/state.h"
#include "core/wifi.h"
#include "telemetry/telemetry_manager.h"
#include "telemetry/usb_sim_bridge.h"

namespace
{
    struct SimHubClientConfigSnapshot
    {
        char host[64] = {};
        uint16_t port = 8888;
        uint16_t pollMs = 75;
        TelemetryPreference preference = TelemetryPreference::Auto;
        SimTransportPreference transport = SimTransportPreference::Auto;
        bool networkAvailable = false;
        bool enabled = true;
    };

    portMUX_TYPE g_simHubConfigMux = portMUX_INITIALIZER_UNLOCKED;
    SimHubClientConfigSnapshot g_simHubConfig{};
    TaskHandle_t g_simHubTaskHandle = nullptr;
    bool g_simHubPollSuppressed = false;

    bool shouldPollSimHubNetwork(SimRuntimeTransportMode mode,
                                 ActiveTelemetrySource activeSource,
                                 bool usbFresh)
    {
        switch (mode)
        {
        case SimRuntimeTransportMode::NetworkOnly:
            return true;
        case SimRuntimeTransportMode::UsbOnly:
        case SimRuntimeTransportMode::Disabled:
            return false;
        case SimRuntimeTransportMode::Auto:
        default:
            return !(activeSource == ActiveTelemetrySource::UsbSim || usbFresh);
        }
    }

    String trimmed(const String &value)
    {
        String out = value;
        out.trim();
        return out;
    }

    void setSimHubState(SimHubConnectionState state, bool reachable, bool waiting)
    {
        g_simHubConnectionState = state;
        g_simHubReachable = reachable;
        g_simHubWaitingForData = waiting;
    }

    bool extractJsonToken(const String &payload, const char *key, String &token)
    {
        String needle = String("\"") + key + "\"";
        int keyPos = payload.indexOf(needle);
        if (keyPos < 0)
        {
            return false;
        }

        int valuePos = payload.indexOf(':', keyPos + needle.length());
        if (valuePos < 0)
        {
            return false;
        }

        ++valuePos;
        while (valuePos < payload.length() && isspace(static_cast<unsigned char>(payload[valuePos])) != 0)
        {
            ++valuePos;
        }
        if (valuePos >= payload.length())
        {
            return false;
        }

        if (payload[valuePos] == '"')
        {
            const int start = valuePos + 1;
            const int end = payload.indexOf('"', start);
            if (end < 0)
            {
                return false;
            }

            token = payload.substring(start, end);
            return true;
        }

        int end = valuePos;
        while (end < payload.length())
        {
            const char ch = payload[end];
            if (isspace(static_cast<unsigned char>(ch)) != 0 || ch == ',' || ch == '}' || ch == ']')
            {
                break;
            }
            ++end;
        }

        token = payload.substring(valuePos, end);
        token.trim();
        return token.length() > 0;
    }

    bool extractJsonBool(const String &payload, const char *key, bool &value)
    {
        String token;
        if (!extractJsonToken(payload, key, token))
        {
            return false;
        }

        token.toLowerCase();
        if (token == "true")
        {
            value = true;
            return true;
        }
        if (token == "false")
        {
            value = false;
            return true;
        }
        return false;
    }

    bool extractJsonFloat(const String &payload, const char *key, float &value)
    {
        String token;
        if (!extractJsonToken(payload, key, token))
        {
            return false;
        }

        value = token.toFloat();
        return true;
    }

    bool parseLooseBool(const String &payload, bool &value)
    {
        String token = payload;
        token.trim();
        token.toLowerCase();

        if (token == "true" || token == "1" || token == "yes" || token == "on")
        {
            value = true;
            return true;
        }
        if (token == "false" || token == "0" || token == "no" || token == "off")
        {
            value = false;
            return true;
        }
        return false;
    }

    bool extractJsonInt(const String &payload, const char *key, int &value)
    {
        String token;
        if (!extractJsonToken(payload, key, token))
        {
            return false;
        }

        if (token == "N" || token == "n" || token == "R" || token == "r")
        {
            value = 0;
            return true;
        }

        value = token.toInt();
        return true;
    }

    bool parseTimeToMs(const String &token, bool secondsByDefault, uint32_t &milliseconds)
    {
        String trimmedToken = token;
        trimmedToken.trim();
        if (trimmedToken.isEmpty())
        {
            return false;
        }

        if (trimmedToken.indexOf(':') >= 0)
        {
            int lastColon = trimmedToken.lastIndexOf(':');
            int firstColon = trimmedToken.indexOf(':');

            float secondsPart = trimmedToken.substring(lastColon + 1).toFloat();
            int minutes = 0;
            int hours = 0;

            if (firstColon != lastColon)
            {
                hours = trimmedToken.substring(0, firstColon).toInt();
                minutes = trimmedToken.substring(firstColon + 1, lastColon).toInt();
            }
            else
            {
                minutes = trimmedToken.substring(0, lastColon).toInt();
            }

            const float totalSeconds = (static_cast<float>(hours) * 3600.0f) +
                                       (static_cast<float>(minutes) * 60.0f) +
                                       secondsPart;
            if (totalSeconds < 0.0f)
            {
                return false;
            }

            milliseconds = static_cast<uint32_t>(totalSeconds * 1000.0f + 0.5f);
            return true;
        }

        const float numericValue = trimmedToken.toFloat();
        if (numericValue < 0.0f)
        {
            return false;
        }

        const float millisValue =
            secondsByDefault
                ? ((numericValue <= 600.0f) ? numericValue * 1000.0f : numericValue)
                : ((numericValue <= 86400.0f) ? numericValue * 1000.0f : numericValue);
        milliseconds = static_cast<uint32_t>(millisValue + 0.5f);
        return true;
    }

    bool extractJsonTimeMs(const String &payload, const char *key, bool secondsByDefault, uint32_t &milliseconds)
    {
        String token;
        if (!extractJsonToken(payload, key, token))
        {
            return false;
        }

        return parseTimeToMs(token, secondsByDefault, milliseconds);
    }

    bool extractJsonFloatAny(const String &payload, std::initializer_list<const char *> keys, float &value)
    {
        for (const char *key : keys)
        {
            if (extractJsonFloat(payload, key, value))
            {
                return true;
            }
        }
        return false;
    }

    bool extractJsonLooseBool(const String &payload, const char *key, bool &value)
    {
        String token;
        if (!extractJsonToken(payload, key, token))
        {
            return false;
        }
        token.trim();
        token.toLowerCase();
        if (token == "true" || token == "1" || token == "yes" || token == "on")
        {
            value = true;
            return true;
        }
        if (token == "false" || token == "0" || token == "no" || token == "off")
        {
            value = false;
            return true;
        }
        return false;
    }

    bool extractJsonIntAny(const String &payload, std::initializer_list<const char *> keys, int &value)
    {
        for (const char *key : keys)
        {
            if (extractJsonInt(payload, key, value))
            {
                return true;
            }
        }
        return false;
    }

    bool extractJsonBoolAny(const String &payload, std::initializer_list<const char *> keys, bool &value)
    {
        for (const char *key : keys)
        {
            if (extractJsonBool(payload, key, value))
            {
                return true;
            }
        }
        return false;
    }

    bool extractJsonTimeAny(const String &payload,
                            std::initializer_list<const char *> keys,
                            bool secondsByDefault,
                            uint32_t &milliseconds)
    {
        for (const char *key : keys)
        {
            if (extractJsonTimeMs(payload, key, secondsByDefault, milliseconds))
            {
                return true;
            }
        }
        return false;
    }

    bool extractJsonLooseBoolAny(const String &payload, std::initializer_list<const char *> keys, bool &value)
    {
        for (const char *key : keys)
        {
            if (extractJsonLooseBool(payload, key, value))
            {
                return true;
            }
        }
        return false;
    }

    SideLedFlag parseSimHubFlagName(const String &value)
    {
        String token = value;
        token.trim();
        token.toLowerCase();
        if (token == "green")
        {
            return SideLedFlag::Green;
        }
        if (token == "yellow")
        {
            return SideLedFlag::Yellow;
        }
        if (token == "blue")
        {
            return SideLedFlag::Blue;
        }
        if (token == "red")
        {
            return SideLedFlag::Red;
        }
        if (token == "white")
        {
            return SideLedFlag::White;
        }
        if (token == "black")
        {
            return SideLedFlag::Black;
        }
        if (token == "orange" || token == "blackorange" || token == "technical")
        {
            return SideLedFlag::Orange;
        }
        if (token == "checkered" || token == "chequered")
        {
            return SideLedFlag::Checkered;
        }
        return SideLedFlag::None;
    }

    uint8_t spotterSeverityFromDistance(float distance, bool detected)
    {
        if (!detected)
        {
            return 0;
        }
        if (distance > 0.0f && distance <= 0.14f)
        {
            return 4;
        }
        if (distance > 0.0f && distance <= 0.30f)
        {
            return 2;
        }
        return 1;
    }

    float clampSlipValue(float value)
    {
        return constrain(std::abs(value), 0.0f, 2.0f);
    }

    void populateSimHubSessionMetrics(const String &gameData, const String &simpleData)
    {
        UiSessionData session{};
        float floatValue = 0.0f;
        int intValue = 0;

        if (extractJsonFloatAny(gameData, {"DeltaToBestLap", "LiveDeltaToBestLap", "LiveDeltaToSessionBest", "CurrentLapDeltaToBest", "BestLapDelta"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"DeltaToBestLap", "LiveDeltaToBestLap", "LiveDeltaToSessionBest", "CurrentLapDeltaToBest", "BestLapDelta"}, floatValue))
        {
            session.hasDelta = true;
            session.deltaSeconds = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonTimeAny(gameData, {"PredictedLapTime", "EstimatedLapTime", "CurrentLapPredicted", "LivePredictedLapTime"}, true, session.predictedLapMs) ||
            extractJsonTimeAny(simpleData, {"PredictedLapTime", "EstimatedLapTime", "CurrentLapPredicted", "LivePredictedLapTime"}, true, session.predictedLapMs))
        {
            session.hasPredictedLap = true;
            session.hasAnyData = true;
        }

        if (extractJsonTimeAny(gameData, {"LastLapTime", "LapTimePrevious", "PreviousLapTime"}, true, session.lastLapMs) ||
            extractJsonTimeAny(simpleData, {"LastLapTime", "LapTimePrevious", "PreviousLapTime"}, true, session.lastLapMs))
        {
            session.hasLastLap = true;
            session.hasAnyData = true;
        }

        if (extractJsonTimeAny(gameData, {"BestLapTime", "LapTimeBest", "SessionBestLapTime"}, true, session.bestLapMs) ||
            extractJsonTimeAny(simpleData, {"BestLapTime", "LapTimeBest", "SessionBestLapTime"}, true, session.bestLapMs))
        {
            session.hasBestLap = true;
            session.hasAnyData = true;
        }

        if (!session.hasPredictedLap && session.hasBestLap && session.hasDelta)
        {
            const float predictedMs = static_cast<float>(session.bestLapMs) + (session.deltaSeconds * 1000.0f);
            if (predictedMs > 0.0f)
            {
                session.predictedLapMs = static_cast<uint32_t>(predictedMs + 0.5f);
                session.hasPredictedLap = true;
                session.hasAnyData = true;
            }
        }

        if (extractJsonTimeAny(gameData, {"SessionTimeLeft", "StintTimeLeft", "RemainingTime", "SessionTime", "SessionRunningTime"}, false, session.sessionClockMs) ||
            extractJsonTimeAny(simpleData, {"SessionTimeLeft", "StintTimeLeft", "RemainingTime", "SessionTime", "SessionRunningTime"}, false, session.sessionClockMs))
        {
            session.hasSessionClock = true;
            session.hasAnyData = true;
        }

        if (extractJsonIntAny(gameData, {"Position", "SessionPosition", "RacePosition", "CarPosition"}, intValue) ||
            extractJsonIntAny(simpleData, {"Position", "SessionPosition", "RacePosition", "CarPosition"}, intValue))
        {
            session.position = intValue;
            session.hasPosition = intValue > 0;
            session.hasAnyData = session.hasAnyData || session.hasPosition;
        }

        if (extractJsonIntAny(gameData, {"TotalPositions", "TotalCars", "CarCount", "NbCars"}, intValue) ||
            extractJsonIntAny(simpleData, {"TotalPositions", "TotalCars", "CarCount", "NbCars"}, intValue))
        {
            session.totalPositions = intValue;
            session.hasTotalPositions = intValue > 0;
            session.hasAnyData = session.hasAnyData || session.hasTotalPositions;
        }
        else if (extractJsonIntAny(gameData, {"OpponentsCount", "OpponentCount"}, intValue) ||
                 extractJsonIntAny(simpleData, {"OpponentsCount", "OpponentCount"}, intValue))
        {
            session.totalPositions = max(0, intValue) + 1;
            session.hasTotalPositions = session.totalPositions > 0;
            session.hasAnyData = session.hasAnyData || session.hasTotalPositions;
        }

        if (extractJsonIntAny(gameData, {"CurrentLap", "Lap", "LapNumber"}, intValue) ||
            extractJsonIntAny(simpleData, {"CurrentLap", "Lap", "LapNumber"}, intValue))
        {
            session.lap = intValue;
            session.hasLap = intValue > 0;
            session.hasAnyData = session.hasAnyData || session.hasLap;
        }
        else if (extractJsonIntAny(gameData, {"CompletedLaps"}, intValue) ||
                 extractJsonIntAny(simpleData, {"CompletedLaps"}, intValue))
        {
            session.lap = max(0, intValue) + 1;
            session.hasLap = session.lap > 0;
            session.hasAnyData = session.hasAnyData || session.hasLap;
        }

        if (extractJsonIntAny(gameData, {"TotalLaps", "SessionLaps", "RaceLaps"}, intValue) ||
            extractJsonIntAny(simpleData, {"TotalLaps", "SessionLaps", "RaceLaps"}, intValue))
        {
            session.totalLaps = intValue;
            session.hasTotalLaps = intValue > 0;
            session.hasAnyData = session.hasAnyData || session.hasTotalLaps;
        }

        if (extractJsonFloatAny(gameData, {"Fuel", "FuelRemaining", "FuelLeft", "FuelLevel"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"Fuel", "FuelRemaining", "FuelLeft", "FuelLevel"}, floatValue))
        {
            session.hasFuelLiters = true;
            session.fuelLiters = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"FuelAvgPerLap", "FuelPerLap", "FuelConsumptionPerLap"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"FuelAvgPerLap", "FuelPerLap", "FuelConsumptionPerLap"}, floatValue))
        {
            session.hasFuelAvgPerLap = true;
            session.fuelAvgPerLap = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"FuelLapsRemaining", "FuelEstimatedLaps", "RemainingLapsOnFuel"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"FuelLapsRemaining", "FuelEstimatedLaps", "RemainingLapsOnFuel"}, floatValue))
        {
            session.hasFuelLapsRemaining = true;
            session.fuelLapsRemaining = floatValue;
            session.hasAnyData = true;
        }
        else if (session.hasFuelLiters && session.hasFuelAvgPerLap && session.fuelAvgPerLap > 0.001f)
        {
            session.hasFuelLapsRemaining = true;
            session.fuelLapsRemaining = session.fuelLiters / session.fuelAvgPerLap;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"OilTemp", "OilTemperature", "EngineOilTemp", "OilTempC"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"OilTemp", "OilTemperature", "EngineOilTemp", "OilTempC"}, floatValue))
        {
            session.hasOilTemp = true;
            session.oilTempC = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"OilPressure", "OilPress", "EngineOilPressure", "OilPressureBar"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"OilPressure", "OilPress", "EngineOilPressure", "OilPressureBar"}, floatValue))
        {
            session.hasOilPressure = true;
            session.oilPressureBar = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"OilLevel", "EngineOilLevel"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"OilLevel", "EngineOilLevel"}, floatValue))
        {
            session.hasOilLevel = true;
            session.oilLevel = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"FuelPressure", "FuelPress", "FuelPressureBar"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"FuelPressure", "FuelPress", "FuelPressureBar"}, floatValue))
        {
            session.hasFuelPressure = true;
            session.fuelPressureBar = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"WaterTemp", "WaterTemperature", "CoolantTemp", "CoolantTemperature", "WaterTempC"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"WaterTemp", "WaterTemperature", "CoolantTemp", "CoolantTemperature", "WaterTempC"}, floatValue))
        {
            session.hasWaterTemp = true;
            session.waterTempC = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"BatteryVoltage", "BatteryVolt", "Battery", "Voltage"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"BatteryVoltage", "BatteryVolt", "Battery", "Voltage"}, floatValue))
        {
            session.hasBatteryVolts = true;
            session.batteryVolts = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonIntAny(gameData, {"TcLevel", "TCLevel", "TractionControl", "Tc", "TC"}, intValue) ||
            extractJsonIntAny(simpleData, {"TcLevel", "TCLevel", "TractionControl", "Tc", "TC"}, intValue))
        {
            session.hasTractionControl = true;
            session.tractionControl = intValue;
            session.hasAnyData = true;
        }

        if (extractJsonIntAny(gameData, {"AbsLevel", "ABSLevel", "Abs", "ABS"}, intValue) ||
            extractJsonIntAny(simpleData, {"AbsLevel", "ABSLevel", "Abs", "ABS"}, intValue))
        {
            session.hasAbs = true;
            session.absLevel = intValue;
            session.hasAnyData = true;
        }

        if (extractJsonFloatAny(gameData, {"BrakeBias", "BrakeBalance", "BBalance"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"BrakeBias", "BrakeBalance", "BBalance"}, floatValue))
        {
            session.hasBrakeBias = true;
            session.brakeBias = floatValue;
            session.hasAnyData = true;
        }

        if (extractJsonIntAny(gameData, {"EngineMap", "Map", "EngineMode"}, intValue) ||
            extractJsonIntAny(simpleData, {"EngineMap", "Map", "EngineMode"}, intValue))
        {
            session.hasEngineMap = true;
            session.engineMap = intValue;
            session.hasAnyData = true;
        }

        bool boolValue = false;
        if (extractJsonBoolAny(gameData, {"TcInAction", "TCInAction", "TractionControlActive"}, boolValue) ||
            extractJsonBoolAny(simpleData, {"TcInAction", "TCInAction", "TractionControlActive"}, boolValue))
        {
            session.hasTractionCut = true;
            session.tractionCut = boolValue ? 1 : 0;
            session.hasAnyData = true;
        }
        else if (extractJsonIntAny(gameData, {"TcCut", "TCCut"}, intValue) ||
                 extractJsonIntAny(simpleData, {"TcCut", "TCCut"}, intValue))
        {
            session.hasTractionCut = true;
            session.tractionCut = intValue;
            session.hasAnyData = true;
        }

        g_simHubSessionData = session;
    }

    void populateSimHubSideTelemetry(const String &gameData,
                                     const String &simpleData,
                                     bool pitLimiterOverride,
                                     float throttleNormalized,
                                     float brakeNormalized)
    {
        SideLedTelemetry side{};
        bool boolValue = false;
        float floatValue = 0.0f;

        if ((extractJsonLooseBoolAny(gameData, {"Flag_Green"}, boolValue) ||
             extractJsonLooseBoolAny(simpleData, {"Flag_Green"}, boolValue)) &&
            boolValue)
        {
            side.flags.green = true;
        }
        if ((extractJsonLooseBoolAny(gameData, {"Flag_Yellow"}, boolValue) ||
             extractJsonLooseBoolAny(simpleData, {"Flag_Yellow"}, boolValue)) &&
            boolValue)
        {
            side.flags.yellow = true;
        }
        if ((extractJsonLooseBoolAny(gameData, {"Flag_Blue"}, boolValue) ||
             extractJsonLooseBoolAny(simpleData, {"Flag_Blue"}, boolValue)) &&
            boolValue)
        {
            side.flags.blue = true;
        }
        if ((extractJsonLooseBoolAny(gameData, {"Flag_Red"}, boolValue) ||
             extractJsonLooseBoolAny(simpleData, {"Flag_Red"}, boolValue)) &&
            boolValue)
        {
            side.flags.red = true;
        }
        if ((extractJsonLooseBoolAny(gameData, {"Flag_White"}, boolValue) ||
             extractJsonLooseBoolAny(simpleData, {"Flag_White"}, boolValue)) &&
            boolValue)
        {
            side.flags.white = true;
        }
        if ((extractJsonLooseBoolAny(gameData, {"Flag_Black"}, boolValue) ||
             extractJsonLooseBoolAny(simpleData, {"Flag_Black"}, boolValue)) &&
            boolValue)
        {
            side.flags.black = true;
        }
        if ((extractJsonLooseBoolAny(gameData, {"Flag_Orange"}, boolValue) ||
             extractJsonLooseBoolAny(simpleData, {"Flag_Orange"}, boolValue)) &&
            boolValue)
        {
            side.flags.orange = true;
        }
        if ((extractJsonLooseBoolAny(gameData, {"Flag_Checkered"}, boolValue) ||
             extractJsonLooseBoolAny(simpleData, {"Flag_Checkered"}, boolValue)) &&
            boolValue)
        {
            side.flags.checkered = true;
        }

        String flagName;
        if (extractJsonToken(gameData, "Flag_Name", flagName) || extractJsonToken(simpleData, "Flag_Name", flagName))
        {
            side.flags.current = parseSimHubFlagName(flagName);
        }
        if (side.flags.current == SideLedFlag::None)
        {
            if (side.flags.red)
            {
                side.flags.current = SideLedFlag::Red;
            }
            else if (side.flags.yellow)
            {
                side.flags.current = SideLedFlag::Yellow;
            }
            else if (side.flags.blue)
            {
                side.flags.current = SideLedFlag::Blue;
            }
            else if (side.flags.black)
            {
                side.flags.current = SideLedFlag::Black;
            }
            else if (side.flags.orange)
            {
                side.flags.current = SideLedFlag::Orange;
            }
            else if (side.flags.white)
            {
                side.flags.current = SideLedFlag::White;
            }
            else if (side.flags.checkered)
            {
                side.flags.current = SideLedFlag::Checkered;
            }
            else if (side.flags.green)
            {
                side.flags.current = SideLedFlag::Green;
            }
        }

        if (extractJsonLooseBoolAny(gameData, {"SpotterCarLeft"}, boolValue) ||
            extractJsonLooseBoolAny(simpleData, {"SpotterCarLeft"}, boolValue))
        {
            side.spotter.left = boolValue;
        }
        if (extractJsonLooseBoolAny(gameData, {"SpotterCarRight"}, boolValue) ||
            extractJsonLooseBoolAny(simpleData, {"SpotterCarRight"}, boolValue))
        {
            side.spotter.right = boolValue;
        }

        float leftDistance = -1.0f;
        float rightDistance = -1.0f;
        if (extractJsonFloatAny(gameData, {"SpotterCarLeftDistance"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"SpotterCarLeftDistance"}, floatValue))
        {
            leftDistance = floatValue;
        }
        if (extractJsonFloatAny(gameData, {"SpotterCarRightDistance"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"SpotterCarRightDistance"}, floatValue))
        {
            rightDistance = floatValue;
        }
        side.spotter.leftSeverity = spotterSeverityFromDistance(leftDistance, side.spotter.left);
        side.spotter.rightSeverity = spotterSeverityFromDistance(rightDistance, side.spotter.right);
        side.spotter.leftClose = side.spotter.leftSeverity >= 4;
        side.spotter.rightClose = side.spotter.rightSeverity >= 4;

        if (extractJsonFloatAny(gameData, {"SpotterCarLeftAngle"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"SpotterCarLeftAngle"}, floatValue))
        {
            side.spotter.leftRear = floatValue < 0.0f;
        }
        if (extractJsonFloatAny(gameData, {"SpotterCarRightAngle"}, floatValue) ||
            extractJsonFloatAny(simpleData, {"SpotterCarRightAngle"}, floatValue))
        {
            side.spotter.rightRear = floatValue < 0.0f;
        }

        if (extractJsonLooseBoolAny(gameData, {"PitLimiterOn", "FeedbackData.LimiterActive", "FeedbackData.SpeedLimiterActive"}, boolValue) ||
            extractJsonLooseBoolAny(simpleData, {"PitLimiterOn", "FeedbackData.LimiterActive", "FeedbackData.SpeedLimiterActive"}, boolValue))
        {
            side.warnings.pitLimiter = boolValue;
        }
        side.warnings.pitLimiter = side.warnings.pitLimiter || pitLimiterOverride;

        if (extractJsonLooseBoolAny(gameData, {"IsInPitLane", "IsInPit"}, boolValue) ||
            extractJsonLooseBoolAny(simpleData, {"IsInPitLane", "IsInPit"}, boolValue))
        {
            side.warnings.inPitlane = boolValue;
        }

        if (extractJsonLooseBoolAny(gameData, {"CarSettings_FuelAlertActive", "FuelAlertActive"}, boolValue) ||
            extractJsonLooseBoolAny(simpleData, {"CarSettings_FuelAlertActive", "FuelAlertActive"}, boolValue))
        {
            side.warnings.lowFuel = boolValue;
        }
        else if (extractJsonFloatAny(gameData, {"EstimatedFuelRemaingLaps", "EstimatedFuelRemainingLaps", "FuelEstimatedLaps", "FuelLapsRemaining"}, floatValue) ||
                 extractJsonFloatAny(simpleData, {"EstimatedFuelRemaingLaps", "EstimatedFuelRemainingLaps", "FuelEstimatedLaps", "FuelLapsRemaining"}, floatValue))
        {
            side.warnings.lowFuel = floatValue > 0.0f && floatValue <= 2.0f;
        }

        float oilPressure = 0.0f;
        if (extractJsonFloatAny(gameData, {"OilPressure", "EngineOilPressure", "OilPressureBar"}, oilPressure) ||
            extractJsonFloatAny(simpleData, {"OilPressure", "EngineOilPressure", "OilPressureBar"}, oilPressure))
        {
            side.warnings.oil = oilPressure > 0.0f && oilPressure < 1.2f;
        }

        float oilTemperature = 0.0f;
        if (extractJsonFloatAny(gameData, {"OilTemperature", "OilTemp", "EngineOilTemp"}, oilTemperature) ||
            extractJsonFloatAny(simpleData, {"OilTemperature", "OilTemp", "EngineOilTemp"}, oilTemperature))
        {
            side.warnings.oil = side.warnings.oil || oilTemperature >= 130.0f;
        }

        float waterTemperature = 0.0f;
        if (extractJsonFloatAny(gameData, {"WaterTemperature", "WaterTemp", "CoolantTemperature", "CoolantTemp"}, waterTemperature) ||
            extractJsonFloatAny(simpleData, {"WaterTemperature", "WaterTemp", "CoolantTemperature", "CoolantTemp"}, waterTemperature))
        {
            side.warnings.waterTemp = waterTemperature >= 108.0f;
            side.warnings.engine = waterTemperature >= 115.0f;
        }

        if (extractJsonLooseBoolAny(gameData, {"EngineWarning", "EngineAlarm", "CheckEngine"}, boolValue) ||
            extractJsonLooseBoolAny(simpleData, {"EngineWarning", "EngineAlarm", "CheckEngine"}, boolValue))
        {
            side.warnings.engine = side.warnings.engine || boolValue;
        }

        float damageValue = 0.0f;
        if (extractJsonFloatAny(gameData, {"CarDamagesMax", "CarDamagesAvg", "CarDamage1", "CarDamage2", "CarDamage3", "CarDamage4", "CarDamage5"}, damageValue) ||
            extractJsonFloatAny(simpleData, {"CarDamagesMax", "CarDamagesAvg", "CarDamage1", "CarDamage2", "CarDamage3", "CarDamage4", "CarDamage5"}, damageValue))
        {
            side.warnings.damage = damageValue >= 0.15f;
        }

        float frontLeftSlip = 0.0f;
        float rearLeftSlip = 0.0f;
        float frontRightSlip = 0.0f;
        float rearRightSlip = 0.0f;
        float directTractionLoss = 0.0f;
        const bool hasFrontLeftSlip =
            extractJsonFloatAny(gameData, {"FeedbackData.FrontLeftWheelSlip", "FrontLeftWheelSlip", "WheelSlipFL", "SlipFL"}, frontLeftSlip) ||
            extractJsonFloatAny(simpleData, {"FeedbackData.FrontLeftWheelSlip", "FrontLeftWheelSlip", "WheelSlipFL", "SlipFL"}, frontLeftSlip);
        const bool hasRearLeftSlip =
            extractJsonFloatAny(gameData, {"FeedbackData.RearLeftWheelSlip", "RearLeftWheelSlip", "WheelSlipRL", "SlipRL"}, rearLeftSlip) ||
            extractJsonFloatAny(simpleData, {"FeedbackData.RearLeftWheelSlip", "RearLeftWheelSlip", "WheelSlipRL", "SlipRL"}, rearLeftSlip);
        const bool hasFrontRightSlip =
            extractJsonFloatAny(gameData, {"FeedbackData.FrontRightWheelSlip", "FrontRightWheelSlip", "WheelSlipFR", "SlipFR"}, frontRightSlip) ||
            extractJsonFloatAny(simpleData, {"FeedbackData.FrontRightWheelSlip", "FrontRightWheelSlip", "WheelSlipFR", "SlipFR"}, frontRightSlip);
        const bool hasRearRightSlip =
            extractJsonFloatAny(gameData, {"FeedbackData.RearRightWheelSlip", "RearRightWheelSlip", "WheelSlipRR", "SlipRR"}, rearRightSlip) ||
            extractJsonFloatAny(simpleData, {"FeedbackData.RearRightWheelSlip", "RearRightWheelSlip", "WheelSlipRR", "SlipRR"}, rearRightSlip);
        const bool hasDirectTractionLoss =
            extractJsonFloatAny(gameData, {"FeedbackData.DirectTractionLoss", "DirectTractionLoss", "TractionLoss"}, directTractionLoss) ||
            extractJsonFloatAny(simpleData, {"FeedbackData.DirectTractionLoss", "DirectTractionLoss", "TractionLoss"}, directTractionLoss);
        float longitudinalAccel = 0.0f;
        const bool hasLongitudinalAccel =
            extractJsonFloatAny(gameData, {"LongAccel", "LongitudinalAcceleration", "AccelerationX", "AccelX", "LocalAccelerationX", "VehicleAccelerationX"}, longitudinalAccel) ||
            extractJsonFloatAny(simpleData, {"LongAccel", "LongitudinalAcceleration", "AccelerationX", "AccelX", "LocalAccelerationX", "VehicleAccelerationX"}, longitudinalAccel);

        const float leftSlip = std::max(clampSlipValue(frontLeftSlip), clampSlipValue(rearLeftSlip)) +
                               (hasDirectTractionLoss ? (clampSlipValue(directTractionLoss) * 0.35f) : 0.0f);
        const float rightSlip = std::max(clampSlipValue(frontRightSlip), clampSlipValue(rearRightSlip)) +
                                (hasDirectTractionLoss ? (clampSlipValue(directTractionLoss) * 0.35f) : 0.0f);

        side.traction.throttle = side_led_normalize_input(throttleNormalized);
        side.traction.brake = side_led_normalize_input(brakeNormalized);
        if (hasLongitudinalAccel)
        {
            if (longitudinalAccel > 0.0f)
            {
                side.traction.throttle = max(side.traction.throttle, side_led_normalize_longitudinal_drive(longitudinalAccel));
            }
            else if (longitudinalAccel < 0.0f)
            {
                side.traction.brake = max(side.traction.brake, side_led_normalize_longitudinal_brake(-longitudinalAccel));
            }
        }
        side.traction.leftSlip = leftSlip;
        side.traction.rightSlip = rightSlip;
        side.traction.leftLevel = side_led_traction_level(leftSlip);
        side.traction.rightLevel = side_led_traction_level(rightSlip);
        side.traction.leftCritical = side.traction.leftLevel >= 4;
        side.traction.rightCritical = side.traction.rightLevel >= 4;
        side.traction.direction = side_led_direction_from_inputs(side.traction.throttle, side.traction.brake);
        side.traction.active =
            hasFrontLeftSlip || hasRearLeftSlip || hasFrontRightSlip || hasRearRightSlip || hasDirectTractionLoss ||
            side.traction.leftLevel > 0 || side.traction.rightLevel > 0 ||
            side.traction.throttle >= 0.03f || side.traction.brake >= 0.03f;
        if (side.traction.active && side.traction.direction == SideLedTractionDirection::Off)
        {
            side.traction.direction =
                side.traction.brake > side.traction.throttle
                    ? SideLedTractionDirection::Braking
                    : SideLedTractionDirection::Accelerating;
        }

        g_simHubSideTelemetry = side;
    }

    bool httpGetText(const char *host, uint16_t port, const char *path, String &payload)
    {
        WiFiClient client;
        HTTPClient http;
        http.setReuse(false);
        http.setConnectTimeout(250);
        http.setTimeout(250);

        String url = String("http://") + host + ":" + String(port) + path;
        if (!http.begin(client, url))
        {
            return false;
        }

        const int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            http.end();
            return false;
        }

        payload = http.getString();
        http.end();
        return true;
    }

    bool tryFetchPitLimiterFlag(const SimHubClientConfigSnapshot &config, bool &pitLimiterActive)
    {
        static constexpr const char *kPitLimiterPaths[] = {
            "/Api/GetProperty/DataCorePlugin.GameData.NewData.PitLimiterOn",
            "/Api/GetProperty/DataCorePlugin.GameData.NewData.PitLimiter",
            "/Api/GetProperty/DataCorePlugin.GameData.PitLimiterOn",
            "/Api/GetProperty/DataCorePlugin.GameData.PitLimiter"};

        for (const char *path : kPitLimiterPaths)
        {
            String payload;
            if (!httpGetText(config.host, config.port, path, payload))
            {
                continue;
            }

            if (parseLooseBool(payload, pitLimiterActive))
            {
                return true;
            }
        }

        pitLimiterActive = false;
        return false;
    }

    float normalizeSimHubThrottle(float value)
    {
        return side_led_normalize_input(value);
    }

    bool fetchSimHubFrame(const SimHubClientConfigSnapshot &config)
    {
        String gameData;
        if (!httpGetText(config.host, config.port, "/Api/GetGameData", gameData))
        {
            ++g_simHubDebug.pollErrorCount;
            g_simHubDebug.lastErrorMs = millis();
            g_simHubDebug.lastError = F("GetGameData failed");
            setSimHubState(SimHubConnectionState::Error, false, true);
            return false;
        }

        bool gameRunning = false;
        extractJsonBool(gameData, "GameRunning", gameRunning);
        const bool hasNewData = gameData.indexOf("\"NewData\":null") < 0 && gameData.indexOf("\"NewData\": null") < 0;

        if (!gameRunning || !hasNewData)
        {
            setSimHubState(SimHubConnectionState::WaitingForData, true, true);
            return false;
        }

        String simpleData;
        if (!httpGetText(config.host, config.port, "/Api/GetGameDataSimple", simpleData))
        {
            ++g_simHubDebug.pollErrorCount;
            g_simHubDebug.lastErrorMs = millis();
            g_simHubDebug.lastError = F("GetGameDataSimple failed");
            setSimHubState(SimHubConnectionState::Error, false, true);
            return false;
        }

        int rpm = 0;
        int speedKmh = 0;
        int gear = 0;
        int maxRpm = 0;
        float throttle = 0.0f;
        float brake = 0.0f;
        bool pitLimiterActive = false;

        const bool hasRpm = extractJsonInt(simpleData, "rpms", rpm) || extractJsonInt(simpleData, "rpm", rpm);
        const bool hasSpeed = extractJsonInt(simpleData, "speed", speedKmh);
        const bool hasGear = extractJsonInt(simpleData, "gear", gear);
        extractJsonInt(simpleData, "maxRpm", maxRpm);

        String throttlePayload;
        if (httpGetText(config.host, config.port, "/Api/GetProperty/DataCorePlugin.GameData.NewData.Throttle", throttlePayload))
        {
            if (!extractJsonFloat(throttlePayload, "Throttle", throttle) &&
                !extractJsonFloat(throttlePayload, "throttle", throttle) &&
                !extractJsonFloat(throttlePayload, "value", throttle))
            {
                throttle = throttlePayload.toFloat();
            }
        }
        else if (!(extractJsonFloat(simpleData, "throttle", throttle) ||
                   extractJsonFloatAny(gameData, {"Throttle", "throttle", "Gas", "gas", "Accelerator", "AcceleratorPedal", "AcceleratorPedalPosition", "ThrottleRaw", "PedalThrottle"}, throttle) ||
                   extractJsonFloatAny(simpleData, {"Throttle", "throttle", "Gas", "gas", "Accelerator", "AcceleratorPedal", "AcceleratorPedalPosition", "ThrottleRaw", "PedalThrottle"}, throttle)))
        {
            throttle = 0.0f;
        }
        String brakePayload;
        if (httpGetText(config.host, config.port, "/Api/GetProperty/DataCorePlugin.GameData.NewData.Brake", brakePayload))
        {
            if (!extractJsonFloat(brakePayload, "Brake", brake) &&
                !extractJsonFloat(brakePayload, "brake", brake) &&
                !extractJsonFloat(brakePayload, "BrakePedal", brake) &&
                !extractJsonFloat(brakePayload, "value", brake))
            {
                brake = brakePayload.toFloat();
            }
        }
        else if (!(extractJsonFloat(simpleData, "brake", brake) ||
                   extractJsonFloatAny(gameData, {"Brake", "brake", "BrakePedal", "BrakePedalPosition", "FeedbackData.Brake", "BrakeRaw", "PedalBrake", "BrakeInput"}, brake) ||
                   extractJsonFloatAny(simpleData, {"Brake", "brake", "BrakePedal", "BrakePedalPosition", "FeedbackData.Brake", "BrakeRaw", "PedalBrake", "BrakeInput"}, brake)))
        {
            brake = 0.0f;
        }
        tryFetchPitLimiterFlag(config, pitLimiterActive);
        populateSimHubSessionMetrics(gameData, simpleData);
        populateSimHubSideTelemetry(gameData,
                                    simpleData,
                                    pitLimiterActive,
                                    normalizeSimHubThrottle(throttle),
                                    normalizeSimHubThrottle(brake));

        if (!hasRpm && !hasSpeed && !hasGear)
        {
            setSimHubState(SimHubConnectionState::WaitingForData, true, true);
            return false;
        }

        const unsigned long nowMs = millis();
        const unsigned long previousNetworkMs = g_lastSimHubNetworkTelemetryMs;
        const uint32_t dtMs = (previousNetworkMs > 0 && nowMs > previousNetworkMs)
                                  ? static_cast<uint32_t>(nowMs - previousNetworkMs)
                                  : 0U;
        side_led_enhance_traction_state(g_simHubSideTelemetry.traction,
                                        max(0, speedKmh),
                                        g_simHubVehicleSpeedKmh,
                                        max(0, rpm),
                                        g_simHubCurrentRpm,
                                        dtMs);

        g_simHubCurrentRpm = max(0, rpm);
        g_simHubVehicleSpeedKmh = max(0, speedKmh);
        g_simHubGear = max(0, gear);
        g_simHubThrottle = normalizeSimHubThrottle(throttle);
        g_simHubPitLimiterActive = pitLimiterActive;
        g_simHubMaxSeenRpm = max(max(g_simHubMaxSeenRpm, g_simHubCurrentRpm), maxRpm);
        // Only the network timestamp is authoritative for the network source.
        // g_lastSimHubTelemetryMs is kept as a mirror of the network value
        // purely for legacy readers; it is no longer written by the USB path.
        g_lastSimHubNetworkTelemetryMs = nowMs;
        g_lastSimHubTelemetryMs = nowMs;
        g_simHubEverReceived = true;
        ++g_simHubDebug.pollSuccessCount;
        g_simHubDebug.lastSuccessMs = nowMs;
        g_simHubDebug.lastError = "";
        setSimHubState(SimHubConnectionState::Live, true, false);

        LOG_DEBUG("SIMHUB", "SIMHUB_SAMPLE", String("rpm=") + g_simHubCurrentRpm + " speed=" + g_simHubVehicleSpeedKmh + " gear=" + g_simHubGear);
        return true;
    }

    void simHubTask(void *)
    {
        for (;;)
        {
            SimHubClientConfigSnapshot snapshot{};
            portENTER_CRITICAL(&g_simHubConfigMux);
            snapshot = g_simHubConfig;
            portEXIT_CRITICAL(&g_simHubConfigMux);

            if (!snapshot.enabled)
            {
                setSimHubState(SimHubConnectionState::Disabled, false, false);
                vTaskDelay(pdMS_TO_TICKS(250));
                continue;
            }

            if (snapshot.host[0] == '\0')
            {
                setSimHubState(SimHubConnectionState::WaitingForHost, false, true);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            if (!snapshot.networkAvailable)
            {
                setSimHubState(SimHubConnectionState::WaitingForNetwork, false, true);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            fetchSimHubFrame(snapshot);
            vTaskDelay(pdMS_TO_TICKS(snapshot.pollMs));
        }
    }
}

void initSimHubClient()
{
    if (g_simHubTaskHandle != nullptr)
    {
        return;
    }

    setSimHubState(SimHubConnectionState::WaitingForHost, false, true);
    xTaskCreate(simHubTask, "simHubTask", 7168, nullptr, 1, &g_simHubTaskHandle);
}

void simHubClientUpdateConfig()
{
    const WifiStatus wifiStatus = getWifiStatus();
    const String host = trimmed(cfg.simHubHost);
    const SimRuntimeTransportMode runtimeMode =
        resolveSimRuntimeTransportMode(cfg.telemetryPreference, cfg.simTransportPreference);
    const bool usbFresh = usbSimTelemetryFresh(millis());
    const bool shouldPoll = shouldPollSimHubNetwork(runtimeMode, g_activeTelemetrySource, usbFresh);

    SimHubClientConfigSnapshot next{};
    host.toCharArray(next.host, sizeof(next.host));
    next.port = cfg.simHubPort;
    next.pollMs = cfg.simHubPollMs;
    next.preference = cfg.telemetryPreference;
    next.transport = cfg.simTransportPreference;
    next.networkAvailable = wifiStatus.staConnected || wifiStatus.apActive;
    if (!shouldPoll && telemetryAllowsNetworkSim(cfg.telemetryPreference, cfg.simTransportPreference))
    {
        if (!g_simHubPollSuppressed)
        {
            ++g_simHubDebug.suppressedWhileUsbCount;
            g_simHubPollSuppressed = true;
        }
    }
    else
    {
        g_simHubPollSuppressed = false;
    }
    next.enabled = telemetryAllowsNetworkSim(cfg.telemetryPreference, cfg.simTransportPreference) && shouldPoll;

    portENTER_CRITICAL(&g_simHubConfigMux);
    g_simHubConfig = next;
    portEXIT_CRITICAL(&g_simHubConfigMux);
}
