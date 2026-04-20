#include "config.h"

#include <math.h>
#include <string.h>

#include "utils.h"

namespace
{
    constexpr uint32_t SHIFTLIGHT_ADVANCED_MAGIC = 0x534C5431UL; // "SLT1"
    constexpr int SHIFTLIGHT_MIN_RPM = 500;
    constexpr int SHIFTLIGHT_MAX_RPM = 15000;
    constexpr int GT3_DEFAULT_TARGET_RPM = 7600;
    constexpr int LINEAR_DEFAULT_TARGET_RPM = 7200;

    bool colorLooksUnset(const RgbColor &color)
    {
        return color.r == 0 && color.g == 0 && color.b == 0;
    }

    RgbColor fallbackColorForPosition(uint8_t index,
                                      uint8_t stepCount,
                                      const RgbColor &greenFallback,
                                      const RgbColor &yellowFallback,
                                      const RgbColor &redFallback)
    {
        if (stepCount <= 1)
        {
            return redFallback;
        }

        const float pos = static_cast<float>(index) / static_cast<float>(stepCount - 1);
        if (pos < 0.62f)
        {
            return greenFallback;
        }
        if (pos < 0.84f)
        {
            return yellowFallback;
        }
        return redFallback;
    }

    uint16_t clampShiftRpm(int value)
    {
        return static_cast<uint16_t>(clampInt(value, SHIFTLIGHT_MIN_RPM, SHIFTLIGHT_MAX_RPM));
    }

    uint16_t defaultTargetForLayout(ShiftLightLayoutMode layout)
    {
        return static_cast<uint16_t>(layout == ShiftLightLayoutMode::Gt3Mirrored15 ? GT3_DEFAULT_TARGET_RPM
                                                                                   : LINEAR_DEFAULT_TARGET_RPM);
    }

    void applyGt3Preset(ShiftLightGearProfile &profile)
    {
        const uint8_t stepCount = SHIFTLIGHT_ADVANCED_GT3_STEP_COUNT;
        const int targetShiftRpm = clampShiftRpm(profile.targetShiftRpm > 0 ? profile.targetShiftRpm : GT3_DEFAULT_TARGET_RPM);
        const int firstStepRpm = max(1800, targetShiftRpm - 3100);
        const int lastStepRpm = max(firstStepRpm + 250, targetShiftRpm - 80);

        profile.stepCount = stepCount;
        profile.enabled = 1;
        profile.finalBlinkMode = static_cast<uint8_t>(ShiftLightFlashMode::FullBar);
        profile.overRevMode = static_cast<uint8_t>(ShiftLightFlashMode::FullBar);

        for (uint8_t i = 0; i < stepCount; ++i)
        {
            const float t = stepCount <= 1 ? 1.0f : static_cast<float>(i) / static_cast<float>(stepCount - 1);
            const float shaped = powf(t, 1.7f);
            const int onRpm = firstStepRpm + static_cast<int>(lroundf((lastStepRpm - firstStepRpm) * shaped));
            const int hysteresis = static_cast<int>(lroundf(135.0f - (55.0f * t)));
            profile.steps[i].onRpm = clampShiftRpm(onRpm);
            profile.steps[i].offRpm = clampShiftRpm(onRpm - hysteresis);
            profile.steps[i].flags = 0;
        }

        for (uint8_t i = stepCount; i < SHIFTLIGHT_ADVANCED_MAX_STEPS; ++i)
        {
            profile.steps[i] = ShiftLightStepConfig{};
        }

        profile.targetShiftRpm = clampShiftRpm(targetShiftRpm);
        profile.finalBlinkStartRpm = clampShiftRpm(max<int>(profile.steps[stepCount - 1].onRpm, targetShiftRpm - 45));
        profile.overRevFlashRpm = clampShiftRpm(max<int>(profile.finalBlinkStartRpm + 60, targetShiftRpm + 110));
    }

