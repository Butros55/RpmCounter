#include <unity.h>
#include "core/state.h"
#include "core/config.h"
#include "core/utils.h"

static void test_computeAutoReconnectInterval_fast()
{
    TEST_ASSERT_EQUAL_UINT32(AUTO_RECONNECT_FAST_INTERVAL_MS, computeAutoReconnectInterval(0));
    TEST_ASSERT_EQUAL_UINT32(AUTO_RECONNECT_FAST_INTERVAL_MS, computeAutoReconnectInterval(AUTO_RECONNECT_FAST_ATTEMPTS - 1));
}

static void test_computeAutoReconnectInterval_slow()
{
    TEST_ASSERT_EQUAL_UINT32(AUTO_RECONNECT_SLOW_INTERVAL_MS, computeAutoReconnectInterval(AUTO_RECONNECT_FAST_ATTEMPTS));
    TEST_ASSERT_EQUAL_UINT32(AUTO_RECONNECT_SLOW_INTERVAL_MS, computeAutoReconnectInterval(AUTO_RECONNECT_FAST_ATTEMPTS + 3));
}

static void test_isHttpGraceElapsed_behaviour()
{
    // Exact boundary should keep grace active.
    TEST_ASSERT_FALSE(isHttpGraceElapsed(7000, 2000, 5000, false));
    TEST_ASSERT_TRUE(isHttpGraceElapsed(8001, 2000, 5000, false));
    TEST_ASSERT_TRUE(isHttpGraceElapsed(2500, 2000, 500, true));
}

static void test_shouldAutoReconnectNow_respects_flags()
{
    unsigned long now = 10000;
    // Connected devices must not trigger reconnects.
    TEST_ASSERT_FALSE(shouldAutoReconnectNow(now, true, false, true, false, false, 0, 0, 5000, 0, false));
    // Manual connect blocks auto reconnect.
    TEST_ASSERT_FALSE(shouldAutoReconnectNow(now, true, false, false, false, true, 0, 0, 5000, 0, false));
    // Paused also blocks.
    TEST_ASSERT_FALSE(shouldAutoReconnectNow(now, true, true, false, false, false, 0, 0, 5000, 0, false));
}

static void test_shouldAutoReconnectNow_interval_and_force()
{
    unsigned long now = 12000;
    // Interval elapsed -> reconnect allowed.
    TEST_ASSERT_TRUE(shouldAutoReconnectNow(now, true, false, false, false, false, 0, 0, 5000, 0, false));
    // Interval not elapsed when using slow backoff.
    TEST_ASSERT_FALSE(shouldAutoReconnectNow(7000, true, false, false, false, false, 4000, AUTO_RECONNECT_FAST_ATTEMPTS + 1, 5000, 0, false));
    // Force flag bypasses grace/interval.
    TEST_ASSERT_TRUE(shouldAutoReconnectNow(2500, true, false, false, false, false, 2400, 0, 5000, 2400, true));
}

static void test_hexByte_basic()
{
    TEST_ASSERT_EQUAL(0x1A, hexByte("1A2B", 0));
    TEST_ASSERT_EQUAL(0x2B, hexByte("1A2B", 2));
    TEST_ASSERT_EQUAL(-1, hexByte("", 0));
    TEST_ASSERT_EQUAL(-1, hexByte("1A", 1));
}

void register_state_retry_tests()
{
    RUN_TEST(test_computeAutoReconnectInterval_fast);
    RUN_TEST(test_computeAutoReconnectInterval_slow);
    RUN_TEST(test_isHttpGraceElapsed_behaviour);
    RUN_TEST(test_shouldAutoReconnectNow_respects_flags);
    RUN_TEST(test_shouldAutoReconnectNow_interval_and_force);
    RUN_TEST(test_hexByte_basic);
}
