#ifdef ARDUINO

#include <Arduino.h>
#include <unity.h>

void run_core_tests();
// später:
// void run_bluetooth_tests();
// void run_connectivity_tests();
// Force-include the actual test implementations so PlatformIO links them in one runner.
#include "unit_core/test_clamp_int.cpp"
#include "unit_core/test_state_retry.cpp"

void setup()
{
    UNITY_BEGIN();

    run_core_tests();
    // RUN_TEST-Gruppen hier ergänzen:
    // run_bluetooth_tests();
    // run_connectivity_tests();

    UNITY_END();
}

void loop()
{
}

#endif // ARDUINO
