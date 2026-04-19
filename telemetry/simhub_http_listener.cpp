#include "simhub_http_listener.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include "json_extract.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
    constexpr intptr_t kInvalidSocketHandle = static_cast<intptr_t>(INVALID_SOCKET);
#else
    constexpr intptr_t kInvalidSocketHandle = -1;
#endif

    void close_socket_handle(intptr_t handle)
    {
#ifdef _WIN32
        if (handle != static_cast<intptr_t>(INVALID_SOCKET))
        {
            closesocket(static_cast<SOCKET>(handle));
        }
#else
        if (handle >= 0)
        {
            close(static_cast<int>(handle));
        }
#endif
    }

    std::string trim_copy(std::string value)
    {
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch)
                                                { return std::isspace(ch) == 0; }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch)
                                 { return std::isspace(ch) == 0; })
                        .base(),
                    value.end());
        return value;
    }

    bool parse_loose_number(const std::string &payload, double &value)
    {
        const std::string token = trim_copy(payload);
        if (token.empty())
        {
            return false;
        }

        try
        {
            value = std::stod(token);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool parse_loose_bool(const std::string &payload, bool &value)
    {
        std::string token = trim_copy(payload);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
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

    bool parse_time_to_ms(const std::string &token, bool secondsByDefault, uint32_t &milliseconds)
    {
        const std::string trimmed = trim_copy(token);
        if (trimmed.empty())
        {
            return false;
        }

        if (trimmed.find(':') != std::string::npos)
        {
            std::stringstream parser(trimmed);
            std::string segment;
            std::vector<std::string> parts;
            while (std::getline(parser, segment, ':'))
            {
                parts.push_back(segment);
            }
            if (parts.size() < 2 || parts.size() > 3)
            {
                return false;
            }

            double secondsPart = 0.0;
            try
            {
                secondsPart = std::stod(parts.back());
            }
            catch (...)
            {
                return false;
            }

            int hours = 0;
            int minutes = 0;
            try
            {
                if (parts.size() == 3)
                {
                    hours = std::stoi(parts[0]);
                    minutes = std::stoi(parts[1]);
                }
                else
                {
                    minutes = std::stoi(parts[0]);
                }
            }
            catch (...)
            {
                return false;
            }

            const double totalMs = ((static_cast<double>(hours) * 3600.0) +
                                    (static_cast<double>(minutes) * 60.0) +
                                    secondsPart) *
                                   1000.0;
            if (totalMs < 0.0)
            {
                return false;
            }

            milliseconds = static_cast<uint32_t>(std::llround(totalMs));
            return true;
        }

        double numericValue = 0.0;
        if (!parse_loose_number(trimmed, numericValue) || numericValue < 0.0)
        {
            return false;
        }

        const double millisValue =
            secondsByDefault
                ? ((numericValue <= 600.0) ? numericValue * 1000.0 : numericValue)
                : ((numericValue <= 86400.0) ? numericValue * 1000.0 : numericValue);
        milliseconds = static_cast<uint32_t>(std::llround(millisValue));
        return true;
    }

    bool extract_number(const std::string &payload, std::initializer_list<const char *> keys, double &value)
    {
        return telemetry_json::extract_json_number(payload, keys, value);
    }

    bool extract_int(const std::string &payload, std::initializer_list<const char *> keys, int &value)
    {
        double numericValue = 0.0;
        if (telemetry_json::extract_json_number(payload, keys, numericValue))
        {
            value = static_cast<int>(std::lround(numericValue));
            return true;
        }

        std::string token;
        if (!telemetry_json::extract_json_token(payload, keys, token))
        {
            return false;
        }

        try
        {
            value = std::stoi(trim_copy(token));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool extract_time_value(const std::string &payload,
                            std::initializer_list<const char *> keys,
                            bool secondsByDefault,
                            uint32_t &milliseconds)
    {
        std::string token;
        if (!telemetry_json::extract_json_token(payload, keys, token))
        {
            return false;
        }
        return parse_time_to_ms(token, secondsByDefault, milliseconds);
    }

    bool extract_number_from_payloads(const std::string &primary,
                                      const std::string &secondary,
                                      std::initializer_list<const char *> keys,
                                      double &value)
    {
        return extract_number(primary, keys, value) || extract_number(secondary, keys, value);
    }

    bool extract_int_from_payloads(const std::string &primary,
                                   const std::string &secondary,
                                   std::initializer_list<const char *> keys,
                                   int &value)
    {
        return extract_int(primary, keys, value) || extract_int(secondary, keys, value);
    }

    bool extract_time_from_payloads(const std::string &primary,
                                    const std::string &secondary,
                                    std::initializer_list<const char *> keys,
                                    bool secondsByDefault,
                                    uint32_t &milliseconds)
    {
        return extract_time_value(primary, keys, secondsByDefault, milliseconds) ||
               extract_time_value(secondary, keys, secondsByDefault, milliseconds);
    }

    bool extract_bool_from_payloads(const std::string &primary,
                                    const std::string &secondary,
                                    std::initializer_list<const char *> keys,
                                    bool &value)
    {
        return telemetry_json::extract_json_loose_bool(primary, keys, value) ||
               telemetry_json::extract_json_loose_bool(secondary, keys, value);
    }

    SideLedFlag parse_flag_name(std::string token)
    {
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
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

    uint8_t severity_from_distance(double distance, bool detected)
    {
        if (!detected)
        {
            return 0;
        }
        if (distance > 0.0 && distance <= 0.14)
        {
            return 4;
        }
        if (distance > 0.0 && distance <= 0.30)
        {
            return 2;
        }
        return 1;
    }

    float clamp_slip_value(double value)
    {
        return std::clamp(static_cast<float>(std::abs(value)), 0.0f, 2.0f);
    }

    void assign_side_led_metrics(const std::string &gameDataPayload,
                                 const std::string &simplePayload,
                                 float throttle,
                                 float brake,
                                 NormalizedTelemetryFrame &frame)
    {
        SideLedTelemetry side{};

        bool boolValue = false;
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"Flag_Green"}, boolValue) && boolValue)
        {
            side.flags.green = true;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"Flag_Yellow"}, boolValue) && boolValue)
        {
            side.flags.yellow = true;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"Flag_Blue"}, boolValue) && boolValue)
        {
            side.flags.blue = true;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"Flag_Red"}, boolValue) && boolValue)
        {
            side.flags.red = true;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"Flag_White"}, boolValue) && boolValue)
        {
            side.flags.white = true;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"Flag_Black"}, boolValue) && boolValue)
        {
            side.flags.black = true;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"Flag_Orange"}, boolValue) && boolValue)
        {
            side.flags.orange = true;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"Flag_Checkered"}, boolValue) && boolValue)
        {
            side.flags.checkered = true;
        }

        std::string flagName;
        if (telemetry_json::extract_json_token(gameDataPayload, {"Flag_Name"}, flagName) ||
            telemetry_json::extract_json_token(simplePayload, {"Flag_Name"}, flagName))
        {
            side.flags.current = parse_flag_name(flagName);
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

        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"SpotterCarLeft"}, boolValue))
        {
            side.spotter.left = boolValue;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"SpotterCarRight"}, boolValue))
        {
            side.spotter.right = boolValue;
        }

        double numericValue = 0.0;
        double leftDistance = -1.0;
        double rightDistance = -1.0;
        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"SpotterCarLeftDistance"}, numericValue))
        {
            leftDistance = numericValue;
        }
        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"SpotterCarRightDistance"}, numericValue))
        {
            rightDistance = numericValue;
        }
        side.spotter.leftSeverity = severity_from_distance(leftDistance, side.spotter.left);
        side.spotter.rightSeverity = severity_from_distance(rightDistance, side.spotter.right);
        side.spotter.leftClose = side.spotter.leftSeverity >= 4;
        side.spotter.rightClose = side.spotter.rightSeverity >= 4;

        double angle = 0.0;
        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"SpotterCarLeftAngle"}, angle))
        {
            side.spotter.leftRear = angle < 0.0;
        }
        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"SpotterCarRightAngle"}, angle))
        {
            side.spotter.rightRear = angle < 0.0;
        }

        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"PitLimiterOn", "FeedbackData.LimiterActive", "FeedbackData.SpeedLimiterActive"}, boolValue))
        {
            side.warnings.pitLimiter = boolValue;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"IsInPitLane", "IsInPit"}, boolValue))
        {
            side.warnings.inPitlane = boolValue;
        }
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"CarSettings_FuelAlertActive", "FuelAlertActive"}, boolValue))
        {
            side.warnings.lowFuel = boolValue;
        }
        else if (extract_number_from_payloads(gameDataPayload, simplePayload, {"EstimatedFuelRemaingLaps", "EstimatedFuelRemainingLaps", "FuelEstimatedLaps", "FuelLapsRemaining"}, numericValue))
        {
            side.warnings.lowFuel = numericValue > 0.0 && numericValue <= 2.0;
        }

        double oilPressure = 0.0;
        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"OilPressure", "EngineOilPressure", "OilPressureBar"}, oilPressure))
        {
            side.warnings.oil = oilPressure > 0.0 && oilPressure < 1.2;
        }

        double oilTemperature = 0.0;
        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"OilTemperature", "OilTemp", "EngineOilTemp"}, oilTemperature))
        {
            side.warnings.oil = side.warnings.oil || oilTemperature >= 130.0;
        }

        double waterTemperature = 0.0;
        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"WaterTemperature", "WaterTemp", "CoolantTemperature", "CoolantTemp"}, waterTemperature))
        {
            side.warnings.waterTemp = waterTemperature >= 108.0;
            side.warnings.engine = waterTemperature >= 115.0;
        }

        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"EngineWarning", "EngineAlarm", "CheckEngine"}, boolValue))
        {
            side.warnings.engine = side.warnings.engine || boolValue;
        }

        double damageValue = 0.0;
        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"CarDamagesMax", "CarDamagesAvg", "CarDamage1", "CarDamage2", "CarDamage3", "CarDamage4", "CarDamage5"}, damageValue))
        {
            side.warnings.damage = damageValue >= 0.15;
        }

        double frontLeftSlip = 0.0;
        double rearLeftSlip = 0.0;
        double frontRightSlip = 0.0;
        double rearRightSlip = 0.0;
        double directTractionLoss = 0.0;
        const bool hasFrontLeftSlip = extract_number_from_payloads(gameDataPayload, simplePayload, {"FeedbackData.FrontLeftWheelSlip", "FrontLeftWheelSlip", "WheelSlipFL", "SlipFL"}, frontLeftSlip);
        const bool hasRearLeftSlip = extract_number_from_payloads(gameDataPayload, simplePayload, {"FeedbackData.RearLeftWheelSlip", "RearLeftWheelSlip", "WheelSlipRL", "SlipRL"}, rearLeftSlip);
        const bool hasFrontRightSlip = extract_number_from_payloads(gameDataPayload, simplePayload, {"FeedbackData.FrontRightWheelSlip", "FrontRightWheelSlip", "WheelSlipFR", "SlipFR"}, frontRightSlip);
        const bool hasRearRightSlip = extract_number_from_payloads(gameDataPayload, simplePayload, {"FeedbackData.RearRightWheelSlip", "RearRightWheelSlip", "WheelSlipRR", "SlipRR"}, rearRightSlip);
        const bool hasDirectTractionLoss = extract_number_from_payloads(gameDataPayload, simplePayload, {"FeedbackData.DirectTractionLoss", "DirectTractionLoss", "TractionLoss"}, directTractionLoss);
        double longitudinalAccel = 0.0;
        const bool hasLongitudinalAccel = extract_number_from_payloads(gameDataPayload,
                                                                       simplePayload,
                                                                       {"LongAccel", "LongitudinalAcceleration", "AccelerationX", "AccelX", "LocalAccelerationX", "VehicleAccelerationX"},
                                                                       longitudinalAccel);

        const float leftSlip = std::max(clamp_slip_value(frontLeftSlip), clamp_slip_value(rearLeftSlip)) +
                               (hasDirectTractionLoss ? (clamp_slip_value(directTractionLoss) * 0.35f) : 0.0f);
        const float rightSlip = std::max(clamp_slip_value(frontRightSlip), clamp_slip_value(rearRightSlip)) +
                                (hasDirectTractionLoss ? (clamp_slip_value(directTractionLoss) * 0.35f) : 0.0f);

        side.traction.throttle = side_led_normalize_input(throttle);
        side.traction.brake = side_led_normalize_input(brake);
        if (hasLongitudinalAccel)
        {
            if (longitudinalAccel > 0.0)
            {
                side.traction.throttle = std::max(side.traction.throttle,
                                                  side_led_normalize_longitudinal_drive(static_cast<float>(longitudinalAccel)));
            }
            else if (longitudinalAccel < 0.0)
            {
                side.traction.brake = std::max(side.traction.brake,
                                               side_led_normalize_longitudinal_brake(static_cast<float>(-longitudinalAccel)));
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

        frame.sideLeds = side;
    }

    void assign_session_metrics(const std::string &gameDataPayload,
                                const std::string &simplePayload,
                                NormalizedTelemetryFrame &frame)
    {
        UiSessionData session{};

        double deltaSeconds = 0.0;
        if (extract_number_from_payloads(gameDataPayload,
                                         simplePayload,
                                         {"DeltaToBestLap", "LiveDeltaToBestLap", "LiveDeltaToSessionBest", "CurrentLapDeltaToBest", "BestLapDelta"},
                                         deltaSeconds))
        {
            session.hasDelta = true;
            session.deltaSeconds = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_time_from_payloads(gameDataPayload,
                                       simplePayload,
                                       {"PredictedLapTime", "EstimatedLapTime", "CurrentLapPredicted", "LivePredictedLapTime"},
                                       true,
                                       session.predictedLapMs))
        {
            session.hasPredictedLap = true;
            session.hasAnyData = true;
        }

        if (extract_time_from_payloads(gameDataPayload,
                                       simplePayload,
                                       {"LastLapTime", "LapTimePrevious", "PreviousLapTime"},
                                       true,
                                       session.lastLapMs))
        {
            session.hasLastLap = true;
            session.hasAnyData = true;
        }

        if (extract_time_from_payloads(gameDataPayload,
                                       simplePayload,
                                       {"BestLapTime", "LapTimeBest", "SessionBestLapTime"},
                                       true,
                                       session.bestLapMs))
        {
            session.hasBestLap = true;
            session.hasAnyData = true;
        }

        if (!session.hasPredictedLap && session.hasBestLap && session.hasDelta)
        {
            const double predictedMs = static_cast<double>(session.bestLapMs) + (static_cast<double>(session.deltaSeconds) * 1000.0);
            if (predictedMs > 0.0)
            {
                session.predictedLapMs = static_cast<uint32_t>(std::llround(predictedMs));
                session.hasPredictedLap = true;
                session.hasAnyData = true;
            }
        }

        if (extract_time_from_payloads(gameDataPayload,
                                       simplePayload,
                                       {"SessionTimeLeft", "StintTimeLeft", "RemainingTime", "SessionTime", "SessionRunningTime"},
                                       false,
                                       session.sessionClockMs))
        {
            session.hasSessionClock = true;
            session.hasAnyData = true;
        }

        if (extract_int_from_payloads(gameDataPayload,
                                      simplePayload,
                                      {"Position", "SessionPosition", "RacePosition", "CarPosition"},
                                      session.position))
        {
            session.hasPosition = session.position > 0;
            session.hasAnyData = session.hasAnyData || session.hasPosition;
        }

        if (extract_int_from_payloads(gameDataPayload,
                                      simplePayload,
                                      {"TotalPositions", "TotalCars", "CarCount", "NbCars"},
                                      session.totalPositions))
        {
            session.hasTotalPositions = session.totalPositions > 0;
            session.hasAnyData = session.hasAnyData || session.hasTotalPositions;
        }
        else
        {
            int opponentsCount = 0;
            if (extract_int_from_payloads(gameDataPayload, simplePayload, {"OpponentsCount", "OpponentCount"}, opponentsCount))
            {
                session.totalPositions = std::max(0, opponentsCount) + 1;
                session.hasTotalPositions = session.totalPositions > 0;
                session.hasAnyData = session.hasAnyData || session.hasTotalPositions;
            }
        }

        if (extract_int_from_payloads(gameDataPayload,
                                      simplePayload,
                                      {"CurrentLap", "Lap", "LapNumber"},
                                      session.lap))
        {
            session.hasLap = session.lap > 0;
            session.hasAnyData = session.hasAnyData || session.hasLap;
        }
        else
        {
            int completedLaps = 0;
            if (extract_int_from_payloads(gameDataPayload, simplePayload, {"CompletedLaps"}, completedLaps))
            {
                session.lap = std::max(0, completedLaps) + 1;
                session.hasLap = session.lap > 0;
                session.hasAnyData = session.hasAnyData || session.hasLap;
            }
        }

        if (extract_int_from_payloads(gameDataPayload,
                                      simplePayload,
                                      {"TotalLaps", "SessionLaps", "RaceLaps"},
                                      session.totalLaps))
        {
            session.hasTotalLaps = session.totalLaps > 0;
            session.hasAnyData = session.hasAnyData || session.hasTotalLaps;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"Fuel", "FuelRemaining", "FuelLeft", "FuelLevel"}, deltaSeconds))
        {
            session.hasFuelLiters = true;
            session.fuelLiters = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"FuelAvgPerLap", "FuelPerLap", "FuelConsumptionPerLap"}, deltaSeconds))
        {
            session.hasFuelAvgPerLap = true;
            session.fuelAvgPerLap = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"FuelLapsRemaining", "FuelEstimatedLaps", "RemainingLapsOnFuel"}, deltaSeconds))
        {
            session.hasFuelLapsRemaining = true;
            session.fuelLapsRemaining = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }
        else if (session.hasFuelLiters && session.hasFuelAvgPerLap && session.fuelAvgPerLap > 0.001f)
        {
            session.hasFuelLapsRemaining = true;
            session.fuelLapsRemaining = session.fuelLiters / session.fuelAvgPerLap;
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"OilTemp", "OilTemperature", "EngineOilTemp", "OilTempC"}, deltaSeconds))
        {
            session.hasOilTemp = true;
            session.oilTempC = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"OilPressure", "OilPress", "EngineOilPressure", "OilPressureBar"}, deltaSeconds))
        {
            session.hasOilPressure = true;
            session.oilPressureBar = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"OilLevel", "EngineOilLevel"}, deltaSeconds))
        {
            session.hasOilLevel = true;
            session.oilLevel = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"FuelPressure", "FuelPress", "FuelPressureBar"}, deltaSeconds))
        {
            session.hasFuelPressure = true;
            session.fuelPressureBar = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"WaterTemp", "WaterTemperature", "CoolantTemp", "CoolantTemperature", "WaterTempC"}, deltaSeconds))
        {
            session.hasWaterTemp = true;
            session.waterTempC = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"BatteryVoltage", "BatteryVolt", "Battery", "Voltage"}, deltaSeconds))
        {
            session.hasBatteryVolts = true;
            session.batteryVolts = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_int_from_payloads(gameDataPayload, simplePayload, {"TcLevel", "TCLevel", "TractionControl", "Tc", "TC"}, session.tractionControl))
        {
            session.hasTractionControl = true;
            session.hasAnyData = true;
        }

        if (extract_int_from_payloads(gameDataPayload, simplePayload, {"AbsLevel", "ABSLevel", "Abs", "ABS"}, session.absLevel))
        {
            session.hasAbs = true;
            session.hasAnyData = true;
        }

        if (extract_number_from_payloads(gameDataPayload, simplePayload, {"BrakeBias", "BrakeBalance", "BBalance"}, deltaSeconds))
        {
            session.hasBrakeBias = true;
            session.brakeBias = static_cast<float>(deltaSeconds);
            session.hasAnyData = true;
        }

        if (extract_int_from_payloads(gameDataPayload, simplePayload, {"EngineMap", "Map", "EngineMode"}, session.engineMap))
        {
            session.hasEngineMap = true;
            session.hasAnyData = true;
        }

        bool tcCutActive = false;
        if (extract_bool_from_payloads(gameDataPayload, simplePayload, {"TcInAction", "TCInAction", "TractionControlActive"}, tcCutActive))
        {
            session.hasTractionCut = true;
            session.tractionCut = tcCutActive ? 1 : 0;
            session.hasAnyData = true;
        }
        else if (extract_int_from_payloads(gameDataPayload, simplePayload, {"TcCut", "TCCut"}, session.tractionCut))
        {
            session.hasTractionCut = true;
            session.hasAnyData = true;
        }

        frame.session = session;
    }
}

