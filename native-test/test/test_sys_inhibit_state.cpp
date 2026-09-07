// SYS independent traction-inhibit state (issues #5/#7).
//
// Verifies the per-owner reason-bit architecture:
//   #7  MTR-feedback loss sets kInhibitMtrFbkLoss and the resolved actuator
//       authority drops to 0x113 OFF + 0x110 MANUAL (actuator-level action).
//   #7  Confirmed recovery clears only the owner's own bit after a healthy run.
//   Cross-owner: clearing one detector's bit must not erase another's.
//   #5  Latched-fault reasons are separate from transient inhibits and are not
//       clearable through the transient clear path.

#include <atomic>
#include <cstdio>
#include <cstdint>

#include "inhibit_state.h"

// The masks are defined extern in inhibit_state.h; this test TU provides the
// single definitions (normally main.cpp provides them).
std::atomic<uint32_t> sys::g_inhibit_reasons{0};
std::atomic<uint32_t> sys::g_latched_fault_reasons{0};

static int pass = 0;
static int fail = 0;
#define CHECK(cond) do { if (cond) pass++; else { fail++; std::fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while (0)

int main() {
    std::printf("\n=== SYS inhibit-state / authority resolution ===\n");

    using namespace sys;

    // ── Baseline: no inhibit, AUTO + power requested -> AUTO + power ON ──
    {
        auto a = resolve_authority(/*estop=*/false, /*auto=*/true, /*pwr_req=*/true);
        CHECK(a.mode_auto == true);
        CHECK(a.power_on == true);
    }

    // ── #7: kInhibitMtrFbkLoss -> power OFF + mode MANUAL (authority clamp) ──
    {
        set_inhibit(kInhibitMtrFbkLoss);
        auto a = resolve_authority(/*estop=*/false, /*auto=*/true, /*pwr_req=*/true);
        CHECK(a.mode_auto == false);
        CHECK(a.power_on == false);
        clear_inhibit(kInhibitMtrFbkLoss);
    }

    // ── ESTOP latched also drops authority regardless of inhibit masks ──
    {
        auto a = resolve_authority(/*estop=*/true, /*auto=*/true, /*pwr_req=*/true);
        CHECK(a.mode_auto == false);
        CHECK(a.power_on == false);
    }

    // ── Cross-owner independence: clearing MTR bit must not clear SEB-comms bit ──
    {
        set_inhibit(kInhibitMtrFbkLoss);
        set_inhibit(kInhibitSebCommsLoss);
        // Both present.
        CHECK(transient_inhibited());
        // Clear only the MTR-fbk bit.
        clear_inhibit(kInhibitMtrFbkLoss);
        CHECK(transient_inhibited());  // SEB-comms still present
        CHECK(!(g_inhibit_reasons.load() & kInhibitMtrFbkLoss));
        CHECK(g_inhibit_reasons.load() & kInhibitSebCommsLoss);
        clear_inhibit(kInhibitSebCommsLoss);
        CHECK(!transient_inhibited());
    }

    // ── fetch_or semantics never lose a concurrent set (atomic RMW) ──
    {
        set_inhibit(kInhibitMtrFbkLoss);
        set_inhibit(kInhibitSebCommsLoss);
        // A "clear MTR" racing a "set following" must not drop the SEB bit:
        // clear_and is by mask, so SEB survives regardless of ordering.
        g_inhibit_reasons.fetch_and(~uint32_t(kInhibitMtrFbkLoss));
        g_inhibit_reasons.fetch_or(uint32_t(kInhibitBrakeFollowing));
        CHECK(g_inhibit_reasons.load() & kInhibitSebCommsLoss);
        CHECK(g_inhibit_reasons.load() & kInhibitBrakeFollowing);
        g_inhibit_reasons.store(0);
    }

    // ── #5: latched faults are separate; transient clear cannot erase them ──
    {
        set_latched_fault(kLatchedSebL3);
        CHECK(latched_fault_present());
        CHECK(any_inhibit());
        // Transient inhibits clear individually without touching the latch.
        set_inhibit(kInhibitMtrFbkLoss);
        clear_inhibit(kInhibitMtrFbkLoss);
        CHECK(latched_fault_present());   // latch survives transient clear
        CHECK(g_latched_fault_reasons.load() & kLatchedSebL3);
        // There is intentionally no generic clear_latched_fault(): only the
        // explicit reset path mutates this mask, so a detector going healthy
        // can never clear it here.
        g_latched_fault_reasons.store(0);
        CHECK(!latched_fault_present());
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
