#include <unity.h>
#include <cmath>
#include <cstdint>
#include "can/can_protocol.h"
#include "steering_control.h"

using namespace rt;

bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = false;

static void boot_to_active(SteeringControl& sc, uint32_t& now_ms, int16_t sync_angle = 0) {
    can::VcuSesReq out;
    int ticks = (kSteerBootWaitMs * kSteerCmdRateHz) / 1000;
    int dt_ms = 1000 / kSteerCmdRateHz;
    for (int i = 0; i < ticks; ++i) {
        sc.tick(INT16_MIN, 0, now_ms += dt_ms, out);
    }
    TEST_ASSERT_EQUAL(SteerState::STEER_LISTEN_SYNC, sc.state());
    sc.tick(sync_angle, 1, now_ms += 20, out);
    TEST_ASSERT_EQUAL(SteerState::STEER_ACTIVE, sc.state());
}

void setUp(void) {}
void tearDown(void) {}

void test_steering_obstacle_estop_hold_angle_clamp(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    boot_to_active(sc, now_ms);

    int32_t angle_30deg_mdeg = 30 * 1000;
    int32_t speed_25kmh_mmps = (25 * 1000000) / 3600; // 6944 mm/s
    sc.set_target(angle_30deg_mdeg, speed_25kmh_mmps);

    sc.start_estop(true);
    TEST_ASSERT_EQUAL(SteerState::ESTOP_HOLD_THEN_SILENT, sc.state());

    can::VcuSesReq out;
    sc.set_estop_hold_time(now_ms);
    sc.tick(0, 1, now_ms += 20, out);

    int16_t cmd_angle = out.target_angle;
    int16_t cmd_offset_free = cmd_angle - rt::kSbwAngleOffset;
    TEST_ASSERT_TRUE(std::abs(cmd_offset_free) <= 60);
}

void test_steering_obstacle_estop_within_limit(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    boot_to_active(sc, now_ms);

    sc.set_target(3 * 1000, 555);
    sc.start_estop(true);
    TEST_ASSERT_EQUAL(SteerState::ESTOP_HOLD_THEN_SILENT, sc.state());
}

void test_steering_non_obstacle_estop_ramp_to_zero(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    boot_to_active(sc, now_ms, 300);

    sc.start_estop(false);
    TEST_ASSERT_EQUAL(SteerState::ESTOP_RAMP_TO_ZERO, sc.state());

    can::VcuSesReq out;
    sc.tick(300, 1, now_ms, out);
    int16_t first_step = out.target_angle;
    int16_t first_step_offset_free = first_step - rt::kSbwAngleOffset;
    TEST_ASSERT_TRUE(first_step_offset_free < 300);
}

void test_steering_ramp_following_error_fault(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    boot_to_active(sc, now_ms, 300);
    sc.start_estop(false);

    can::VcuSesReq out;
    bool faulted = false;
    for (int i = 0; i < 80; ++i, now_ms += 20) {
        int16_t actual = 300;
        sc.tick(actual, 1, now_ms, out);
        if (sc.state() == SteerState::STEER_FAULT) {
            faulted = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(faulted);
    TEST_ASSERT_EQUAL(SteerState::STEER_FAULT, sc.state());
}

void test_steering_ramp_following_error_not_triggered(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    boot_to_active(sc, now_ms, 100);
    sc.start_estop(false);

    can::VcuSesReq out;
    int16_t cmd = 100;
    bool faulted = false;
    for (int i = 0; i < 60; ++i, now_ms += 20) {
        if (cmd > 4) cmd -= 4; else cmd = 0;
        int16_t actual = cmd + 10;
        sc.tick(actual, 1, now_ms, out);
        if (sc.state() == SteerState::STEER_FAULT) {
            faulted = true;
            break;
        }
    }
    TEST_ASSERT_FALSE(faulted);
    TEST_ASSERT_EQUAL(SteerState::ESTOP_RAMP_TO_ZERO, sc.state());
}

void test_steering_hold_then_silent_timeout(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    boot_to_active(sc, now_ms);

    sc.set_target(10 * 1000, 555);
    sc.start_estop(true);
    TEST_ASSERT_EQUAL(SteerState::ESTOP_HOLD_THEN_SILENT, sc.state());

    sc.set_estop_hold_time(now_ms);

    can::VcuSesReq out;
    for (int i = 0; i < 26; ++i, now_ms += 20) {
        sc.tick(0, 1, now_ms, out);
        if (sc.state() == SteerState::STEER_FAULT) break;
    }
    TEST_ASSERT_EQUAL(SteerState::STEER_FAULT, sc.state());
}

void test_steering_exit_estop_deferred_ramp(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    boot_to_active(sc, now_ms);

    sc.set_target(300 * 10, 0);
    sc.start_estop(false);
    TEST_ASSERT_EQUAL(SteerState::ESTOP_RAMP_TO_ZERO, sc.state());

    sc.exit_estop();
    TEST_ASSERT_EQUAL(SteerState::ESTOP_RAMP_TO_ZERO, sc.state());

    can::VcuSesReq dummy;
    for (int i = 0; i < 160; ++i) {
        int16_t ses_angle = 0;
        if (i < 150) ses_angle = int16_t(300 - i * 2);
        sc.tick(ses_angle, 1, now_ms += 20, dummy);
    }
    TEST_ASSERT_EQUAL(SteerState::STEER_ACTIVE, sc.state());
}

void test_steering_exit_estop_deferred_hold(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    boot_to_active(sc, now_ms);

    sc.start_estop(true);
    sc.set_estop_hold_time(now_ms);
    TEST_ASSERT_EQUAL(SteerState::ESTOP_HOLD_THEN_SILENT, sc.state());

    sc.exit_estop();
    TEST_ASSERT_EQUAL(SteerState::ESTOP_HOLD_THEN_SILENT, sc.state());

    can::VcuSesReq dummy;
    now_ms += 501;
    sc.tick(INT16_MIN, 0, now_ms, dummy);
    TEST_ASSERT_EQUAL(SteerState::STEER_ACTIVE, sc.state());
}

void test_steering_fault_recovery(void) {
    SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;

    can::VcuSesReq dummy;
    int ticks = (kSteerBootWaitMs * kSteerCmdRateHz) / 1000;
    int dt_ms = 1000 / kSteerCmdRateHz;
    for (int i = 0; i < ticks; ++i)
        sc.tick(INT16_MIN, 0, now_ms += dt_ms, dummy);
    TEST_ASSERT_EQUAL(SteerState::STEER_LISTEN_SYNC, sc.state());
    now_ms += 5001;
    sc.tick(INT16_MIN, 0, now_ms, dummy);
    TEST_ASSERT_EQUAL(SteerState::STEER_FAULT, sc.state());

    sc.reset_to_listen(now_ms);
    TEST_ASSERT_EQUAL(SteerState::STEER_LISTEN_SYNC, sc.state());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_steering_obstacle_estop_hold_angle_clamp);
    RUN_TEST(test_steering_obstacle_estop_within_limit);
    RUN_TEST(test_steering_non_obstacle_estop_ramp_to_zero);
    RUN_TEST(test_steering_ramp_following_error_fault);
    RUN_TEST(test_steering_ramp_following_error_not_triggered);
    RUN_TEST(test_steering_hold_then_silent_timeout);
    RUN_TEST(test_steering_exit_estop_deferred_ramp);
    RUN_TEST(test_steering_exit_estop_deferred_hold);
    RUN_TEST(test_steering_fault_recovery);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
