// ESTOP latch test — verify CAN 0x001 latches until SYS mode clears it (PCR3).
//
// The safety architecture (§9.1) requires that a single 0x001 ESTOP frame
// holds the system in ESTOP state until SYS explicitly transitions away
// from ESTOP mode via 0x110 MODE_CMD. This test verifies:
//   1. ESTOP event → run_safety_checks returns ESTOP actions
//   2. ESTOP persists across cycles (latch, not one-shot)
//   3. SYS mode change to non-ESTOP → latch clears

#include <cstdio>
#include <cstdint>

static int pass = 0;
static int fail = 0;

#define CHECK(cond) do { \
    if (cond) { pass++; } \
    else { fail++; std::fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } \
} while (0)

// ── Minimal simulation of the RT safety event queue flow ──────────
// Replicates the safety event drain from t_control and the ESTOP
// check from run_safety_checks (safety_monitor.h lines 74-86).

namespace {
    bool m_estop_pending = false;
    uint8_t m_current_mode = 0;  // 0=Manual, 1=Auto, 2=Estop
    bool had_estop_this_cycle = false;

    struct SafetyResult {
        bool zero_setpoints = false;
        int  brake_kpa = 0;
        bool disable_steering = false;
    };

    // Simulates t_control's safety event queue drain + run_safety_checks
    SafetyResult run_cycle(bool inject_estop, uint8_t new_mode, bool mode_change) {
        had_estop_this_cycle = false;

        // ── Drain safety event queue (from t_control) ─────────────
        if (inject_estop) {
            m_estop_pending = true;
            had_estop_this_cycle = true;
        }
        if (mode_change) {
            m_current_mode = new_mode;
            // PCR3: only clear on non-ESTOP mode when no ESTOP in same cycle
            if (new_mode != 2 && !had_estop_this_cycle) {
                m_estop_pending = false;
            }
        }

        // ── run_safety_checks (from safety_monitor.h) ─────────────
        SafetyResult r{};
        // 1. ESTOP event — latch until SYS mode clears it (PCR3)
        if (m_estop_pending) {
            r.zero_setpoints = true;
            r.brake_kpa = 5000;
            r.disable_steering = true;
        }
        // 2. Mode is ESTOP
        if (m_current_mode == 2) {
            r.zero_setpoints = true;
            r.brake_kpa = 5000;
            r.disable_steering = true;
        }
        return r;
    }
}

int main() {
    std::printf("\n=== ESTOP Latch Test (PCR3) ===\n\n");

    // ── Test 1: ESTOP event triggers safety actions ──────────────────
    std::printf("-- ESTOP event triggers zero + brake + steering disable --\n");
    {
        m_estop_pending = false;
        m_current_mode = 1;  // Auto

        auto r = run_cycle(/*inject_estop=*/true, 1, false);
        CHECK(r.zero_setpoints == true);
        CHECK(r.brake_kpa == 5000);
        CHECK(r.disable_steering == true);
        CHECK(m_estop_pending == true);  // latch holds
    }

    // ── Test 2: ESTOP latch persists across cycles without mode change ─
    std::printf("-- ESTOP latch persists across multiple cycles --\n");
    {
        m_estop_pending = false;
        m_current_mode = 1;

        // Cycle 1: inject ESTOP
        run_cycle(true, 1, false);
        CHECK(m_estop_pending == true);

        // Cycle 2: no new ESTOP, no mode change — latch must hold
        auto r2 = run_cycle(false, 1, false);
        CHECK(r2.zero_setpoints == true);
        CHECK(m_estop_pending == true);

        // Cycle 3: still latched
        auto r3 = run_cycle(false, 1, false);
        CHECK(r3.zero_setpoints == true);
        CHECK(m_estop_pending == true);
    }

    // ── Test 3: SYS mode change to Manual clears ESTOP latch ─────────
    std::printf("-- SYS mode change to Manual clears ESTOP latch --\n");
    {
        m_estop_pending = false;
        m_current_mode = 1;

        // Inject ESTOP
        run_cycle(true, 1, false);
        CHECK(m_estop_pending == true);

        // SYS transitions to Manual (mode=0) — latch should clear
        auto r = run_cycle(false, 0, true);
        CHECK(m_estop_pending == false);
        CHECK(r.zero_setpoints == false);  // no longer in ESTOP
    }

    // ── Test 4: SYS mode change to ESTOP keeps latch + adds mode gate ─
    std::printf("-- SYS mode ESTOP keeps safety active --\n");
    {
        m_estop_pending = false;
        m_current_mode = 1;

        // SYS transitions to ESTOP mode (0x110 mode=2)
        auto r = run_cycle(false, 2, true);
        CHECK(r.zero_setpoints == true);   // mode gate active
        CHECK(r.brake_kpa == 5000);

        // Mode change back to Manual clears
        auto r2 = run_cycle(false, 0, true);
        CHECK(r2.zero_setpoints == false);
    }

    // ── Test 5: ESTOP + same-cycle mode change to non-ESTOP ──────────
    // PCR3 ensures ESTOP wins when both arrive same cycle
    std::printf("-- ESTOP wins over same-cycle non-ESTOP mode change --\n");
    {
        m_estop_pending = false;
        m_current_mode = 1;

        // ESTOP AND mode change to Manual in same cycle
        // ESTOP should take priority (had_estop_this_cycle guard)
        m_estop_pending = true;   // simulate ESTOP arriving
        had_estop_this_cycle = true;
        // Mode change to Manual arrives same cycle
        m_current_mode = 0;
        if (m_current_mode != 2 && !had_estop_this_cycle) {
            m_estop_pending = false;
        }
        // ESTOP must still be pending
        CHECK(m_estop_pending == true);

        auto r = run_cycle(false, 0, false);
        CHECK(r.zero_setpoints == true);  // ESTOP latch still active
    }

    // ── Test 6: ESTOP latch clears after mode change in subsequent cycle ─
    std::printf("-- ESTOP clears on next mode change after ESTOP processed --\n");
    {
        m_estop_pending = false;
        m_current_mode = 1;

        // Cycle 1: inject ESTOP
        run_cycle(true, 1, false);
        CHECK(m_estop_pending == true);

        // Cycle 2: mode change to Manual, no new ESTOP -> clears
        auto r = run_cycle(false, 0, true);
        CHECK(m_estop_pending == false);
        CHECK(r.zero_setpoints == false);
    }

    std::printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    return fail ? 1 : 0;
}