    void applyLinearPreset(ShiftLightGearProfile &profile)
    {
        const uint8_t stepCount = SHIFTLIGHT_ADVANCED_LINEAR_STEP_COUNT;
        const int targetShiftRpm = clampShiftRpm(profile.targetShiftRpm > 0 ? profile.targetShiftRpm : LINEAR_DEFAULT_TARGET_RPM);
        const int firstStepRpm = max(1500, targetShiftRpm - 3200);
        const int lastStepRpm = max(firstStepRpm + 500, targetShiftRpm - 70);

        profile.stepCount = stepCount;
        profile.enabled = 1;
        profile.finalBlinkMode = static_cast<uint8_t>(ShiftLightFlashMode::LastStep);
        profile.overRevMode = static_cast<uint8_t>(ShiftLightFlashMode::FullBar);

        for (uint8_t i = 0; i < stepCount; ++i)
        {
            const float t = stepCount <= 1 ? 1.0f : static_cast<float>(i) / static_cast<float>(stepCount - 1);
            const float shaped = powf(t, 1.18f);
            const int onRpm = firstStepRpm + static_cast<int>(lroundf((lastStepRpm - firstStepRpm) * shaped));
            const int hysteresis = static_cast<int>(lroundf(110.0f - (35.0f * t)));
            profile.steps[i].onRpm = clampShiftRpm(onRpm);
            profile.steps[i].offRpm = clampShiftRpm(onRpm - hysteresis);
            profile.steps[i].flags = 0;
        }

        profile.targetShiftRpm = clampShiftRpm(targetShiftRpm);
        profile.finalBlinkStartRpm = clampShiftRpm(max<int>(profile.steps[stepCount - 1].onRpm, targetShiftRpm - 55));
        profile.overRevFlashRpm = clampShiftRpm(max<int>(profile.finalBlinkStartRpm + 70, targetShiftRpm + 120));
    }

    void applyPresetForLayout(ShiftLightGearProfile &profile, ShiftLightLayoutMode layout)
    {
        if (layout == ShiftLightLayoutMode::Gt3Mirrored15)
        {
            applyGt3Preset(profile);
        }
        else
        {
            applyLinearPreset(profile);
        }
    }

    void ensureGearProfileValid(ShiftLightGearProfile &profile,
                                ShiftLightLayoutMode layout,
                                const RgbColor &greenFallback,
                                const RgbColor &yellowFallback,
                                const RgbColor &redFallback)
    {
        const uint8_t expectedStepCount = shiftLightStepCountForLayout(layout);
        if (expectedStepCount == 0)
        {
            return;
        }

        if (profile.targetShiftRpm < SHIFTLIGHT_MIN_RPM)
        {
            profile.targetShiftRpm = defaultTargetForLayout(layout);
        }

        if (profile.stepCount != expectedStepCount)
        {
            applyPresetForLayout(profile, layout);
        }

        profile.stepCount = expectedStepCount;
        profile.enabled = profile.enabled ? 1 : 0;
        profile.finalBlinkMode = static_cast<uint8_t>(shiftLightClampFlashMode(profile.finalBlinkMode));
        profile.overRevMode = static_cast<uint8_t>(shiftLightClampFlashMode(profile.overRevMode));

        uint16_t prevOn = 0;
        uint16_t prevOff = 0;
        for (uint8_t i = 0; i < expectedStepCount; ++i)
        {
            ShiftLightStepConfig &step = profile.steps[i];

            uint16_t onRpm = clampShiftRpm(step.onRpm > 0 ? step.onRpm : (profile.targetShiftRpm - ((expectedStepCount - i) * 120)));
            if (i > 0 && onRpm <= prevOn)
            {
                onRpm = clampShiftRpm(prevOn + 50);
            }

            uint16_t offRpm = clampShiftRpm(step.offRpm > 0 ? step.offRpm : (onRpm - 100));
            if (offRpm > onRpm)
            {
                offRpm = onRpm;
            }
            if (i > 0 && offRpm < prevOff)
            {
                offRpm = min<uint16_t>(onRpm, prevOff);
            }

            step.onRpm = onRpm;
            step.offRpm = offRpm;
            if (colorLooksUnset(step.color))
            {
                step.color = fallbackColorForPosition(i, expectedStepCount, greenFallback, yellowFallback, redFallback);
            }
            step.flags = 0;
            prevOn = onRpm;
            prevOff = offRpm;
        }

        for (uint8_t i = expectedStepCount; i < SHIFTLIGHT_ADVANCED_MAX_STEPS; ++i)
        {
            profile.steps[i] = ShiftLightStepConfig{};
        }

        const uint16_t lastStepOn = profile.steps[expectedStepCount - 1].onRpm;
        if (profile.targetShiftRpm < lastStepOn)
        {
            profile.targetShiftRpm = clampShiftRpm(lastStepOn + 40);
        }

        profile.finalBlinkStartRpm =
            clampShiftRpm(max<int>(profile.finalBlinkStartRpm > 0 ? profile.finalBlinkStartRpm : (profile.targetShiftRpm - 60),
                                   lastStepOn));
        if (profile.overRevFlashRpm < profile.finalBlinkStartRpm)
        {
            profile.overRevFlashRpm = clampShiftRpm(profile.finalBlinkStartRpm + 80);
        }
    }

