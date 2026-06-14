// Phase R4: control_logic test — CAN-based output (no intermcu).
// g++ -std=c++17 -I. -I../src -I../../shared test_control_logic.cpp ../src/control_logic.cpp ../src/physics_model.cpp ../src/speed_pid.cpp -o test_control_logic && ./test_control_logic

#include <cstdio>
#include <cmath>

#include "config.h"
#include "control_logic.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== R4: RT Control Logic Tests (CAN output) ===\n\n");
    using namespace rt;

    // 1. Drive command produces correct RtDriveCmd (0x202) fields
    {
        PhysicsModel physics;
        SpeedPid::Gains gains{1.0f, 0.0f, 0.0f, 100.0f};
        SpeedPid pid(gains);
        auto out = resolve_drive_setpoint(
            physics, pid, DriveCmd{1000, 0}, 0, kObstacleClearDistMM + 1, 0, 0.01f);

        CHECK(out.drive.motor_speed_mmps == 1000);
        CHECK(out.drive.gear == uint8_t(can::Gear::D));  // positive speed → D
        CHECK(out.brake.brake_pressure_kpa == 0);
        printf("  ok  forward drive → D gear, no brake\n");
    }

    // 2. Reverse speed → R gear
    {
        PhysicsModel physics;
        SpeedPid pid;
        auto out = resolve_drive_setpoint(
            physics, pid, DriveCmd{-300, 0}, 0, kObstacleClearDistMM + 1, 0, 0.01f);

        CHECK(out.drive.gear == uint8_t(can::Gear::R));
        CHECK(out.drive.motor_speed_mmps == -300);
        printf("  ok  reverse drive → R gear\n");
    }

    // 3. Zero speed → N gear
    {
        PhysicsModel physics;
        SpeedPid pid;
        auto out = resolve_drive_setpoint(
            physics, pid, DriveCmd{0, 0}, 0, kObstacleClearDistMM + 1, 0, 0.01f);

        CHECK(out.drive.motor_speed_mmps == 0);
        CHECK(out.drive.gear == uint8_t(can::Gear::N));
        printf("  ok  zero speed → N gear\n");
    }

    // 4. Obstacle at stop distance → speed zero
    {
        PhysicsModel physics;
        SpeedPid pid;
        auto out = resolve_drive_setpoint(
            physics, pid, DriveCmd{2000, 0}, 0, kObstacleStopDistMM, 0, 0.01f);

        CHECK(out.drive.motor_speed_mmps == 0);
        printf("  ok  obstacle stop → zero speed\n");
    }

    // 5. Brake arbitration: Jetson request passes through (RT floor = 0)
    {
        PhysicsModel physics;
        SpeedPid pid;
        auto out = resolve_drive_setpoint(
            physics, pid, DriveCmd{500, 0}, 0, kObstacleClearDistMM + 1, 5000, 0.01f);

        CHECK(out.brake.brake_pressure_kpa == 5000);
        printf("  ok  brake request passes through\n");
    }

    // 6. CAN frame round-trip: RtDriveCmd → can::Frame → RtDriveCmd
    {
        can::RtDriveCmd orig{2000, uint8_t(can::Gear::D)};
        can::Frame f;
        orig.to_frame(f);
        auto back = can::RtDriveCmd::from_frame(f);
        CHECK(back.motor_speed_mmps == 2000);
        CHECK(back.gear == uint8_t(can::Gear::D));
        CHECK(f.id == 0x202);
        CHECK(f.dlc == 5);
        printf("  ok  RtDriveCmd CAN round-trip\n");
    }

    // 7. CAN frame round-trip: RtBrakeCmd → can::Frame → RtBrakeCmd
    {
        can::RtBrakeCmd orig{5000};
        can::Frame f;
        orig.to_frame(f);
        auto back = can::RtBrakeCmd::from_frame(f);
        CHECK(back.brake_pressure_kpa == 5000);
        CHECK(f.id == 0x203);
        CHECK(f.dlc == 4);
        printf("  ok  RtBrakeCmd CAN round-trip\n");
    }

    // 8. No intermcu headers needed
    {
        ControlOutput out;
        out.drive.motor_speed_mmps = 0;
        out.drive.gear = 0;
        out.brake.brake_pressure_kpa = 0;
        // Compiles without intermcu — the test itself verifies this
        printf("  ok  no inter_mcu dependency\n");
    }

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
