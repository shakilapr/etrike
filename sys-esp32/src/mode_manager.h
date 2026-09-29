#pragma once
// SYS mode manager — push button debounce, mode transitions.
// MODE button (GPIO11): toggle MANUAL↔AUTO (AUTO only while run enabled);
//   held kEstopLongPressMs (5 s) runs the validated reset transaction
//   (exits ESTOP, or clears a latched fault while in MANUAL).
// START button (GPIO41): NC latching run/enable control. Latched → run
//   enabled; released (unlatch) → motion inhibited (NOT an ESTOP).
// Call tick() at 10 Hz with normalized GPIO readings (true = pressed/latched).

#include <atomic>
#include <cstdint>
#include "config.h"
#include "inhibit_state.h"

#ifndef ENABLE_CAN_HMI
#define ENABLE_CAN_HMI true
#endif

namespace sys {

class ModeManager {
public:
    void init();

    // Call at 10 Hz. Returns true if mode changed (caller sends CAN 0x110).
    // hw_estop_active gates the MODE long-press reset so the operator cannot
    // clear ESTOP while the physical mushroom is still asserted.
    bool tick(bool mode_btn_pressed, bool start_btn_pressed,
              bool hw_estop_active = false);

    void force_estop();
    void set_from_can(uint8_t m);

    // START run/enable latch: true while the operator START button is latched.
    // task_mode feeds this into resolve_authority() so releasing START drops
    // 0x113 power and clamps 0x110 to MANUAL without latching ESTOP.
    bool run_enabled() const { return m_run_enabled.load(std::memory_order_relaxed); }

    // Validated ESTOP→MANUAL reset (issue #7). Returns false — and remains in
    // ESTOP — unless every *currently latched* safety fault's underlying cause
    // is no longer asserted; on success it clears the latched faults and
    // transitions to MANUAL atomically. This is the only path that clears a
    // latched safety fault, so no observer ever sees mode==MANUAL while a
    // latched fault is still set.
    bool try_exit_estop();

    // Validated latched-fault clear while NOT in ESTOP (MANUAL). Same
    // transaction semantics as try_exit_estop(); keeps mode MANUAL on success.
    bool reset_latched_faults_if_clearable();

    // Remote ESTOP reset (BUG-10). Returns false if blocker_mask != 0 or not in ESTOP.
    bool try_exit_estop_remote(uint16_t blocker_mask);

    // Parses incoming 0x111 HMI_MODE_REQ. Returns true if mode changed.
    bool parse_hmi_mode(uint8_t requested_mode);

    // Driver brake takeover: if in AUTO, pulling the handlebar brake lever immediately
    // transitions mode to MANUAL to remove motor propulsion authority.
    bool handle_driver_brake_takeover(bool lever_pressed);

    can::Mode mode() const { return m_mode.load(std::memory_order_relaxed); }
    uint8_t mode_u8() const { return uint8_t(m_mode.load(std::memory_order_relaxed)); }
    const char* name() const;

    // System ESTOP latch predicate (safety invariant, issue #4).
    // The persistent ESTOP state SYS publishes (0x011.estop_active and
    // 0x7FE.estop_active) must reflect the *system* latch, not merely the
    // hardware ESTOP button. A software ESTOP (CAN 0x001, SEB L3, EGAS,
    // bus-off, MTR-reported-ESTOP) latches ModeManager into ESTOP; while that
    // latch is held, estop_active MUST be 1 so downstream (RT/MTR) cannot
    // two-frame-clear into a false all-clear. It only drops to 0 once SYS has
    // been explicitly reset out of ESTOP.
    static constexpr bool estop_latched(can::Mode mode, bool hw_estop) {
        return mode == can::Mode::Estop || hw_estop;
    }

private:
    void set_mode(can::Mode m) { m_mode.store(m, std::memory_order_relaxed); }
    static bool falling_edge(bool prev, bool now) { return prev && !now; }
    static bool rising_edge(bool prev, bool now) { return !prev && now; }

    std::atomic<can::Mode> m_mode{can::Mode::Manual};
    std::atomic<bool> m_run_enabled{false};
    int       m_debounce = 0;
    bool      m_prev_mode_btn  = true;  // pull-up: HIGH
    bool      m_prev_start_btn = true;
    bool      m_start_seeded   = false;  // ignore a START already latched at boot
    int       m_estop_longpress_ctr = 0;  // tracks MODE btn hold for reset
};

}  // namespace sys

