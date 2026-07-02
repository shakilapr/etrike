// g++ -std=c++17 -DTESTING -I. -I../src -I../../shared -I../../shared/can \
//     test_safety_monitor.cpp -o test_sm && ./test_sm
//
// Verifies: safety_monitor.h — heartbeat timeout, frozen counter, startup grace.

#include <cstdio>
#include <cstdint>
#include "config.h"
#include "shared_config.h"
#include "safety_monitor.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)

// Override get_time_us for deterministic testing
namespace sys {
    extern int64_t g_sys_test_time_us;
}

int main() {
    printf("\n=== Safety Monitor: Heartbeat, Timeout, Grace ===\n\n");
    using namespace sys;

    // ── Test 1: Heartbeat OK during startup grace period ────────────────
    printf("-- Startup grace: heartbeat OK with no data --\n");
    {
        SafetyMonitor sm;
        sm.init();
        g_sys_test_time_us = 0;

        // At t=0, heartbeat should be OK (startup grace)
        CHECK(sm.heartbeat_ok());

        // At t=2.9s, still OK
        g_sys_test_time_us = int64_t(shared::kStartupGracePeriodMs - 100) * 1000;
        CHECK(sm.heartbeat_ok());
    }

    // ── Test 2: Heartbeat timeout after grace without data ──────────────
    printf("-- Timeout after grace with no heartbeat --\n");
    {
        SafetyMonitor sm;
        sm.init();
        g_sys_test_time_us = 0;

        // At t=3.1s with no heartbeat → should fail
        g_sys_test_time_us = int64_t(shared::kStartupGracePeriodMs + 100) * 1000;
        CHECK(!sm.heartbeat_ok());
    }

    // ── Test 3: Heartbeat OK with received data ────────────────────────
    printf("-- Heartbeat OK with received counter --\n");
    {
        SafetyMonitor sm;
        sm.init();
        g_sys_test_time_us = 0;

        // Feed heartbeat at t=1s
        g_sys_test_time_us = 1'000'000;
        sm.feed_heartbeat_rt(42);

        // At t=1.5s — still OK (within 1000ms timeout)
        g_sys_test_time_us = 1'500'000;
        CHECK(sm.heartbeat_ok());

        // At t=2.5s — should have timed out (1500ms > 1000ms since last feed)
        g_sys_test_time_us = 2'500'000;
        CHECK(!sm.heartbeat_ok());
    }

    // ── Test 4: Frozen alive counter = missed heartbeat ─────────────────
    printf("-- Frozen counter treated as missed --\n");
    {
        SafetyMonitor sm;
        sm.init();
        g_sys_test_time_us = 0;

        // Feed heartbeat at t=1s
        g_sys_test_time_us = 1'000'000;
        sm.feed_heartbeat_rt(42);

        // Feed same counter value at t=1.2s — should be IGNORED
        g_sys_test_time_us = 1'200'000;
        sm.feed_heartbeat_rt(42);  // frozen!

        // Now jump to t=2.5s — should have timed out (last real update was 1.0s)
        g_sys_test_time_us = 2'500'000;
        CHECK(!sm.heartbeat_ok());
    }

    // ── Test 5: Normal counter update keeps heartbeat alive ─────────────
    printf("-- Incrementing counter keeps heartbeat alive --\n");
    {
        SafetyMonitor sm;
        sm.init();
        g_sys_test_time_us = 0;

        for (int i = 0; i < 10; ++i) {
            g_sys_test_time_us += 100'000;  // 100ms intervals
            sm.feed_heartbeat_rt(i);
            CHECK(sm.heartbeat_ok());
        }
    }

    // ── Test 6: Heartbeat state flags ───────────────────────────────────
    printf("-- ESTOP and brake lever flags --\n");
    {
        SafetyMonitor sm;
        sm.init();

        CHECK(!sm.estop_active());
        CHECK(!sm.brake_lever_pressed());

        sm.set_estop(true);
        CHECK(sm.estop_active());

        sm.set_estop(false);
        CHECK(!sm.estop_active());

        sm.set_brake_lever(true);
        CHECK(sm.brake_lever_pressed());

        sm.set_brake_lever(false);
        CHECK(!sm.brake_lever_pressed());
    }

    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    return fail ? 1 : 0;
}
