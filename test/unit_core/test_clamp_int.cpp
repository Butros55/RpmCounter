// test/unit_core/test_clampInt.cpp
#include <unity.h>
#include "core/utils.h"

void register_state_retry_tests();

static void test_clampInt_within_bounds(void)
{
    TEST_ASSERT_EQUAL(5, clampInt(5, 0, 10));
    TEST_ASSERT_EQUAL(0, clampInt(0, -5, 5));
    TEST_ASSERT_EQUAL(-3, clampInt(-3, -10, 10));
}

static void test_clampInt_too_low(void)
{
    TEST_ASSERT_EQUAL(0, clampInt(-50, 0, 10));
    TEST_ASSERT_EQUAL(-5, clampInt(-20, -5, 5));
}

static void test_clampInt_too_high(void)
{
    TEST_ASSERT_EQUAL(10, clampInt(999, 0, 10));
    TEST_ASSERT_EQUAL(5, clampInt(100, -5, 5));
}

// Wird von beiden Runnern benutzt (ESP32 + native)
void run_core_tests()
{
    RUN_TEST(test_clampInt_within_bounds);
    RUN_TEST(test_clampInt_too_low);
    RUN_TEST(test_clampInt_too_high);
    register_state_retry_tests();
}
