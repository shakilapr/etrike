// Multi-task watchdog native test — verify stall detection
#include <atomic>
#include <cstdio>
#include <cstdint>

// ── Simulated alive counters (pattern from RT + SYS) ──────────────
static std::atomic<uint32_t> g_alive_control{0};
static std::atomic<uint32_t> g_alive_dispatch{0};
static std::atomic<uint32_t> g_alive_tx_low{0};
static std::atomic<uint32_t> g_alive_tx_high{0};
static uint32_t g_tick = 0;

#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); return 1; } } while(0)

// ── Replicated check_task_watchdog from RT main.cpp ───────────────
static bool all_tasks_alive(uint32_t deadline_ms) {
    for (auto* a : {&g_alive_control, &g_alive_dispatch, &g_alive_tx_low, &g_alive_tx_high}) {
        if (g_tick - a->load(std::memory_order_relaxed) > deadline_ms)
            return false;
    }
    return true;
}

static int test_all_tasks_ticking() {
    // All tasks update their counters
    g_alive_control.store(g_tick, std::memory_order_relaxed);
    g_alive_dispatch.store(g_tick, std::memory_order_relaxed);
    g_alive_tx_low.store(g_tick, std::memory_order_relaxed);
    g_alive_tx_high.store(g_tick, std::memory_order_relaxed);

    CHECK(all_tasks_alive(500), "All tasks ticking: should report alive");
    std::printf("  PASS: All tasks alive\n");
    return 0;
}

static int test_control_stalled() {
    // control task hasn't ticked for 600ms
    g_alive_control.store(0, std::memory_order_relaxed);
    g_alive_dispatch.store(g_tick, std::memory_order_relaxed);
    g_alive_tx_low.store(g_tick, std::memory_order_relaxed);
    g_alive_tx_high.store(g_tick, std::memory_order_relaxed);
    g_tick = 600;

    CHECK(!all_tasks_alive(500), "Control stalled 600ms: should report dead");
    std::printf("  PASS: Control task stall detected\n");
    return 0;
}

static int test_dispatch_stalled() {
    g_alive_control.store(g_tick, std::memory_order_relaxed);
    g_alive_dispatch.store(0, std::memory_order_relaxed);
    g_alive_tx_low.store(g_tick, std::memory_order_relaxed);
    g_alive_tx_high.store(g_tick, std::memory_order_relaxed);
    g_tick = 600;

    CHECK(!all_tasks_alive(500), "Dispatch stalled 600ms: should report dead");
    std::printf("  PASS: Dispatch task stall detected\n");
    return 0;
}

static int test_stalled_below_deadline() {
    // All tasks ticked 400ms ago — within 500ms deadline
    g_alive_control.store(100, std::memory_order_relaxed);
    g_alive_dispatch.store(100, std::memory_order_relaxed);
    g_alive_tx_low.store(100, std::memory_order_relaxed);
    g_alive_tx_high.store(100, std::memory_order_relaxed);
    g_tick = 500;  // 400ms elapsed

    CHECK(all_tasks_alive(500), "400ms stall: still within 500ms deadline");
    std::printf("  PASS: Below-threshold stall not reported\n");
    return 0;
}

int main() {
    int failures = 0;
    failures += test_all_tasks_ticking();
    failures += test_control_stalled();
    failures += test_dispatch_stalled();
    failures += test_stalled_below_deadline();

    if (failures == 0) std::printf("\nAll watchdog tests PASSED\n");
    else std::printf("\n%d test(s) FAILED\n", failures);
    return failures;
}
