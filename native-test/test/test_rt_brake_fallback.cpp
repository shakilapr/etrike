// RT SEB brake-ownership emergency fallback state machine (issue #3).
//
// Verifies:
//   1. Startup acquisition: before a SYS 0x7B9 is observed (or the arm grace
//      elapses), a missing SYS heartbeat must NOT trigger emergency fallback.
//   2. SYS-DEGRADED: SYS heartbeat lost but SYS 0x7B9 still present -> motion
//      would be prohibited by the caller, but RT does NOT become the 0x7B9 writer.
//   3. EMERGENCY_FALLBACK: SYS heartbeat lost AND 0x7B9 absent past the guard
//      -> RT becomes the emergency writer (emergency_tx_0x7B9).
//   4. Recovery does NOT clear until SYS 0x7B9 is re-observed for N fresh frames
//      AFTER the handback epoch (own-loopback frames cannot satisfy it).
//   5. A SYS heartbeat that returns while 0x7B9 never stopped returns to NORMAL
//      directly (no false emergency).

#include <cstdio>
#include <cstdint>

#include "config.h"
#include "brake_fallback.h"

static int pass = 0;
static int fail = 0;
#define CHECK(cond) do { if (cond) pass++; else { fail++; std::fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while (0)
#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) pass++; else { fail++; std::fprintf(stderr, "  FAIL %s:%d (%d != %d)\n", __FILE__, __LINE__, (int)_a, (int)_b); } \
} while (0)

static rt::SebFallbackInput mk(int64_t now, bool hb, bool seen_7b9, bool grace) {
    rt::SebFallbackInput in;
    in.now_us = now;
    in.sys_hb_fresh = hb;
    in.sys_0x7B9_observed = seen_7b9;
    in.startup_grace_active = grace;
    return in;
}

