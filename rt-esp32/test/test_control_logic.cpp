// g++ -std=c++17 -I. -I../src -I../../shared test_control_logic.cpp ../src/control_logic.cpp ../src/physics_model.cpp ../src/speed_pid.cpp -o test_control_logic && ./test_control_logic

#include <cstdio>
#include <cmath>

#include "config.h"
#include "control_logic.h"
#include "intermcu/intermcu_protocol.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== RT Control Logic Tests ===\n\n");
    using namespace rt;

    // RT sends raw PWM effort over the direct inter-MCU link.
    {
        PhysicsModel physics;
        SpeedPid::Gains gains{2.0f, 0.0f, 0.0f, 100.0f};
        SpeedPid pid(gains);
        auto sp = resolve_drive_setpoint(
            physics, pid, DriveCmd{1000, 0}, 0, kObstacleClearDistMM + 1, 0.01f);

        CHECK(sp.motor_effort_pwm == 2000);
        CHECK(sp.steer_angle_mdeg == 0);
        CHECK((sp.flags & inter_mcu::kFlagAutoEnable) != 0);
        CHECK((sp.flags & inter_mcu::kFlagEpsEnable) != 0);
        printf("  ok  PID output serialized as inter-MCU effort\n");
    }

    // Effort is clamped to the signed 13-bit PWM range.
    {
        PhysicsModel physics;
        SpeedPid::Gains gains{10.0f, 0.0f, 0.0f, 100.0f};
        SpeedPid pid(gains);
        auto sp = resolve_drive_setpoint(
            physics, pid, DriveCmd{3000, 0}, 0, kObstacleClearDistMM + 1, 0.01f);

        CHECK(sp.motor_effort_pwm == inter_mcu::kMotorEffortMax);
        printf("  ok  effort clamp\n");
    }

    // Obstacle limiting reduces the PID speed target before effort is computed.
    {
        PhysicsModel physics;
        SpeedPid::Gains gains{1.0f, 0.0f, 0.0f, 100.0f};
        SpeedPid pid(gains);
        auto sp = resolve_drive_setpoint(
            physics, pid, DriveCmd{2000, 0}, 0, kObstacleStopDistMM, 0.01f);

        CHECK(sp.motor_effort_pwm == 0);
        printf("  ok  obstacle stop feeds PID target\n");
    }

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
