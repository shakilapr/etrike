/*
 * virtual_can_bus.h — Thread-safe virtual CAN bus for host simulation.
 *
 * Two bus instances (high + low) replicate the physical CAN topology.
 * Each bus is a ring buffer with std::mutex.  Supports fault injection
 * for testing error-recovery paths.
 */
#pragma once

#include <cstdint>
#include <mutex>
#include <array>
#include "protocol/core/frame.hpp"

namespace can {
namespace sim {

enum class FaultType : uint8_t {
    NONE,
    DROP_FRAME,        // drop the next frame queued
    CORRUPT_DATA,      // flip a random byte
    SET_BUS_OFF,       // set error counters to 255 → bus-off
    INJECT_ERROR_FRAME, // lose arbitration (forces retransmit)
};

class VirtualCanBus {
public:
    static constexpr size_t kQueueDepth = 256;

    VirtualCanBus() : m_head(0), m_tail(0), m_count(0),
                      m_tec(0), m_rec(0), m_fault(FaultType::NONE),
                      m_fault_can_id(0) {}

    /* ── CAN operations ──────────────────────────────────── */

    // Send a frame. Returns true if queued.
    bool send(const etrike::protocol::Frame& frame, uint32_t timeout_ms = 0);

    // Receive a frame (non-blocking). Returns true if a frame was available.
    bool receive(etrike::protocol::Frame& out, uint32_t timeout_ms = 0);

    // Check if any frames are waiting.
    bool has_pending() const;

    // Error counters (matching TWAI register semantics).
    void get_error_counters(uint8_t& tec, uint8_t& rec) const {
        tec = m_tec; rec = m_rec;
    }

    /* ── fault injection (test harness) ──────────────────── */

    void inject_fault(FaultType type, uint32_t can_id = 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fault = type;
        m_fault_can_id = can_id;
    }

    void clear_faults() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fault = FaultType::NONE;
        m_fault_can_id = 0;
        m_tec = 0;
        m_rec = 0;
    }

    void set_error_counters(uint8_t tec, uint8_t rec) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tec = tec; m_rec = rec;
    }

    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_count;
    }

private:
    bool apply_fault(etrike::protocol::Frame& frame);

    mutable std::mutex m_mutex;
    std::array<etrike::protocol::Frame, kQueueDepth> m_queue;
    size_t m_head, m_tail, m_count;
    uint8_t m_tec, m_rec;
    FaultType m_fault;
    uint32_t m_fault_can_id;
};

} // namespace sim
} // namespace can
