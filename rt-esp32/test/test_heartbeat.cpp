// g++ -std=c++17 -I. -I../src -I../../shared test_heartbeat.cpp -o test_heartbeat && ./test_heartbeat
//
// Verifies: heartbeat.h — independent per-bus alive counters (architecture §7.7, §8.6).

#include <cstdio>
#include <cstdint>
#include "can/can_protocol.h"
#include "heartbeat.h"

static int pass=0, fail=0;
#define CHECK(cond) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s:%d\n",__FILE__,__LINE__);} } while(0)

int main(){
    printf("\n=== Heartbeat: Independent Per-Bus Counters ===\n\n");
    using namespace rt;

    // ── Test 1: Independent counters — low and high diverge ──────────
    printf("-- Independent counters diverge --\n");
    {
        DualHeartbeat hb;
        hb.init();
        can::Frame f_low, f_high;

        // After init, both counters are 0
        CHECK(hb.ctr_low()  == 0);
        CHECK(hb.ctr_high() == 0);

        // Tick low 3 times, high 1 time — counters should differ
        hb.tick_low(f_low);
        hb.tick_low(f_low);
        hb.tick_low(f_low);
        hb.tick_high(f_high);

        CHECK(hb.ctr_low()  == 3);
        CHECK(hb.ctr_high() == 1);
        CHECK(hb.ctr_low() != hb.ctr_high());
        printf("  low=%u high=%u (should differ)\n", hb.ctr_low(), hb.ctr_high());
    }

    // ── Test 2: Both counters increment independently ────────────────
    printf("-- Both counters increment --\n");
    {
        DualHeartbeat hb;
        hb.init();
        can::Frame f;

        for (int i = 0; i < 10; ++i) hb.tick_low(f);
        for (int i = 0; i < 5;  ++i) hb.tick_high(f);

        CHECK(hb.ctr_low()  == 10);
        CHECK(hb.ctr_high() == 5);
        printf("  low=%u high=%u after 10/5 ticks\n", hb.ctr_low(), hb.ctr_high());
    }

    // ── Test 3: Counter wrap at 256 (uint8_t) ────────────────────────
    printf("-- Counter wrap at 256 --\n");
    {
        DualHeartbeat hb;
        hb.init();
        can::Frame f;

        // Advance low counter to 255, then one more → 0 (wrap)
        for (int i = 0; i < 255; ++i) hb.tick_low(f);
        CHECK(hb.ctr_low() == 255);
        hb.tick_low(f);
        CHECK(hb.ctr_low() == 0);  // wrap
        printf("  low wrapped: 255→%u\n", hb.ctr_low());

        // High counter is independent, still at 0
        CHECK(hb.ctr_high() == 0);
        printf("  high independent: still %u\n", hb.ctr_high());
    }

    // ── Test 4: Frame contents — correct ID and DLC ──────────────────
    printf("-- Frame encoding --\n");
    {
        DualHeartbeat hb;
        hb.init();
        can::Frame f;

        hb.tick_low(f);
        CHECK(f.id  == 0x7FD);
        CHECK(f.dlc == 1);
        CHECK(f.u8_at(0) == 1);  // first tick → counter=1

        hb.tick_high(f);
        CHECK(f.id  == 0x7FD);
        CHECK(f.dlc == 1);
        CHECK(f.u8_at(0) == 1);  // first high tick → counter=1

        // After more ticks, counters reflect independent values
        hb.tick_low(f);   // low: 2
        hb.tick_low(f);   // low: 3
        hb.tick_high(f);  // high: 2
        hb.tick_low(f);   // low: 4

        hb.tick_low(f);
        CHECK(f.u8_at(0) == 5);  // low counter in frame

        hb.tick_high(f);
        CHECK(f.u8_at(0) == 3);  // high counter in frame
        printf("  frame ID=0x%03X DLC=%u payload confirmed\n", f.id, f.dlc);
    }

    // ── Test 5: Re-init resets both counters ─────────────────────────
    printf("-- Re-init resets counters --\n");
    {
        DualHeartbeat hb;
        hb.init();
        can::Frame f;

        for (int i = 0; i < 100; ++i) hb.tick_low(f);
        for (int i = 0; i < 50;  ++i) hb.tick_high(f);
        CHECK(hb.ctr_low()  == 100);
        CHECK(hb.ctr_high() == 50);

        hb.init();  // reset
        CHECK(hb.ctr_low()  == 0);
        CHECK(hb.ctr_high() == 0);
        printf("  reset: low=%u high=%u\n", hb.ctr_low(), hb.ctr_high());
    }

    printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
