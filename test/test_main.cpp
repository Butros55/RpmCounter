#ifdef ARDUINO

#include <Arduino.h>
#include <unity.h>
#include "unit_core/test_clamp_int.cpp"

// Deklarationen der Runner aus Unterordnern
void run_core_tests();
// später:
// void run_bluetooth_tests();
// void run_connectivity_tests();

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
    // nicht benutzt in PlatformIO Tests
}

#endif // ARDUINO
