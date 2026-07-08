#include <unity.h>
#include <cmath>
#include <cstdint>
#include "pid_controller.h"
#include "speed_controller.h"

using namespace rt;

void setUp(void) {}
void tearDown(void) {}

void test_pid_p_only_clamped(void) {
    PidController pid;
    pid.ki = 0.0f; pid.kd = 0.0f;
    float out = pid.update(100.0f, 80.0f, 0.01f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, out);
}

void test_pid_negative_error_clamped(void) {
    PidController pid;
    pid.ki = 0.0f; pid.kd = 0.0f;
    float out = pid.update(50.0f, 120.0f, 0.01f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, out);
}

void test_pid_integral_accumulation(void) {
    PidController pid;
    pid.kp = 0.0f; pid.kd = 0.0f;
    float out = 0.0f;
    for (int i = 0; i < 50; ++i) {
        out = pid.update(10.0f, 0.0f, 0.01f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, out);
}

void test_pid_derivative_on_measurement(void) {
    PidController pid;
    pid.kp = 0.0f; pid.ki = 0.0f;
    pid.update(100.0f, 80.0f, 0.01f);
    float out = pid.update(100.0f, 85.0f, 0.01f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, out);
}

void test_pid_anti_windup_clamp(void) {
    PidController pid;
    pid.kp = 0.0f; pid.kd = 0.0f;
    pid.output_min = -0.5f;
    pid.output_max =  0.5f;
    float out = 0.0f;
    for (int i = 0; i < 200; ++i) {
        out = pid.update(200.0f, 100.0f, 0.01f);
    }
    TEST_ASSERT_TRUE(out <= 0.5f && out >= -0.5f);
}

void test_pid_setpoint_change_reset(void) {
    PidController pid;
    pid.kp = 0.0f; pid.kd = 0.0f;
    pid.setpoint_change_threshold = 200.0f;
    for (int i = 0; i < 50; ++i) {
        pid.update(100.0f, 90.0f, 0.01f);
    }
    float out = pid.update(500.0f, 90.0f, 0.01f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.41f, out);
}

void test_pid_d_term_low_pass_filter(void) {
    PidController pid;
    pid.kp = 0.0f; pid.ki = 0.0f;
    pid.d_filter_alpha = 0.7f;
    pid.update(100.0f, 80.0f, 0.01f);
    float out = pid.update(100.0f, 90.0f, 0.01f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, out);
}

void test_pid_reset_clears_state(void) {
    PidController pid;
    for (int i = 0; i < 50; ++i) pid.update(100.0f, 80.0f, 0.01f);
    pid.reset();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.integral);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.prev_error);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.prev_measurement);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.prev_setpoint);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.d_filtered);
    float out = pid.update(1000.0f, 900.0f, 0.01f);
    TEST_ASSERT_TRUE(out >= 0.0f);
}

void test_speed_controller_encoder_guard(void) {
    SpeedController sc;
    int16_t pid_out = 12345;
    sc.update_shadow_pid(500, 0, 0.01f, pid_out);
    TEST_ASSERT_EQUAL(0, pid_out);
}

void test_speed_controller_normal_operation(void) {
    SpeedController sc;
    int16_t pid_out = 0;
    sc.update_shadow_pid(1000, 900, 0.01f, pid_out);
    TEST_ASSERT_TRUE(pid_out > 0);
}

void test_pid_slow_ramp_anti_windup(void) {
    PidController pid;
    pid.kp = 1.0f;
    pid.ki = 0.1f;
    pid.kd = 0.0f;
    pid.output_min = -1.0f;
    pid.output_max = 1.0f;
    for (int i = 0; i < 100; ++i) {
        float setpoint = 100.0f + i * 10.0f;
        (void)pid.update(setpoint, 0.0f, 0.01f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pid.integral);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_pid_p_only_clamped);
    RUN_TEST(test_pid_negative_error_clamped);
    RUN_TEST(test_pid_integral_accumulation);
    RUN_TEST(test_pid_derivative_on_measurement);
    RUN_TEST(test_pid_anti_windup_clamp);
    RUN_TEST(test_pid_setpoint_change_reset);
    RUN_TEST(test_pid_d_term_low_pass_filter);
    RUN_TEST(test_pid_reset_clears_state);
    RUN_TEST(test_speed_controller_encoder_guard);
    RUN_TEST(test_speed_controller_normal_operation);
    RUN_TEST(test_pid_slow_ramp_anti_windup);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
