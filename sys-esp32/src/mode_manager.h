#pragma once
// SYS mode manager — push button debounce, mode transitions.
// MODE button (GPIO11): toggle MANUAL↔AUTO. Ignored in ESTOP.
// START button (GPIO32): ESTOP→MANUAL. No effect otherwise.
// Call tick() at 10 Hz with GPIO readings. Returns true on mode change.

#include <atomic>
#include <cstdint>
#include "config.h"

#define ENABLE_CAN_HMI true

namespace sys {

class ModeManager {
public:
    void init();

    // Call at 10 Hz. Returns true if mode changed (caller sends CAN 0x110).
    bool tick(bool mode_btn_pressed, bool start_btn_pressed);

    void force_estop();
    void set_from_can(uint8_t m);
    
    // Parses incoming 0x111 HMI_MODE_REQ. Returns true if mode changed.
    bool parse_hmi_mode(uint8_t requested_mode);

    can::Mode mode() const { return m_mode.load(std::memory_order_relaxed); }
    uint8_t mode_u8() const { return uint8_t(m_mode.load(std::memory_order_relaxed)); }
    const char* name() const;

    // System ESTOP latch predicate (safety invariant, issue #4).
    // The persistent ESTOP state SYS publishes (0x011.estop_active and
    // 0x7FE.estop_active) must reflect the *system* latch, not merely the
    // hardware ESTOP button. A software ESTOP (CAN 0x001, SEB L3, EGAS,
    // bus-off, MTR-reported-ESTOP) latches ModeManager into ESTOP; while
    // that latch is held estop_active MUST be 1 so downstream (RT/MTR)
    // cannot two-frame-clear into a false all-clear. It only drops to 0
    // once SYS has been explicitly reset out of ESTOP.
    static constexpr bool estop_latched(can::Mode mode, bool hw_estop) {
        return mode == can::Mode::Estop || hw_estop;
    }

private:
    void set_mode(can::Mode m) { m_mode.store(m, std::memory_order_relaxed); }
    static bool falling_edge(bool prev, bool now) { return prev && !now; }

    std::atomic<can::Mode> m_mode{can::Mode::Manual};
    int       m_debounce = 0;
    bool      m_prev_mode_btn  = true;  // pull-up: HIGH
    bool      m_prev_start_btn = true;
    int       m_estop_longpress_ctr = 0;  // tracks MODE btn hold during ESTOP
};

}  // namespace sys
