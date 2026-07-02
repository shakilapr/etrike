#pragma once
// Safety monitor — ESTOP button, brake lever, heartbeat watchdog.
// Runs at priority 5 (life-critical). Architecture.md §8.6.

#include <atomic>
#include <cstdint>
#include "config.h"

namespace sys {

class SafetyMonitor {
public:
    void init();

    // GPIO state (polled by caller)
    bool estop_active() const         { return m_estop; }
    bool brake_lever_pressed() const  { return m_brake_lever; }

    // Set from GPIO reads each tick
    void set_estop(bool active)       { m_estop = active; }
    void set_brake_lever(bool pressed){ m_brake_lever = pressed; }

    // Feed RT heartbeat alive counter (call on each valid 0x7FD receipt)
    void feed_heartbeat_rt(uint8_t alive_ctr);

    // Returns true if RT heartbeat is fresh (within timeout, startup grace applied)
    bool heartbeat_ok() const;

private:
    // Shared state: read by multiple tasks (brake, lights, can_tx, diag, etc.),
    // written only by safety_task.  Must be atomic to prevent data races.
    std::atomic<bool> m_estop       {false};
    std::atomic<bool> m_brake_lever {false};
    std::atomic<int64_t>  m_last_hb_us{0};
    uint8_t  m_last_hb_ctr = 0;
    std::atomic<bool> m_hb_ever_seen{false};
};

// Returns monotonic microseconds (host: stub with incrementing counter)
int64_t get_time_us();
// Host test-time injection (set before calling feed_heartbeat_rt / heartbeat_ok).
// Only used when TESTING is defined; otherwise esp_timer_get_time() is used.
extern int64_t g_sys_test_time_us;

}  // namespace sys
