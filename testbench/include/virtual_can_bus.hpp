#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "can_bus.hpp"

namespace testbench {

class VirtualCanBus : public ICanBus {
public:
    struct PendingFrame {
        uint32_t deliver_at_ms;
        NodeId source;
        etrike::protocol::Frame frame;
    };

    struct TraceEntry {
        uint32_t timestamp_ms;
        NodeId source;
        etrike::protocol::Frame frame;
    };

    explicit VirtualCanBus(std::string name);
    ~VirtualCanBus() override = default;

    // ── ICanBus Implementation ──────────────────────────────────────
    bool send(NodeId source, const etrike::protocol::Frame& frame) override;
    void register_node(NodeId id, FrameCallback cb) override;
    void tick(uint32_t now_ms, uint32_t dt_ms) override;
    const std::string& name() const override { return name_; }

    // ── Fault Injection Engine ──────────────────────────────────────
    // Drop next 'count' frames matching can_id
    void drop(uint32_t can_id, uint32_t count = 1);

    // Delay frames matching can_id by delay_ms
    void delay(uint32_t can_id, uint32_t delay_ms);

    // Corrupt a specific byte of next frame matching can_id
    void corrupt_byte(uint32_t can_id, uint8_t byte_idx, uint8_t mask = 0xFF);

    // Disconnect / reconnect a node from this bus
    void disconnect(NodeId node);
    void reconnect(NodeId node);
    bool is_disconnected(NodeId node) const;

    // Clear all active faults
    void clear_faults();

    // ── Trace & Assertion Helpers ───────────────────────────────────
    const std::vector<TraceEntry>& trace() const { return trace_; }
    void clear_trace() { trace_.clear(); }
    size_t count_frames(uint32_t can_id) const;
    std::vector<etrike::protocol::Frame> find_frames(uint32_t can_id) const;
    bool has_frame(uint32_t can_id) const;

private:
    std::string name_;
    std::map<NodeId, FrameCallback> subscribers_;
    std::deque<PendingFrame> pending_queue_;
    std::vector<TraceEntry> trace_;
    std::set<NodeId> disconnected_nodes_;

    // Fault state
    std::map<uint32_t, uint32_t> drop_counts_;
    std::map<uint32_t, uint32_t> delays_ms_;
    struct ByteCorruption {
        uint8_t index;
        uint8_t mask;
    };
    std::map<uint32_t, ByteCorruption> corruptions_;

    uint32_t current_time_ms_{0};

    bool apply_faults_and_queue(NodeId source, etrike::protocol::Frame frame);
};

} // namespace testbench
