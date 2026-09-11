#pragma once

#include <array>
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
        uint32_t ready_at_ms;   // earliest time the frame may start (send + delay + jitter)
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

    // Freeze the payload of frames matching can_id: the first payload seen is
    // replayed on subsequent frames (simulates a stuck rolling counter / stale
    // producer that keeps transmitting). count = frames to freeze
    // (default = until unfreeze/clear_faults).
    void freeze_payload(uint32_t can_id, uint32_t count = 0xFFFFFFFFu);
    void unfreeze(uint32_t can_id);

    // Disconnect / reconnect a node from this bus
    void disconnect(NodeId node);
    void reconnect(NodeId node);
    bool is_disconnected(NodeId node) const;

    // Clear all active faults
    void clear_faults();

    // ── Realistic bus model (opt-in) ────────────────────────────────
    // When enabled, frames are serialized (finite bus time per frame) and
    // delivered in ascending CAN-ID order (arbitration), optionally with
    // self-reception and deterministic jitter. Default OFF preserves the
    // zero-latency behavior used by the signal-flow tests.
    void set_realistic(bool on) { realistic_ = on; }
    bool realistic() const { return realistic_; }
    void set_bitrate(uint32_t bits_per_sec) { bitrate_ = bits_per_sec; }
    void set_jitter_ms(uint32_t jitter_ms) { jitter_ms_ = jitter_ms; }
    void set_self_reception(bool on) { self_reception_ = on; }

    // IDs observed from more than one producing node on this bus (a CAN
    // collision / duplicate-sender condition).
    std::map<uint32_t, uint32_t> conflicts() const;

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
    std::map<uint32_t, std::array<uint8_t, 8>> frozen_payloads_;
    std::map<uint32_t, uint32_t> freeze_counts_;

    // Realistic timing model state
    bool     realistic_{false};
    bool     self_reception_{false};
    uint32_t bitrate_{500000};
    uint32_t jitter_ms_{0};
    uint32_t bus_free_ms_{0};
    uint32_t jitter_state_{0x1234567u};
    std::map<uint32_t, std::set<NodeId>> producers_;

    uint32_t current_time_ms_{0};

    bool apply_faults_and_queue(NodeId source, etrike::protocol::Frame frame);
    void deliver(const PendingFrame& item);
    uint32_t tx_time_ms(const etrike::protocol::Frame& frame) const;
};

} // namespace testbench
