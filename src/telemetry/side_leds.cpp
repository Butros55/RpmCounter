#include "telemetry/side_leds.h"

#include <algorithm>
#include <cmath>

namespace
{
    uint32_t scale_color(uint32_t color, uint8_t brightness)
    {
        const uint32_t red = ((color >> 16) & 0xFFu) * brightness / 255u;
        const uint32_t green = ((color >> 8) & 0xFFu) * brightness / 255u;
        const uint32_t blue = (color & 0xFFu) * brightness / 255u;
        return (red << 16) | (green << 8) | blue;
    }

    uint8_t clamp_led_count(uint8_t value)
    {
        return static_cast<uint8_t>(std::clamp<int>(value,
                                                    static_cast<int>(SIDE_LED_MIN_COUNT_PER_SIDE),
                                                    static_cast<int>(SIDE_LED_MAX_COUNT_PER_SIDE)));
    }

    uint8_t level_from_ratio(float ratio, uint8_t ledCount)
    {
        const float clamped = std::clamp(ratio, 0.0f, 1.0f);
        if (clamped <= 0.01f || ledCount == 0)
        {
            return 0;
        }

        const int scaled = static_cast<int>(std::lround(clamped * static_cast<float>(ledCount)));
        return static_cast<uint8_t>(std::clamp(scaled, 1, static_cast<int>(ledCount)));
    }

    uint8_t level_from_slip(float slip, uint8_t ledCount)
    {
        return level_from_ratio(std::min(1.0f, std::max(0.0f, slip) / 0.28f), ledCount);
    }

    float clamp01(float value)
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    float normalize_with_deadzone(float value, float deadzone, float fullScale)
    {
        if (!std::isfinite(value) || fullScale <= deadzone)
        {
            return 0.0f;
        }
        if (value <= deadzone)
        {
            return 0.0f;
        }
        return clamp01((value - deadzone) / (fullScale - deadzone));
    }

    uint32_t traction_color_for(const SideLedConfig &config,
                                SideLedTractionDirection direction,
                                uint8_t severity,
                                bool critical)
    {
        if (critical || severity >= 4)
        {
            return scale_color(config.colors.tractionHigh, config.brightness);
        }

        if (direction == SideLedTractionDirection::Braking)
        {
            return scale_color(severity >= 2 ? config.colors.tractionMedium : config.colors.flagWhite,
                               config.brightness);
        }

        if (severity >= 2)
        {
            return scale_color(config.colors.tractionMedium, config.brightness);
        }

        return scale_color(config.colors.tractionLow, config.brightness);
    }

    void clear_lane(std::array<uint32_t, SIDE_LED_MAX_COUNT_PER_SIDE> &lane)
    {
        lane.fill(0);
    }

    void fill_lane(std::array<uint32_t, SIDE_LED_MAX_COUNT_PER_SIDE> &lane,
                   uint8_t ledCount,
                   uint8_t level,
                   uint32_t color,
                   bool fromTop)
    {
        clear_lane(lane);
        const uint8_t clampedCount = clamp_led_count(ledCount);
        const uint8_t clampedLevel = std::min<uint8_t>(level, clampedCount);
        for (uint8_t index = 0; index < clampedLevel; ++index)
        {
            const size_t slot = fromTop
                                    ? static_cast<size_t>(index)
                                    : static_cast<size_t>(clampedCount - 1U - index);
            lane[slot] = color;
        }
    }

    void apply_mirror_if_needed(SideLedRenderFrame &frame)
    {
        frame.right = frame.left;
        frame.rightLevel = frame.leftLevel;
    }

    void apply_invert_if_needed(SideLedRenderFrame &frame)
    {
        std::swap(frame.left, frame.right);
        std::swap(frame.leftLevel, frame.rightLevel);
    }

