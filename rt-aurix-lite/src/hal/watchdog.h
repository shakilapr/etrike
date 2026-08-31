#pragma once
// HAL interface — external watchdog (TPS3850-Q1).
//
// The portable WatchdogSupervisor decides *whether* servicing is allowed
// (health). This interface performs the actual hardware action required
// by the configured circuit (e.g., pulse, falling edge, timer-assisted
// waveform). It is NOT a "toggle" — the target implementation owns the
// exact electrical behavior.

namespace rta::hal {

class Watchdog {
public:
    virtual ~Watchdog() = default;

    // Perform the hardware action required to service the external
    // watchdog. The caller is responsible for having verified health
    // (see rta::WatchdogSupervisor::service_allowed).
    virtual void service() = 0;
};

}  // namespace rta::hal