SimHubHttpListener::SimHubHttpListener() = default;

SimHubHttpListener::~SimHubHttpListener()
{
    stop();
}

bool SimHubHttpListener::start(uint16_t port, uint32_t pollIntervalMs, bool debugLogging)
{
    stop();
    port_ = port;
    pollIntervalMs_ = std::max<uint32_t>(pollIntervalMs, 15U);
    debugLogging_ = debugLogging;

    stopRequested_ = false;
    running_ = true;
    worker_ = std::thread(&SimHubHttpListener::run, this);
    std::cout << "[telemetry] Polling SimHub API on http://127.0.0.1:" << port_ << '\n';
    return true;
}

void SimHubHttpListener::stop()
{
    stopRequested_ = true;
    if (worker_.joinable())
    {
        worker_.join();
    }
    running_ = false;

    std::lock_guard<std::mutex> lock(mutex_);
    latestFrame_ = NormalizedTelemetryFrame{};
    latestSequence_ = 0;
    lastPolledSequence_ = 0;
    sourceReachable_ = false;
    waitingForData_ = true;
}

bool SimHubHttpListener::isRunning() const
{
    return running_;
}

bool SimHubHttpListener::poll(NormalizedTelemetryFrame &frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (latestSequence_ == 0 || latestSequence_ == lastPolledSequence_)
    {
        return false;
    }

    frame = latestFrame_;
    lastPolledSequence_ = latestSequence_;
    return true;
}

