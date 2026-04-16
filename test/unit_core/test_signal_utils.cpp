#include <unity.h>

#include "signal_utils.h"

static void test_median3Int_returns_middle_value()
{
    TEST_ASSERT_EQUAL(20, median3Int(10, 20, 30));
    TEST_ASSERT_EQUAL(20, median3Int(30, 20, 10));
    TEST_ASSERT_EQUAL(20, median3Int(20, 10, 30));
}

static void test_isShortGapSpike_requires_short_gap_and_large_delta()
{
    TEST_ASSERT_TRUE(isShortGapSpike(1500, 2800, 16, 1000, 40));
    TEST_ASSERT_FALSE(isShortGapSpike(1500, 2100, 16, 1000, 40));
    TEST_ASSERT_FALSE(isShortGapSpike(1500, 2800, 80, 1000, 40));
    TEST_ASSERT_FALSE(isShortGapSpike(1500, 2800, 0, 1000, 40));
}

static void test_isShortGapDip_requires_short_gap_and_large_drop()
{
    TEST_ASSERT_TRUE(isShortGapDip(4200, 2600, 12, 1200, 40));
    TEST_ASSERT_FALSE(isShortGapDip(4200, 3300, 12, 1200, 40));
    TEST_ASSERT_FALSE(isShortGapDip(4200, 2600, 80, 1200, 40));
    TEST_ASSERT_FALSE(isShortGapDip(2600, 4200, 12, 1200, 40));
}

static void test_cooldownElapsed_respects_recent_events()
{
    TEST_ASSERT_TRUE(cooldownElapsed(1000, 0, 5000));
    TEST_ASSERT_FALSE(cooldownElapsed(3000, 1000, 5000));
    TEST_ASSERT_TRUE(cooldownElapsed(7000, 1000, 5000));
}

static void test_applyDisplayLevelHysteresis_stabilizes_linear_levels()
{
    int level = 5;
    level = applyDisplayLevelHysteresis(level, 5.49f, 30);
    TEST_ASSERT_EQUAL(5, level);
    level = applyDisplayLevelHysteresis(level, 5.57f, 30);
    TEST_ASSERT_EQUAL(6, level);
    level = applyDisplayLevelHysteresis(level, 5.41f, 30);
    TEST_ASSERT_EQUAL(6, level);
    level = applyDisplayLevelHysteresis(level, 5.21f, 30);
    TEST_ASSERT_EQUAL(5, level);
}

static void test_applyDisplayLevelHysteresis_handles_gt3_pair_steps()
{
    int level = 7;
    level = applyDisplayLevelHysteresis(level, 7.54f, 15);
    TEST_ASSERT_EQUAL(7, level);
    level = applyDisplayLevelHysteresis(level, 7.62f, 15);
    TEST_ASSERT_EQUAL(8, level);
    level = applyDisplayLevelHysteresis(level, 7.31f, 15);
    TEST_ASSERT_EQUAL(8, level);
    level = applyDisplayLevelHysteresis(level, 7.18f, 15);
    TEST_ASSERT_EQUAL(7, level);
}

static void test_displayLevelTail_helpers_cover_fill_and_fade_cases()
{
    TEST_ASSERT_EQUAL(-1, displayLevelTailIndex(0, 0.0f, 15));
    TEST_ASSERT_EQUAL(0, displayLevelTailIndex(0, 0.42f, 15));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.42f, displayLevelTailIntensity(0, 0.42f, 15));

    TEST_ASSERT_EQUAL(5, displayLevelTailIndex(5, 5.38f, 15));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.38f, displayLevelTailIntensity(5, 5.38f, 15));

    TEST_ASSERT_EQUAL(4, displayLevelTailIndex(5, 4.61f, 15));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.61f, displayLevelTailIntensity(5, 4.61f, 15));

    TEST_ASSERT_EQUAL(-1, displayLevelTailIndex(15, 15.0f, 15));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, displayLevelTailIntensity(15, 15.0f, 15));
}

void register_signal_utils_tests()
{
    RUN_TEST(test_median3Int_returns_middle_value);
    RUN_TEST(test_isShortGapSpike_requires_short_gap_and_large_delta);
    RUN_TEST(test_isShortGapDip_requires_short_gap_and_large_drop);
    RUN_TEST(test_cooldownElapsed_respects_recent_events);
    RUN_TEST(test_applyDisplayLevelHysteresis_stabilizes_linear_levels);
    RUN_TEST(test_applyDisplayLevelHysteresis_handles_gt3_pair_steps);
    RUN_TEST(test_displayLevelTail_helpers_cover_fill_and_fade_cases);
}
