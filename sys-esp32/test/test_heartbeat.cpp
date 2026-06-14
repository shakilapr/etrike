// Phase 6: sys-esp32/src/heartbeat.h validation
// g++ -std=c++17 -I../src -I../../shared test_heartbeat.cpp -o t && ./t

#include <cstdio>
#include <cassert>
#include "heartbeat.h"
#include "can/can_protocol.h"

static int fails = 0;
#define CHK(d) printf("  %-50s ", d)
#define OK      printf("PASS\n")
#define BAD(m)  do { printf("FAIL: %s\n", m); ++fails; } while(0)

int main() {
    printf("Phase 6: sys-esp32/src/heartbeat.h\n\n");
    sys::Heartbeat hb;
    can::Frame f;

    hb.init();
    CHK("init: counter=0"); if (hb.counter() == 0) OK; else BAD("init");

    hb.tick(f);
    CHK("tick 1: counter=1, ID=0x7FE, DLC=1");
    if (hb.counter() == 1 && f.id == 0x7FE && f.dlc == 1) OK; else BAD("tick1");

    CHK("tick 1: alive_ctr=1");
    if (f.u8_at(0) == 1) OK; else BAD("ctr1");

    // Skip to 255
    for (int i = 0; i < 254; ++i) hb.tick(f);
    CHK("tick 255: counter=255 (0xFF)");
    if (hb.counter() == 255 && f.u8_at(0) == 255) OK; else BAD("wrap");

    hb.tick(f);
    CHK("tick 256: counter wraps to 0 (255→0)");
    if (hb.counter() == 0 && f.u8_at(0) == 0) OK; else BAD("no wrap");

    hb.tick(f);
    CHK("tick 257: counter=1 after wrap");
    if (hb.counter() == 1) OK; else BAD("post-wrap");

    CHK("frame ID consistent (0x7FE)");
    if (f.id == 0x7FE) OK; else BAD("id");

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
