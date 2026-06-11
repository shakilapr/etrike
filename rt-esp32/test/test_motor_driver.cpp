// g++ -std=c++17 -I. -I../src test_motor_driver.cpp ../src/motor_driver.cpp -o test_motor_driver && ./test_motor_driver

#include <cstdio>
#include "stubs.h"
#include "../src/config.h"
#include "../src/motor_driver.h"
// intermcu/ removed — obsolete inter-MCU protocol

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== Motor Driver Tests ===\n\n");
    using namespace sys;

    MotorDriver motor;

    // MANUAL path: speed command maps linearly to PWM duty.
    motor.set_speed(kMotorMaxSpeedMmps);
    CHECK(g_mock_gpio[kMotorDirGpio] == 1);
    CHECK(g_mock_ledc_duty[LEDC_CHANNEL_1] == inter_mcu::kMotorEffortMax);
    printf("  ok  set_speed maps max speed to max duty\n");

    motor.set_speed(kMotorMaxSpeedMmps / 2);
    CHECK(g_mock_ledc_duty[LEDC_CHANNEL_1] >= (inter_mcu::kMotorEffortMax / 2) - 1);
    CHECK(g_mock_ledc_duty[LEDC_CHANNEL_1] <= (inter_mcu::kMotorEffortMax / 2) + 1);
    printf("  ok  set_speed is speed-mapped\n");

    // AUTO path: PID effort command is applied directly, not speed-mapped.
    motor.set_effort(3000);
    CHECK(g_mock_gpio[kMotorDirGpio] == 1);
    CHECK(g_mock_ledc_duty[LEDC_CHANNEL_1] == 3000);
    printf("  ok  set_effort applies raw duty\n");

    motor.set_effort(-3000);
    CHECK(g_mock_gpio[kMotorDirGpio] == 0);
    CHECK(g_mock_ledc_duty[LEDC_CHANNEL_1] == 3000);
    printf("  ok  set_effort preserves sign as direction\n");

    motor.set_effort(inter_mcu::kMotorEffortMax + 1000);
    CHECK(g_mock_ledc_duty[LEDC_CHANNEL_1] == inter_mcu::kMotorEffortMax);
    printf("  ok  set_effort clamps high effort\n");

    motor.stop();
    CHECK(g_mock_ledc_duty[LEDC_CHANNEL_1] == 0);
    CHECK(g_mock_gpio[kMotorDirGpio] == 0);
    printf("  ok  stop clears duty and direction\n");

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
