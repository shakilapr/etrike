// Dual heartbeat independence test — each bus has independent counter.
// Pattern: test_heartbeat.cpp (stale), test_components.cpp
//
// DualHeartbeat sends independent heartbeat frames on low bus (→SYS) and
// high bus (→Host) via tick_low() / tick_high().  Each maintains its own
// uint8_t counter.  If one bus fails, the other continues independently.
//
// Architecture §7.7, §8.6 — independent per-bus alive counters.

#include <cstdio>
#include <cstdint>
#include "protocol/core/frame.hpp"
#include "heartbeat.h"

using etrike::protocol::Frame;

static int pass = 0, fail = 0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"  FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)

int main() {
    std::printf("\n=== Dual Heartbeat Independence ===\n\n");
    using namespace rt;

    // ── Test 1: Both counters start at 0 after init ──────────────
    std::printf("-- Both counters start at 0 --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f;
        CHECK(hb.ctr_low()  == 0);
        CHECK(hb.ctr_high() == 0);
        std::printf("  low=%u high=%u\n", hb.ctr_low(), hb.ctr_high());
    }

    // ── Test 2: Low and high counters increment independently ────
    std::printf("-- Independent increments --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f_low, f_high;

        hb.tick_low(f_low);
        hb.tick_low(f_low);
        hb.tick_low(f_low);
        hb.tick_high(f_high);

        CHECK(hb.ctr_low()  == 3);
        CHECK(hb.ctr_high() == 1);
        CHECK(hb.ctr_low() != hb.ctr_high());
        std::printf("  low=%u high=%u (independently diverge)\n",
                    hb.ctr_low(), hb.ctr_high());
    }

    // ── Test 3: Tick low bus only — high stays unchanged ────────
    std::printf("-- Low-bus only: high unchanged --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f;

        hb.tick_low(f);
        CHECK(hb.ctr_low()  == 1);
        CHECK(hb.ctr_high() == 0);

        hb.tick_low(f);
        hb.tick_low(f);
        CHECK(hb.ctr_low()  == 3);
        CHECK(hb.ctr_high() == 0);  // high never ticked
        std::printf("  low=%u high=%u (high stuck at 0)\n",
                    hb.ctr_low(), hb.ctr_high());
    }

    // ── Test 4: Tick high bus only — low stays unchanged ────────
    std::printf("-- High-bus only: low unchanged --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f;

        hb.tick_high(f);
        CHECK(hb.ctr_low()  == 0);
        CHECK(hb.ctr_high() == 1);

        hb.tick_high(f);
        hb.tick_high(f);
        CHECK(hb.ctr_low()  == 0);  // low never ticked
        CHECK(hb.ctr_high() == 3);
        std::printf("  low=%u high=%u (low stuck at 0)\n",
                    hb.ctr_low(), hb.ctr_high());
    }

    // ── Test 5: Many ticks — counters track independently ───────
    std::printf("-- Many ticks — independent tracking --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f;

        for (int i = 0; i < 100; ++i) hb.tick_low(f);
        for (int i = 0; i < 50;  ++i) hb.tick_high(f);

        CHECK(hb.ctr_low()  == 100);
        CHECK(hb.ctr_high() == 50);
        std::printf("  low=%u high=%u after 100/50 ticks\n",
                    hb.ctr_low(), hb.ctr_high());
    }

    // ── Test 6: Counter wrap at 256 (uint8_t) — independent ─────
    std::printf("-- Counter wrap at 256 --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f;

        // Advance low to 254, high stays at 0
        for (int i = 0; i < 254; ++i) hb.tick_low(f);
        CHECK(hb.ctr_low()  == 254);
        CHECK(hb.ctr_high() == 0);

        // Low wraps: 254 → 255 → 0
        hb.tick_low(f);
        CHECK(hb.ctr_low()  == 255);
        hb.tick_low(f);
        CHECK(hb.ctr_low()  == 0);  // wrap

        // High still untouched
        CHECK(hb.ctr_high() == 0);
        std::printf("  low wrapped: 254→255→0, high still %u\n", hb.ctr_high());
    }

    // ── Test 7: Both counters wrap independently ────────────────
    std::printf("-- Both counters wrap --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f;

        // Advance both: low to 255, high to 255
        for (int i = 0; i < 255; ++i) { hb.tick_low(f); hb.tick_high(f); }
        CHECK(hb.ctr_low()  == 255);
        CHECK(hb.ctr_high() == 255);

        // Both wrap on next tick
        hb.tick_low(f);
        hb.tick_high(f);
        CHECK(hb.ctr_low()  == 0);
        CHECK(hb.ctr_high() == 0);
        std::printf("  both wrapped: low=0 high=0\n");
    }

    // ── Test 8: Frame encoding — correct CAN ID and DLC ─────────
    std::printf("-- Frame encoding --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f;

        hb.tick_low(f);
        CHECK(f.id  == 0x7FD);
        CHECK(f.dlc == 2);
        CHECK(f.data[0] == 1);  // first tick → low counter=1

        hb.tick_high(f);
        CHECK(f.id  == 0x7FD);
        CHECK(f.dlc == 2);
        CHECK(f.data[0] == 1);  // first high tick → high counter=1

        std::printf("  low frame: ID=0x%03X DLC=%u ctr=%u\n",
                    f.id, f.dlc, f.data[0]);
    }

    // ── Test 9: Health flags can be set independently per bus ───
    std::printf("-- Health flags --\n");
    {
        DualHeartbeat hb;
        hb.init();
        Frame f;

        // Low bus with estop flag, high bus with mode_auto flag
        hb.tick_low(f, 0x02);  // bit1 = estop_active
        CHECK((f.data[1] & 0x02) != 0);
        CHECK((f.data[1] & 0x04) == 0);  // mode_auto NOT set

        hb.tick_high(f, 0x04); // bit2 = mode_auto
        CHECK((f.data[1] & 0x04) != 0);
        CHECK((f.data[1] & 0x02) == 0);  // estop NOT set

        std::printf("  health flags independent per bus\n");
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
