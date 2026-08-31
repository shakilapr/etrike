#pragma once
// Typed cross-domain messages. These are the values exchanged between
// functional domains through ipc channels. Domain logic itself never
// references ipc; the app orchestrators route these.

#include <cstdint>
#include "core/types.h"
#include "core/result.h"

namespace rta {

// Mode change event (body -> motion / tx).
struct ModeEvent {
    Mode mode = Mode::Manual;
    bool valid = false;
};

// ESTOP event (safety -> all domains).
struct EstopEvent {
    bool    active = false;       // true = ESTOP latched
    std::uint8_t reason = 0;      // rta::kEstopReason*
    bool    obstacle_triggered = false;
    bool    valid = false;
};

// Per-unit health (each functional unit publishes its alive/health).
struct UnitHealth {
    std::uint32_t unit_id = 0;    // e.g., 0=can_rx_low, ...
    bool          alive = true;
    bool          ok = false;
};

// Aggregate health snapshot (heartbeat aggregates per-core health).
struct HealthSnapshot {
    bool    task_ok = true;
    bool    can_low_ok = true;
    bool    can_high_ok = true;
    std::uint8_t alive_ctr = 0;   // per-bus counters maintained by caller
    bool    valid = false;
};

}  // namespace rta
