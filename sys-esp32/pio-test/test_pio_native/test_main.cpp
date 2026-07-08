#include <unity.h>

#include "../../src/mode_manager.cpp"
#include "../../src/safety_monitor.cpp"

void test_mode_manager_starts_manual() {
    sys::ModeManager mm;
    mm.init();
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Mode::Manual), uint8_t(mm.mode()));
}

void test_mode_manager_rejects_can_estop() {
    sys::ModeManager mm;
    mm.init();
    mm.set_from_can(uint8_t(can::Mode::Auto));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Mode::Auto), uint8_t(mm.mode()));
    mm.set_from_can(uint8_t(can::Mode::Estop));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Mode::Auto), uint8_t(mm.mode()));
}

void test_safety_monitor_frozen_counter_times_out() {
    sys::SafetyMonitor sm;
    sm.init();
    sys::g_sys_test_time_us = int64_t(shared::kStartupGracePeriodMs + 100) * 1000;
    sm.feed_heartbeat_rt(7);
    TEST_ASSERT_TRUE(sm.heartbeat_ok());

    sys::g_sys_test_time_us += 100 * 1000;
    sm.feed_heartbeat_rt(7);
    sys::g_sys_test_time_us += int64_t(sys::kHeartbeatTimeoutMsRt + 1) * 1000;
    TEST_ASSERT_FALSE(sm.heartbeat_ok());
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mode_manager_starts_manual);
    RUN_TEST(test_mode_manager_rejects_can_estop);
    RUN_TEST(test_safety_monitor_frozen_counter_times_out);
    return UNITY_END();
}
