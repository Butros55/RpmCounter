#include "simhub_udp_listener.h"

#include <algorithm>
#include <cctype>
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
#include <fcntl.h>
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

    bool is_would_block_error()
    {
#ifdef _WIN32
        const int errorCode = WSAGetLastError();
        return errorCode == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
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

        try
        {
            const double numericValue = std::stod(trimmed);
            if (numericValue < 0.0)
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
        catch (...)
        {
            return false;
        }
    }

    bool extract_int_value(const std::string &payload, std::initializer_list<const char *> keys, int &value)
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

    void assign_session_metrics(const std::string &payload, NormalizedTelemetryFrame &frame)
    {
        UiSessionData session{};

        double numericValue = 0.0;
        if (telemetry_json::extract_json_number(payload,
                                                {"DeltaToBestLap", "LiveDeltaToBestLap", "LiveDeltaToSessionBest", "CurrentLapDeltaToBest", "BestLapDelta"},
                                                numericValue))
        {
            session.hasDelta = true;
            session.deltaSeconds = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (extract_time_value(payload, {"PredictedLapTime", "EstimatedLapTime", "CurrentLapPredicted", "LivePredictedLapTime"}, true, session.predictedLapMs))
        {
            session.hasPredictedLap = true;
            session.hasAnyData = true;
        }

        if (extract_time_value(payload, {"LastLapTime", "LapTimePrevious", "PreviousLapTime"}, true, session.lastLapMs))
        {
            session.hasLastLap = true;
            session.hasAnyData = true;
        }

        if (extract_time_value(payload, {"BestLapTime", "LapTimeBest", "SessionBestLapTime"}, true, session.bestLapMs))
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

        if (extract_time_value(payload, {"SessionTimeLeft", "StintTimeLeft", "RemainingTime", "SessionTime", "SessionRunningTime"}, false, session.sessionClockMs))
        {
            session.hasSessionClock = true;
            session.hasAnyData = true;
        }

        if (extract_int_value(payload, {"Position", "SessionPosition", "RacePosition", "CarPosition"}, session.position))
        {
            session.hasPosition = session.position > 0;
            session.hasAnyData = session.hasAnyData || session.hasPosition;
        }

        if (extract_int_value(payload, {"TotalPositions", "TotalCars", "CarCount", "NbCars"}, session.totalPositions))
        {
            session.hasTotalPositions = session.totalPositions > 0;
            session.hasAnyData = session.hasAnyData || session.hasTotalPositions;
        }
        else
        {
            int opponentsCount = 0;
            if (extract_int_value(payload, {"OpponentsCount", "OpponentCount"}, opponentsCount))
            {
                session.totalPositions = std::max(0, opponentsCount) + 1;
                session.hasTotalPositions = session.totalPositions > 0;
                session.hasAnyData = session.hasAnyData || session.hasTotalPositions;
            }
        }

        if (extract_int_value(payload, {"CurrentLap", "Lap", "LapNumber"}, session.lap))
        {
            session.hasLap = session.lap > 0;
            session.hasAnyData = session.hasAnyData || session.hasLap;
        }
        else
        {
            int completedLaps = 0;
            if (extract_int_value(payload, {"CompletedLaps"}, completedLaps))
            {
                session.lap = std::max(0, completedLaps) + 1;
                session.hasLap = session.lap > 0;
                session.hasAnyData = session.hasAnyData || session.hasLap;
            }
        }

        if (extract_int_value(payload, {"TotalLaps", "SessionLaps", "RaceLaps"}, session.totalLaps))
        {
            session.hasTotalLaps = session.totalLaps > 0;
            session.hasAnyData = session.hasAnyData || session.hasTotalLaps;
        }

        if (telemetry_json::extract_json_number(payload, {"Fuel", "FuelRemaining", "FuelLeft", "FuelLevel"}, numericValue))
        {
            session.hasFuelLiters = true;
            session.fuelLiters = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"FuelAvgPerLap", "FuelPerLap", "FuelConsumptionPerLap"}, numericValue))
        {
            session.hasFuelAvgPerLap = true;
            session.fuelAvgPerLap = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"FuelLapsRemaining", "FuelEstimatedLaps", "RemainingLapsOnFuel"}, numericValue))
        {
            session.hasFuelLapsRemaining = true;
            session.fuelLapsRemaining = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }
        else if (session.hasFuelLiters && session.hasFuelAvgPerLap && session.fuelAvgPerLap > 0.001f)
        {
            session.hasFuelLapsRemaining = true;
            session.fuelLapsRemaining = session.fuelLiters / session.fuelAvgPerLap;
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"OilTemp", "OilTemperature", "EngineOilTemp", "OilTempC"}, numericValue))
        {
            session.hasOilTemp = true;
            session.oilTempC = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"OilPressure", "OilPress", "EngineOilPressure", "OilPressureBar"}, numericValue))
        {
            session.hasOilPressure = true;
            session.oilPressureBar = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"OilLevel", "EngineOilLevel"}, numericValue))
        {
            session.hasOilLevel = true;
            session.oilLevel = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"FuelPressure", "FuelPress", "FuelPressureBar"}, numericValue))
        {
            session.hasFuelPressure = true;
            session.fuelPressureBar = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"WaterTemp", "WaterTemperature", "CoolantTemp", "CoolantTemperature", "WaterTempC"}, numericValue))
        {
            session.hasWaterTemp = true;
            session.waterTempC = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"BatteryVoltage", "BatteryVolt", "Battery", "Voltage"}, numericValue))
        {
            session.hasBatteryVolts = true;
            session.batteryVolts = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (extract_int_value(payload, {"TcLevel", "TCLevel", "TractionControl", "Tc", "TC"}, session.tractionControl))
        {
            session.hasTractionControl = true;
            session.hasAnyData = true;
        }

        if (extract_int_value(payload, {"AbsLevel", "ABSLevel", "Abs", "ABS"}, session.absLevel))
        {
            session.hasAbs = true;
            session.hasAnyData = true;
        }

        if (telemetry_json::extract_json_number(payload, {"BrakeBias", "BrakeBalance", "BBalance"}, numericValue))
        {
            session.hasBrakeBias = true;
            session.brakeBias = static_cast<float>(numericValue);
            session.hasAnyData = true;
        }

        if (extract_int_value(payload, {"EngineMap", "Map", "EngineMode"}, session.engineMap))
        {
            session.hasEngineMap = true;
            session.hasAnyData = true;
        }

        bool tcCutActive = false;
        if (telemetry_json::extract_json_bool(payload, {"TcInAction", "TCInAction", "TractionControlActive"}, tcCutActive))
        {
            session.hasTractionCut = true;
            session.tractionCut = tcCutActive ? 1 : 0;
            session.hasAnyData = true;
        }
        else if (extract_int_value(payload, {"TcCut", "TCCut"}, session.tractionCut))
        {
            session.hasTractionCut = true;
            session.hasAnyData = true;
        }

        frame.session = session;
    }

    void assign_side_led_metrics(const std::string &payload,
                                 float throttle,
                                 float brake,
                                 NormalizedTelemetryFrame &frame)
    {
        SideLedTelemetry side{};

        bool boolValue = false;
        if (telemetry_json::extract_json_loose_bool(payload, {"Flag_Green"}, boolValue) && boolValue)
        {
            side.flags.green = true;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"Flag_Yellow"}, boolValue) && boolValue)
        {
            side.flags.yellow = true;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"Flag_Blue"}, boolValue) && boolValue)
        {
            side.flags.blue = true;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"Flag_Red"}, boolValue) && boolValue)
        {
            side.flags.red = true;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"Flag_White"}, boolValue) && boolValue)
        {
            side.flags.white = true;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"Flag_Black"}, boolValue) && boolValue)
        {
            side.flags.black = true;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"Flag_Orange"}, boolValue) && boolValue)
        {
            side.flags.orange = true;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"Flag_Checkered"}, boolValue) && boolValue)
        {
            side.flags.checkered = true;
        }

        std::string flagName;
        if (telemetry_json::extract_json_token(payload, {"Flag_Name"}, flagName))
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

        if (telemetry_json::extract_json_loose_bool(payload, {"SpotterCarLeft"}, boolValue))
        {
            side.spotter.left = boolValue;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"SpotterCarRight"}, boolValue))
        {
            side.spotter.right = boolValue;
        }

        double numericValue = 0.0;
        double leftDistance = -1.0;
        double rightDistance = -1.0;
        if (telemetry_json::extract_json_number(payload, {"SpotterCarLeftDistance"}, numericValue))
        {
            leftDistance = numericValue;
        }
        if (telemetry_json::extract_json_number(payload, {"SpotterCarRightDistance"}, numericValue))
        {
            rightDistance = numericValue;
        }
        side.spotter.leftSeverity = severity_from_distance(leftDistance, side.spotter.left);
        side.spotter.rightSeverity = severity_from_distance(rightDistance, side.spotter.right);
        side.spotter.leftClose = side.spotter.leftSeverity >= 4;
        side.spotter.rightClose = side.spotter.rightSeverity >= 4;

        double angle = 0.0;
        if (telemetry_json::extract_json_number(payload, {"SpotterCarLeftAngle"}, angle))
        {
            side.spotter.leftRear = angle < 0.0;
        }
        if (telemetry_json::extract_json_number(payload, {"SpotterCarRightAngle"}, angle))
        {
            side.spotter.rightRear = angle < 0.0;
        }

        if (telemetry_json::extract_json_loose_bool(payload, {"PitLimiterOn", "FeedbackData.LimiterActive", "FeedbackData.SpeedLimiterActive"}, boolValue))
        {
            side.warnings.pitLimiter = boolValue;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"IsInPitLane", "IsInPit"}, boolValue))
        {
            side.warnings.inPitlane = boolValue;
        }
        if (telemetry_json::extract_json_loose_bool(payload, {"CarSettings_FuelAlertActive", "FuelAlertActive"}, boolValue))
        {
            side.warnings.lowFuel = boolValue;
        }
        else if (telemetry_json::extract_json_number(payload, {"EstimatedFuelRemaingLaps", "EstimatedFuelRemainingLaps", "FuelEstimatedLaps", "FuelLapsRemaining"}, numericValue))
        {
            side.warnings.lowFuel = numericValue > 0.0 && numericValue <= 2.0;
        }

        double oilPressure = 0.0;
        if (telemetry_json::extract_json_number(payload, {"OilPressure", "EngineOilPressure", "OilPressureBar"}, oilPressure))
        {
            side.warnings.oil = oilPressure > 0.0 && oilPressure < 1.2;
        }

        double oilTemperature = 0.0;
        if (telemetry_json::extract_json_number(payload, {"OilTemperature", "OilTemp", "EngineOilTemp"}, oilTemperature))
        {
            side.warnings.oil = side.warnings.oil || oilTemperature >= 130.0;
        }

        double waterTemperature = 0.0;
        if (telemetry_json::extract_json_number(payload, {"WaterTemperature", "WaterTemp", "CoolantTemperature", "CoolantTemp"}, waterTemperature))
        {
            side.warnings.waterTemp = waterTemperature >= 108.0;
            side.warnings.engine = waterTemperature >= 115.0;
        }

        if (telemetry_json::extract_json_loose_bool(payload, {"EngineWarning", "EngineAlarm", "CheckEngine"}, boolValue))
        {
            side.warnings.engine = side.warnings.engine || boolValue;
        }

        double damageValue = 0.0;
        if (telemetry_json::extract_json_number(payload, {"CarDamagesMax", "CarDamagesAvg", "CarDamage1", "CarDamage2", "CarDamage3", "CarDamage4", "CarDamage5"}, damageValue))
        {
            side.warnings.damage = damageValue >= 0.15;
        }

        double frontLeftSlip = 0.0;
        double rearLeftSlip = 0.0;
        double frontRightSlip = 0.0;
        double rearRightSlip = 0.0;
        double directTractionLoss = 0.0;
        const bool hasFrontLeftSlip = telemetry_json::extract_json_number(payload, {"FeedbackData.FrontLeftWheelSlip", "FrontLeftWheelSlip", "WheelSlipFL", "SlipFL"}, frontLeftSlip);
        const bool hasRearLeftSlip = telemetry_json::extract_json_number(payload, {"FeedbackData.RearLeftWheelSlip", "RearLeftWheelSlip", "WheelSlipRL", "SlipRL"}, rearLeftSlip);
        const bool hasFrontRightSlip = telemetry_json::extract_json_number(payload, {"FeedbackData.FrontRightWheelSlip", "FrontRightWheelSlip", "WheelSlipFR", "SlipFR"}, frontRightSlip);
        const bool hasRearRightSlip = telemetry_json::extract_json_number(payload, {"FeedbackData.RearRightWheelSlip", "RearRightWheelSlip", "WheelSlipRR", "SlipRR"}, rearRightSlip);
        const bool hasDirectTractionLoss = telemetry_json::extract_json_number(payload, {"FeedbackData.DirectTractionLoss", "DirectTractionLoss", "TractionLoss"}, directTractionLoss);
        double longitudinalAccel = 0.0;
        const bool hasLongitudinalAccel = telemetry_json::extract_json_number(payload,
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
}

