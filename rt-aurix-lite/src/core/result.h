#pragma once
// Deterministic result carrier: state + fault bitmask. No logging.
// Domain logic returns these; the app/runtime maps them to diagnostics.

#include <cstdint>

namespace rta {

// Fault bitmask — safety/control fault flags. Bit values are stable.
enum Fault : std::uint32_t {
    kFaultNone           = 0u,
    kFaultSteering       = 1u << 0,  // steering FSM in fault/silent
    kFaultBrake          = 1u << 1,  // brake degraded/error
    kFaultEgasMismatch   = 1u << 2,  // EGAS L2 speed mismatch
    kFaultLiveness       = 1u << 3,  // peer heartbeat/liveness lost
    kFaultCommandStale   = 1u << 4,  // host command stale
    kFaultCanBusLow      = 1u << 5,  // CAN_LOW bus-off/repeated error
    kFaultCanBusHigh     = 1u << 6,  // CAN_HIGH bus-off/repeated error
    kFaultInternal       = 1u << 7,  // internal/runtime fault
};

constexpr bool has_fault(std::uint32_t faults, Fault f) noexcept {
    return (faults & static_cast<std::uint32_t>(f)) != 0u;
}

}  // namespace rta
