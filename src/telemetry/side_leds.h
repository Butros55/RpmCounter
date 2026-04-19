#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr size_t SIDE_LED_MIN_COUNT_PER_SIDE = 4;
constexpr size_t SIDE_LED_MAX_COUNT_PER_SIDE = 16;

enum class SideLedPreset : uint8_t
{
    Gt3 = 0,
    Casual = 1,
    Minimal = 2
};

enum class SideLedSource : uint8_t
{
    Off = 0,
    Warning,
    Flag,
    Spotter,
    Traction,
    Idle,
    Test
};

enum class SideLedPriority : uint8_t
{
    Off = 0,
    Idle = 1,
    Spotter = 2,
    Flag = 3,
    Warning = 4,
    Critical = 5,
    Test = 6
};

enum class SideLedFlag : uint8_t
{
    None = 0,
    Green,
    Yellow,
    Blue,
    Red,
    White,
    Black,
    Orange,
    Checkered
};

enum class SideLedEvent : uint8_t
{
    None = 0,
    Idle,
    TractionAccelerating,
    TractionBraking,
    TractionCoast
};

enum class SideLedPriorityMode : uint8_t
{
    Low = 0,
    Normal = 1,
    High = 2,
    Override = 3
};

enum class SideLedWarningPriorityMode : uint8_t
{
    Normal = 0,
    CriticalOnly = 1,
    AlwaysOverride = 2
};

enum class SideLedTestPattern : uint8_t
{
    None = 0,
    Accelerate,
    Brake,
    TractionLeft,
    TractionRight,
    TractionBoth
};

enum class SideLedTractionDirection : uint8_t
{
    Off = 0,
    Accelerating,
    Braking
};

struct SpotterState
{
    bool left = false;
    bool right = false;
    bool leftClose = false;
    bool rightClose = false;
    bool leftRear = false;
    bool rightRear = false;
    uint8_t leftSeverity = 0;
    uint8_t rightSeverity = 0;
};

struct FlagState
{
    SideLedFlag current = SideLedFlag::None;
    bool green = false;
    bool yellow = false;
    bool blue = false;
    bool red = false;
    bool white = false;
    bool black = false;
    bool orange = false;
    bool checkered = false;
};

struct WarningState
{
    bool lowFuel = false;
    bool engine = false;
    bool oil = false;
    bool waterTemp = false;
    bool damage = false;
    bool pitLimiter = false;
    bool inPitlane = false;
};

struct TractionState
{
    bool active = false;
    float throttle = 0.0f;
    float brake = 0.0f;
    float leftSlip = 0.0f;
    float rightSlip = 0.0f;
    uint8_t leftLevel = 0;
    uint8_t rightLevel = 0;
    bool leftCritical = false;
    bool rightCritical = false;
    SideLedTractionDirection direction = SideLedTractionDirection::Off;
};

struct SideLedTelemetry
{
    FlagState flags{};
    SpotterState spotter{};
    WarningState warnings{};
    TractionState traction{};
};

struct SideLedColorMap
{
    uint32_t off = 0x000000u;
    uint32_t dim = 0x10202Au;
    uint32_t spotter = 0xD254FFu;
    uint32_t spotterClose = 0xFF5A8Cu;
    uint32_t tractionLow = 0x4AF07Bu;
    uint32_t tractionMedium = 0xFFD44Au;
    uint32_t tractionHigh = 0xFF6A48u;
    uint32_t flagGreen = 0x4AF07Bu;
    uint32_t flagYellow = 0xFFD74Au;
    uint32_t flagBlue = 0x5AAEFFu;
    uint32_t flagRed = 0xFF5A48u;
    uint32_t flagWhite = 0xF5F7FBu;
    uint32_t flagBlack = 0xFF8C38u;
    uint32_t flagCheckered = 0xE6EEF8u;
    uint32_t warningLowFuel = 0xFFAE46u;
    uint32_t warningEngine = 0xFF5A48u;
    uint32_t warningOil = 0xFF7B4Au;
    uint32_t warningWater = 0x6FD4FFu;
    uint32_t warningDamage = 0xFF6A62u;
    uint32_t warningPitLimiter = 0x9FEAFFu;
};

