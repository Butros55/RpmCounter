#include <unity.h>

#include "telemetry/telemetry_manager.h"

static void test_selectTelemetryRuntimeSource_auto_prefers_usb()
{
    const ActiveTelemetrySource source =
        selectTelemetryRuntimeSource(TelemetryPreference::Auto, SimTransportPreference::Auto, true, true, true);
    TEST_ASSERT_EQUAL(ActiveTelemetrySource::UsbSim, source);
}

static void test_selectTelemetryRuntimeSource_auto_falls_back_to_network()
{
    const ActiveTelemetrySource source =
        selectTelemetryRuntimeSource(TelemetryPreference::Auto, SimTransportPreference::Auto, false, true, true);
    TEST_ASSERT_EQUAL(ActiveTelemetrySource::SimHubNetwork, source);
}

static void test_selectTelemetryRuntimeSource_auto_falls_back_to_obd_after_sim()
{
    const ActiveTelemetrySource source =
        selectTelemetryRuntimeSource(TelemetryPreference::Auto, SimTransportPreference::Auto, false, false, true);
    TEST_ASSERT_EQUAL(ActiveTelemetrySource::Obd, source);
}

static void test_selectTelemetryRuntimeSource_usb_only_never_uses_network()
{
    const ActiveTelemetrySource source =
        selectTelemetryRuntimeSource(TelemetryPreference::SimHub, SimTransportPreference::UsbSerial, false, true, true);
    TEST_ASSERT_EQUAL(ActiveTelemetrySource::None, source);
}

static void test_selectTelemetryRuntimeSource_network_only_never_uses_usb()
{
    const ActiveTelemetrySource source =
        selectTelemetryRuntimeSource(TelemetryPreference::SimHub, SimTransportPreference::Network, true, false, true);
    TEST_ASSERT_EQUAL(ActiveTelemetrySource::None, source);
}

static void test_selectTelemetryRuntimeSource_fixed_modes_use_only_their_transport()
{
    TEST_ASSERT_EQUAL(ActiveTelemetrySource::UsbSim,
                      selectTelemetryRuntimeSource(TelemetryPreference::SimHub, SimTransportPreference::UsbSerial, true, true, false));
    TEST_ASSERT_EQUAL(ActiveTelemetrySource::SimHubNetwork,
                      selectTelemetryRuntimeSource(TelemetryPreference::SimHub, SimTransportPreference::Network, true, true, false));
}

static void test_telemetrySourceIsFallback_marks_network_and_obd_only_in_auto()
{
    TEST_ASSERT_FALSE(telemetrySourceIsFallback(ActiveTelemetrySource::UsbSim, TelemetryPreference::Auto, SimTransportPreference::Auto));
    TEST_ASSERT_TRUE(telemetrySourceIsFallback(ActiveTelemetrySource::SimHubNetwork, TelemetryPreference::Auto, SimTransportPreference::Auto));
    TEST_ASSERT_TRUE(telemetrySourceIsFallback(ActiveTelemetrySource::Obd, TelemetryPreference::Auto, SimTransportPreference::Auto));
    TEST_ASSERT_FALSE(telemetrySourceIsFallback(ActiveTelemetrySource::SimHubNetwork, TelemetryPreference::SimHub, SimTransportPreference::Network));
}

static void test_telemetry_transport_helpers_follow_mode_rules()
{
    TEST_ASSERT_TRUE(telemetryAllowsUsbSim(TelemetryPreference::SimHub, SimTransportPreference::Auto));
    TEST_ASSERT_TRUE(telemetryAllowsNetworkSim(TelemetryPreference::SimHub, SimTransportPreference::Auto));
    TEST_ASSERT_FALSE(telemetryAllowsNetworkSim(TelemetryPreference::SimHub, SimTransportPreference::UsbSerial));
    TEST_ASSERT_FALSE(telemetryAllowsUsbSim(TelemetryPreference::SimHub, SimTransportPreference::Network));
    TEST_ASSERT_TRUE(telemetrySupportsTransportFallback(TelemetryPreference::SimHub, SimTransportPreference::Auto));
    TEST_ASSERT_FALSE(telemetrySupportsTransportFallback(TelemetryPreference::SimHub, SimTransportPreference::UsbSerial));
}

static void test_simSessionStateDebounceMs_waiting_is_slower_than_live()
{
    TEST_ASSERT_EQUAL_UINT32(100, simSessionStateDebounceMs(SimSessionState::Live));
    TEST_ASSERT_EQUAL_UINT32(1200, simSessionStateDebounceMs(SimSessionState::WaitingForData));
    TEST_ASSERT_EQUAL_UINT32(1500, simSessionStateDebounceMs(SimSessionState::Error));
}

static void test_telemetrySimSessionCandidateReady_requires_full_debounce_window()
{
    TEST_ASSERT_FALSE(telemetrySimSessionCandidateReady(SimSessionState::WaitingForData, 1000, 1500));
    TEST_ASSERT_TRUE(telemetrySimSessionCandidateReady(SimSessionState::WaitingForData, 1000, 2200));
    TEST_ASSERT_FALSE(telemetrySimSessionCandidateReady(SimSessionState::Live, 500, 550));
    TEST_ASSERT_TRUE(telemetrySimSessionCandidateReady(SimSessionState::Live, 500, 600));
}

void register_telemetry_manager_tests()
{
    RUN_TEST(test_selectTelemetryRuntimeSource_auto_prefers_usb);
    RUN_TEST(test_selectTelemetryRuntimeSource_auto_falls_back_to_network);
    RUN_TEST(test_selectTelemetryRuntimeSource_auto_falls_back_to_obd_after_sim);
    RUN_TEST(test_selectTelemetryRuntimeSource_usb_only_never_uses_network);
    RUN_TEST(test_selectTelemetryRuntimeSource_network_only_never_uses_usb);
    RUN_TEST(test_selectTelemetryRuntimeSource_fixed_modes_use_only_their_transport);
    RUN_TEST(test_telemetrySourceIsFallback_marks_network_and_obd_only_in_auto);
    RUN_TEST(test_telemetry_transport_helpers_follow_mode_rules);
    RUN_TEST(test_simSessionStateDebounceMs_waiting_is_slower_than_live);
    RUN_TEST(test_telemetrySimSessionCandidateReady_requires_full_debounce_window);
}
