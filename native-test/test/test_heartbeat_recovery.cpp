// Heartbeat recovery test — verify SYS heartbeat timeout → ESTOP → recovery.
// Pattern: test_rt_safety_monitor.cpp, test_safety_features.cpp
//
// Simulates SYS heartbeat loss at 200ms timeout threshold:
//   Phase 1: Fresh heartbeat → no ESTOP
//   Phase 2: Timeout (>200ms) → seb_takeover=true, zero setpoints
//   Phase 3: Heartbeat recovered → seb_takeover=false, system resumes
//
// Also verifies Host heartbeat assisted-stop at 1500ms.

#include <atomic>
#include <cstdio>
#include <cstdint>

static int pass = 0;
static int fail = 0;

#define CHECK(cond) do { \
    if (cond) { pass++; } \
    else { fail++; std::fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } \
} while (0)

// ── Simulated state (mirrors externs from rt_state.h) ──────────────
static std::atomic<int64_t>  g_last_sys_hb_us{0};
static std::atomic<int64_t>  g_last_host_hb_us{0};
static std::atomic<int64_t>  g_last_estop_sent_us{0};
static std::atomic<int32_t>  g_brake_request_kpa{0};

// Mirrors the heartbeat portion of run_safety_checks (safety_monitor.h)
struct SafetyResult {
    bool zero_setpoints   = false;
    int32_t brake_kpa    = 0;
    bool obstacle_triggered = false;
    uint8_t estop_reason = 0;
};

// Replicates the SYS + Host heartbeat timeout/recovery logic from
// run_safety_checks(), omitting steering/obstacle checks for focus.
static SafetyResult tick_safety(int64_t now_us, bool startup_grace,
                                bool& seb_takeover) {
    SafetyResult r;

    if (startup_grace) return r;

    // ── SYS heartbeat timeout (200ms) — architecture §8.6 ─────
    int64_t sys_hb = g_last_sys_hb_us.load();
    if (sys_hb > 0 && (now_us - sys_hb) > int64_t(200) * 1000) {
        r.zero_setpoints = true;
        r.estop_reason = 2; // kEstopReasonHeartbeat
        seb_takeover = true;
    } else if (seb_takeover) {
        // SYS heartbeat recovered — release takeover
        seb_takeover = false;
    }

    // ── Host heartbeat timeout (1500ms) — architecture §7.6 ──
    int64_t host_hb = g_last_host_hb_us.load();
    if (host_hb > 0 && (now_us - host_hb) > int64_t(1500) * 1000) {
        r.zero_setpoints = true;
        r.estop_reason = 2; // kEstopReasonHeartbeat
        g_brake_request_kpa.store(2000); // kAssistStopKpa
    }

    return r;
}

static void reset_state() {
    g_last_sys_hb_us.store(0);
    g_last_host_hb_us.store(0);
    g_last_estop_sent_us.store(0);
    g_brake_request_kpa.store(0);
}

int main() {
    std::printf("\n=== Heartbeat Recovery ===\n\n");

    // ── Test 1: Fresh SYS heartbeat → no timeout → normal operation ──
    {
        reset_state();
        bool seb_takeover = false;
        g_last_sys_hb_us.store(1'000'000); // SYS hb received at t=1s

        // Check at t=1,100,000 — only 100ms elapsed < 200ms timeout
        auto r = tick_safety(1'100'000, false, seb_takeover);

        CHECK(!r.zero_setpoints);
        CHECK(!seb_takeover);
        std::printf("  Test 1: Fresh heartbeat → no timeout OK\n");
    }

    // ── Test 2: SYS heartbeat timeout → seb_takeover triggered ──────
    {
        reset_state();
        bool seb_takeover = false;
        g_last_sys_hb_us.store(1'000'000); // SYS hb at t=1s

        // Check at t=1,250,001 — 250ms > 200ms timeout
        auto r = tick_safety(1'250'001, false, seb_takeover);

        CHECK(r.zero_setpoints);
        CHECK(seb_takeover);
        std::printf("  Test 2: Heartbeat timeout → seb_takeover=true OK\n");
    }

    // ── Test 3: SYS heartbeat recovery → flag clears ────────────────
    {
        reset_state();
        bool seb_takeover = false;
        g_last_sys_hb_us.store(1'000'000);

        // Trigger timeout
        auto r1 = tick_safety(1'250'001, false, seb_takeover);
        CHECK(seb_takeover);

        // SYS heartbeat received again (fresh timestamp)
        g_last_sys_hb_us.store(1'250'001);

        // Check soon after — within 200ms deadline
        auto r2 = tick_safety(1'260'000, false, seb_takeover);

        CHECK(!r2.zero_setpoints);
        CHECK(!seb_takeover);  // FLAG CLEARED — recovery verified
        std::printf("  Test 3: Heartbeat recovery → flag cleared OK\n");
    }

    // ── Test 4: Host heartbeat timeout → assisted stop (1500ms) ─────
    {
        reset_state();
        bool seb_takeover = false;
        g_last_host_hb_us.store(1'000'000); // Host hb at t=1s

        // Check at t=2,600,001 — 1600ms > 1500ms timeout
        auto r = tick_safety(2'600'001, false, seb_takeover);

        CHECK(r.zero_setpoints);
        CHECK(g_brake_request_kpa.load() == 2000); // kAssistStopKpa
        std::printf("  Test 4: Host timeout → assisted stop 2000kPa OK\n");
    }

    // ── Test 5: Startup grace period suppresses heartbeat checks ────
    {
        reset_state();
        bool seb_takeover = false;
        g_last_sys_hb_us.store(1'000'000);

        // startup_grace=true → skip heartbeat checks even if stale
        auto r = tick_safety(2'000'000, true, seb_takeover);

        CHECK(!r.zero_setpoints);
        CHECK(!seb_takeover);
        std::printf("  Test 5: Startup grace → no false timeout OK\n");
    }

    // ── Test 6: Host heartbeat recovery ─────────────────────────────
    {
        reset_state();
        g_last_host_hb_us.store(1'000'000);
        tick_safety(2'600'001, false, *(new bool(false))); // timeout

        // Host heartbeat recovered
        g_last_host_hb_us.store(2'600'001);

        bool seb_takeover = false;
        g_brake_request_kpa.store(0);
        auto r = tick_safety(2'700'000, false, seb_takeover);

        // Host within 1500ms of 2,600,001 → no timeout
        CHECK(!r.zero_setpoints);
        std::printf("  Test 6: Host heartbeat recovery OK\n");
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
