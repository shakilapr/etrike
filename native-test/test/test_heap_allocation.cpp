// Test: Zero Runtime Allocation Verification Harness (§4.3 & §10.2)
// Proves that DiagnosticManager performs fixed-size bookkeeping with 0 dynamic heap allocations.

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "shared/diagnostics.h"

namespace diag = etrike::diagnostics;

static std::atomic<bool> g_trap_allocations{false};
static std::atomic<std::size_t> g_allocation_count{0};

void* operator new(std::size_t size) {
    if (g_trap_allocations.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
    return std::malloc(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    if (g_trap_allocations.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
    return std::malloc(size);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

int main() {
    std::printf("Testing DiagnosticManager zero-allocation runtime contract...\n");

    // Static footprint check
    static_assert(sizeof(diag::DiagnosticManager) <= 4096, "DiagnosticManager exceeds RAM budget!");
    std::printf("DiagnosticManager footprint: %zu bytes (well within budget)\n", sizeof(diag::DiagnosticManager));

    diag::DiagnosticManager mgr{};

    // Arm the heap allocation trap
    g_allocation_count.store(0);
    g_trap_allocations.store(true);

    // Run 1,000,000 mixed diagnostic lifecycle operations
    for (int i = 0; i < 1000000; ++i) {
        diag::DiagId test_id = (i % 2 == 0) ? diag::DiagId::SysSebStatusTimeout : diag::DiagId::RtSysHeartbeatTimeout;
        mgr.raise(test_id, static_cast<std::uint16_t>(i & 0xFFFF));

        diag::DiagReport report{};
        mgr.pop_pending_report(report);

        mgr.recover(test_id);
        mgr.clear(test_id);
        mgr.pop_pending_report(report);

        if (i % 100000 == 0) {
            mgr.replay_active_set();
            mgr.on_estop_episode_cleared();
        }
    }

    g_trap_allocations.store(false);

    if (g_allocation_count.load() == 0) {
        std::printf("PASS: Exactly 0 runtime heap allocations over 1,000,000 operations!\n");
        return 0;
    } else {
        std::printf("FAIL: Detected %zu heap allocations during runtime!\n", g_allocation_count.load());
        return 1;
    }
}
