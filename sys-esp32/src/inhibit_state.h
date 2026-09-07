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

namespace sys {

// Transient / recoverable inhibit reasons (own bit per detector).
enum InhibitReason : uint32_t {
    kInhibitMtrFbkLoss     = 1u << 0,  // 0x206 MTR feedback lost while relevant
    kInhibitSebCommsLoss   = 1u << 1,  // 0x721 SEB status stale
    kInhibitBrakeFollowing = 1u << 2,  // transient brake following excursion
};

// Latched safety faults (cleared only by the explicit reset path).
enum LatchedFaultReason : uint32_t {
    kLatchedBrakeFollowing = 1u << 0,  // confirmed persistent following error
    kLatchedSebL3          = 1u << 1,  // SEB error_status == 3
};

// Defined in main.cpp.
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

inline bool transient_inhibited() { return g_inhibit_reasons.load() != 0u; }
inline bool latched_fault_present() { return g_latched_fault_reasons.load() != 0u; }

}  // namespace sys