    uint32_t computeChecksum(const ShiftLightAdvancedProfile &profile)
    {
        ShiftLightAdvancedProfile temp = profile;
        temp.checksum = 0;
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&temp);
        uint32_t hash = 2166136261UL;
        for (size_t i = 0; i < sizeof(temp); ++i)
        {
            hash ^= bytes[i];
            hash *= 16777619UL;
        }
        return hash;
    }

    String colorToHex(const RgbColor &color)
    {
        char buffer[8];
        snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r, color.g, color.b);
        return String(buffer);
    }

    void appendGearProfileJson(String &json, const ShiftLightGearProfile &profile)
    {
        json += F("{\"enabled\":");
        json += profile.enabled ? F("true") : F("false");
        json += F(",\"targetShiftRpm\":");
        json += String(profile.targetShiftRpm);
        json += F(",\"finalBlinkStartRpm\":");
        json += String(profile.finalBlinkStartRpm);
        json += F(",\"overRevFlashRpm\":");
        json += String(profile.overRevFlashRpm);
        json += F(",\"stepCount\":");
        json += String(profile.stepCount);
        json += F(",\"finalBlinkMode\":");
        json += String(profile.finalBlinkMode);
        json += F(",\"overRevMode\":");
        json += String(profile.overRevMode);
        json += F(",\"steps\":[");
        for (uint8_t i = 0; i < profile.stepCount && i < SHIFTLIGHT_ADVANCED_MAX_STEPS; ++i)
        {
            if (i > 0)
            {
                json += ',';
            }
            json += F("{\"onRpm\":");
            json += String(profile.steps[i].onRpm);
            json += F(",\"offRpm\":");
            json += String(profile.steps[i].offRpm);
            json += F(",\"color\":\"");
            json += colorToHex(profile.steps[i].color);
            json += F("\"}");
        }
        json += F("]}");
    }
}

ShiftLightLayoutMode shiftLightClampLayoutMode(uint8_t raw)
{
    if (raw > static_cast<uint8_t>(ShiftLightLayoutMode::Gt3Mirrored15))
    {
        return ShiftLightLayoutMode::Gt3Mirrored15;
    }
    return static_cast<ShiftLightLayoutMode>(raw);
}

ShiftLightFlashMode shiftLightClampFlashMode(uint8_t raw)
{
    if (raw > static_cast<uint8_t>(ShiftLightFlashMode::FullBar))
    {
        return ShiftLightFlashMode::FullBar;
    }
    return static_cast<ShiftLightFlashMode>(raw);
}

uint8_t shiftLightStepCountForLayout(ShiftLightLayoutMode layout)
{
    return layout == ShiftLightLayoutMode::Gt3Mirrored15 ? SHIFTLIGHT_ADVANCED_GT3_STEP_COUNT
                                                         : SHIFTLIGHT_ADVANCED_LINEAR_STEP_COUNT;
}

const char *shiftLightLayoutModeName(ShiftLightLayoutMode layout)
{
    switch (layout)
    {
    case ShiftLightLayoutMode::Linear30:
        return "linear30";
    case ShiftLightLayoutMode::Gt3Mirrored15:
    default:
        return "gt3_mirrored_15";
    }
}

const char *shiftLightFlashModeName(ShiftLightFlashMode mode)
{
    switch (mode)
    {
    case ShiftLightFlashMode::None:
        return "none";
    case ShiftLightFlashMode::LastStep:
        return "last_step";
    case ShiftLightFlashMode::FullBar:
    default:
        return "full_bar";
    }
}

const char *telemetryVehicleIdentitySourceName(TelemetryVehicleIdentitySource source)
{
    switch (source)
    {
    case TelemetryVehicleIdentitySource::SimHubDirect:
        return "simhub-direct";
    case TelemetryVehicleIdentitySource::UsbBridge:
        return "usb-bridge";
    case TelemetryVehicleIdentitySource::LearnedHeuristic:
        return "learned-heuristic";
    case TelemetryVehicleIdentitySource::ManualFallback:
        return "manual-fallback";
    case TelemetryVehicleIdentitySource::None:
    default:
        return "none";
    }
}