    SideLedTelemetry finalize_test_telemetry(SideLedTelemetry telemetry)
    {
        telemetry.traction.throttle = side_led_normalize_input(telemetry.traction.throttle);
        telemetry.traction.brake = side_led_normalize_input(telemetry.traction.brake);
        telemetry.traction.leftSlip = std::max(0.0f, telemetry.traction.leftSlip);
        telemetry.traction.rightSlip = std::max(0.0f, telemetry.traction.rightSlip);
        telemetry.traction.leftLevel = side_led_traction_level(telemetry.traction.leftSlip);
        telemetry.traction.rightLevel = side_led_traction_level(telemetry.traction.rightSlip);
        telemetry.traction.leftCritical = telemetry.traction.leftLevel >= 4;
        telemetry.traction.rightCritical = telemetry.traction.rightLevel >= 4;
        telemetry.traction.direction = side_led_direction_from_inputs(telemetry.traction.throttle, telemetry.traction.brake);
        telemetry.traction.active =
            telemetry.traction.direction != SideLedTractionDirection::Off ||
            telemetry.traction.leftLevel > 0 ||
            telemetry.traction.rightLevel > 0;
        if (telemetry.traction.active && telemetry.traction.direction == SideLedTractionDirection::Off)
        {
            telemetry.traction.direction =
                telemetry.traction.brake > telemetry.traction.throttle
                    ? SideLedTractionDirection::Braking
                    : SideLedTractionDirection::Accelerating;
        }
        return telemetry;
    }
}

SideLedPreset side_led_preset_from_int(int value)
{
    switch (value)
    {
    case 1:
        return SideLedPreset::Casual;
    case 2:
        return SideLedPreset::Minimal;
    case 0:
    default:
        return SideLedPreset::Gt3;
    }
}

const char *side_led_preset_name(SideLedPreset preset)
{
    switch (preset)
    {
    case SideLedPreset::Casual:
        return "casual";
    case SideLedPreset::Minimal:
        return "minimal";
    case SideLedPreset::Gt3:
    default:
        return "gt3";
    }
}

const char *side_led_preset_label(SideLedPreset preset)
{
    switch (preset)
    {
    case SideLedPreset::Casual:
        return "Casual";
    case SideLedPreset::Minimal:
        return "Minimal";
    case SideLedPreset::Gt3:
    default:
        return "GT3";
    }
}

const char *side_led_source_name(SideLedSource source)
{
    switch (source)
    {
    case SideLedSource::Warning:
        return "warning";
    case SideLedSource::Flag:
        return "flag";
    case SideLedSource::Spotter:
        return "spotter";
    case SideLedSource::Traction:
        return "traction";
    case SideLedSource::Idle:
        return "idle";
    case SideLedSource::Test:
        return "test";
    case SideLedSource::Off:
    default:
        return "off";
    }
}

const char *side_led_priority_name(SideLedPriority priority)
{
    switch (priority)
    {
    case SideLedPriority::Idle:
        return "idle";
    case SideLedPriority::Spotter:
        return "spotter";
    case SideLedPriority::Flag:
        return "flag";
    case SideLedPriority::Warning:
        return "warning";
    case SideLedPriority::Critical:
        return "critical";
    case SideLedPriority::Test:
        return "test";
    case SideLedPriority::Off:
    default:
        return "off";
    }
}

const char *side_led_event_name(SideLedEvent event)
{
    switch (event)
    {
    case SideLedEvent::Idle:
        return "idle";
    case SideLedEvent::TractionAccelerating:
        return "traction-accelerating";
    case SideLedEvent::TractionBraking:
        return "traction-braking";
    case SideLedEvent::TractionCoast:
        return "traction-coast";
    case SideLedEvent::None:
    default:
        return "none";
    }
}

const char *side_led_flag_name(SideLedFlag flag)
{
    switch (flag)
    {
    case SideLedFlag::Green:
        return "green";
    case SideLedFlag::Yellow:
        return "yellow";
    case SideLedFlag::Blue:
        return "blue";
    case SideLedFlag::Red:
        return "red";
    case SideLedFlag::White:
        return "white";
    case SideLedFlag::Black:
        return "black";
    case SideLedFlag::Orange:
        return "orange";
    case SideLedFlag::Checkered:
        return "checkered";
    case SideLedFlag::None:
    default:
        return "none";
    }
}

