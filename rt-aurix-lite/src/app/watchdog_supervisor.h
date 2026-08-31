#pragma once
// Watchdog health supervisor — decides whether the system is healthy
// enough to service the external watchdog (TPS3850-Q1). It does NOT
// toggle WDI; that is the hal/target responsibility.
// Pure domain logic: no logging, no clock reads (TimeUs passed in).

#include <cstdint>
#include "core/time.h"
#include "config/timing_config.h"

namespace rta {

// Health decision for the external watchdog service.
enum class WatchdogDecision : std::uint8_t {
    ServiceAllowed,
    ServiceDenied,
};

class WatchdogSupervisor {
public:
    // Reset health tracking.
    void init() noexcept {
        m_last_host_cmd_us = 0;
        m_host_cmd_seen = false;
    }

    // Record that a fresh host command/heartbeat was received.
    void note_host_alive(TimeUs now_us) noexcept {
        m_last_host_cmd_us = now_us;
        m_host_cmd_seen = true;
    }

    // Decide whether the watchdog may be serviced.
    //   now_us     : current time.
    //   task_ok    : all functional units reported healthy this cycle.
    //   can_ok     : CAN error counters within limits (both buses).
    bool service_allowed(TimeUs now_us, bool task_ok, bool can_ok) const noexcept {
        // Deny if any unit is unhealthy or CAN is degraded.
        if (!task_ok || !can_ok) return false;
        // Deny if no host command has ever arrived (system not engaged).
        if (!m_host_cmd_seen) return false;
        // Deny if the host command is stale (> kHostCmdStaleTimeoutMs).
        if (now_us - m_last_host_cmd_us > static_cast<TimeUs>(kHostCmdStaleTimeoutMs) * 1000) {
            return false;
        }
        return true;
    }

private:
    TimeUs m_last_host_cmd_us = 0;
    bool   m_host_cmd_seen = false;
};

}  // namespace rta