bool SimHubHttpListener::sourceReachable() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sourceReachable_;
}

bool SimHubHttpListener::waitingForData() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return waitingForData_;
}

void SimHubHttpListener::run()
{
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "[telemetry] Failed to initialize WinSock for SimHub API polling\n";
        running_ = false;
        return;
    }
#endif

    while (!stopRequested_)
    {
        bool gameRunning = false;
        bool hasNewData = false;
        const bool stateAvailable = fetch_game_state(gameRunning, hasNewData);
        set_status(stateAvailable, !(stateAvailable && gameRunning && hasNewData));

        if (stateAvailable && gameRunning && hasNewData)
        {
            NormalizedTelemetryFrame frame{};
            if (fetch_frame(frame))
            {
                std::lock_guard<std::mutex> lock(mutex_);
                latestFrame_ = frame;
                ++latestSequence_;
                if (debugLogging_)
                {
                    logFrame(frame);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs_));
    }

#ifdef _WIN32
    WSACleanup();
#endif
    running_ = false;
}

bool SimHubHttpListener::fetch_game_state(bool &gameRunning, bool &hasNewData)
{
    std::string payload;
    if (!http_get("/Api/GetGameData", payload))
    {
        gameRunning = false;
        hasNewData = false;
        return false;
    }

    bool runningFlag = false;
    const bool hasRunningFlag = telemetry_json::extract_json_bool(payload, {"GameRunning", "RunningGameProcessDetected"}, runningFlag);
    gameRunning = hasRunningFlag && runningFlag;
    hasNewData = payload.find("\"NewData\":null") == std::string::npos &&
                 payload.find("\"NewData\": null") == std::string::npos;
    return true;
}

