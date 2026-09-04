#pragma once
// MTR STM32G431 — Motor Actuation Supervisor & Telemetry Engine
// Handles speed mapping to DAC, relay coordination, ESTOP monitoring, and CAN TX.

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "config.h"
#include "relay_controller.h"
#include "dac_controller.h"
#include "protocol/compat/can.hpp"
#include "shared_config.h"

namespace mtr {

class MotorManager {
public:
    MotorManager(RelayController& relays, DacController& dac)
        : relays_(relays), dac_(dac) {}

    void init() {
        relays_.init();
        dac_.init();
        estop_active_ = false;
        comms_healthy_ = false;
        first_frame_seen_ = false;
        target_speed_mmps_ = 0;
        target_gear_ = can::Gear::N;
        active_gear_ = can::Gear::N;
        shift_dwell_start_ms_ = 0;
        current_mode_ = can::Mode::Manual;
    }

    // Process incoming CAN frames
    void handle_frame(const can::Frame& frame, uint32_t now_ms) {
        last_rx_ms_ = now_ms;
        first_frame_seen_ = true;

        switch (frame.id) {
        case can::kIdSafetyEstop: { // 0x001 DLC 0 (Explicit Emergency Stop)
            trigger_estop();
            break;
        }

        case can::kIdSysModeCmd: { // 0x110
            can::gen::SysModeCmd mode_cmd{};
            if (can::gen::decode_sys_mode_cmd(frame.view(), mode_cmd) == can::gen::CodecStatus::Ok) {
                current_mode_ = static_cast<can::Mode>(mode_cmd.mode);
                if (current_mode_ == can::Mode::Estop) {
                    trigger_estop();
                } else if (current_mode_ == can::Mode::Manual || current_mode_ == can::Mode::Auto) {
                    // Safe mode command clears latched estop
                    clear_estop();
                }
            }
            break;
        }

        case can::kIdRtDriveCmd: { // 0x204 (motor_speed_mmps int32, gear uint8)
            can::gen::RtDriveCmd cmd{};
            if (can::gen::decode_rt_drive_cmd(frame.view(), cmd) == can::gen::CodecStatus::Ok) {
                target_speed_mmps_ = cmd.motor_speed_mmps;
                target_gear_ = static_cast<can::Gear>(cmd.gear);
            }
            break;
        }

        case can::kIdHmiPwrReq: { // 0x112
            can::gen::HmiPwrReq pwr{};
            if (can::gen::decode_hmi_pwr_req(frame.view(), pwr) == can::gen::CodecStatus::Ok) {
                ignition_on_ = (pwr.req_start != 0);
            }
            break;
        }

        case 0x0BBu: { // RM_RELAY_STATE (Legacy fallback compatibility)
            if (frame.dlc >= 1) {
                uint8_t state = frame.data[0];
                if (state == 0x05) { // Drive
                    target_gear_ = can::Gear::D;
                    ignition_on_ = true;
                } else if (state == 0x09) { // Reverse
                    target_gear_ = can::Gear::R;
                    ignition_on_ = true;
                } else if (state == 0x03) { // Park / Neutral
                    target_gear_ = can::Gear::N;
                    ignition_on_ = true;
                } else { // 0x00 Off
                    target_gear_ = can::Gear::N;
                    ignition_on_ = false;
                }
            }
            break;
        }

        case 0x0AAu: { // RM_THROTTLE_RAW (Legacy fallback compatibility)
            if (frame.dlc >= 2) {
                uint16_t raw_throttle = static_cast<uint16_t>(frame.data[0]) |
                                       (static_cast<uint16_t>(frame.data[1]) << 8);
                target_speed_mmps_ = (raw_throttle > 0)
                    ? static_cast<int32_t>((static_cast<uint32_t>(raw_throttle) * kMaxForwardSpeedMmps) / 65535U)
                    : 0;
            }
            break;
        }

        default:
            break;
        }
    }

