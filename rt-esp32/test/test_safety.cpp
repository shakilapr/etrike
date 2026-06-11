// g++ -std=c++17 -I. -I../src -I../src test_safety.cpp ../src/safety_monitor.cpp -o test_safety && ./test_safety

#include <cstdio>
#include <cstdint>
#include "stubs.h"
#include "config.h"
#include "safety_monitor.h"

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; } \
    else { ++tests_fail; fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while(0)

int main() {
    printf("\n=== Safety Monitor Tests ===\n\n");
    using namespace sys;

    // Init: no faults
    g_mock_gpio[kEstopGpio] = 1;       // not pressed (pull-up)
    g_mock_gpio[kBrakeLeverGpio] = 1;  // not pressed

    SafetyMonitor sm;
    sm.init();
    CHECK(kHeartbeatIntervalMs <= 100);
    CHECK(kHeartbeatTimeoutMs <= 200);
    CHECK(kHeartbeatTimeoutMs > kHeartbeatIntervalMs);
    printf("  ok  heartbeat timing bounded for moving vehicle\n");

    CHECK(!sm.estop_active());
    CHECK(!sm.brake_lever_pressed());
    printf("  ok  init — no faults\n");

    // Estop detect
    g_mock_gpio[kEstopGpio] = 0;  // pressed
    CHECK(sm.estop_active());
    g_mock_gpio[kEstopGpio] = 1;  // released
    CHECK(!sm.estop_active());
    printf("  ok  estop detect\n");

    // Brake lever
    g_mock_gpio[kBrakeLeverGpio] = 0;
    CHECK(sm.brake_lever_pressed());
    g_mock_gpio[kBrakeLeverGpio] = 1;
    CHECK(!sm.brake_lever_pressed());
    printf("  ok  brake lever detect\n");

    // Heartbeat not ok initially after startup time has advanced.
    g_mock_time_us = (kHeartbeatTimeoutMs + 1) * 1000;
    CHECK(!sm.heartbeat_ok());
    printf("  ok  heartbeat not ok initially\n");

    // Feed RT link heartbeat → ok. Jetson command freshness is owned by RT.
    g_mock_time_us = 0;
    sm.feed_heartbeat_rt();
    CHECK(sm.heartbeat_ok());
    printf("  ok  heartbeat ok after RT link feed\n");

    // Within timeout
    g_mock_time_us = (kHeartbeatTimeoutMs - 50) * 1000;
    CHECK(sm.heartbeat_ok());
    printf("  ok  heartbeat ok before timeout\n");

    // Past timeout
    g_mock_time_us = (kHeartbeatTimeoutMs + 50) * 1000;
    CHECK(!sm.heartbeat_ok());
    printf("  ok  heartbeat timeout\n");

    // RT-only is sufficient in the split topology.
    g_mock_time_us = 500'000;
    sm.feed_heartbeat_rt();  // updates RT timestamp
    CHECK(sm.heartbeat_ok());
    printf("  ok  heartbeat rt-only is ok\n");

    // Legacy Jetson feed is a no-op and must not break the RT heartbeat state.
    sm.feed_heartbeat_jetson();
    CHECK(sm.heartbeat_ok());
    printf("  ok  legacy Jetson feed no-op\n");

    printf("\n--- %d/%d passed, %d failed ---\n\n", tests_pass, tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
