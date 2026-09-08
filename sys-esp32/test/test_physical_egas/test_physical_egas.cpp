#include <unity.h>
#include <cstdint>
#include "physical_egas.h"

using namespace sys;

void setUp(void) {}
void tearDown(void) {}

// Physical EGAS only runs when the sensor is installed AND reporting VALID fresh
// frames. Everything else must be inert (the caller decides sensor-loss policy).
void test_disabled_unless_installed_valid_fresh(void) {
    PhysicalEgasMonitor m;
    PhysicalEgasInput in;
    in.commanded_mmps = 0;
    in.measured_mmps = 2500;  // huge speed, must NOT trip when sensor not usable
    in.sensor_state = 2;      // VALID
    in.frame_fresh = true;
    in.sensor_installed = false;
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));

    in.sensor_installed = true;
    in.sensor_state = 0;  // NOT_INSTALLED
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));

    in.sensor_state = 3;  // FAULT
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));

    in.sensor_state = 2;  // VALID but stale
    in.frame_fresh = false;
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));
}

void test_runaway_when_moving_while_not_commanded(void) {
    PhysicalEgasMonitor m;
    PhysicalEgasInput in;
    in.sensor_installed = true;
    in.sensor_state = 2;
    in.frame_fresh = true;
    in.commanded_mmps = 0;
    in.measured_mmps = 600;  // > 400 runaway threshold

    for (int i = 0; i < kPhysicalEgasTripFrames - 1; ++i)
        TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::Runaway),
                      uint8_t(m.update(in)));

    // Command returns, measured stops -> recovers.
    in.measured_mmps = 0;
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));
}

void test_direction_mismatch(void) {
    PhysicalEgasMonitor m;
    PhysicalEgasInput in;
    in.sensor_installed = true;
    in.sensor_state = 2;
    in.frame_fresh = true;
    in.commanded_mmps = 1000;   // forward
    in.measured_mmps = -1000;   // moving backward

    for (int i = 0; i < kPhysicalEgasTripFrames - 1; ++i)
        TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::DirectionMismatch),
                      uint8_t(m.update(in)));
}

void test_stall_when_commanded_but_not_moving(void) {
    PhysicalEgasMonitor m;
    PhysicalEgasInput in;
    in.sensor_installed = true;
    in.sensor_state = 2;
    in.frame_fresh = true;
    in.commanded_mmps = 1000;
    in.measured_mmps = 0;  // wheel not physically moving

    for (int i = 0; i < kPhysicalEgasTripFrames - 1; ++i)
        TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::Stall), uint8_t(m.update(in)));
}

void test_sensor_lost_resets_pending_trip(void) {
    PhysicalEgasMonitor m;
    PhysicalEgasInput in;
    in.sensor_installed = true;
    in.sensor_state = 2;
    in.frame_fresh = true;
    in.commanded_mmps = 1000;
    in.measured_mmps = 0;

    for (int i = 0; i < kPhysicalEgasTripFrames - 1; ++i)
        m.update(in);  // accumulating stall credit

    in.frame_fresh = false;  // stream lost: counters must reset, not fire later
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));

    in.frame_fresh = true;
    in.measured_mmps = 0;
    TEST_ASSERT_EQUAL(uint8_t(PhysicalEgasVerdict::OK), uint8_t(m.update(in)));
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_unless_installed_valid_fresh);
    RUN_TEST(test_runaway_when_moving_while_not_commanded);
    RUN_TEST(test_direction_mismatch);
    RUN_TEST(test_stall_when_commanded_but_not_moving);
    RUN_TEST(test_sensor_lost_resets_pending_trip);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
