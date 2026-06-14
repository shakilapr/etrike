// Phase 4: sys-esp32/src/can_driver.h validation
// g++ -std=c++17 -I../src -I../../shared test_can_driver.cpp -o test_can_driver && ./test_can_driver

#include <cstdio>
#include <cassert>
#include "config.h"
#include "can/can_protocol.h"

// Test CAN config without requiring ESP-IDF headers (can_driver.h pulls in twai.h)
// Instead, verify the config values directly.

static int fails = 0;
#define CHK(d) printf("  %-50s ", d)
#define OK      printf("PASS\n")
#define BAD(m)  do { printf("FAIL: %s\n", m); ++fails; } while(0)

int main() {
    printf("Phase 4: sys-esp32/src/can_driver.h\n\n");

    printf("== CAN driver config (low-level TWAI) ==\n");
    CHK("TX GPIO = 5");        if (sys::kCanTxGpio == 5) OK; else BAD("tx");
    CHK("RX GPIO = 4");        if (sys::kCanRxGpio == 4) OK; else BAD("rx");
    CHK("bitrate = 500 kbit/s");if (sys::kCanBitrateHz == 500000) OK; else BAD("bitrate");
    CHK("TX != RX (no short)"); if (sys::kCanTxGpio != sys::kCanRxGpio) OK; else BAD("short");

    printf("\n== Frame construction via shared header ==\n");
    // Smoke-test: build a frame using the shared protocol
    can::Frame f;
    can::SysSafetySts{true, false}.to_frame(f);
    CHK("SysSafetySts frame ID=0x011");
    if (f.id == 0x011 && f.dlc == 2) OK; else BAD("id/dlc");

    can::SysModeCmd{1}.to_frame(f);
    CHK("SysModeCmd frame ID=0x110");
    if (f.id == 0x110 && f.dlc == 1) OK; else BAD("id/dlc");

    can::SysThrottleSts{1500}.to_frame(f);
    CHK("SysThrottleSts frame ID=0x120");
    if (f.id == 0x120 && f.dlc == 2) OK; else BAD("id/dlc");

    can::SysDiagRpt{}.to_frame(f);
    CHK("SysDiagRpt frame ID=0x600");
    if (f.id == 0x600 && f.dlc == 8) OK; else BAD("id/dlc");

    // SYS heartbeat
    f.id = 0x7FE; f.dlc = 1; f.put_u8(0, 42);
    CHK("SYS heartbeat ID=0x7FE, DLC=1, ctr=42");
    if (f.id == 0x7FE && f.dlc == 1 && f.u8_at(0) == 42) OK; else BAD("hb");

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
