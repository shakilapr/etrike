// Watchdog wraparound test — ensure no false-positive on uint32_t overflow.
// Pattern: test_task_watchdog.cpp
//
// uint32_t tick counter wraps at ~49 days (100 Hz).  The watchdog check
//   (g_tick - alive > deadline) relies on unsigned modular arithmetic.
// This test verifies correct behavior at and past the UINT32_MAX boundary,
// and compares with a uint64_t implementation that handles larger ranges.

#include <atomic>
#include <cstdio>
#include <cstdint>

#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); return 1; } } while(0)

// ── Simulated 32-bit alive counters (mirrors test_task_watchdog.cpp) ─
static std::atomic<uint32_t> g_alive_control{0};
static std::atomic<uint32_t> g_alive_dispatch{0};
static std::atomic<uint32_t> g_alive_tx_low{0};
static std::atomic<uint32_t> g_alive_tx_high{0};
static uint32_t g_tick = 0;

// Replicated from rt main.cpp: all_tasks_alive
static bool all_tasks_alive_32(uint32_t deadline_ms) {
    for (auto* a : {&g_alive_control, &g_alive_dispatch, &g_alive_tx_low, &g_alive_tx_high}) {
        if (g_tick - a->load(std::memory_order_relaxed) > deadline_ms)
            return false;
    }
    return true;
}

// ── Simulated 64-bit version for comparison ────────────────────────
static std::atomic<uint64_t> g_alive_ctrl_64{0};
static std::atomic<uint64_t> g_alive_disp_64{0};
static std::atomic<uint64_t> g_alive_tx_low_64{0};
static std::atomic<uint64_t> g_alive_tx_high_64{0};
static uint64_t g_tick_64 = 0;

static bool all_tasks_alive_64(uint64_t deadline_ms) {
    for (auto* a : {&g_alive_ctrl_64, &g_alive_disp_64, &g_alive_tx_low_64, &g_alive_tx_high_64}) {
        if (g_tick_64 - a->load(std::memory_order_relaxed) > deadline_ms)
            return false;
    }
    return true;
}

// ── Helpers ────────────────────────────────────────────────────────
static void tick_all_32(uint32_t now) {
    g_tick = now;
    g_alive_control.store(now, std::memory_order_relaxed);
    g_alive_dispatch.store(now, std::memory_order_relaxed);
    g_alive_tx_low.store(now, std::memory_order_relaxed);
    g_alive_tx_high.store(now, std::memory_order_relaxed);
}

static void tick_all_64(uint64_t now) {
    g_tick_64 = now;
    g_alive_ctrl_64.store(now, std::memory_order_relaxed);
    g_alive_disp_64.store(now, std::memory_order_relaxed);
    g_alive_tx_low_64.store(now, std::memory_order_relaxed);
    g_alive_tx_high_64.store(now, std::memory_order_relaxed);
}

// ════════════════════════════════════════════════════════════════════

static int test_near_wrap_normal() {
    // All tasks ticked at UINT32_MAX - 5, g_tick = UINT32_MAX - 5
    // All within 500ms deadline → alive
    tick_all_32(UINT32_MAX - 5);
    CHECK(all_tasks_alive_32(500), "near-wrap normal: all alive within deadline");
    std::printf("  PASS: Near-wrap normal operation OK\n");
    return 0;
}

static int test_near_wrap_stale() {
    // Tasks ticked at UINT32_MAX - 600, now g_tick = UINT32_MAX - 5
    // diff = 595 > 500 → stale
    g_alive_control.store(UINT32_MAX - 600, std::memory_order_relaxed);
    g_alive_dispatch.store(UINT32_MAX - 600, std::memory_order_relaxed);
    g_alive_tx_low.store(UINT32_MAX - 600, std::memory_order_relaxed);
    g_alive_tx_high.store(UINT32_MAX - 600, std::memory_order_relaxed);
    g_tick = UINT32_MAX - 5;
    CHECK(!all_tasks_alive_32(500), "near-wrap stale: >500ms detected");
    std::printf("  PASS: Near-wrap stale detection OK\n");
    return 0;
}

static int test_post_wrap_normal() {
    // g_tick wrapped from UINT32_MAX → 5.
    // All tasks ticked at UINT32_MAX - 5 (just before wrap).
    // Unsigned diff = 5 - (UINT32_MAX - 5) = 11 → within 500ms
    // This is a known limitation of uint32_t: tasks that were alive
    // just before wrap appear to have ticked only ~11 ticks ago.
    tick_all_32(UINT32_MAX - 5);
    g_tick = 5;  // wraparound occurred
    CHECK(all_tasks_alive_32(500), "post-wrap: no false positive (tasks alive before wrap)");
    std::printf("  PASS: Post-wrap normal (unsigned modular math OK)\n");
    return 0;
}

static int test_post_wrap_stale_dead() {
    // Task died at UINT32_MAX - 600, g_tick wrapped to 5.
    // Unsigned diff = 5 - (UINT32_MAX - 600) = 611 > 500 → stale detected
    g_alive_control.store(UINT32_MAX - 600, std::memory_order_relaxed);
    g_alive_dispatch.store(UINT32_MAX - 600, std::memory_order_relaxed);
    g_alive_tx_low.store(UINT32_MAX - 600, std::memory_order_relaxed);
    g_alive_tx_high.store(UINT32_MAX - 600, std::memory_order_relaxed);
    g_tick = 5;
    CHECK(!all_tasks_alive_32(500), "post-wrap stale: dead task detected across wrap");
    std::printf("  PASS: Post-wrap stale detection OK\n");
    return 0;
}

static int test_64bit_wraparound() {
    // uint64_t at UINT64_MAX boundary — same modular arithmetic but
    // range is so large it never wraps in practice.
    tick_all_64(UINT64_MAX - 5);
    CHECK(all_tasks_alive_64(500), "64-bit near-wrap: alive");
    g_tick_64 = 5;  // wraparound (billions of years at 100Hz)
    CHECK(all_tasks_alive_64(500), "64-bit post-wrap: alive via modular math");
    std::printf("  PASS: uint64_t wraparound OK\n");
    return 0;
}

static int test_u64_large_range() {
    // uint64_t can represent ~584 million years at 100Hz without wrap.
    // Verify that large differences (>4G) don't cause false positives.
    uint64_t far_future = uint64_t(1) << 40; // 1 trillion ticks
    g_alive_ctrl_64.store(0, std::memory_order_relaxed);
    g_alive_disp_64.store(0, std::memory_order_relaxed);
    g_alive_tx_low_64.store(0, std::memory_order_relaxed);
    g_alive_tx_high_64.store(0, std::memory_order_relaxed);
    g_tick_64 = far_future;
    CHECK(!all_tasks_alive_64(500), "64-bit large range: dead task detected");
    std::printf("  PASS: uint64_t large range OK\n");
    return 0;
}

int main() {
    int failures = 0;
    std::printf("\n=== Watchdog Wraparound ===\n\n");

    failures += test_near_wrap_normal();
    failures += test_near_wrap_stale();
    failures += test_post_wrap_normal();
    failures += test_post_wrap_stale_dead();
    failures += test_64bit_wraparound();
    failures += test_u64_large_range();

    if (failures == 0) std::printf("\nAll watchdog wraparound tests PASSED\n");
    else std::printf("\n%d test(s) FAILED\n", failures);
    return failures;
}
