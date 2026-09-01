// AURIX Lite Kit V2 (TC375) — RT firmware entry.
//
// This is the TARGET entry point (Phase D). It wires the host-validated
// rta:: controllers (rt-aurix-lite/src/) to the board HAL
// (rt-aurix-lite/board/hal_aurix). The runtime mechanism (AMP FreeRTOS,
// cyclic executors) is decided at the Phase D4 gate; until then this
// skeleton shows the intended wiring.
//
// The executable shape mirrors the deterministic simulator
// (src/platform/host/simulator.cpp): CPU0/1/2 executors call the same
// rta:: app/domain functions.

#include <cstdint>

#include "board/hal_aurix/aurix_hal.h"
#include "app/controllers.h"

using rta::board::AurixCan;
using rta::board::AurixGpio;
using rta::board::AurixClock;
using rta::board::AurixWatchdog;

namespace {

AurixCan    g_can;
AurixGpio   g_gpio;
AurixClock  g_clock;
AurixWatchdog g_wdt;
rta::MotionController g_motion;

// CPU0 data-plane executor: decode RX -> typed inputs.
void cpu0_executor(rta::TimeUs now) {
    (void)now;
    // decode host/mtr/ses/seb frames via rta::protocol::adapters.
}

// CPU1 motion+safety executor (100 Hz control).
void cpu1_executor(rta::TimeUs now) {
    (void)now;
    // g_motion.control(...) using typed inputs from cpu0_executor.
}

// CPU2 body executor.
void cpu2_executor(rta::TimeUs now) {
    (void)now;
}

}  // namespace

int main(void) {
    // Phase D0 init.
    g_clock.init();
    g_gpio.init();
    g_can.init();
    g_wdt.init();
    g_motion.init();

    // Runtime loop — placeholder until the D4 runtime model is chosen.
    for (;;) {
        const rta::TimeUs now = g_clock.monotonic_us();
        cpu0_executor(now);
        cpu1_executor(now);
        cpu2_executor(now);
    }
    return 0;
}
