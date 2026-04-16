#include <unity.h>

#include "ambient_light_algo.h"

static void test_ambientNormalizeLux_uses_log_curve_and_clamps()
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ambientNormalizeLux(0.0f, 2.0f, 5000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, ambientNormalizeLux(9000.0f, 2.0f, 5000.0f));
    TEST_ASSERT_TRUE(ambientNormalizeLux(100.0f, 2.0f, 5000.0f) > ambientNormalizeLux(10.0f, 2.0f, 5000.0f));
}

static void test_ambientComputeTargetBrightness_respects_manual_ceiling()
{
    AutoBrightnessCurveConfig config{};
    config.manualMax = 90;
    config.minBrightness = 18;
    config.strengthPct = 100;
    config.luxMin = 2;
    config.luxMax = 5000;

    TEST_ASSERT_EQUAL(18, ambientComputeTargetBrightness(0.0f, config));
    TEST_ASSERT_EQUAL(90, ambientComputeTargetBrightness(7000.0f, config));
}

static void test_ambientComputeTargetBrightness_strength_scales_curve()
{
    AutoBrightnessCurveConfig dimmer{};
    dimmer.manualMax = 120;
    dimmer.minBrightness = 20;
    dimmer.strengthPct = 70;
    dimmer.luxMin = 2;
    dimmer.luxMax = 5000;

    AutoBrightnessCurveConfig brighter = dimmer;
    brighter.strengthPct = 140;

    const int dimValue = ambientComputeTargetBrightness(400.0f, dimmer);
    const int brightValue = ambientComputeTargetBrightness(400.0f, brighter);
    TEST_ASSERT_TRUE(brightValue > dimValue);
    TEST_ASSERT_TRUE(brightValue <= brighter.manualMax);
}

static void test_ambientComputeResponseAlpha_increases_with_response()
{
    TEST_ASSERT_TRUE(ambientComputeResponseAlpha(80) > ambientComputeResponseAlpha(20));
}

void register_ambient_light_algo_tests()
{
    RUN_TEST(test_ambientNormalizeLux_uses_log_curve_and_clamps);
    RUN_TEST(test_ambientComputeTargetBrightness_respects_manual_ceiling);
    RUN_TEST(test_ambientComputeTargetBrightness_strength_scales_curve);
    RUN_TEST(test_ambientComputeResponseAlpha_increases_with_response);
}