const char *resolvedShiftProfileSourceName(ResolvedShiftProfileSource source)
{
    switch (source)
    {
    case ResolvedShiftProfileSource::ExactVehicleProfile:
        return "exact-vehicle";
    case ResolvedShiftProfileSource::ClassProfile:
        return "class";
    case ResolvedShiftProfileSource::LearnedVehicleProfile:
        return "learned-vehicle";
    case ResolvedShiftProfileSource::LearnedClassProfile:
        return "learned-class";
    case ResolvedShiftProfileSource::GenericGt3:
        return "generic-gt3";
    case ResolvedShiftProfileSource::GenericLinear:
        return "generic-linear";
    case ResolvedShiftProfileSource::ManualSimpleFallback:
    default:
        return "manual-simple-fallback";
    }
}

const char *autoShiftProfilePreferenceName(AutoShiftProfilePreference preference)
{
    switch (preference)
    {
    case AutoShiftProfilePreference::ClassFallback:
        return "class-fallback";
    case AutoShiftProfilePreference::GenericFallback:
        return "generic-fallback";
    case AutoShiftProfilePreference::ExactVehicle:
    default:
        return "exact-vehicle";
    }
}

void shiftLightGenerateGearProfilePreset(ShiftLightGearProfile &profile,
                                         ShiftLightLayoutMode layout,
                                         uint16_t targetShiftRpm)
{
    profile.targetShiftRpm = targetShiftRpm;
    applyPresetForLayout(profile, layout);
}

void shiftLightResetAdvancedProfileFromSimple(ShiftLightAdvancedProfile &profile, const AppConfig &source)
{
    profile = ShiftLightAdvancedProfile{};
    profile.magic = SHIFTLIGHT_ADVANCED_MAGIC;
    profile.version = SHIFTLIGHT_ADVANCED_VERSION;
    profile.enabled = 0;
    profile.layout = static_cast<uint8_t>(source.mode == 3 ? ShiftLightLayoutMode::Gt3Mirrored15
                                                           : ShiftLightLayoutMode::Linear30);
    profile.useGearOverrides = 1;
    profile.fastResponseForSim = 1;
    profile.allowNeutralProfile = 1;
    profile.activeSlot = 0;

    const ShiftLightLayoutMode layout = shiftLightClampLayoutMode(profile.layout);

    profile.defaultProfile.targetShiftRpm =
        clampShiftRpm(source.autoScaleMaxRpm ? max(source.fixedMaxRpm, 6500) : max(source.fixedMaxRpm, 6500));
    shiftLightGenerateGearProfilePreset(profile.defaultProfile, layout, profile.defaultProfile.targetShiftRpm);

    for (uint8_t slot = 0; slot < SHIFTLIGHT_ADVANCED_GEAR_SLOT_COUNT; ++slot)
    {
        profile.gearProfiles[slot] = profile.defaultProfile;
        profile.gearProfiles[slot].enabled = 1;
        if (slot == SHIFTLIGHT_ADVANCED_NEUTRAL_SLOT)
        {
            profile.gearProfiles[slot].targetShiftRpm = profile.defaultProfile.targetShiftRpm;
        }
        else
        {
            const int gearOffset = static_cast<int>(slot - 1) * 85;
            profile.gearProfiles[slot].targetShiftRpm = clampShiftRpm(profile.defaultProfile.targetShiftRpm + gearOffset);
            shiftLightGenerateGearProfilePreset(profile.gearProfiles[slot], layout, profile.gearProfiles[slot].targetShiftRpm);
        }
    }

    shiftLightPrepareAdvancedProfileForSave(profile, source.greenColor, source.yellowColor, effectiveRedColor());
}

