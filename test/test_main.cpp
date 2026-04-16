#ifdef ARDUINO

#include <Arduino.h>
#include <unity.h>

void run_core_tests();
void register_ambient_light_algo_tests();
void register_gesture_sensor_tests();
void register_signal_utils_tests();
void register_telemetry_manager_tests();
// später:
// void run_bluetooth_tests();
// void run_connectivity_tests();
// Force-include the actual test implementations so PlatformIO links them in one runner.
#include "unit_core/test_clamp_int.cpp"
#include "unit_core/test_ambient_light_algo.cpp"
#include "unit_core/test_gesture_sensor.cpp"
#include "unit_core/test_signal_utils.cpp"
#include "unit_core/test_state_retry.cpp"
#include "unit_core/test_telemetry_manager.cpp"

void setup()
{
    UNITY_BEGIN();

    run_core_tests();
    register_ambient_light_algo_tests();
    register_gesture_sensor_tests();
    register_signal_utils_tests();
    register_telemetry_manager_tests();
    // RUN_TEST-Gruppen hier ergänzen:
    // run_bluetooth_tests();
    // run_connectivity_tests();

    UNITY_END();
}

void loop()
{
}

#endif // ARDUINO