bool SimHubHttpListener::fetch_frame(NormalizedTelemetryFrame &frame)
{
    std::string gameDataPayload;
    http_get("/Api/GetGameData", gameDataPayload);

    std::string simplePayload;
    if (!http_get("/Api/GetGameDataSimple", simplePayload))
    {
        return false;
    }

    double rpm = 0.0;
    double speed = 0.0;
    double throttle = 0.0;
    double brake = 0.0;
    int gear = 0;

    const bool hasRpm = telemetry_json::extract_json_number(simplePayload, {"rpms", "rpm", "Rpm", "RPM"}, rpm);
    const bool hasSpeed = telemetry_json::extract_json_number(simplePayload, {"speed", "speedKmh", "SpeedKmh"}, speed);
    const bool hasGear = telemetry_json::extract_json_gear(simplePayload, {"gear", "Gear"}, gear);
    if (!hasRpm && !hasSpeed && !hasGear)
    {
        return false;
    }

    std::string throttlePayload;
        if (http_get("/Api/GetProperty/DataCorePlugin.GameData.NewData.Throttle", throttlePayload))
        {
            if (!telemetry_json::extract_json_number(throttlePayload, {"Throttle", "throttle", "value"}, throttle))
            {
                parse_loose_number(throttlePayload, throttle);
            }
        }
        else if (!extract_number_from_payloads(gameDataPayload,
                                               simplePayload,
                                               {"Throttle", "throttle", "Gas", "gas", "Accelerator", "AcceleratorPedal", "AcceleratorPedalPosition", "ThrottleRaw", "PedalThrottle"},
                                               throttle))
        {
            throttle = 0.0;
        }

        std::string brakePayload;
        if (http_get("/Api/GetProperty/DataCorePlugin.GameData.NewData.Brake", brakePayload))
        {
            if (!telemetry_json::extract_json_number(brakePayload, {"Brake", "brake", "BrakePedal", "value"}, brake))
            {
                parse_loose_number(brakePayload, brake);
            }
        }
        else
        {
            extract_number_from_payloads(gameDataPayload,
                                         simplePayload,
                                         {"Brake", "brake", "BrakePedal", "BrakePedalPosition", "FeedbackData.Brake", "BrakeRaw", "PedalBrake", "BrakeInput"},
                                         brake);
        }

    frame.rpm = std::max(0, static_cast<int>(std::lround(rpm)));
    frame.speedKmh = std::max(0, static_cast<int>(std::lround(speed)));
    frame.gear = std::max(0, gear);
    frame.throttle = side_led_normalize_input(static_cast<float>(throttle));
    assign_session_metrics(gameDataPayload, simplePayload, frame);
    assign_side_led_metrics(gameDataPayload,
                            simplePayload,
                            frame.throttle,
                            side_led_normalize_input(static_cast<float>(brake)),
                            frame);
    frame.stale = false;
    frame.usingFallback = false;
    frame.live = true;
    return true;
}

