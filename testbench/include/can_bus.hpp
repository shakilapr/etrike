#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "protocol/core/frame.hpp"

namespace testbench {

enum class NodeId : uint8_t {
    HOST,
    RT,
    SYS,
    MTR,
    SES,
    SEB,
    RM,
    TEST_HARNESS
};

inline const char* node_name(NodeId id) {
    switch (id) {
        case NodeId::HOST: return "HOST";
        case NodeId::RT:   return "RT";
        case NodeId::SYS:  return "SYS";
        case NodeId::MTR:  return "MTR";
        case NodeId::SES:  return "SES";
        case NodeId::SEB:  return "SEB";
        case NodeId::RM:   return "RM";
        case NodeId::TEST_HARNESS: return "HARNESS";
        default: return "UNKNOWN";
    }
}

using FrameCallback = std::function<void(const etrike::protocol::Frame&)>;

class ICanBus {
public:
    virtual ~ICanBus() = default;

    // Send a frame from a specific source node onto the bus.
    virtual bool send(NodeId source, const etrike::protocol::Frame& frame) = 0;

    // Register a subscriber callback for a node on this bus.
    virtual void register_node(NodeId id, FrameCallback cb) = 0;

    // Progress time and deliver any queued / in-flight frames.
    virtual void tick(uint32_t now_ms, uint32_t dt_ms) = 0;

    // Human-readable bus name (e.g. "HIGH_CAN", "LOW_CAN").
    virtual const std::string& name() const = 0;
};

} // namespace testbench
