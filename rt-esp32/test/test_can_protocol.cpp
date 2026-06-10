// g++ -std=c++17 -I. -I../../shared test_can_protocol.cpp -o test_can && ./test_can

#include <cstdio>
#include <cstdint>

#include "can/can_protocol.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== CAN Protocol Tests ===\n\n");

    // HostDriveCmd: public Jetson -> RT command.
    {
        can::Frame f;
        can::HostDriveCmd{1500, -200}.to_frame(f);
        CHECK(f.id == can::kIdHostDriveCmd);
        CHECK(f.dlc == 8);
        auto cmd = can::HostDriveCmd::from_frame(f);
        CHECK(cmd.speed_mmps == 1500);
        CHECK(cmd.yaw_rate_mrad_s == -200);
        printf("  ok  HostDriveCmd round-trip\n");
    }

    // RtObstacleDist: RT -> Jetson public telemetry.
    {
        can::Frame f;
        can::RtObstacleDist{1234}.to_frame(f);
        CHECK(f.id == can::kIdRtObstacleDist);
        CHECK(f.dlc == 4);
        auto d = can::RtObstacleDist::from_frame(f);
        CHECK(d.distance_mm == 1234);
        printf("  ok  RtObstacleDist round-trip\n");
    }

    // Mirrored SYS safety status: RT publishes SYS state on the public bus.
    {
        can::Frame f;
        can::SysSafetyStatus{true, false}.to_frame(f);
        CHECK(f.id == can::kIdSysSafetyStatus);
        CHECK(f.dlc == 2);
        auto s = can::SysSafetyStatus::from_frame(f);
        CHECK(s.estop_active);
        CHECK(!s.heartbeat_ok);
        printf("  ok  mirrored SysSafetyStatus\n");
    }

    // Syntree command IDs live only on the SYS private CAN bus.
    {
        can::Frame f;
        can::SyntreeEpsCommand{}.to_frame(f);
        CHECK(f.id == can::kIdSyntreeEpsCommand);
        CHECK(f.dlc == 8);

        can::SyntreeSebCommand{}.to_frame(f);
        CHECK(f.id == can::kIdSyntreeSebCommand);
        CHECK(f.dlc == 8);
        printf("  ok  Syntree private CAN command IDs\n");
    }

    // E-stop helper covers the public safety IDs.
    {
        CHECK(can::is_estop_id(can::kIdSysEstop));
        CHECK(can::is_estop_id(can::kIdRtEstop));
        CHECK(can::is_estop_id(can::kIdHostEstop));
        CHECK(!can::is_estop_id(can::kIdHostDriveCmd));
        printf("  ok  is_estop_id helper\n");
    }

    // All CAN IDs in the catalog are unique.
    {
        uint32_t ids[] = {
            can::kIdSysEstop, can::kIdRtEstop, can::kIdHostEstop,
            can::kIdSysSafetyStatus, can::kIdSysThrottlePos,
            can::kIdRtStateReport, can::kIdRtPidFeedback,
            can::kIdHostDriveCmd, can::kIdRtObstacleDist, can::kIdSysDiag,
            can::kIdHeartbeat,
            can::kIdSyntreeEpsCommand, can::kIdSyntreeEpsStatus,
            can::kIdSyntreeSebCommand, can::kIdSyntreeSebStatus,
        };
        for (size_t i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
            for (size_t j = i + 1; j < sizeof(ids)/sizeof(ids[0]); ++j) {
                CHECK(ids[i] != ids[j]);
            }
        }
        printf("  ok  unique CAN IDs\n");
    }

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