bool SimHubHttpListener::http_get(const char *path, std::string &responseBody) const
{
    responseBody.clear();

    const intptr_t socketHandle = static_cast<intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (socketHandle == kInvalidSocketHandle)
    {
        return false;
    }

    const int timeoutMs = 300;
#ifdef _WIN32
    setsockopt(static_cast<SOCKET>(socketHandle), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(static_cast<SOCKET>(socketHandle), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000);
    setsockopt(static_cast<int>(socketHandle), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(static_cast<int>(socketHandle), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int connectResult = 0;
#ifdef _WIN32
    connectResult = ::connect(static_cast<SOCKET>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#else
    connectResult = ::connect(static_cast<int>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#endif
    if (connectResult != 0)
    {
        close_socket_handle(socketHandle);
        return false;
    }

    const std::string request = std::string("GET ") + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";

    size_t sentBytes = 0;
    while (sentBytes < request.size())
    {
#ifdef _WIN32
        const int chunk = ::send(static_cast<SOCKET>(socketHandle), request.data() + sentBytes, static_cast<int>(request.size() - sentBytes), 0);
#else
        const int chunk = static_cast<int>(::send(static_cast<int>(socketHandle), request.data() + sentBytes, request.size() - sentBytes, 0));
#endif
        if (chunk <= 0)
        {
            close_socket_handle(socketHandle);
            return false;
        }
        sentBytes += static_cast<size_t>(chunk);
    }

    std::string response;
    std::array<char, 2048> buffer{};
    while (true)
    {
#ifdef _WIN32
        const int bytesRead = ::recv(static_cast<SOCKET>(socketHandle), buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const int bytesRead = static_cast<int>(::recv(static_cast<int>(socketHandle), buffer.data(), buffer.size(), 0));
#endif
        if (bytesRead <= 0)
        {
            break;
        }
        response.append(buffer.data(), static_cast<size_t>(bytesRead));
    }

    close_socket_handle(socketHandle);

    const size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        return false;
    }

    if (response.rfind("HTTP/1.1 200", 0) != 0 && response.rfind("HTTP/1.0 200", 0) != 0)
    {
        return false;
    }

    responseBody = response.substr(headerEnd + 4);
    return true;
}

void SimHubHttpListener::set_status(bool sourceReachable, bool waitingForData)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sourceReachable_ = sourceReachable;
    waitingForData_ = waitingForData;
}

void SimHubHttpListener::logFrame(const NormalizedTelemetryFrame &frame) const
{
    std::cout << "[telemetry] SimHub API sample received\n";
    std::cout << "[telemetry] Parsed rpm=" << frame.rpm
              << " speed=" << frame.speedKmh
              << " gear=" << frame.gear
              << " throttle=" << frame.throttle
              << '\n';
}
