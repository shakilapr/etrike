#pragma once
// Independent, per-owner traction-inhibit reason bits (issues #5/#7).
//
// Two masks keep transient inhibit ownership separate from latched safety
// faults, so one subsystem can never accidentally erase another's fault by
// clearing a shared boolean:
//
//   g_inhibit_reasons       — B-class / transient / recoverable. Each detector
//                             sets/clears ONLY its own bit via fetch_or /
//                             fetch_and (atomic RMW — no lost updates between
//                             concurrent tasks).
//   g_latched_fault_reasons — latched safety faults. Mutated ONLY by the
//                             explicit reset path; a detector going healthy
//                             must never clear its own latch here.
//
// Ownership:
//   MTR feedback loss   -> kInhibitMtrFbkLoss       (MTR-fbk detector)
//   SEB comms loss      -> kInhibitSebCommsLoss     (SEB-status detector)
//   following excursion -> kInhibitBrakeFollowing   (transient, hysteresis)
//   persistent following-> kLatchedBrakeFollowing   (reset path only)
//   SEB L3              -> kLatchedSebL3            (reset path only)
#include <atomic>
#include <cstdint>
#include "config.h"


// SEB 0x721-derived live state, defined in inhibit_state.cpp at GLOBAL scope to
// match the unqualified references in main.cpp's SEB-status ingest path.
//   g_seb_error_status : error_status field (bits6-7 of byte0). ==3 ⇒ SEB L3.
//   g_seb_status_byte0 : raw byte0; 0xFF means "no 0x721 frame seen yet",
//     which the reset validator treats as "brake-following cause not proven
//     clear".
extern std::atomic<uint8_t> g_seb_error_status;
extern std::atomic<uint8_t> g_seb_status_byte0;
extern std::atomic<bool>    g_seb_seen;

