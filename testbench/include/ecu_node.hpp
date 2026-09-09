#pragma once

#include <cstdint>
#include <string>
#include "can_bus.hpp"

namespace testbench {

class IEcuNode {
public:
    virtual ~IEcuNode() = default;

    virtual NodeId id() const = 0;
    virtual const char* name() const = 0;

    // Reset internal state machines to initial power-on state
    virtual void init() = 0;

    // Periodic time-step tick
    virtual void step(uint32_t now_ms, uint32_t dt_ms) = 0;

    // Receive a frame delivered from a registered CAN bus
    virtual void receive_can(const std::string& bus_name, const etrike::protocol::Frame& frame) = 0;
};

} // namespace testbench
