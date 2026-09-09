#pragma once
// SysModel — SYS authority/ESTOP peer (LOW bus only).
//
// PROVISIONAL PEER for Phases A/B: its job is to exercise RT's real authority
// and estop handling over the wire with real codecs + real E2E CRC. Phase D
// replaces it with the real extracted SysCore (sys::ModeManager/SafetyMonitor
// already run on-host in native-test/test_vehicle_integration.cpp).
//
// Wire behavior mirrors the vehicle:
//   * 0x7FE SYS_HEARTBEAT   @ 100 ms (advancing alive_ctr, estop/mode bits)
//   * 0x011 SYS_SAFETY_STS  @ 200 ms (advancing counter + real CRC-8; estop bit)
//   * 0x110 SYS_MODE_CMD    @ 100 ms (Manual/Auto authority)
//   * 0x113 SYS_PWR_CMD     @ 100 ms (power request; OFF->ON edge = MTR REARM)
//   * 0x001 SAFETY_ESTOP    broadcast once on the ESTOP press edge.
//
// Operator inputs (debounce/filtering belongs to the future SysCore):
//   estop button press latches ESTOP until START is pressed (SW latch);
//   MODE toggles Manual <-> Auto only while not latched.
#include <cstdint>

#include "protocol/compat/can.hpp"
#include "runtime/sim_clock.h"
#include "runtime/virtual_can.h"

namespace sim {

class SysModel {
public:
    void boot(int64_t now_us) {
        estop_latched_ = false;
        estop_pressed_ = false;
        mode_auto_ = false;
        power_request_ = true;
        next_hb_us_ = now_us + 100'000;
        next_sts_us_ = now_us + 200'000;
        next_mode_us_ = now_us + 100'000;
        next_pwr_us_ = now_us + 100'000;
        roll_sts_ = 0;
        roll_hb_ = 0;
        roll_mode_ = 0;
        roll_pwr_ = 0;
    }

    // ---- operator inputs ----
    void set_estop_button(bool pressed) {
        if (pressed && !estop_pressed_) on_estop_press_edge_ = true;
        estop_pressed_ = pressed;
        if (pressed) estop_latched_ = true;
    }

    void press_start() {
        // START is the operator reset: ESTOP latch -> Manual.
        if (estop_latched_) {
            estop_latched_ = false;
            estop_pressed_ = false;
            mode_auto_ = false;
        }
    }

    // MODE button: Manual <-> Auto toggle (ignored while ESTOP latched).
    void press_mode() {
        if (!estop_latched_) mode_auto_ = !mode_auto_;
    }

    bool estop_active() const noexcept { return estop_latched_; }
    bool mode_auto() const noexcept { return mode_auto_; }

    // Authority output while ESTOP is latched is MANUAL regardless of the
    // operator's mode button (mirrors the real SYS authority resolver clamp).
    bool effective_auto() const noexcept { return mode_auto_ && !estop_latched_; }

    // Force the 0x113 power request (OFF->ON edge = MTR REARM in Phase F).
    void set_power_request(bool on) noexcept { power_request_ = on; }

    void publish(int64_t now_us, VirtualCanBus& bus) {
        if (estop_pressed_ && on_estop_press_edge_) {
            on_estop_press_edge_ = false;
            can::gen::SafetyEstop estop{};
            can::Frame fr;
            if (can::gen::encode_safety_estop(estop, fr) ==
                can::gen::CodecStatus::Ok)
                bus.transmit(Bus::Low, fr);
        }
        if (now_us >= next_hb_us_) {
            next_hb_us_ += 100'000;
            can::gen::SysHeartbeat hb{};
            hb.alive_ctr = roll_hb_++;
            hb.heartbeat_ok = true;
            hb.estop_active = estop_latched_;
            hb.mode_auto = effective_auto();
            hb.can_ok = true;
            can::Frame fr;
            if (can::encode_frame(hb, fr) == can::gen::CodecStatus::Ok)
                bus.transmit(Bus::Low, fr);
        }
        if (now_us >= next_sts_us_) {
            next_sts_us_ += 200'000;
            can::gen::SysSafetySts sts{};
            sts.estop_active = estop_latched_;
            sts.heartbeat_ok = true;
            sts.rolling_counter = roll_sts_++;
            sts.e2e_crc = 0;
            can::Frame tmp;
            if (can::gen::encode_sys_safety_sts(sts, tmp) ==
                can::gen::CodecStatus::Ok) {
                sts.e2e_crc = can::e2e::sys_safety_sts_crc(tmp.data.data());
                can::Frame fr;
                if (can::gen::encode_sys_safety_sts(sts, fr) ==
                    can::gen::CodecStatus::Ok)
                    bus.transmit(Bus::Low, fr);
            }
        }
        if (now_us >= next_mode_us_) {
            next_mode_us_ += 100'000;
            can::gen::SysModeCmd msg{};
            msg.mode = effective_auto();  // 1 = AUTO, 0 = MANUAL
            msg.rolling_counter = roll_mode_++;
            can::Frame fr;
            if (can::gen::encode_sys_mode_cmd(msg, fr) ==
                can::gen::CodecStatus::Ok)
                bus.transmit(Bus::Low, fr);
        }
        if (now_us >= next_pwr_us_) {
            next_pwr_us_ += 100'000;
            can::gen::SysPwrCmd msg{};
            msg.power_state = power_request_;
            msg.rolling_counter = roll_pwr_++;
            can::Frame fr;
            if (can::gen::encode_sys_pwr_cmd(msg, fr) ==
                can::gen::CodecStatus::Ok)
                bus.transmit(Bus::Low, fr);
        }
    }

private:
    bool estop_latched_ = false;
    bool estop_pressed_ = false;
    bool mode_auto_ = false;
    bool power_request_ = true;
    bool on_estop_press_edge_ = false;
    int64_t next_hb_us_ = 0;
    int64_t next_sts_us_ = 0;
    int64_t next_mode_us_ = 0;
    int64_t next_pwr_us_ = 0;
    uint8_t roll_sts_ = 0;
    uint8_t roll_hb_ = 0;
    uint8_t roll_mode_ = 0;
    uint8_t roll_pwr_ = 0;
};

}  // namespace sim
