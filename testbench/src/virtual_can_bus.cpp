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

    uint32_t deliver_at = current_time_ms_ + delay;
    pending_queue_.push_back(PendingFrame{deliver_at, source, frame});

    // Record trace
    trace_.push_back(TraceEntry{current_time_ms_, source, frame});

    return true;
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

    // Process all frames due up to now_ms
    std::deque<PendingFrame> remaining;
    while (!pending_queue_.empty()) {
        PendingFrame item = pending_queue_.front();
        pending_queue_.pop_front();

        if (item.deliver_at_ms <= now_ms) {
            // Deliver to all connected subscribers except sender
            for (auto& [node_id, callback] : subscribers_) {
                if (node_id == item.source) {
                    continue; // No self-reception by default
                }
                if (is_disconnected(node_id)) {
                    continue; // Receiver is disconnected
                }
                if (callback) {
                    callback(item.frame);
                }
            }
        } else {
            remaining.push_back(item);
        }
    }
    pending_queue_ = std::move(remaining);
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
