#pragma once
// MTR STM32G431 — Motor Actuation Supervisor & Telemetry Engine
// Handles speed mapping to DAC, relay coordination, ESTOP monitoring, and CAN TX.

#include <cstdint>
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
        current_mode_ = can::Mode::Manual;
    }

    // Process incoming CAN frames
    void handle_frame(const can::Frame& frame, uint32_t now_ms) {
        last_rx_ms_ = now_ms;
        first_frame_seen_ = true;

        switch (frame.id) {
        case can::kIdSafetyEstop: { // 0x001 DLC 0
            trigger_estop();
            break;
        }

        case can::kIdSysModeCmd: { // 0x110
            can::gen::SysModeCmd mode_cmd{};
            if (can::gen::decode_sys_mode_cmd(frame.view(), mode_cmd) == can::gen::CodecStatus::Ok) {
                current_mode_ = static_cast<can::Mode>(mode_cmd.mode);
                if (current_mode_ == can::Mode::Estop) {
                    trigger_estop();
                }
            }
            break;
        }

        case can::kIdRtDriveCmd: { // 0x204 (motor_speed_mmps int32, gear uint8)
            can::gen::RtDriveCmd cmd{};
            if (can::gen::decode_rt_drive_cmd(frame.view(), cmd) == can::gen::CodecStatus::Ok) {
                target_speed_mmps_ = cmd.motor_speed_mmps;
                target_gear_ = static_cast<can::Gear>(cmd.gear);
                legacy_mode_active_ = false;
            }
            break;
        }

        case 0x0BB: { // Legacy Relay State (fallback)
            uint8_t b = frame.data[0];
            uint8_t mode = b & 0x0F;
            if (mode == 0x03) {
                target_gear_ = can::Gear::N;
            } else if (mode == 0x05) {
                target_gear_ = can::Gear::D;
            } else if (mode == 0x09) {
                target_gear_ = can::Gear::R;
            } else {
                target_gear_ = can::Gear::N;
            }
            break;
        }

        case 0x0AA: { // Legacy 16-bit Throttle (fallback)
            uint16_t raw = (static_cast<uint16_t>(frame.data[0]) << 8) | frame.data[1];
            // Convert 16-bit raw to DAC code (same formula as legacy: (val + 8) >> 4)
            legacy_dac_code_ = (raw + 8) >> 4;
            legacy_mode_active_ = true;
            break;
        }

        default:
            break;
        }
    }

    // Periodic evaluation (called at 5 ms rate)
    void tick(uint32_t now_ms) {
        // 1. Check Comms Watchdog (500 ms)
        if (first_frame_seen_ && (now_ms - last_rx_ms_ > kWatchdogTimeoutMs)) {
            trigger_estop();
        }

        // 2. Evaluate Actuation
        if (estop_active_) {
            relays_.set_state(RelayController::State::Off);
            dac_.force_zero();
            return;
        }

        // Drive or Reverse active
        bool drive_enabled = (target_gear_ == can::Gear::D || target_gear_ == can::Gear::S);
        bool reverse_enabled = (target_gear_ == can::Gear::R);
        bool neutral_active = (target_gear_ == can::Gear::N);

        // Update Relays
        relays_.set_gear(target_gear_, true);

        // Update Throttle DAC
        if (legacy_mode_active_) {
            bool throttle_allowed = (target_gear_ != can::Gear::N);
            dac_.set_throttle(legacy_dac_code_, throttle_allowed);
        } else {
            int32_t eff_speed = drive_enabled ? target_speed_mmps_ : (reverse_enabled ? -target_speed_mmps_ : 0);
            if (eff_speed <= 0) {
                // If commanded speed is positive in reverse, accept it as magnitude as well
                if (reverse_enabled && target_speed_mmps_ > 0) {
                    eff_speed = target_speed_mmps_;
                }
            }

            if (neutral_active || eff_speed <= 0) {
                dac_.force_zero();
            } else {
                uint16_t code = calculate_dac_code_(eff_speed, drive_enabled, reverse_enabled);
                dac_.set_throttle(code, true);
            }
        }
    }

    void trigger_estop() {
        estop_active_ = true;
        target_speed_mmps_ = 0;
        target_gear_ = can::Gear::N;
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
    can::Mode current_mode_{can::Mode::Manual};

    bool legacy_mode_active_{false};
    uint16_t legacy_dac_code_{0};
};

}  // namespace mtr