namespace sys {

// Transient / recoverable inhibit reasons (own bit per detector).
enum InhibitReason : uint32_t {
    kInhibitMtrFbkLoss     = 1u << 0,  // 0x206 MTR feedback lost while relevant
    kInhibitSebCommsLoss   = 1u << 1,  // 0x721 SEB status stale
    kInhibitBrakeFollowing = 1u << 2,  // transient brake following excursion
};

// Latched safety faults (cleared only by the explicit reset path).
enum LatchedFaultReason : uint32_t {
    kLatchedBrakeFollowing    = 1u << 0,  // confirmed persistent following error
    kLatchedSebL3             = 1u << 1,  // SEB error_status == 3
    kLatchedMtrEstopAckFailed = 1u << 2,  // MTR failed to acknowledge ESTOP
};

// Remote ESTOP reset blocker mask bits (BUG-10)
enum EstopResetBlocker : uint16_t {
    kResetBlockNone             = 0,
    kResetBlockPhysicalEstop    = 1u << 0,  // Hardware button pressed / active
    kResetBlockLatchedFault     = 1u << 1,  // Latched fault cause still asserted
    kResetBlockMoving           = 1u << 2,  // Measured speed > 50 mm/s
    kResetBlockHeartbeatLoss    = 1u << 3,  // Heartbeat not ok
    kResetBlockMtrEstopActive   = 1u << 4,  // MTR never acknowledged ESTOP
    kResetBlockTransientInhibit = 1u << 5,  // Transient inhibit active
    kResetBlockInvalidToken     = 1u << 6,  // Invalid magic auth token
};


// Defined in inhibit_state.cpp (linked in both the firmware and the native
// unit-test build — which excludes main.cpp).
extern std::atomic<uint32_t> g_inhibit_reasons;
extern std::atomic<uint32_t> g_latched_fault_reasons;

inline void set_inhibit(InhibitReason r) {
    g_inhibit_reasons.fetch_or(static_cast<uint32_t>(r), std::memory_order_relaxed);
}
inline void clear_inhibit(InhibitReason r) {
    g_inhibit_reasons.fetch_and(~static_cast<uint32_t>(r), std::memory_order_relaxed);
}
inline void set_latched_fault(LatchedFaultReason r) {
    g_latched_fault_reasons.fetch_or(static_cast<uint32_t>(r), std::memory_order_relaxed);
}
// No generic clear_latched_fault() outside the reset path: latched faults are
// owned exclusively by the explicit reset handler.

// ── Validated ESTOP reset transaction (issue #7) ────────────────────────────
// The system may leave ESTOP only when every *currently latched* safety fault's
// underlying cause is no longer asserted. A latch whose cause is still active
// must NOT be cleared — that would paper over a live fault and let RT/MTR
// two-frame-clear into motion. The reset is performed atomically by
// ModeManager::try_exit_estop(): it gates the mode transition on this predicate
// and clears the latch in the same step, so no observer ever sees
// mode == Manual while a latched fault remains set.
inline bool latched_causes_currently_clearable() {
    uint32_t latched = g_latched_fault_reasons.load(std::memory_order_relaxed);
    if ((latched & kLatchedSebL3) &&
        g_seb_error_status.load(std::memory_order_relaxed) >= 3) {
        return false;  // SEB L3 still asserted
    }
    if ((latched & kLatchedBrakeFollowing) &&
        g_seb_status_byte0.load(std::memory_order_relaxed) == 0xFF) {
        return false;  // no fresh 0x721 ⇒ brake-following cause not proven clear
    }
    return true;
}

inline void clear_latched_fault_reasons() {
    g_latched_fault_reasons.fetch_and(
        ~(static_cast<uint32_t>(kLatchedSebL3) |
          static_cast<uint32_t>(kLatchedBrakeFollowing) |
          static_cast<uint32_t>(kLatchedMtrEstopAckFailed)),
        std::memory_order_relaxed);
}

inline bool transient_inhibited() { return g_inhibit_reasons.load() != 0u; }
inline bool latched_fault_present() { return g_latched_fault_reasons.load() != 0u; }
inline bool any_inhibit() { return transient_inhibited() || latched_fault_present(); }

// Evaluate all blockers preventing ESTOP remote reset (BUG-10)
// mtr_ack_confirmed: true if MTR has acknowledged the ESTOP (or on test bench without MTR).
// Reject reset ONLY if MTR NEVER acknowledged the ESTOP.
// If MTR acknowledged, ESTOP_ACTIVE is expected to remain 1 until SYS drops 0x011.
inline uint16_t get_estop_reset_blockers(
    bool physical_estop,
    bool hb_ok,
    int16_t measured_speed_mmps,
    bool mtr_ack_confirmed = true,
    uint16_t token = kRemoteResetTokenMagic
) {
    uint16_t mask = kResetBlockNone;
    if (token != kRemoteResetTokenMagic) {
        mask |= kResetBlockInvalidToken;
    }
    if (physical_estop) {
        mask |= kResetBlockPhysicalEstop;
    }
    if (!latched_causes_currently_clearable()) {
        mask |= kResetBlockLatchedFault;
    }
    int16_t abs_speed = (measured_speed_mmps < 0) ? -measured_speed_mmps : measured_speed_mmps;
    if (abs_speed > kResetMaxMovingSpeedMmps) {
        mask |= kResetBlockMoving;
    }
    if (!hb_ok) {
        mask |= kResetBlockHeartbeatLoss;
    }
    if (!mtr_ack_confirmed) {
        mask |= kResetBlockMtrEstopActive;
    }
    if (transient_inhibited()) {
        mask |= kResetBlockTransientInhibit;
    }
    return mask;
}


// Aggregate "brake/traction fault" for operator feedback (ready bulb, 0x600
// diag). Includes B-class inhibits (MTR feedback / SEB comms loss) because the
// vehicle is not ready to drive while brake availability or propulsion health
// is unconfirmed.
inline bool traction_fault_present() { return any_inhibit(); }

// Resolved actuator authority given the system state (issues #5/#7).
// task_mode uses these to decide what 0x110/0x113 must carry so that a
// traction inhibit is an *actuator-level* action, not an internal zero:
//   - 0x110 is clamped to MANUAL whenever ESTOP is latched or any inhibit is
//     active (so MTR sees non-driving mode authority).
//   - 0x113 power is OFF whenever ESTOP is latched or any inhibit is active.
struct ResolvedAuthority {
    bool mode_auto = false;   // transmitted 0x110.mode (true => AUTO)
    bool power_on  = false;   // transmitted 0x113.power_state (true => ON)
};

inline ResolvedAuthority resolve_authority(bool mode_is_estop, bool resolved_mode_auto,
                                           bool power_requested) {
    const bool stop = mode_is_estop || any_inhibit();
    ResolvedAuthority out;
    out.mode_auto = !stop && resolved_mode_auto;
    out.power_on  = !stop && power_requested;
    return out;
}

}  // namespace sys
