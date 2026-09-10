#pragma once

#include <cstdint>
#include <atomic>
#include "config.h"
#include "shared_config.h"

namespace sys {

/// State machine for MTR ESTOP acknowledgment monitoring (BUG-03).
/// Tracks pending ACK from 0x206, maintains deadline & retry counter,
/// clears immediately upon receiving ESTOP_ACTIVE, and escalates to
/// latched fault only when retries are exhausted.
class MtrEstopAckWatchdog {
public:
    enum class Action : uint8_t {
        None,
        Confirmed,
        Retry,
        ExhaustedFault,
    };

    void trigger(uint32_t now_tick, uint8_t current_mtr_fault_flags) {
        m_ack_received.store(false, std::memory_order_release);
        if (current_mtr_fault_flags & shared::kMtrFaultEstopActive) {
            // Already acknowledged by MTR
            m_pending.store(false, std::memory_order_release);
            m_ack_received.store(true, std::memory_order_release);
            return;
        }
        m_pending.store(true, std::memory_order_release);
        m_deadline.store(now_tick + static_cast<uint32_t>(sys::kMtrEstopAckTimeoutMs),
                         std::memory_order_release);
        m_retries_left.store(static_cast<uint8_t>(sys::kMtrEstopAckMaxRetries),
                             std::memory_order_release);
    }

    void on_feedback_received(uint8_t mtr_fault_flags) {
        if (mtr_fault_flags & shared::kMtrFaultEstopActive) {
            m_pending.store(false, std::memory_order_release);
            m_ack_received.store(true, std::memory_order_release);
        }
    }

    Action check_tick(uint32_t now_tick, uint8_t mtr_fault_flags) {
        if (!m_pending.load(std::memory_order_acquire)) {
            return Action::None;
        }

        if (mtr_fault_flags & shared::kMtrFaultEstopActive) {
            m_pending.store(false, std::memory_order_release);
            m_ack_received.store(true, std::memory_order_release);
            return Action::Confirmed;
        }

        if (now_tick >= m_deadline.load(std::memory_order_relaxed)) {
            uint8_t retries = m_retries_left.load(std::memory_order_relaxed);
            if (retries > 0) {
                m_retries_left.store(retries - 1, std::memory_order_relaxed);
                m_deadline.store(now_tick + static_cast<uint32_t>(sys::kMtrEstopAckTimeoutMs),
                                 std::memory_order_release);
                return Action::Retry;
            } else {
                m_pending.store(false, std::memory_order_release);
                m_latched_fault.store(true, std::memory_order_relaxed);
                return Action::ExhaustedFault;
            }
        }

        return Action::None;
    }

    bool is_pending() const {
        return m_pending.load(std::memory_order_acquire);
    }

    bool has_acknowledged() const {
        return m_ack_received.load(std::memory_order_acquire);
    }

    uint8_t retries_left() const {
        return m_retries_left.load(std::memory_order_relaxed);
    }

    uint32_t deadline() const {
        return m_deadline.load(std::memory_order_relaxed);
    }

    bool has_latched_fault() const {
        return m_latched_fault.load(std::memory_order_relaxed);
    }

    void reset() {
        m_pending.store(false, std::memory_order_release);
        m_ack_received.store(false, std::memory_order_release);
        m_deadline.store(0, std::memory_order_release);
        m_retries_left.store(0, std::memory_order_release);
        m_latched_fault.store(false, std::memory_order_relaxed);
    }

private:
    std::atomic<bool>     m_pending{false};
    std::atomic<bool>     m_ack_received{false};
    std::atomic<uint32_t> m_deadline{0};
    std::atomic<uint8_t>  m_retries_left{0};
    std::atomic<bool>     m_latched_fault{false};
};

}  // namespace sys
