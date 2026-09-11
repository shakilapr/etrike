#include "virtual_can_bus.hpp"
#include <algorithm>

namespace testbench {

VirtualCanBus::VirtualCanBus(std::string name)
    : name_(std::move(name)) {}

void VirtualCanBus::register_node(NodeId id, FrameCallback cb) {
    subscribers_[id] = std::move(cb);
}

void VirtualCanBus::drop(uint32_t can_id, uint32_t count) {
    drop_counts_[can_id] += count;
}

void VirtualCanBus::delay(uint32_t can_id, uint32_t delay_ms) {
    delays_ms_[can_id] = delay_ms;
}

void VirtualCanBus::corrupt_byte(uint32_t can_id, uint8_t byte_idx, uint8_t mask) {
    corruptions_[can_id] = ByteCorruption{byte_idx, mask};
}

void VirtualCanBus::freeze_payload(uint32_t can_id, uint32_t count) {
    freeze_counts_[can_id] = count;
    frozen_payloads_.erase(can_id);  // first observed frame becomes the template
}

void VirtualCanBus::unfreeze(uint32_t can_id) {
    freeze_counts_.erase(can_id);
    frozen_payloads_.erase(can_id);
}

void VirtualCanBus::disconnect(NodeId node) {
    disconnected_nodes_.insert(node);
}

void VirtualCanBus::reconnect(NodeId node) {
    disconnected_nodes_.erase(node);
}

bool VirtualCanBus::is_disconnected(NodeId node) const {
    return disconnected_nodes_.find(node) != disconnected_nodes_.end();
}

void VirtualCanBus::clear_faults() {
    drop_counts_.clear();
    delays_ms_.clear();
    corruptions_.clear();
    frozen_payloads_.clear();
    freeze_counts_.clear();
    disconnected_nodes_.clear();
}

bool VirtualCanBus::apply_faults_and_queue(NodeId source, etrike::protocol::Frame frame) {
    // If sender is disconnected, frame never leaves
    if (is_disconnected(source)) {
        return false;
    }

    // Check frame drop fault
    auto drop_it = drop_counts_.find(frame.id);
    if (drop_it != drop_counts_.end() && drop_it->second > 0) {
        drop_it->second--;
        if (drop_it->second == 0) {
            drop_counts_.erase(drop_it);
        }
        return false; // Dropped
    }

    // Check corruption fault
    auto corrupt_it = corruptions_.find(frame.id);
    if (corrupt_it != corruptions_.end()) {
        if (corrupt_it->second.index < frame.dlc) {
            frame.data[corrupt_it->second.index] ^= corrupt_it->second.mask;
        }
        corruptions_.erase(corrupt_it);
    }

    // Check freeze fault: replay the first captured payload (stuck counter).
    auto freeze_it = freeze_counts_.find(frame.id);
    if (freeze_it != freeze_counts_.end() && freeze_it->second > 0) {
        auto tmpl = frozen_payloads_.find(frame.id);
        if (tmpl == frozen_payloads_.end()) {
            frozen_payloads_[frame.id] = frame.data;  // capture template
        } else {
            frame.data = tmpl->second;
        }
        if (freeze_it->second != 0xFFFFFFFFu) {
            if (--freeze_it->second == 0) {
                frozen_payloads_.erase(frame.id);
                freeze_counts_.erase(freeze_it);
            }
        }
    }

    // Check delay fault
    uint32_t delay = 0;
    auto delay_it = delays_ms_.find(frame.id);
    if (delay_it != delays_ms_.end()) {
        delay = delay_it->second;
    }

    uint32_t jitter = 0;
    if (realistic_ && jitter_ms_ > 0) {
        jitter_state_ = jitter_state_ * 1664525u + 1013904223u;  // deterministic LCG
        jitter = (jitter_state_ >> 16) % (jitter_ms_ + 1u);
    }

    producers_[frame.id].insert(source);
    uint32_t ready_at = current_time_ms_ + delay + jitter;
    pending_queue_.push_back(PendingFrame{ready_at, source, frame});

    // Record trace
    trace_.push_back(TraceEntry{current_time_ms_, source, frame});

    return true;
}

void VirtualCanBus::deliver(const PendingFrame& item) {
    for (auto& [node_id, callback] : subscribers_) {
        if (node_id == item.source && !self_reception_) {
            continue;  // no self-reception by default
        }
        if (is_disconnected(node_id)) {
            continue;  // receiver is disconnected
        }
        if (callback) {
            callback(item.frame);
        }
    }
}

uint32_t VirtualCanBus::tx_time_ms(const etrike::protocol::Frame& frame) const {
    // Standard frame overhead (SOF/arbitration/control/CRC/ACK/EOF) plus payload,
    // rounded up to the 1 ms tick resolution used by the harness.
    uint32_t bits = 44u + 8u * static_cast<uint32_t>(frame.dlc) + (frame.extended ? 20u : 0u);
    uint32_t t = (bits * 1000u + bitrate_ - 1u) / bitrate_;
    return t == 0u ? 1u : t;
}

std::map<uint32_t, uint32_t> VirtualCanBus::conflicts() const {
    std::map<uint32_t, uint32_t> out;
    for (const auto& [id, sources] : producers_) {
        if (sources.size() > 1) out[id] = static_cast<uint32_t>(sources.size());
    }
    return out;
}

bool VirtualCanBus::send(NodeId source, const etrike::protocol::Frame& frame) {
    return apply_faults_and_queue(source, frame);
}

void VirtualCanBus::tick(uint32_t now_ms, uint32_t dt_ms) {
    (void)dt_ms;
    current_time_ms_ = now_ms;

    if (pending_queue_.empty()) {
        return;
    }

    if (!realistic_) {
        // Zero-latency model: deliver all frames due up to now_ms, in order.
        std::deque<PendingFrame> remaining;
        while (!pending_queue_.empty()) {
            PendingFrame item = pending_queue_.front();
            pending_queue_.pop_front();
            if (item.ready_at_ms <= now_ms) {
                deliver(item);
            } else {
                remaining.push_back(item);
            }
        }
        pending_queue_ = std::move(remaining);
        return;
    }

    // Realistic model: among frames ready to arbitrate, the lowest CAN ID wins
    // each free bus slot; each frame occupies the bus for tx_time_ms.
    std::vector<PendingFrame> ready;
    std::deque<PendingFrame> not_ready;
    while (!pending_queue_.empty()) {
        PendingFrame item = pending_queue_.front();
        pending_queue_.pop_front();
        if (item.ready_at_ms <= now_ms) {
            ready.push_back(item);
        } else {
            not_ready.push_back(item);
        }
    }
    std::sort(ready.begin(), ready.end(), [](const PendingFrame& a, const PendingFrame& b) {
        if (a.frame.id != b.frame.id) return a.frame.id < b.frame.id;  // priority
        return a.ready_at_ms < b.ready_at_ms;
    });
    for (const auto& item : ready) {
        const uint32_t start = std::max(bus_free_ms_, item.ready_at_ms);
        if (start > now_ms) {
            not_ready.push_back(item);  // bus busy until after now
            continue;
        }
        deliver(item);
        bus_free_ms_ = start + tx_time_ms(item.frame);
    }
    pending_queue_ = std::move(not_ready);
}

size_t VirtualCanBus::count_frames(uint32_t can_id) const {
    size_t count = 0;
    for (const auto& entry : trace_) {
        if (entry.frame.id == can_id) {
            count++;
        }
    }
    return count;
}

std::vector<etrike::protocol::Frame> VirtualCanBus::find_frames(uint32_t can_id) const {
    std::vector<etrike::protocol::Frame> res;
    for (const auto& entry : trace_) {
        if (entry.frame.id == can_id) {
            res.push_back(entry.frame);
        }
    }
    return res;
}

bool VirtualCanBus::has_frame(uint32_t can_id) const {
    for (const auto& entry : trace_) {
        if (entry.frame.id == can_id) {
            return true;
        }
    }
    return false;
}

} // namespace testbench