const char *side_led_test_pattern_name(SideLedTestPattern pattern)
{
    switch (pattern)
    {
    case SideLedTestPattern::Accelerate:
        return "accelerate";
    case SideLedTestPattern::Brake:
        return "brake";
    case SideLedTestPattern::TractionLeft:
        return "traction-left";
    case SideLedTestPattern::TractionRight:
        return "traction-right";
    case SideLedTestPattern::TractionBoth:
        return "traction-both";
    case SideLedTestPattern::None:
    default:
        return "none";
    }
}

const char *side_led_test_pattern_label(SideLedTestPattern pattern)
{
    switch (pattern)
    {
    case SideLedTestPattern::Accelerate:
        return "Beschleunigen";
    case SideLedTestPattern::Brake:
        return "Bremsen";
    case SideLedTestPattern::TractionLeft:
        return "Slip links";
    case SideLedTestPattern::TractionRight:
        return "Slip rechts";
    case SideLedTestPattern::TractionBoth:
        return "Slip beide";
    case SideLedTestPattern::None:
    default:
        return "Aus";
    }
}

const char *side_led_traction_direction_name(SideLedTractionDirection direction)
{
    switch (direction)
    {
    case SideLedTractionDirection::Accelerating:
        return "accelerating";
    case SideLedTractionDirection::Braking:
        return "braking";
    case SideLedTractionDirection::Off:
    default:
        return "off";
    }
}

SideLedConfig side_led_config_for_preset(SideLedPreset preset)
{
    SideLedConfig config{};
    config.preset = preset;

    switch (preset)
    {
    case SideLedPreset::Casual:
        config.ledCountPerSide = 6;
        config.brightness = 160;
        config.closeCarBlinkingEnabled = false;
        config.severityLevelsEnabled = false;
        break;
    case SideLedPreset::Minimal:
        config.ledCountPerSide = 4;
        config.brightness = 132;
        config.closeCarBlinkingEnabled = false;
        config.severityLevelsEnabled = false;
        break;
    case SideLedPreset::Gt3:
    default:
        config.ledCountPerSide = 8;
        config.brightness = 180;
        config.closeCarBlinkingEnabled = true;
        config.severityLevelsEnabled = true;
        break;
    }

    config.allowTraction = true;
    config.allowSpotter = false;
    config.allowFlags = false;
    config.allowWarnings = false;
    config.idleAnimationEnabled = false;
    normalize_side_led_config(config);
    return config;
}

void normalize_side_led_config(SideLedConfig &config)
{
    config.preset = side_led_preset_from_int(static_cast<int>(config.preset));
    config.ledCountPerSide = clamp_led_count(config.ledCountPerSide);
    config.brightness = static_cast<uint8_t>(std::clamp<int>(config.brightness, 0, 255));
    config.blinkSpeedSlowMs = std::clamp<uint16_t>(config.blinkSpeedSlowMs, 80, 1500);
    config.blinkSpeedFastMs = std::clamp<uint16_t>(config.blinkSpeedFastMs, 40, 900);
    if (config.blinkSpeedFastMs > config.blinkSpeedSlowMs)
    {
        std::swap(config.blinkSpeedFastMs, config.blinkSpeedSlowMs);
    }
    config.minimumFlagHoldMs = std::clamp<uint16_t>(config.minimumFlagHoldMs, 0, 2000);
    config.minimumWarningHoldMs = std::clamp<uint16_t>(config.minimumWarningHoldMs, 0, 2000);
    config.minimumSpotterHoldMs = std::clamp<uint16_t>(config.minimumSpotterHoldMs, 0, 2000);
}

