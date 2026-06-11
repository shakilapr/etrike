#pragma once
// Mode state machine — MANUAL ↔ AUTO (switch), ANY → ESTOP (button/CAN).
// ESTOP cannot be overridden by the mode switch.

#include "can_protocol.h"

namespace sys {

class ModeManager {
public:
    ModeManager() = default;

    void init();                      // Configure mode switch GPIO
    can::Mode current() const;        // Thread-safe
    void set(can::Mode m);            // ESTOP cannot be overridden
    void poll();                      // Read switch, update mode (call @ 10 Hz)

private:
    bool m_prev_switch = false;       // debounce state
};

}  // namespace sys
