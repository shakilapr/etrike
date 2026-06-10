#pragma once
// Safety monitor — E-stop button, brake lever, heartbeat watchdog.
// Calls mode_set(ESTOP) on fault.  Runs at priority 5 (life-critical).

#include <cstdint>

namespace sys {

class SafetyMonitor {
public:
    SafetyMonitor() = default;

    void init();
    bool estop_active() const;         // GPIO1 LOW
    bool brake_lever_pressed() const;  // GPIO2 LOW

    // Heartbeat tracking — call on each RT inter-MCU heartbeat receipt.
    void feed_heartbeat_rt();
    void feed_heartbeat_jetson();      // Legacy/test hook; no-op in split topology.
    bool heartbeat_ok() const;         // RT link heartbeat within timeout

private:
    int64_t m_last_hb_rt_us     = 0;
};

}  // namespace sys
