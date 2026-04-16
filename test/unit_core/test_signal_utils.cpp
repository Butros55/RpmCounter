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

void register_signal_utils_tests()
{
    RUN_TEST(test_median3Int_returns_middle_value);
    RUN_TEST(test_isShortGapSpike_requires_short_gap_and_large_delta);
}
