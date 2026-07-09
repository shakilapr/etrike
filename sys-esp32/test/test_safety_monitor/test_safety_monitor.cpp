#include <unity.h>
#include <cstdint>
#include "shared_config.h"
#include "safety_monitor.h"

using namespace sys;

namespace sys {
    extern int64_t g_sys_test_time_us;
}

void setUp(void) {}
void tearDown(void) {}

void test_safety_startup_grace_heartbeat_ok(void) {
    SafetyMonitor sm;
    sm.init();
    g_sys_test_time_us = 0;
    TEST_ASSERT_TRUE(sm.heartbeat_ok());

    g_sys_test_time_us = int64_t(shared::kStartupGracePeriodMs - 100) * 1000;
    TEST_ASSERT_TRUE(sm.heartbeat_ok());
}

void test_safety_timeout_after_grace(void) {
    SafetyMonitor sm;
    sm.init();
    g_sys_test_time_us = 0;

    g_sys_test_time_us = int64_t(shared::kStartupGracePeriodMs + 100) * 1000;
    TEST_ASSERT_FALSE(sm.heartbeat_ok());
}

void test_safety_heartbeat_ok_with_data(void) {
    SafetyMonitor sm;
    sm.init();
    g_sys_test_time_us = 0;

    int64_t t0 = 1000000;
    g_sys_test_time_us = t0;
    sm.feed_heartbeat_rt(42);

    int64_t timeout_us = sys::kHeartbeatTimeoutMsRt * 1000LL;

    g_sys_test_time_us = t0 + timeout_us / 2;
    TEST_ASSERT_TRUE(sm.heartbeat_ok());

    g_sys_test_time_us = t0 + timeout_us + 100000; // timeout + 100ms
    TEST_ASSERT_FALSE(sm.heartbeat_ok());
}

void test_safety_frozen_counter_treated_as_missed(void) {
    SafetyMonitor sm;
    sm.init();
    g_sys_test_time_us = 0;

    int64_t t0 = 1000000;
    g_sys_test_time_us = t0;
    sm.feed_heartbeat_rt(42);

    int64_t timeout_us = sys::kHeartbeatTimeoutMsRt * 1000LL;

    g_sys_test_time_us = t0 + timeout_us / 2;
    sm.feed_heartbeat_rt(42);

    g_sys_test_time_us = t0 + timeout_us + 100000;
    TEST_ASSERT_FALSE(sm.heartbeat_ok());
}

void test_safety_normal_counter_keeps_alive(void) {
    SafetyMonitor sm;
    sm.init();
    g_sys_test_time_us = 0;

    for (int i = 0; i < 10; ++i) {
        g_sys_test_time_us += 100000;
        sm.feed_heartbeat_rt(i);
        TEST_ASSERT_TRUE(sm.heartbeat_ok());
    }
}

void test_safety_flags(void) {
    SafetyMonitor sm;
    sm.init();

    TEST_ASSERT_FALSE(sm.estop_active());
    TEST_ASSERT_FALSE(sm.brake_lever_pressed());

    sm.set_estop(true);
    TEST_ASSERT_TRUE(sm.estop_active());

    sm.set_estop(false);
    TEST_ASSERT_FALSE(sm.estop_active());

    sm.set_brake_lever(true);
    TEST_ASSERT_TRUE(sm.brake_lever_pressed());

    sm.set_brake_lever(false);
    TEST_ASSERT_FALSE(sm.brake_lever_pressed());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_safety_startup_grace_heartbeat_ok);
    RUN_TEST(test_safety_timeout_after_grace);
    RUN_TEST(test_safety_heartbeat_ok_with_data);
    RUN_TEST(test_safety_frozen_counter_treated_as_missed);
    RUN_TEST(test_safety_normal_counter_keeps_alive);
    RUN_TEST(test_safety_flags);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