SimHubUdpListener::SimHubUdpListener() = default;

SimHubUdpListener::~SimHubUdpListener()
{
    stop();
}

bool SimHubUdpListener::start(uint16_t port, bool debugLogging)
{
    stop();
    debugLogging_ = debugLogging;

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "[telemetry] Failed to initialize WinSock\n";
        return false;
    }
    winsockReady_ = true;
#endif

    const intptr_t socketHandle = static_cast<intptr_t>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (socketHandle == kInvalidSocketHandle)
    {
        std::cerr << "[telemetry] Failed to create SimHub UDP socket\n";
        stop();
        return false;
    }

#ifdef _WIN32
    u_long nonBlocking = 1;
    if (ioctlsocket(static_cast<SOCKET>(socketHandle), FIONBIO, &nonBlocking) != 0)
    {
        std::cerr << "[telemetry] Failed to configure non-blocking UDP socket\n";
        close_socket_handle(socketHandle);
        stop();
        return false;
    }
#else
    const int flags = fcntl(static_cast<int>(socketHandle), F_GETFL, 0);
    if (flags < 0 || fcntl(static_cast<int>(socketHandle), F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "[telemetry] Failed to configure non-blocking UDP socket\n";
        close_socket_handle(socketHandle);
        stop();
        return false;
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    int bindResult = 0;
#ifdef _WIN32
    bindResult = ::bind(static_cast<SOCKET>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#else
    bindResult = ::bind(static_cast<int>(socketHandle), reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#endif

    if (bindResult != 0)
    {
        std::cerr << "[telemetry] Failed to bind SimHub UDP socket to port " << port << '\n';
        close_socket_handle(socketHandle);
        stop();
        return false;
    }

    socketHandle_ = socketHandle;
    running_ = true;
    std::cout << "[telemetry] Listening for SimHub UDP on 127.0.0.1:" << port << '\n';
    return true;
}

void SimHubUdpListener::stop()
{
    if (socketHandle_ != kInvalidSocketHandle)
    {
        close_socket_handle(socketHandle_);
        socketHandle_ = kInvalidSocketHandle;
    }
    running_ = false;

#ifdef _WIN32
    if (winsockReady_)
    {
        WSACleanup();
        winsockReady_ = false;
    }
#endif
}

bool SimHubUdpListener::isRunning() const
{
    return running_;
}

bool SimHubUdpListener::poll(uint32_t nowMs, NormalizedTelemetryFrame &frame)
{
    if (!running_ || socketHandle_ == kInvalidSocketHandle)
    {
        return false;
    }

    bool receivedPacket = false;
    while (true)
    {
        char buffer[4096]{};
        sockaddr_in remoteAddress{};
#ifdef _WIN32
        int remoteLength = sizeof(remoteAddress);
        const int bytesRead = recvfrom(static_cast<SOCKET>(socketHandle_),
                                       buffer,
                                       static_cast<int>(sizeof(buffer) - 1),
                                       0,
                                       reinterpret_cast<sockaddr *>(&remoteAddress),
                                       &remoteLength);
#else
        socklen_t remoteLength = sizeof(remoteAddress);
        const int bytesRead = static_cast<int>(recvfrom(static_cast<int>(socketHandle_),
                                                        buffer,
                                                        sizeof(buffer) - 1,
                                                        0,
                                                        reinterpret_cast<sockaddr *>(&remoteAddress),
                                                        &remoteLength));
#endif

        if (bytesRead < 0)
        {
            if (is_would_block_error())
            {
                break;
            }

            std::cerr << "[telemetry] Error while receiving SimHub UDP packet\n";
            break;
        }

        buffer[bytesRead] = '\0';
        const std::string payload(buffer, static_cast<size_t>(bytesRead));
        NormalizedTelemetryFrame parsedFrame{};
        if (parsePacket(payload, nowMs, parsedFrame))
        {
            frame = parsedFrame;
            receivedPacket = true;
            if (debugLogging_)
            {
                logPacket(payload, parsedFrame);
            }
        }
    }

    return receivedPacket;
}

bool SimHubUdpListener::parsePacket(const std::string &payload, uint32_t nowMs, NormalizedTelemetryFrame &frame) const
{
    double rpm = 0.0;
    double speed = 0.0;
    double throttle = 0.0;
    double brake = 0.0;
    int gear = 0;

    const bool hasRpm = telemetry_json::extract_json_number(payload, {"rpm", "Rpm", "RPM", "engineRpm", "EngineRpm", "rpms"}, rpm);
    const bool hasSpeed = telemetry_json::extract_json_number(payload, {"speedKmh", "SpeedKmh", "speed", "Speed", "vehicleSpeed"}, speed);
    const bool hasGear = telemetry_json::extract_json_gear(payload, {"gear", "Gear", "currentGear", "CurrentGear"}, gear);
    const bool hasThrottle = telemetry_json::extract_json_number(payload, {"throttle", "Throttle", "gas", "Gas", "Accelerator", "AcceleratorPedal", "AcceleratorPedalPosition", "ThrottleRaw", "PedalThrottle"}, throttle);
    const bool hasBrake = telemetry_json::extract_json_number(payload, {"brake", "Brake", "BrakePedal", "BrakePedalPosition", "FeedbackData.Brake", "BrakeRaw", "PedalBrake", "BrakeInput"}, brake);

    if (!hasRpm && !hasSpeed && !hasGear)
    {
        return false;
    }

    frame.rpm = std::max(0, static_cast<int>(std::lround(rpm)));
    frame.speedKmh = std::max(0, static_cast<int>(std::lround(speed)));
    frame.gear = std::max(0, gear);
    frame.throttle = hasThrottle ? side_led_normalize_input(static_cast<float>(throttle)) : 0.0f;
    assign_session_metrics(payload, frame);
    assign_side_led_metrics(payload,
                            frame.throttle,
                            hasBrake ? side_led_normalize_input(static_cast<float>(brake)) : 0.0f,
                            frame);
    frame.timestampMs = nowMs;
    frame.stale = false;
    frame.usingFallback = false;
    frame.live = true;
    return true;
}

void SimHubUdpListener::logPacket(const std::string &payload, const NormalizedTelemetryFrame &frame) const
{
    std::cout << "[telemetry] SimHub packet received (" << payload.size() << " bytes)\n";
    std::cout << "[telemetry] Parsed rpm=" << frame.rpm
              << " speed=" << frame.speedKmh
              << " gear=" << frame.gear
              << " throttle=" << frame.throttle
              << '\n';
}
