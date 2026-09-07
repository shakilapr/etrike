#pragma once

// -----------------------------------------------------------------------------
// Runtime developer-bypass evaluation (shared by SYS and RT firmware).
// -----------------------------------------------------------------------------
// Both SYS and RT use the identical SYSTEM_RUN_MODE policy:
//   mode 0 (PRODUCTION)            -> all bypasses disabled.
//   mode 1 (HARDWARE BENCH)        -> the active-low DEVELOPER_OVERRIDE_PIN
//                                     jumper decides: pin LOW (jumped to GND)
//                                     enables all bypasses, pin HIGH disables.
//   mode 2 (CAN/SOFTWARE BENCH)    -> all bypasses enabled (no physical peers).
//
// The firmware main.cpp files read the GPIO and then call this pure function;
// unit tests call the same function so the tested logic is the shipped logic.

#include <cstdint>

namespace etrike {

struct BypassState {
    bool bench_solo_mode{false};     // developer override active (amber bulb / TX admit)
    bool bypass_eps_sync{false};     // skip EPS-C sync/liveness supervision
    bool bypass_seb_sync{false};     // skip SEB status / comms-loss supervision
    bool bypass_mtr_absent{false};   // tolerate MTR feedback absence (EGAS, ACK, freshness)
};

// run_mode: 0 (production), 1 (hardware bench), 2 (CAN/software bench).
// override_pin_low: true when the active-low developer jumper reads LOW (GND).
inline BypassState evaluate_run_mode_bypasses(int run_mode, bool override_pin_low) noexcept {
    BypassState out;
    const bool enable = (run_mode == 2) || (run_mode == 1 && override_pin_low);
    out.bench_solo_mode = enable;
    out.bypass_eps_sync = enable;
    out.bypass_seb_sync = enable;
    out.bypass_mtr_absent = enable;
    return out;
}

}  // namespace etrike