bool shiftLightSanitizeAdvancedProfile(ShiftLightAdvancedProfile &profile,
                                       const RgbColor &greenFallback,
                                       const RgbColor &yellowFallback,
                                       const RgbColor &redFallback)
{
    profile.magic = SHIFTLIGHT_ADVANCED_MAGIC;
    profile.version = SHIFTLIGHT_ADVANCED_VERSION;
    profile.enabled = profile.enabled ? 1 : 0;
    profile.useGearOverrides = profile.useGearOverrides ? 1 : 0;
    profile.fastResponseForSim = profile.fastResponseForSim ? 1 : 0;
    profile.allowNeutralProfile = profile.allowNeutralProfile ? 1 : 0;
    profile.activeSlot = 0;
    profile.reserved = 0;

    const ShiftLightLayoutMode layout = shiftLightClampLayoutMode(profile.layout);
    profile.layout = static_cast<uint8_t>(layout);

    ensureGearProfileValid(profile.defaultProfile, layout, greenFallback, yellowFallback, redFallback);
    for (uint8_t slot = 0; slot < SHIFTLIGHT_ADVANCED_GEAR_SLOT_COUNT; ++slot)
    {
        ensureGearProfileValid(profile.gearProfiles[slot], layout, greenFallback, yellowFallback, redFallback);
    }

    return profile.defaultProfile.stepCount == shiftLightStepCountForLayout(layout);
}

void shiftLightPrepareAdvancedProfileForSave(ShiftLightAdvancedProfile &profile,
                                             const RgbColor &greenFallback,
                                             const RgbColor &yellowFallback,
                                             const RgbColor &redFallback)
{
    shiftLightSanitizeAdvancedProfile(profile, greenFallback, yellowFallback, redFallback);
    profile.checksum = computeChecksum(profile);
}

bool shiftLightStoredAdvancedProfileValid(const ShiftLightAdvancedProfile &profile)
{
    if (profile.magic != SHIFTLIGHT_ADVANCED_MAGIC)
    {
        return false;
    }
    if (profile.version != SHIFTLIGHT_ADVANCED_VERSION)
    {
        return false;
    }
    return profile.checksum == computeChecksum(profile);
}

ShiftLightResolvedGearProfile shiftLightResolveGearProfile(const ShiftLightAdvancedProfile &profile, int gear)
{
    ShiftLightResolvedGearProfile resolved{};
    resolved.requestedGear = gear;
    resolved.profile = &profile.defaultProfile;
    resolved.resolvedSlot = -1;
    resolved.usingFallback = true;
    resolved.valid = true;

    const bool useNeutral = gear <= 0;
    if (useNeutral && profile.allowNeutralProfile)
    {
        const ShiftLightGearProfile &neutralProfile = profile.gearProfiles[SHIFTLIGHT_ADVANCED_NEUTRAL_SLOT];
        if (neutralProfile.enabled)
        {
            resolved.profile = &neutralProfile;
            resolved.resolvedSlot = SHIFTLIGHT_ADVANCED_NEUTRAL_SLOT;
            resolved.usingFallback = false;
            return resolved;
        }
    }

    if (profile.useGearOverrides && gear >= 1 && gear <= 8)
    {
        const ShiftLightGearProfile &gearProfile = profile.gearProfiles[gear];
        if (gearProfile.enabled)
        {
            resolved.profile = &gearProfile;
            resolved.resolvedSlot = gear;
            resolved.usingFallback = false;
        }
    }

    return resolved;
}

String shiftLightAdvancedProfileToJson(const ShiftLightAdvancedProfile &profile)
{
    String json;
    json.reserve(14000);
    json += F("{\"enabled\":");
    json += profile.enabled ? F("true") : F("false");
    json += F(",\"version\":");
    json += String(profile.version);
    json += F(",\"layout\":");
    json += String(profile.layout);
    json += F(",\"useGearOverrides\":");
    json += profile.useGearOverrides ? F("true") : F("false");
    json += F(",\"fastResponseForSim\":");
    json += profile.fastResponseForSim ? F("true") : F("false");
    json += F(",\"allowNeutralProfile\":");
    json += profile.allowNeutralProfile ? F("true") : F("false");
    json += F(",\"activeSlot\":");
    json += String(profile.activeSlot);
    json += F(",\"defaultProfile\":");
    appendGearProfileJson(json, profile.defaultProfile);
    json += F(",\"gearProfiles\":[");
    for (uint8_t slot = 0; slot < SHIFTLIGHT_ADVANCED_GEAR_SLOT_COUNT; ++slot)
    {
        if (slot > 0)
        {
            json += ',';
        }
        appendGearProfileJson(json, profile.gearProfiles[slot]);
    }
    json += F("]}");
    return json;
}