    // Periodic evaluation (called at 5 ms rate)
    void tick(uint32_t now_ms) {
        // 1. Check Comms Watchdog (500 ms)
        bool comms_timed_out = first_frame_seen_ && (now_ms - last_rx_ms_ > kWatchdogTimeoutMs);

        // 2. Evaluate Actuation (fail-safe on latched ESTOP or active CAN timeout)
        if (estop_active_ || comms_timed_out) {
            relays_.set_state(RelayController::State::Off);
            dac_.force_zero();
            return;
        }

        // Update Relays with live ignition state and direction shift arc-protection
        bool is_direction_shift = (active_gear_ == can::Gear::D && target_gear_ == can::Gear::R) ||
                                  (active_gear_ == can::Gear::R && target_gear_ == can::Gear::D);

        if (is_direction_shift && shift_dwell_start_ms_ == 0) {
            shift_dwell_start_ms_ = now_ms;
            dac_.force_zero();
            relays_.set_gear(can::Gear::N, ignition_on_);
        }

        if (shift_dwell_start_ms_ != 0) {
            if (now_ms - shift_dwell_start_ms_ < kShiftDwellMs) {
                dac_.force_zero();
                return;
            }
            // Dwell complete, allow shift
            shift_dwell_start_ms_ = 0;
            active_gear_ = target_gear_;
        } else {
            active_gear_ = target_gear_;
        }

        relays_.set_gear(active_gear_, ignition_on_);

        if (!ignition_on_) {
            dac_.force_zero();
            return;
        }

        // Update Throttle DAC (canonical path only)
        bool drive_enabled = ignition_on_ && (active_gear_ == can::Gear::D);
        bool reverse_enabled = ignition_on_ && (active_gear_ == can::Gear::R);
        bool neutral_active = (active_gear_ == can::Gear::N) || !ignition_on_;

        int32_t speed_mag = std::abs(target_speed_mmps_);

        if (neutral_active || speed_mag == 0 || (!drive_enabled && !reverse_enabled)) {
            dac_.force_zero();
        } else {
            uint16_t code = calculate_dac_code_(speed_mag, drive_enabled, reverse_enabled);
            dac_.set_throttle(code, true);
        }
    }

    void trigger_estop() {
        estop_active_ = true;
        target_speed_mmps_ = 0;
        target_gear_ = can::Gear::N;
        active_gear_ = can::Gear::N;
        shift_dwell_start_ms_ = 0;
        relays_.set_state(RelayController::State::Off);
        dac_.force_zero();
    }

    void clear_estop() {
        estop_active_ = false;
    }

    // Generate 0x120 SYS_THROTTLE_STS (100 Hz)
    can::Frame build_throttle_status_frame() const {
        can::gen::SysThrottleSts sts{};
        sts.speed_mmps = static_cast<int16_t>(target_speed_mmps_);
        can::Frame fr;
        can::gen::encode_sys_throttle_sts(sts, fr);
        return fr;
    }

    // Generate 0x206 MTR_MOTOR_FBK (50 Hz)
    can::Frame build_motor_feedback_frame() const {
        can::gen::MtrMotorFbk fbk{};
        fbk.actual_speed_mmps = static_cast<int16_t>(target_speed_mmps_);
        fbk.gear_state = static_cast<uint8_t>(relays_.current_gear());

        uint8_t flags = 0;
        if (estop_active_) {
            flags |= shared::kMtrFaultEstopActive; // Redundant ESTOP ACK to SYS (Gap #15)
        }
        flags |= shared::kMtrFaultStartupReady; // Bit 4
        fbk.fault_flags = flags;

        can::Frame fr;
        can::gen::encode_mtr_motor_fbk(fbk, fr);
        return fr;
    }

    bool is_estop_active() const { return estop_active_; }
    int32_t target_speed_mmps() const { return target_speed_mmps_; }
    can::Gear target_gear() const { return target_gear_; }

private:
    uint16_t calculate_dac_code_(int32_t speed_mmps, bool forward, bool reverse) const {
        if (speed_mmps <= 0) return 0;

        int32_t max_speed = forward ? kMaxForwardSpeedMmps : kMaxReverseSpeedMmps;
        float norm = static_cast<float>(speed_mmps) / static_cast<float>(max_speed);
        norm = std::clamp(norm, 0.0f, 1.0f);

        // Linear interpolation across safe active window [kDacMinCode (655), kDacMaxCode (1966)]
        float code_f = static_cast<float>(kDacMinCode) + norm * static_cast<float>(kDacMaxCode - kDacMinCode);
        return static_cast<uint16_t>(code_f);
    }

    RelayController& relays_;
    DacController& dac_;

    bool estop_active_{false};
    bool comms_healthy_{false};
    bool first_frame_seen_{false};
    uint32_t last_rx_ms_{0};

    int32_t target_speed_mmps_{0};
    can::Gear target_gear_{can::Gear::N};
    can::Gear active_gear_{can::Gear::N};
    can::Mode current_mode_{can::Mode::Manual};
    bool ignition_on_{false};

    static constexpr uint32_t kShiftDwellMs{50};
    uint32_t shift_dwell_start_ms_{0};
};

}  // namespace mtr
