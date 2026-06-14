// Safety monitor implementation. Architecture.md §8.6.
// Startup grace: 3000ms. Timeout: 1000ms. Alive counter validation.

#include "safety_monitor.h"

namespace sys {

int64_t g_sys_test_time_us = 0;
int64_t get_time_us() { return g_sys_test_time_us; }

void SafetyMonitor::init() {
    m_estop       = false;
    m_brake_lever = false;
    m_last_hb_us  = 0;
    m_last_hb_ctr = 0;
    m_hb_ever_seen = false;
}

void SafetyMonitor::feed_heartbeat_rt(uint8_t alive_ctr) {
    // Alive counter validation: frozen counter = stuck CAN controller
    if (m_hb_ever_seen && alive_ctr == m_last_hb_ctr) {
        return;  // frozen — don't update timestamp
    }
    m_last_hb_ctr = alive_ctr;
    m_last_hb_us  = get_time_us();
    m_hb_ever_seen = true;
}

bool SafetyMonitor::heartbeat_ok() const {
    int64_t now = get_time_us();

    // Startup grace: if never seen, OK for first 3 seconds
    if (m_last_hb_us == 0)
        return (now < int64_t(kStartupGracePeriodMs) * 1000);

    return (now - m_last_hb_us) < int64_t(kHeartbeatTimeoutMs) * 1000;
}

}  // namespace sys