struct SideLedConfig
{
    bool enabled = true;
    SideLedPreset preset = SideLedPreset::Gt3;
    uint8_t ledCountPerSide = 8;
    uint8_t brightness = 180;
    bool allowSpotter = true;
    bool allowFlags = true;
    bool allowWarnings = true;
    bool allowTraction = true;
    uint16_t blinkSpeedSlowMs = 320;
    uint16_t blinkSpeedFastMs = 100;
    SideLedPriorityMode blueFlagPriority = SideLedPriorityMode::High;
    SideLedPriorityMode yellowFlagPriority = SideLedPriorityMode::Override;
    SideLedWarningPriorityMode warningPriorityMode = SideLedWarningPriorityMode::Normal;
    bool invertLeftRight = false;
    bool mirrorMode = false;
    bool closeCarBlinkingEnabled = true;
    bool severityLevelsEnabled = true;
    bool idleAnimationEnabled = false;
    bool testMode = false;
    uint16_t minimumFlagHoldMs = 450;
    uint16_t minimumWarningHoldMs = 350;
    uint16_t minimumSpotterHoldMs = 180;
    SideLedColorMap colors{};
};

struct SideLedPriorityResult
{
    SideLedSource source = SideLedSource::Off;
    SideLedPriority priority = SideLedPriority::Off;
    SideLedEvent event = SideLedEvent::None;
};

struct SideLedRenderFrame
{
    std::array<uint32_t, SIDE_LED_MAX_COUNT_PER_SIDE> left{};
    std::array<uint32_t, SIDE_LED_MAX_COUNT_PER_SIDE> right{};
    SideLedSource source = SideLedSource::Off;
    SideLedPriority priority = SideLedPriority::Off;
    SideLedEvent event = SideLedEvent::None;
    SideLedTractionDirection direction = SideLedTractionDirection::Off;
    bool blinkFast = false;
    bool blinkSlow = false;
    bool visible = false;
    uint8_t ledCountPerSide = 0;
    uint8_t leftLevel = 0;
    uint8_t rightLevel = 0;
};

struct SideLedTestState
{
    bool active = false;
    SideLedTestPattern pattern = SideLedTestPattern::None;
    uint32_t untilMs = 0;
};

SideLedPreset side_led_preset_from_int(int value);
const char *side_led_preset_name(SideLedPreset preset);
const char *side_led_preset_label(SideLedPreset preset);
const char *side_led_source_name(SideLedSource source);
const char *side_led_priority_name(SideLedPriority priority);
const char *side_led_event_name(SideLedEvent event);
const char *side_led_flag_name(SideLedFlag flag);
const char *side_led_test_pattern_name(SideLedTestPattern pattern);
const char *side_led_test_pattern_label(SideLedTestPattern pattern);
const char *side_led_traction_direction_name(SideLedTractionDirection direction);

SideLedConfig side_led_config_for_preset(SideLedPreset preset);
void normalize_side_led_config(SideLedConfig &config);
SideLedTelemetry build_side_led_test_telemetry(SideLedTestPattern pattern);
bool side_led_test_active(const SideLedTestState &state, uint32_t nowMs);
float side_led_normalize_input(float value);
uint8_t side_led_traction_level(float slip);
SideLedTractionDirection side_led_direction_from_inputs(float throttle, float brake);
float side_led_normalize_longitudinal_drive(float accelMps2);
float side_led_normalize_longitudinal_brake(float decelMps2);
float side_led_drive_level_from_vehicle_dynamics(int speedKmh,
                                                 int previousSpeedKmh,
                                                 int rpm,
                                                 int previousRpm,
                                                 uint32_t dtMs);
float side_led_brake_level_from_vehicle_dynamics(int speedKmh,
                                                 int previousSpeedKmh,
                                                 int rpm,
                                                 int previousRpm,
                                                 uint32_t dtMs);
void side_led_enhance_traction_state(TractionState &traction,
                                     int speedKmh,
                                     int previousSpeedKmh,
                                     int rpm,
                                     int previousRpm,
                                     uint32_t dtMs);

class SideLedController
{
public:
    void reset();
    SideLedRenderFrame update(const SideLedTelemetry &telemetry,
                              const SideLedConfig &config,
                              uint32_t nowMs,
                              const SideLedTestState *testState = nullptr);

    const SideLedPriorityResult &lastPriorityResult() const;

private:
    SideLedPriorityResult resolve_priority(const SideLedTelemetry &telemetry,
                                           const SideLedConfig &config,
                                           bool isTestFrame) const;
    SideLedRenderFrame render_frame(const SideLedTelemetry &telemetry,
                                    const SideLedConfig &config,
                                    const SideLedPriorityResult &result,
                                    uint32_t nowMs) const;
    SideLedRenderFrame render_idle(const SideLedConfig &config, uint32_t nowMs) const;
    SideLedRenderFrame build_off_frame(const SideLedConfig &config) const;

    SideLedPriorityResult activeResult_{};
    SideLedRenderFrame activeFrame_{};
};
