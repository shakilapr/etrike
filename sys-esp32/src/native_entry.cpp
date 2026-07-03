// Native test entry point for SYS safety & mode manager.
// Build: pio run -e native
// Run:   .pio/build/native/program.exe

#include <cstdio>
#include "safety_monitor.h"
#include "mode_manager.h"
#include "config.h"              // kHeartbeatTimeoutMsRt
#include "shared_config.h"       // kStartupGracePeriodMs

// Access test-time variable (defined in safety_monitor.cpp when TESTING is set).
// The native env passes -D TESTING so get_time_us() returns this value.
namespace sys { extern int64_t g_sys_test_time_us; }

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { pass++; } else { fprintf(stderr, "FAIL %s\n", msg); fail++; } \
} while(0)
#define CHECK(cond, msg) do { \
    if (cond) { pass++; } else { fprintf(stderr, "FAIL %s\n", msg); fail++; } \
} while(0)

int main() {
    printf("\n=== SYS — Safety & Mode Manager Native Tests ===\n\n");

    // ═══ SafetyMonitor ═══
    printf("-- SafetyMonitor: startup grace --\n");
    {
        sys::SafetyMonitor sm; sm.init();
        sys::g_sys_test_time_us = 0;
        CHECK(sm.heartbeat_ok(), "HB OK at t=0 (startup grace)");

        sys::g_sys_test_time_us = int64_t(shared::kStartupGracePeriodMs - 100) * 1000;
        CHECK(sm.heartbeat_ok(), "HB OK before grace expires");

        sys::g_sys_test_time_us = int64_t(shared::kStartupGracePeriodMs + 100) * 1000;
        CHECK(!sm.heartbeat_ok(), "HB lost after grace without data");
    }

    printf("-- SafetyMonitor: heartbeat feed --\n");
    {
        sys::SafetyMonitor sm; sm.init();
        sys::g_sys_test_time_us = int64_t(shared::kStartupGracePeriodMs + 100) * 1000;
        sm.feed_heartbeat_rt(5);
        CHECK(sm.heartbeat_ok(), "HB OK after feed");
        sm.feed_heartbeat_rt(6);
        CHECK(sm.heartbeat_ok(), "HB OK with incrementing counter");
        // Frozen counter: timestamp not updated → HB times out after timeout window
        sm.feed_heartbeat_rt(6);  // frozen, ignored
        sys::g_sys_test_time_us += int64_t(sys::kHeartbeatTimeoutMsRt + 1) * 1000;
        CHECK(!sm.heartbeat_ok(), "HB lost after timeout with frozen counter");
    }

    printf("-- SafetyMonitor: ESTOP & brake lever --\n");
    {
        sys::SafetyMonitor sm; sm.init();
        CHECK(!sm.estop_active(), "ESTOP inactive initially");
        CHECK(!sm.brake_lever_pressed(), "brake lever not pressed");
        sm.set_estop(true);
        CHECK(sm.estop_active(), "ESTOP active after set");
        sm.set_brake_lever(true);
        CHECK(sm.brake_lever_pressed(), "brake lever pressed");
    }

    // ═══ ModeManager ═══
    // tick(mode_btn, start_btn): true=pressed, false=not pressed.
    // Toggle on falling edge (press→release). Debounce=5 ticks after change.
    printf("-- ModeManager: toggle --\n");
    {
        sys::ModeManager mm; mm.init();
        CHECK(mm.mode() == can::Mode::Manual, "starts in Manual");
        mm.tick(true, false);   // press MODE
        mm.tick(false, false);  // release → toggle to Auto
        CHECK(mm.mode() == can::Mode::Auto, "Manual → Auto");

        // Clear debounce before next toggle
        for (int i = 0; i < 6; i++) mm.tick(false, false);
        mm.tick(true, false);
        mm.tick(false, false);
        CHECK(mm.mode() == can::Mode::Manual, "Auto → Manual");
    }

    printf("-- ModeManager: ESTOP force --\n");
    {
        sys::ModeManager mm; mm.init();
        mm.force_estop();
        CHECK(mm.mode() == can::Mode::Estop, "force_estop sets ESTOP");
        // MODE button ignored in ESTOP (short press)
        mm.tick(true, false); mm.tick(false, false);
        CHECK(mm.mode() == can::Mode::Estop, "MODE ignored in ESTOP");
        // START button (falling edge: press→release)
        mm.tick(false, true);   // press START
        mm.tick(false, false);  // release → ESTOP→Manual
        CHECK(mm.mode() == can::Mode::Manual, "START: ESTOP→Manual");
    }

    printf("-- ModeManager: set from CAN --\n");
    {
        sys::ModeManager mm; mm.init();
        mm.set_from_can(1);
        CHECK(mm.mode() == can::Mode::Auto, "CAN sets Auto");
        // ESTOP (2) rejected — safety state, not a mode command
        mm.set_from_can(2);
        CHECK(mm.mode() == can::Mode::Auto, "CAN rejects ESTOP");
        mm.set_from_can(0);
        CHECK(mm.mode() == can::Mode::Manual, "CAN sets Manual");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
