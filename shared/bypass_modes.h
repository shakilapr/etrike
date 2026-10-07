#pragma once

// -----------------------------------------------------------------------------
// Runtime developer-bypass & ControlPath evaluation (shared by SYS and RT firmware).
// -----------------------------------------------------------------------------
// Both SYS and RT use the identical SYSTEM_RUN_MODE policy:
//   mode 0 (PRODUCTION)            -> Production path; all bypasses disabled.
//   mode 1 (HARDWARE BENCH)        -> the active-low DEVELOPER_OVERRIDE_PIN
//                                     jumper decides at boot: pin LOW (jumped to GND)
//                                     enables BenchPassthrough, pin HIGH enables Production.
//   mode 2 (CAN/SOFTWARE BENCH)    -> BenchPassthrough (no physical peers).
//
// The firmware main.cpp files sample the GPIO once at boot and call this pure
// function; unit tests call the same function so the tested logic is the shipped logic.

#include <cstdint>

namespace etrike {

// Rule 1: Explicit type contract distinguishing safety-arbitrated Production
// from unchecked BenchPassthrough.
enum class ControlPath : uint8_t {
    Production = 0,       // Full ASIL safety arbitration, supervisors, and dynamic clamps
    BenchPassthrough = 1  // Unchecked transparent relay from high level directly to actuators
};

// Rule 10: Explicit policy defining behavior when high-level commands stop arriving.
enum class HighLevelLossPolicy : uint8_t {
    ZeroOutput = 0,       // Default: Safely zero all actuator outputs and Neutral after timeout
    HoldLast = 1          // Lab bench lock: Hold the last received command indefinitely
};

struct BypassState {
    ControlPath control_path{ControlPath::Production};
    HighLevelLossPolicy loss_policy{HighLevelLossPolicy::ZeroOutput};
    uint32_t loss_timeout_ms{500};    // Dedicated high-level loss timeout for BenchPassthrough

    bool bench_solo_mode{false};     // preserved for backward compat
    bool bypass_eps_sync{false};     // skip SES sync/liveness supervision
    bool bypass_seb_sync{false};     // skip SEB status / comms-loss supervision
    bool bypass_mtr_absent{false};   // tolerate MTR feedback absence (EGAS, ACK, freshness)
};

// run_mode: 0 (production), 1 (hardware bench), 2 (CAN/software bench).
// override_pin_low: true when the active-low developer jumper reads LOW (GND).
inline BypassState evaluate_run_mode_bypasses(int run_mode, bool override_pin_low) noexcept {
    BypassState out;
    const bool enable = (run_mode == 2) || (run_mode == 1 && override_pin_low);
    out.control_path = enable ? ControlPath::BenchPassthrough : ControlPath::Production;
    out.loss_policy = HighLevelLossPolicy::ZeroOutput;
    out.loss_timeout_ms = 500;
    out.bench_solo_mode = enable;
    out.bypass_eps_sync = enable;
    out.bypass_seb_sync = enable;
    out.bypass_mtr_absent = enable;
    return out;
}

}  // namespace etrike
