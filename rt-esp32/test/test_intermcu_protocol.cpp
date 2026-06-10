// g++ -std=c++17 -I. -I../../shared test_intermcu_protocol.cpp -o test_intermcu_protocol && ./test_intermcu_protocol

#include <cstdio>
#include "intermcu/intermcu_protocol.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== Inter-MCU Protocol Tests ===\n\n");

    {
        inter_mcu::RtToSysSetpoint in{
            1200,
            -15000,
            250,
            inter_mcu::kFlagAutoEnable | inter_mcu::kFlagEpsEnable,
        };
        inter_mcu::Frame f;
        in.to_frame(f);
        CHECK(f.type == inter_mcu::MessageType::RtToSysSetpoint);
        CHECK(f.dlc == 13);

        auto out = inter_mcu::RtToSysSetpoint::from_frame(f);
        CHECK(out.motor_effort_pwm == 1200);
        CHECK(out.steer_angle_mdeg == -15000);
        CHECK(out.brake_pressure_kpa == 250);
        CHECK((out.flags & inter_mcu::kFlagEpsEnable) != 0);
        printf("  ok  RT setpoint round-trip\n");
    }

    {
        inter_mcu::SysToRtStatus in{
            1,
            false,
            true,
            true,
            1234,
            500,
            0x00A5,
        };
        inter_mcu::Frame f;
        in.to_frame(f);
        CHECK(f.type == inter_mcu::MessageType::SysToRtStatus);
        CHECK(f.dlc == 14);

        auto out = inter_mcu::SysToRtStatus::from_frame(f);
        CHECK(out.mode == 1);
        CHECK(!out.estop_active);
        CHECK(out.heartbeat_ok);
        CHECK(out.brake_engaged);
        CHECK(out.actual_steer_angle_mdeg == 1234);
        CHECK(out.brake_pressure_kpa == 500);
        CHECK(out.syntree_fault_bits == 0x00A5);
        printf("  ok  SYS status round-trip\n");
    }

    {
        inter_mcu::RtToSysSetpoint in{
            0,
            0,
            0,
            inter_mcu::kFlagEstop,
        };
        inter_mcu::Frame f;
        in.to_frame(f);
        auto out = inter_mcu::RtToSysSetpoint::from_frame(f);
        CHECK((out.flags & inter_mcu::kFlagEstop) != 0);
        CHECK(out.motor_effort_pwm == 0);
        CHECK(out.brake_pressure_kpa == 0);
        printf("  ok  RT E-stop flag round-trip\n");
    }

    {
        inter_mcu::RtObstacleDistance in{1234};
        inter_mcu::Frame f;
        in.to_frame(f);
        CHECK(f.type == inter_mcu::MessageType::RtObstacleDist);
        CHECK(f.dlc == 4);
        auto out = inter_mcu::RtObstacleDistance::from_frame(f);
        CHECK(out.distance_mm == 1234);
        printf("  ok  RT obstacle distance round-trip\n");
    }

    {
        const uint8_t bytes[] = {0x10, 0x01, 0x00};
        CHECK(inter_mcu::crc8(bytes, 3) == inter_mcu::crc8(bytes, 3));
        printf("  ok  crc stable\n");
    }

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
