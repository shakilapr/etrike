// Safety monitor implementation. Architecture.md §8.6.
// Startup grace: 3000ms. Timeout: 200ms. Alive counter validation.

#include "safety_monitor.h"
#ifndef TESTING
#include "esp_timer.h"
#endif

namespace sys {

int64_t g_sys_test_time_us = 0;
int64_t get_time_us() {
#ifdef TESTING
    return g_sys_test_time_us;
#else
    return esp_timer_get_time();
#endif
}

void SafetyMonitor::init() {
    m_estop       = false;
    m_brake_lever = false;
    m_last_hb_us.store(0);
    m_last_hb_ctr = 0;
    m_hb_ever_seen = false;
}

void SafetyMonitor::feed_heartbeat_rt(uint8_t alive_ctr) {
    // Alive counter validation: frozen counter = stuck CAN controller
    if (m_hb_ever_seen.load(std::memory_order_relaxed) && alive_ctr == m_last_hb_ctr) {
        return;  // frozen — don't update timestamp
    }
    m_last_hb_ctr = alive_ctr;
    m_last_hb_us.store(get_time_us());
    m_hb_ever_seen.store(true, std::memory_order_relaxed);
}

bool SafetyMonitor::heartbeat_ok() const {
    int64_t now = get_time_us();

    // Startup grace: if never seen, OK for first 3 seconds
    int64_t last = m_last_hb_us.load();
    if (last == 0)
        return (now < int64_t(shared::kStartupGracePeriodMs) * 1000);

    return (now - last) < int64_t(kHeartbeatTimeoutMsRt) * 1000;
}

}  // namespace sys
