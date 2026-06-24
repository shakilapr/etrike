/*
 * virtual_can_bus.cpp — Implementation.
 */
#include "virtual_can_bus.h"
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>

namespace can {
namespace sim {

bool VirtualCanBus::send(const can::Frame& frame, uint32_t timeout_ms) {
    can::Frame f = frame;  // copy before fault injection

    std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (true) {
        if (lock.try_lock()) {
            if (m_count < kQueueDepth) {
                if (!apply_fault(f)) {
                    m_queue[m_tail] = f;
                    m_tail = (m_tail + 1) % kQueueDepth;
                    m_count++;
                }
                // On successful TX: decrease TEC by 1 (min 0)
                if (m_tec > 0) m_tec--;
                lock.unlock();
                return true;
            }
            lock.unlock();
        }

        if (timeout_ms == 0) break;
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::yield();
    }
    return false;  // buffer full
}

bool VirtualCanBus::receive(can::Frame& out, uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (true) {
        if (lock.try_lock()) {
            if (m_count > 0) {
                out = m_queue[m_head];
                m_head = (m_head + 1) % kQueueDepth;
                m_count--;
                lock.unlock();
                return true;
            }
            lock.unlock();
        }

        if (timeout_ms == 0) break;
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::yield();
    }
    return false;  // no frames
}

bool VirtualCanBus::has_pending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count > 0;
}

bool VirtualCanBus::apply_fault(can::Frame& frame) {
    if (m_fault == FaultType::NONE) return false;

    if (m_fault_can_id != 0 && frame.id != m_fault_can_id)
        return false;  // fault targets specific ID, not this one

    switch (m_fault) {
    case FaultType::DROP_FRAME:
        return true;   // discard

    case FaultType::CORRUPT_DATA:
        if (frame.dlc > 0) {
            int idx = rand() % frame.dlc;
            frame.data[idx] ^= 0xFF;
        }
        break;

    case FaultType::SET_BUS_OFF:
        m_tec = 255;
        m_rec = 255;
        break;

    case FaultType::INJECT_ERROR_FRAME:
        // Simulate arbitration loss: increment TEC
        m_tec = (m_tec < 248) ? m_tec + 8 : 255;
        if (m_tec >= 255) m_rec = (m_rec < 128) ? m_rec + 1 : 255;
        break;

    default:
        break;
    }
    return false;
}

} // namespace sim
} // namespace can
