#pragma once
// Safety monitor — E-stop button, brake lever, heartbeat watchdog.
// Calls mode_set(ESTOP) on fault. Runs at priority 5 (life-critical).
// TODO Phase 6: full rewrite for unified single-ESP32 (heartbeat from CAN 0x7FF, not inter-MCU)

#include <cstdint>

namespace sys {

class SafetyMonitor {
public:
    SafetyMonitor() = default;

    void init();
    bool estop_active() const;         // GPIO1 LOW
    bool brake_lever_pressed() const;  // GPIO2 LOW

    // Heartbeat tracking — call on each Jetson 0x7FF heartbeat receipt.
    void feed_heartbeat();
    bool heartbeat_ok() const;         // Jetson heartbeat within timeout

private:
    int64_t m_last_hb_us = 0;
};

}  // namespace sys
