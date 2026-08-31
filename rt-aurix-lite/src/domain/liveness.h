#pragma once
// Liveness supervision — frozen-counter detection + timeout logic.
// Pure domain logic: no CAN IDs, no IPC, no logging, no clock reads.
// Time is passed in as TimeUs.

#include <cstdint>
#include "core/time.h"

namespace rta {

// Tracks a peer's heartbeat liveness via a wrapping alive counter.
// A "frozen" counter (no advance within the timeout) is treated as lost.
class LivenessMonitor {
public:
    LivenessMonitor() = default;

    // observe: record the latest counter value seen from the peer.
    //   now_us    : current monotonic time.
    //   counter   : peer alive counter (8-bit wrapping).
    //   timeout_us: maximum allowed gap between counter advances.
    // Returns true if the peer was already considered lost (i.e., a fresh
    // frame after a timeout — caller may log recovery).
    bool observe(TimeUs now_us, std::uint8_t counter, TimeUs timeout_us) noexcept {
        if (m_last_counter_valid && counter == m_last_counter) {
            // Counter unchanged — check staleness.
            m_lost = (now_us - m_last_seen_us) > timeout_us;
            return false;
        }
        // Counter advanced (or first frame) — peer is alive.
        m_last_counter = counter;
        m_last_counter_valid = true;
        m_last_seen_us = now_us;
        bool was_lost = m_lost;
        m_lost = false;
        return was_lost;
    }

    // alive: whether the peer is currently considered alive.
    bool alive() const noexcept { return !m_lost; }

    // last_seen_us: time of last valid (advancing) frame.
    TimeUs last_seen_us() const noexcept { return m_last_seen_us; }

    void reset() noexcept {
        m_last_counter_valid = false;
        m_lost = false;
        m_last_seen_us = 0;
        m_last_counter = 0;
    }

private:
    bool     m_last_counter_valid = false;
    bool     m_lost = false;
    TimeUs   m_last_seen_us = 0;
    std::uint8_t m_last_counter = 0;
};

// Simple freshness timeout: tracks the last time a frame was seen and
// reports whether it has gone stale. Used for frames without a counter
// (e.g., MTR 0x206 liveness via staleness).
class FreshnessMonitor {
public:
    FreshnessMonitor() = default;

    void observe(TimeUs now_us) noexcept {
        m_last_seen_us = now_us;
        m_ever_seen = true;
    }

    // stale: true if the frame has not been seen within timeout_us.
    bool stale(TimeUs now_us, TimeUs timeout_us) const noexcept {
        if (!m_ever_seen) return true;  // never seen -> stale
        return (now_us - m_last_seen_us) > timeout_us;
    }

    TimeUs last_seen_us() const noexcept { return m_last_seen_us; }
    bool ever_seen() const noexcept { return m_ever_seen; }

    void reset() noexcept {
        m_ever_seen = false;
        m_last_seen_us = 0;
    }

private:
    bool   m_ever_seen = false;
    TimeUs m_last_seen_us = 0;
};

}  // namespace rta