int main() {
    std::printf("\n=== RT SEB brake-ownership fallback (issue #3) ===\n");
    const int64_t boot = 1'000'000;      // us
    const int64_t g = int64_t(rt::kSebFallbackGuardMs) * 1000;

    // ── 1. Startup acquisition: missing HB before arming must NOT fall back ──
    {
        rt::SebBrakeFallback fb;
        fb.init(boot);
        // Global boot grace active: no SYS at all.
        auto out = fb.update(mk(boot + 1'000'000, /*hb=*/false, /*seen=*/false, /*grace=*/true));
        CHECK(out.state == rt::SebBrakeState::NORMAL);
        CHECK(!out.emergency_tx_0x7B9);

        // Grace over, still no SYS 0x7B9 seen and none observed; SYS HB missing.
        // Arm grace (kSebFallbackArmGraceMs) keeps us disarmed until it elapses
        // (boot at 1.0 s; arm grace 2 s => armed at 3.0 s absolute).
        out = fb.update(mk(boot + 500'000, /*hb=*/false, /*seen=*/false, /*grace=*/false));
        CHECK(out.state == rt::SebBrakeState::NORMAL);   // not yet armed (1.5 s < 2 s)
        CHECK(!out.emergency_tx_0x7B9);

        // Past the arm grace with SYS HB still missing -> armed; enter SYS_DEGRADED.
        out = fb.update(mk(boot + 3'500'000, /*hb=*/false, /*seen=*/false, /*grace=*/false));
        CHECK(out.state == rt::SebBrakeState::SYS_DEGRADED);

        // 0x7B9 has never been seen; past the guard -> EMERGENCY_FALLBACK.
        out = fb.update(mk(boot + 3'500'000 + g + 100'000, /*hb=*/false, /*seen=*/false, /*grace=*/false));
        CHECK(out.state == rt::SebBrakeState::EMERGENCY_FALLBACK);
        CHECK(out.emergency_tx_0x7B9);
    }

    // ── 2. SYS heartbeat lost but SYS 0x7B9 STILL present -> SYS_DEGRADED only ──
    {
        rt::SebBrakeFallback fb;
        fb.init(boot);
        // Healthy SYS for a while (HB + 0x7B9 observed).
        int64_t now = boot;
        for (int i = 0; i < 40; ++i) {
            now += 100'000;
            auto out = fb.update(mk(now, /*hb=*/true, /*seen=*/true, /*grace=*/false));
            CHECK(out.state == rt::SebBrakeState::NORMAL);
            CHECK(!out.emergency_tx_0x7B9);
        }
        // SYS HB lost but 0x7B9 continues (brake task alive) -> SYS_DEGRADED.
        for (int i = 0; i < 40; ++i) {   // 4 s of degraded with 0x7B9 present
            now += 100'000;
            auto out = fb.update(mk(now, /*hb=*/false, /*seen=*/true, /*grace=*/false));
            CHECK(out.state == rt::SebBrakeState::SYS_DEGRADED);
            CHECK(!out.emergency_tx_0x7B9);   // RT must NOT write 0x7B9
            CHECK(!out.emergency_0x001);
        }
        // SYS heartbeat returns while its 0x7B9 never stopped -> back to NORMAL.
        auto out = fb.update(mk(now + 100'000, /*hb=*/true, /*seen=*/true, /*grace=*/false));
        CHECK(out.state == rt::SebBrakeState::NORMAL);
    }

    // ── 3. SYS-DEGRADED -> EMERGENCY_FALLBACK once 0x7B9 also disappears ──
    {
        rt::SebBrakeFallback fb;
        fb.init(boot);
        int64_t now = boot + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 200'000;
        // SYS was healthy and observed; now HB lost.
        auto out = fb.update(mk(now, /*hb=*/false, /*seen=*/true, /*grace=*/false));
        CHECK(out.state == rt::SebBrakeState::SYS_DEGRADED);
        // 0x7B9 continues for a while, then stops.
        for (int i = 0; i < 10; ++i) { now += 100'000;
            out = fb.update(mk(now, false, true, false));
            CHECK(out.state == rt::SebBrakeState::SYS_DEGRADED); }
        // 0x7B9 stops; past the guard -> EMERGENCY_FALLBACK.
        now += g + 100'000;
        out = fb.update(mk(now, /*hb=*/false, /*seen=*/false, /*grace=*/false));
        CHECK(out.state == rt::SebBrakeState::EMERGENCY_FALLBACK);
        CHECK(out.emergency_tx_0x7B9);
        CHECK(out.emergency_0x001);
    }

    // ── 4. Handback: HB returns; RT stops TX; needs N fresh POST-epoch 0x7B9 ──
    {
        rt::SebBrakeFallback fb;
        fb.init(boot);
        int64_t now = boot + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 200'000;
        // Drive into EMERGENCY_FALLBACK.
        auto out = fb.update(mk(now, false, true, false));      // SYS_DEGRADED
        now += g + 100'000;
        out = fb.update(mk(now, false, false, false));           // EMERGENCY_FALLBACK
        CHECK(out.state == rt::SebBrakeState::EMERGENCY_FALLBACK);

        // SYS HB returns; RT's own emergency TX may be echoed (seen=true from RT's
        // own loopback). The machine must require frames AFTER the handback epoch.
        now += 100'000;
        out = fb.update(mk(now, /*hb=*/true, /*seen=*/true, /*grace=*/false));
        // One or two frames is not enough (needs kSebHandbackVerifyFrames).
        for (int i = 0; i < rt::kSebHandbackVerifyFrames - 1; ++i) {
            now += 100'000;
            out = fb.update(mk(now, true, true, false));
            CHECK(out.state == rt::SebBrakeState::EMERGENCY_FALLBACK);
        }
        now += 100'000;
        out = fb.update(mk(now, true, true, false));
        CHECK(out.state == rt::SebBrakeState::NORMAL);
        CHECK(!out.emergency_tx_0x7B9);
    }

    // ── 5. Emergency persists while SYS never returns ──
    {
        rt::SebBrakeFallback fb;
        fb.init(boot);
        int64_t now = boot + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 200'000;
        auto out = fb.update(mk(now, false, true, false));
        now += g + 100'000;
        out = fb.update(mk(now, false, false, false));
        CHECK(out.state == rt::SebBrakeState::EMERGENCY_FALLBACK);
        for (int i = 0; i < 30; ++i) {
            now += 100'000;
            out = fb.update(mk(now, false, false, false));
            CHECK(out.state == rt::SebBrakeState::EMERGENCY_FALLBACK);
            CHECK(out.emergency_tx_0x7B9);
        }
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