SideLedTelemetry build_side_led_test_telemetry(SideLedTestPattern pattern)
{
    SideLedTelemetry telemetry{};
    switch (pattern)
    {
    case SideLedTestPattern::Accelerate:
        telemetry.traction.throttle = 0.84f;
        telemetry.traction.leftSlip = 0.08f;
        telemetry.traction.rightSlip = 0.10f;
        break;
    case SideLedTestPattern::Brake:
        telemetry.traction.brake = 0.88f;
        telemetry.traction.leftSlip = 0.07f;
        telemetry.traction.rightSlip = 0.09f;
        break;
    case SideLedTestPattern::TractionLeft:
        telemetry.traction.throttle = 0.72f;
        telemetry.traction.leftSlip = 0.28f;
        telemetry.traction.rightSlip = 0.04f;
        break;
    case SideLedTestPattern::TractionRight:
        telemetry.traction.throttle = 0.72f;
        telemetry.traction.leftSlip = 0.04f;
        telemetry.traction.rightSlip = 0.28f;
        break;
    case SideLedTestPattern::TractionBoth:
        telemetry.traction.brake = 0.76f;
        telemetry.traction.leftSlip = 0.22f;
        telemetry.traction.rightSlip = 0.24f;
        break;
    case SideLedTestPattern::None:
    default:
        break;
    }
    return finalize_test_telemetry(telemetry);
}

bool side_led_test_active(const SideLedTestState &state, uint32_t nowMs)
{
    return state.active && state.pattern != SideLedTestPattern::None && (state.untilMs == 0 || nowMs <= state.untilMs);
}

