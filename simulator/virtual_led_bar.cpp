#include "virtual_led_bar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr uint32_t kInactiveColor = 0x000000u;

    uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               static_cast<uint32_t>(b);
    }

    uint32_t scale_color(uint32_t color, float scale)
    {
        scale = std::clamp(scale, 0.0f, 1.0f);
        const uint8_t r = static_cast<uint8_t>(((color >> 16) & 0xFFu) * scale);
        const uint8_t g = static_cast<uint8_t>(((color >> 8) & 0xFFu) * scale);
        const uint8_t b = static_cast<uint8_t>((color & 0xFFu) * scale);
        return rgb(r, g, b);
    }

    uint32_t color_for_position(float positionPct,
                                const SimulatorLedBarConfig &config,
                                float brightnessScale)
    {
        uint32_t base = config.greenColor;
        if (positionPct >= static_cast<float>(config.redEndPct))
        {
            base = config.redColor;
        }
        else if (positionPct >= static_cast<float>(config.yellowEndPct))
        {
            base = config.yellowColor;
        }

        return scale_color(base, brightnessScale);
    }

    uint32_t inactive_led_color(float brightnessScale)
    {
        return scale_color(rgb(26, 31, 39), std::max(0.25f, brightnessScale * 0.6f));
    }

    uint32_t stale_led_color(float brightnessScale)
    {
        return scale_color(rgb(40, 52, 66), std::max(0.25f, brightnessScale * 0.55f));
    }

    uint32_t blink_interval_ms(int blinkSpeedPct)
    {
        const int clamped = std::clamp(blinkSpeedPct, 0, 100);
        if (clamped <= 0)
        {
            return 0;
        }
        if (clamped >= 100)
        {
            return 1;
        }
        return static_cast<uint32_t>(480 - (clamped * 4));
    }

    void fill_inactive(std::vector<uint32_t> &leds, uint32_t color)
    {
        std::fill(leds.begin(), leds.end(), color);
    }

    void render_linear_mode(VirtualLedBarFrame &frame,
                            const SimulatorLedBarConfig &config,
                            float brightnessScale,
                            int ledCount,
                            int litCount,
                            bool shouldBlink,
                            bool blinkOn)
    {
        const uint32_t inactiveColor = inactive_led_color(brightnessScale);
        fill_inactive(frame.leds, inactiveColor);

        if (config.mode == SimulatorLedMode::Aggressive && shouldBlink)
        {
            const uint32_t blinkColor = blinkOn ? color_for_position(100.0f, config, brightnessScale) : kInactiveColor;
            std::fill(frame.leds.begin(), frame.leds.end(), blinkColor);
            frame.blinkActive = blinkOn;
            frame.litCount = blinkOn ? ledCount : 0;
            return;
        }

        int activeCount = 0;
        for (int i = 0; i < ledCount; ++i)
        {
            if (i >= litCount)
            {
                continue;
            }

            const float positionPct = ((static_cast<float>(i) + 1.0f) / static_cast<float>(ledCount)) * 100.0f;
            if (config.mode == SimulatorLedMode::F1 && shouldBlink && positionPct >= static_cast<float>(config.redEndPct))
            {
                frame.leds[static_cast<size_t>(i)] = blinkOn ? color_for_position(positionPct, config, brightnessScale) : kInactiveColor;
            }
            else
            {
                frame.leds[static_cast<size_t>(i)] = color_for_position(positionPct, config, brightnessScale);
            }

            if (frame.leds[static_cast<size_t>(i)] != kInactiveColor)
            {
                ++activeCount;
            }
        }

        frame.blinkActive = shouldBlink && blinkOn;
        frame.litCount = activeCount;
    }

    void render_gt3_mode(VirtualLedBarFrame &frame,
                         const SimulatorLedBarConfig &config,
                         float brightnessScale,
                         int ledCount,
                         float rpmRatio,
                         bool shouldBlink,
                         bool blinkOn)
    {
        const uint32_t inactiveColor = inactive_led_color(brightnessScale);
        fill_inactive(frame.leds, inactiveColor);

        if (shouldBlink)
        {
            const uint32_t blinkColor = blinkOn ? color_for_position(100.0f, config, brightnessScale) : kInactiveColor;
            std::fill(frame.leds.begin(), frame.leds.end(), blinkColor);
            frame.blinkActive = blinkOn;
            frame.litCount = blinkOn ? ledCount : 0;
            return;
        }

        const int pairCount = (ledCount + 1) / 2;
        const int pairsOn = std::clamp(static_cast<int>(std::ceil(rpmRatio * static_cast<float>(pairCount))), 0, pairCount);

        for (int rank = 0; rank < pairsOn; ++rank)
        {
            const float positionPct = ((static_cast<float>(rank) + 1.0f) / static_cast<float>(pairCount)) * 100.0f;
            const uint32_t color = color_for_position(positionPct, config, brightnessScale);
            const int left = rank;
            const int right = ledCount - 1 - rank;
            frame.leds[static_cast<size_t>(left)] = color;
            frame.leds[static_cast<size_t>(right)] = color;
        }

        frame.blinkActive = false;
        frame.litCount = std::min(ledCount, pairsOn * 2 - ((ledCount % 2 != 0 && pairsOn == pairCount) ? 1 : 0));
    }
}

