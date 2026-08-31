#pragma once
// HAL interface — CAN. Pure abstract interface. The host simulation
// implements it over virtual_can; the target AURIX backend implements it
// over iLLD MCMCAN. The portable layer exposes semantic urgency
// (TxClass::Urgent) rather than mailbox-level details.

#include <cstdint>
#include "protocol/route_table.h"
#include "protocol/core/frame.hpp"

namespace rta::hal {

enum class TxClass : std::uint8_t {
    Normal,
    Urgent,   // ESTOP / safety-critical — target routes to the proven emergency resource
};

// Two logical CAN buses (low = actuators, high = Jetson).
// Re-export Bus from route_table for convenience.
using Bus = ::rta::Bus;

class Can {
public:
    virtual ~Can() = default;

    // Transmit a frame on the given bus. Returns true if queued/accepted.
    virtual bool transmit(Bus bus, const etrike::protocol::Frame& frame,
                          TxClass cls = TxClass::Normal) = 0;

    // Receive a frame from the given bus (non-blocking). Returns true if
    // a frame was available.
    virtual bool receive(Bus bus, etrike::protocol::Frame& out) = 0;

    // CAN error counters for a bus (TEC/REC). 0/0 = healthy.
    virtual void error_counters(Bus bus, std::uint8_t& tec, std::uint8_t& rec) const = 0;

    // Whether a bus is currently off.
    virtual bool bus_off(Bus bus) const = 0;
};

}  // namespace rta::hal
