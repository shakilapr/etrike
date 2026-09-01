#pragma once
// AURIX board HAL — implements the portable rta::hal interfaces over
// iLLD for the KIT_A2G_TC375_LITE (TC375, LQFP-176).
//
// STATUS: Scaffold for target bring-up (Phase D). The MCMCAN/port init
// bodies are filled during D0 (walking skeleton) and D2 (real CAN).
// The portable firmware (src/) calls only these interfaces.

#include "hal/can.h"
#include "hal/gpio.h"
#include "hal/clock.h"
#include "hal/watchdog.h"
#include "board/board_pins.h"

namespace rta::board {

// ── CAN (CAN0 Node 0 low / CAN0 Node 2 high) ────────────────────────
class AurixCan : public rta::hal::Can {
public:
    AurixCan() = default;

    // Phase D2: initialize CAN0 node 0 (low) and node 2 (high) with the
    // pinned transceivers at 500 kbit/s. Fill from iLLD IfxCan driver.
    void init();

    bool transmit(Bus b, const etrike::protocol::Frame& frame,
                  rta::hal::TxClass cls) override;
    bool receive(Bus b, etrike::protocol::Frame& out) override;
    void error_counters(Bus b, std::uint8_t& tec, std::uint8_t& rec) const override;
    bool bus_off(Bus b) const override;
};

// ── GPIO (rider inputs + relay outputs) ─────────────────────────────
class AurixGpio : public rta::hal::Gpio {
public:
    AurixGpio() = default;

    void init();  // Phase D0: configure pin modes (inputs pull-up, outputs).

    bool read(rta::hal::InputSignal sig) const override;
    void write(rta::hal::OutputSignal sig, bool asserted) override;
};

// ── Clock (STM tick) ────────────────────────────────────────────────
class AurixClock : public rta::hal::Clock {
public:
    AurixClock() = default;
    void init();  // Phase D0: start STM.
    TimeUs monotonic_us() override;
};

// ── Watchdog (TPS3850-Q1 WDI) ───────────────────────────────────────
class AurixWatchdog : public rta::hal::Watchdog {
public:
    AurixWatchdog() = default;
    void init();  // Phase D0: configure WDI GPIO.
    void service() override;  // perform the TPS3850-Q1 WDI pulse
};

}  // namespace rta::board
