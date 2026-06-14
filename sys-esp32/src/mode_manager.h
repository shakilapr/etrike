#pragma once
// SYS mode manager — push button debounce, mode transitions.
// MODE button (GPIO11): toggle MANUAL↔AUTO. Ignored in ESTOP.
// START button (GPIO32): ESTOP→MANUAL. No effect otherwise.
// Call tick() at 10 Hz with GPIO readings. Returns true on mode change.

#include <cstdint>
#include "config.h"
#include "can/can_protocol.h"

namespace sys {

class ModeManager {
public:
    void init();

    // Call at 10 Hz. Returns true if mode changed (caller sends CAN 0x110).
    bool tick(bool mode_btn_pressed, bool start_btn_pressed);

    void force_estop();
    void set_from_can(uint8_t m);

    can::Mode mode() const { return m_mode; }
    uint8_t mode_u8() const { return uint8_t(m_mode); }
    const char* name() const;

private:
    void set_mode(can::Mode m) { m_mode = m; }
    static bool falling_edge(bool prev, bool now) { return prev && !now; }

    can::Mode m_mode = can::Mode::Manual;
    int       m_debounce = 0;
    bool      m_prev_mode_btn  = true;  // pull-up: HIGH
    bool      m_prev_start_btn = true;
};

}  // namespace sys