float side_led_normalize_input(float value)
{
    if (!std::isfinite(value))
    {
        return 0.0f;
    }
    if (value > 1.0f)
    {
        value /= 100.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

uint8_t side_led_traction_level(float slip)
{
    const float clamped = std::max(0.0f, slip);
    if (clamped >= 0.22f)
    {
        return 4;
    }
    if (clamped >= 0.16f)
    {
        return 3;
    }
    if (clamped >= 0.10f)
    {
        return 2;
    }
    if (clamped >= 0.07f)
    {
        return 1;
    }
    return 0;
}

SideLedTractionDirection side_led_direction_from_inputs(float throttle, float brake)
{
    const float normalizedThrottle = side_led_normalize_input(throttle);
    const float normalizedBrake = side_led_normalize_input(brake);
    if (normalizedBrake >= 0.12f && normalizedBrake >= normalizedThrottle + 0.08f)
    {
        return SideLedTractionDirection::Braking;
    }
    if (normalizedThrottle >= 0.34f && normalizedThrottle >= normalizedBrake + 0.10f)
    {
        return SideLedTractionDirection::Accelerating;
    }
    if (normalizedBrake >= 0.10f)
    {
        return SideLedTractionDirection::Braking;
    }
    return SideLedTractionDirection::Off;
}

float side_led_normalize_longitudinal_drive(float accelMps2)
{
    return normalize_with_deadzone(accelMps2, 1.45f, 3.60f);
}

float side_led_normalize_longitudinal_brake(float decelMps2)
{
    return normalize_with_deadzone(decelMps2, 0.55f, 4.20f);
}

float side_led_drive_level_from_vehicle_dynamics(int speedKmh,
                                                 int previousSpeedKmh,
                                                 int rpm,
                                                 int previousRpm,
                                                 uint32_t dtMs)
{
    if (dtMs < 25U || dtMs > 1200U)
    {
        return 0.0f;
    }

    const float dtSeconds = static_cast<float>(dtMs) / 1000.0f;
    const float speedDeltaMps = static_cast<float>(speedKmh - previousSpeedKmh) / 3.6f;
    const float accelMps2 = speedDeltaMps / dtSeconds;
    float drive = side_led_normalize_longitudinal_drive(accelMps2) * 0.16f;

    const float rpmRate = static_cast<float>(rpm - previousRpm) / dtSeconds;
    if (speedKmh <= 18 || previousSpeedKmh <= 18)
    {
        drive = std::max(drive, normalize_with_deadzone(rpmRate, 1200.0f, 3600.0f) * 0.12f);
    }

    return clamp01(drive);
}

float side_led_brake_level_from_vehicle_dynamics(int speedKmh,
                                                 int previousSpeedKmh,
                                                 int rpm,
                                                 int previousRpm,
                                                 uint32_t dtMs)
{
    if (dtMs < 25U || dtMs > 1200U)
    {
        return 0.0f;
    }

    const float dtSeconds = static_cast<float>(dtMs) / 1000.0f;
    const float speedDeltaMps = static_cast<float>(speedKmh - previousSpeedKmh) / 3.6f;
    const float decelMps2 = (-speedDeltaMps) / dtSeconds;
    float brake = side_led_normalize_longitudinal_brake(decelMps2) * 1.00f;

    const float rpmDropRate = static_cast<float>(previousRpm - rpm) / dtSeconds;
    if (previousSpeedKmh >= 20)
    {
        brake = std::max(brake, normalize_with_deadzone(rpmDropRate, 500.0f, 3000.0f) * 0.32f);
    }

    return clamp01(brake);
}

void side_led_enhance_traction_state(TractionState &traction,
                                     int speedKmh,
                                     int previousSpeedKmh,
                                     int rpm,
                                     int previousRpm,
                                     uint32_t dtMs)
{
    const float rawThrottle = side_led_normalize_input(traction.throttle);
    const float rawBrake = side_led_normalize_input(traction.brake);
    traction.throttle = rawThrottle;
    traction.brake = rawBrake;
    traction.leftSlip = std::max(0.0f, traction.leftSlip);
    traction.rightSlip = std::max(0.0f, traction.rightSlip);

    const float derivedDrive = side_led_drive_level_from_vehicle_dynamics(speedKmh,
                                                                          previousSpeedKmh,
                                                                          rpm,
                                                                          previousRpm,
                                                                          dtMs);
    const float derivedBrake = side_led_brake_level_from_vehicle_dynamics(speedKmh,
                                                                          previousSpeedKmh,
                                                                          rpm,
                                                                          previousRpm,
                                                                          dtMs);
    const float maxSlip = std::max(traction.leftSlip, traction.rightSlip);
    const bool launchWindow = std::max(speedKmh, previousSpeedKmh) <= 18;
    const bool realDriveSlip = maxSlip >= 0.09f;
    const bool launchAssist = launchWindow && rawThrottle >= 0.97f && derivedDrive >= 0.10f;
    const bool brakeSlipWarning = maxSlip >= 0.06f && (rawBrake >= 0.22f || derivedBrake >= 0.06f);
    const float driveSlipLevel = normalize_with_deadzone(maxSlip, 0.08f, 0.20f);
    const float brakeSlipLevel = normalize_with_deadzone(maxSlip, 0.05f, 0.16f);

    float mergedDrive = 0.0f;
    if (realDriveSlip)
    {
        mergedDrive = std::max(0.12f, 0.12f + driveSlipLevel * 0.88f);
    }
    else if (launchAssist)
    {
        mergedDrive = std::max(0.10f, derivedDrive * 0.75f);
    }

    float mergedBrake = std::max(derivedBrake, normalize_with_deadzone(rawBrake, 0.26f, 0.90f) * 0.28f);
    if (brakeSlipWarning)
    {
        mergedBrake = std::max(mergedBrake, 0.10f + brakeSlipLevel * 0.82f);
    }

    mergedDrive = clamp01(mergedDrive);
    mergedBrake = clamp01(mergedBrake);

    traction.leftLevel = side_led_traction_level(traction.leftSlip);
    traction.rightLevel = side_led_traction_level(traction.rightSlip);
    traction.leftCritical = traction.leftLevel >= 4;
    traction.rightCritical = traction.rightLevel >= 4;
    traction.throttle = mergedDrive;
    traction.brake = mergedBrake;

    const bool driveActive = mergedDrive >= 0.10f;
    const bool brakeActive = mergedBrake >= 0.08f;
    const bool slipActive = maxSlip >= 0.07f;

    traction.active = driveActive || brakeActive || slipActive;
    if (!traction.active)
    {
        traction.direction = SideLedTractionDirection::Off;
        return;
    }

    if (brakeActive && mergedBrake >= mergedDrive + 0.03f)
    {
        traction.direction = SideLedTractionDirection::Braking;
    }
    else if (driveActive)
    {
        traction.direction = SideLedTractionDirection::Accelerating;
    }
    else if (rawBrake >= 0.22f || derivedBrake >= 0.06f)
    {
        traction.direction = SideLedTractionDirection::Braking;
    }
    else
    {
        traction.direction = SideLedTractionDirection::Accelerating;
    }
}

void SideLedController::reset()
{
    activeResult_ = SideLedPriorityResult{};
    activeFrame_ = SideLedRenderFrame{};
}

SideLedRenderFrame SideLedController::update(const SideLedTelemetry &telemetry,
                                             const SideLedConfig &config,
                                             uint32_t nowMs,
                                             const SideLedTestState *testState)
{
    SideLedConfig normalized = config;
    normalize_side_led_config(normalized);

    if (!normalized.enabled)
    {
        reset();
        return build_off_frame(normalized);
    }

    const bool useTestOverride =
        normalized.testMode && testState != nullptr && side_led_test_active(*testState, nowMs);
    const SideLedTelemetry effectiveTelemetry =
        useTestOverride ? build_side_led_test_telemetry(testState->pattern) : telemetry;

    activeResult_ = resolve_priority(effectiveTelemetry, normalized, useTestOverride);
    activeFrame_ = render_frame(effectiveTelemetry, normalized, activeResult_, nowMs);
    return activeFrame_;
}

const SideLedPriorityResult &SideLedController::lastPriorityResult() const
{
    return activeResult_;
}

SideLedPriorityResult SideLedController::resolve_priority(const SideLedTelemetry &telemetry,
                                                          const SideLedConfig &config,
                                                          bool isTestFrame) const
{
    SideLedPriorityResult result{};
    if (isTestFrame)
    {
        result.source = SideLedSource::Test;
        result.priority = SideLedPriority::Test;
    }
    else if (config.allowTraction && telemetry.traction.active)
    {
        result.source = SideLedSource::Traction;
        result.priority = (telemetry.traction.leftCritical || telemetry.traction.rightCritical)
                              ? SideLedPriority::Critical
                              : SideLedPriority::Warning;
    }
    else if (config.idleAnimationEnabled)
    {
        result.source = SideLedSource::Idle;
        result.priority = SideLedPriority::Idle;
    }

    switch (telemetry.traction.direction)
    {
    case SideLedTractionDirection::Braking:
        result.event = SideLedEvent::TractionBraking;
        break;
    case SideLedTractionDirection::Accelerating:
        result.event = SideLedEvent::TractionAccelerating;
        break;
    case SideLedTractionDirection::Off:
    default:
        result.event = result.source == SideLedSource::Idle ? SideLedEvent::Idle : SideLedEvent::TractionCoast;
        break;
    }

    if (result.source == SideLedSource::Off)
    {
        result.event = SideLedEvent::None;
    }
    return result;
}

SideLedRenderFrame SideLedController::render_frame(const SideLedTelemetry &telemetry,
                                                   const SideLedConfig &config,
                                                   const SideLedPriorityResult &result,
                                                   uint32_t nowMs) const
{
    if (result.source == SideLedSource::Off)
    {
        return build_off_frame(config);
    }

    if (result.source == SideLedSource::Idle)
    {
        return render_idle(config, nowMs);
    }

    SideLedRenderFrame frame = build_off_frame(config);
    frame.source = result.source;
    frame.priority = result.priority;
    frame.event = result.event;
    frame.visible = config.enabled;
    frame.direction = telemetry.traction.direction;

    const uint8_t ledCount = frame.ledCountPerSide;
    const float controlInput = frame.direction == SideLedTractionDirection::Braking
                                   ? side_led_normalize_input(telemetry.traction.brake)
                                   : side_led_normalize_input(telemetry.traction.throttle);
    const uint8_t maxLoadLevel = frame.direction == SideLedTractionDirection::Braking
                                     ? std::max<uint8_t>(2, std::min<uint8_t>(4, static_cast<uint8_t>((ledCount + 2U) / 4U)))
                                     : 1U;
    const uint8_t controlLevel = std::min<uint8_t>(maxLoadLevel, level_from_ratio(controlInput, ledCount));
    const uint8_t leftSlipLevel = level_from_slip(telemetry.traction.leftSlip, ledCount);
    const uint8_t rightSlipLevel = level_from_slip(telemetry.traction.rightSlip, ledCount);
    const bool leftWarning = telemetry.traction.leftSlip >= (frame.direction == SideLedTractionDirection::Braking ? 0.15f : 0.18f);
    const bool rightWarning = telemetry.traction.rightSlip >= (frame.direction == SideLedTractionDirection::Braking ? 0.15f : 0.18f);

    frame.leftLevel = std::max(controlLevel, leftSlipLevel);
    frame.rightLevel = std::max(controlLevel, rightSlipLevel);

    if (frame.direction == SideLedTractionDirection::Off &&
        (frame.leftLevel > 0 || frame.rightLevel > 0))
    {
        frame.direction = telemetry.traction.brake > telemetry.traction.throttle
                              ? SideLedTractionDirection::Braking
                              : SideLedTractionDirection::Accelerating;
        frame.event = frame.direction == SideLedTractionDirection::Braking
                          ? SideLedEvent::TractionBraking
                          : SideLedEvent::TractionAccelerating;
    }

    const bool visibleFrame = true;
    frame.blinkFast = false;

    const bool fromTop = frame.direction == SideLedTractionDirection::Braking;
    const uint32_t leftColor =
        traction_color_for(config,
                           frame.direction,
                           telemetry.traction.leftLevel,
                           telemetry.traction.leftCritical || leftWarning);
    const uint32_t rightColor =
        traction_color_for(config,
                           frame.direction,
                           telemetry.traction.rightLevel,
                           telemetry.traction.rightCritical || rightWarning);

    fill_lane(frame.left, ledCount, visibleFrame ? frame.leftLevel : 0, leftColor, fromTop);
    fill_lane(frame.right, ledCount, visibleFrame ? frame.rightLevel : 0, rightColor, fromTop);

    if (config.mirrorMode)
    {
        apply_mirror_if_needed(frame);
    }
    if (config.invertLeftRight)
    {
        apply_invert_if_needed(frame);
    }
    return frame;
}

SideLedRenderFrame SideLedController::render_idle(const SideLedConfig &config, uint32_t nowMs) const
{
    SideLedRenderFrame frame = build_off_frame(config);
    frame.source = SideLedSource::Idle;
    frame.priority = SideLedPriority::Idle;
    frame.event = SideLedEvent::Idle;
    frame.visible = config.enabled;

    const uint8_t step = static_cast<uint8_t>(
        (nowMs / std::max<uint16_t>(120, config.blinkSpeedSlowMs / 2U)) %
        std::max<uint8_t>(1, frame.ledCountPerSide));
    const uint32_t color = scale_color(config.colors.dim, std::max<uint8_t>(24, config.brightness / 2));
    frame.left[static_cast<size_t>(step)] = color;
    frame.right[static_cast<size_t>(frame.ledCountPerSide - 1U - step)] = color;
    frame.leftLevel = 1;
    frame.rightLevel = 1;
    frame.blinkSlow = true;
    return frame;
}

SideLedRenderFrame SideLedController::build_off_frame(const SideLedConfig &config) const
{
    SideLedRenderFrame frame{};
    frame.ledCountPerSide = clamp_led_count(config.ledCountPerSide);
    frame.visible = config.enabled;
    frame.direction = SideLedTractionDirection::Off;
    return frame;
}
