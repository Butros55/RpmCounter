#include <unity.h>

#include "hardware/gesture_sensor.h"

static void test_gestureSensorNextMode_wraps_forward()
{
    TEST_ASSERT_EQUAL(1, gestureSensorNextMode(0));
    TEST_ASSERT_EQUAL(2, gestureSensorNextMode(1));
    TEST_ASSERT_EQUAL(3, gestureSensorNextMode(2));
    TEST_ASSERT_EQUAL(0, gestureSensorNextMode(3));
}

static void test_gestureSensorPreviousMode_wraps_backward()
{
    TEST_ASSERT_EQUAL(3, gestureSensorPreviousMode(0));
    TEST_ASSERT_EQUAL(0, gestureSensorPreviousMode(1));
    TEST_ASSERT_EQUAL(1, gestureSensorPreviousMode(2));
    TEST_ASSERT_EQUAL(2, gestureSensorPreviousMode(3));
}

static void test_gestureSensorModeAfterDirection_maps_left_and_right()
{
    TEST_ASSERT_EQUAL(2, gestureSensorModeAfterDirection(1, GestureDirection::Right));
    TEST_ASSERT_EQUAL(0, gestureSensorModeAfterDirection(1, GestureDirection::Left));
    TEST_ASSERT_EQUAL(1, gestureSensorModeAfterDirection(1, GestureDirection::None));
}

static void test_gestureSensorCooldownReady_blocks_recent_swipes()
{
    TEST_ASSERT_TRUE(gestureSensorCooldownReady(1000, 0, 650));
    TEST_ASSERT_FALSE(gestureSensorCooldownReady(1400, 1000, 650));
    TEST_ASSERT_TRUE(gestureSensorCooldownReady(1650, 1000, 650));
}

static void test_gestureSensorClassifyDeltas_accepts_horizontal_swipes_only()
{
    TEST_ASSERT_EQUAL(GestureDirection::Right, gestureSensorClassifyDeltas(26, 5, 18, 8));
    TEST_ASSERT_EQUAL(GestureDirection::Left, gestureSensorClassifyDeltas(-24, 4, 18, 8));
    TEST_ASSERT_EQUAL(GestureDirection::None, gestureSensorClassifyDeltas(12, 3, 18, 8));
    TEST_ASSERT_EQUAL(GestureDirection::None, gestureSensorClassifyDeltas(20, 17, 18, 8));
}

void register_gesture_sensor_tests()
{
    RUN_TEST(test_gestureSensorNextMode_wraps_forward);
    RUN_TEST(test_gestureSensorPreviousMode_wraps_backward);
    RUN_TEST(test_gestureSensorModeAfterDirection_maps_left_and_right);
    RUN_TEST(test_gestureSensorCooldownReady_blocks_recent_swipes);
    RUN_TEST(test_gestureSensorClassifyDeltas_accepts_horizontal_swipes_only);
}
