#pragma once
// ── System READY indication (purely observational) ─────────────────────────
//
// READY answers: "is the whole command path up, error-free, so that if the Host
// commands ignition/gear/speed it will actually go?" It is an operator / telemetry
// indication ONLY. It must never feed resolve_authority(), inhibit_state, ESTOP,
// or any outgoing frame — wiring it into those would let a display signal change
// actuation authority.
//
// Evidence sources (all live, low CAN):
//   SYS local  : latched faults + transient inhibits (inhibit_state.h)
//   RT         : 0x7FD heartbeat + 0x501 RT_NODE_STATUS (covers SES/steering)
//   MTR        : 0x502 MTR_NODE_STATUS (ignition + output proof)
//   SEB        : 0x721 SEB_STATUS (freshness + rolling + error_status)
//   Host path  : valid 0x111 HMI_MODE_REQ + 0x112 HMI_PWR_REQ streams
//
// Bypass-aware: on a bench where an actuator is intentionally absent, the caller
// sets mtr_required/seb_required false (mirroring g_bypass_mtr_absent /
// g_bypass_seb_sync) so a missing peer degrades to a distinct cadence instead of
// reporting a hard fault. RT is always required — it is the command conduit.

#include <cstdint>

namespace sys {

enum class SystemReadyLevel : uint8_t {
    Blocked    = 0,  // a real error / missing required peer — green OFF
    MtrAbsent  = 1,  // system up, only MTR missing (bench) — green FAST blink
    HostAbsent = 2,  // system up, no Host request stream — green BREATHE
    Full       = 3,  // everything present, healthy, host commanding — green SOLID
};

struct SystemReadyInputs {
    bool estop_or_inhibit = true;   // any SYS-local ESTOP/inhibit/latched fault
    bool rt_ok            = false;  // RT heartbeat + 0x501 healthy
    bool mtr_ok           = false;  // 0x502 healthy
    bool seb_ok           = false;  // 0x721 healthy
    bool host_ok          = false;  // valid Host request stream present
    bool mtr_required     = true;   // false under g_bypass_mtr_absent
    bool seb_required     = true;   // false under g_bypass_seb_sync
};

constexpr SystemReadyLevel evaluate_system_ready(const SystemReadyInputs& in) {
    if (in.estop_or_inhibit) return SystemReadyLevel::Blocked;
    if (!in.rt_ok)           return SystemReadyLevel::Blocked;
    if (in.mtr_required && !in.mtr_ok) return SystemReadyLevel::Blocked;
    if (in.seb_required && !in.seb_ok) return SystemReadyLevel::Blocked;
    if (!in.mtr_ok)  return SystemReadyLevel::MtrAbsent;
    if (!in.host_ok) return SystemReadyLevel::HostAbsent;
    return SystemReadyLevel::Full;
}

}  // namespace sys