VirtualLedBarFrame build_virtual_led_bar_frame(const UiRuntimeState &state,
                                               const SimulatorLedBarConfig &config,
                                               uint32_t nowMs)
{
    VirtualLedBarFrame frame{};
    const int ledCount = std::clamp(config.activeLedCount, 1, 60);
    frame.leds.assign(static_cast<size_t>(ledCount), 0);

    const float brightnessScale = std::clamp(static_cast<float>(config.brightness) / 255.0f, 0.08f, 1.0f);
    const int startRpm = std::clamp(config.startRpm, 0, 12000);
    const int maxRpm = std::clamp(config.effectiveMaxRpm, startRpm + 1, 14000);
    const float rpmRatio = std::clamp((static_cast<float>(state.rpm) - static_cast<float>(startRpm)) /
                                          static_cast<float>(std::max(1, maxRpm - startRpm)),
                                      0.0f,
                                      1.0f);
    frame.rpmRatio = rpmRatio;

    if (state.telemetryStale && !state.telemetryUsingFallback)
    {
        const uint32_t stale = stale_led_color(brightnessScale);
        std::fill(frame.leds.begin(), frame.leds.end(), stale);
        frame.litCount = 0;
        return frame;
    }

    const int litCount = std::clamp(static_cast<int>(std::ceil(rpmRatio * static_cast<float>(ledCount))), 0, ledCount);
    const bool shouldBlink = config.mode != SimulatorLedMode::Casual &&
                             rpmRatio * 100.0f >= static_cast<float>(config.blinkStartPct);
    const uint32_t intervalMs = blink_interval_ms(config.blinkSpeedPct);
    const bool blinkOn = !shouldBlink || intervalMs <= 1 || ((nowMs / intervalMs) % 2U) == 0U;

    switch (config.mode)
    {
    case SimulatorLedMode::Gt3:
        render_gt3_mode(frame, config, brightnessScale, ledCount, rpmRatio, shouldBlink, blinkOn);
        break;
    case SimulatorLedMode::Aggressive:
    case SimulatorLedMode::F1:
    case SimulatorLedMode::Casual:
    default:
        render_linear_mode(frame, config, brightnessScale, ledCount, litCount, shouldBlink, blinkOn);
        break;
    }

    return frame;
}

std::string virtual_led_color_hex(uint32_t rgbValue)
{
    char buffer[8];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "#%02X%02X%02X",
                  static_cast<unsigned int>((rgbValue >> 16) & 0xFFu),
                  static_cast<unsigned int>((rgbValue >> 8) & 0xFFu),
                  static_cast<unsigned int>(rgbValue & 0xFFu));
    return std::string(buffer);
}
