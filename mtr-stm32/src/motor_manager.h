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
#include "stream_validity.h"

namespace mtr {

class MotorManager {
public:
    MotorManager(RelayController& relays, DacController& dac)
        : relays_(relays), dac_(dac) {}

    void init() {
        relays_.init();
        dac_.init();
        estop_active_ = false;
        comms_timed_out_ = false;
        comms_healthy_ = false;
        first_frame_seen_ = false;
        target_speed_mmps_ = 0;
        target_gear_ = can::Gear::N;
        active_gear_ = can::Gear::N;
        shift_dwell_start_ms_ = 0;
        current_mode_ = can::Mode::Manual;
        mode_valid_ = false;
        power_valid_ = false;
        power_state_on_ = false;
        // Bind authority stream supervisors (Low bus, SYS as producer).
        mode_val_.set_key(1 /*low*/, can::kIdSysModeCmd, kAuthFreshMs);
        pwr_val_.set_key(1 /*low*/, can::kIdSysPwrCmd, kAuthFreshMs);
    }

    // Process incoming CAN frames
    void handle_frame(const can::Frame& frame, uint32_t now_ms) {
        last_rx_ms_ = now_ms;
        first_frame_seen_ = true;
        comms_timed_out_ = false;
        comms_healthy_ = true; // Maintain link health flag — cleared in tick() on watchdog expiry

        switch (frame.id) {
        case can::kIdSafetyEstop: { // 0x001 DLC 0 (Explicit Emergency Stop)
            trigger_estop();
            break;
        }

        case can::kIdSysModeCmd: { // 0x110 — authoritative mode command from SYS
            can::gen::SysModeCmd mode_cmd{};
            if (can::gen::decode_sys_mode_cmd(frame.view(), mode_cmd) == can::gen::CodecStatus::Ok) {
                const bool ok = mode_val_.observe(
                    static_cast<std::uint8_t>(mode_cmd.rolling_counter), now_ms);
                mode_valid_ = ok;
                if (!ok) break;  // stale/invalid authority: keep last mode, inhibit drive
                can::Mode prev_mode = current_mode_;
                current_mode_ = static_cast<can::Mode>(mode_cmd.mode);
                if (current_mode_ == can::Mode::Estop) {
                    trigger_estop();
                } else if (prev_mode == can::Mode::Estop &&
                           (current_mode_ == can::Mode::Manual || current_mode_ == can::Mode::Auto)) {
                    // Only clear latched ESTOP on an explicit recovery transition out of Mode::Estop
                    clear_estop();
                }
            }
            break;
        }

        case can::kIdRtDriveCmd: { // 0x204 (motor_speed_mmps int32, gear uint8)
            can::gen::RtDriveCmd cmd{};
            if (can::gen::decode_rt_drive_cmd(frame.view(), cmd) == can::gen::CodecStatus::Ok) {
                // Mode authority lost: inhibit the drive command (no autonomous propulsion).
                if (!mode_valid_) break;
                target_speed_mmps_ = cmd.motor_speed_mmps;
                target_gear_ = static_cast<can::Gear>(cmd.gear);
                // ESTOP reset sequence: Ignition ON, Gear Neutral, speed 0
                if (estop_active_ && ignition_on_ && target_gear_ == can::Gear::N && target_speed_mmps_ == 0) {
                    clear_estop();
                }
            }
            break;
        }

        case can::kIdSysPwrCmd: { // 0x113 — authoritative power command from SYS
            can::gen::SysPwrCmd pwr{};
            if (can::gen::decode_sys_pwr_cmd(frame.view(), pwr) == can::gen::CodecStatus::Ok) {
                const bool ok = pwr_val_.observe(
                    static_cast<std::uint8_t>(pwr.rolling_counter), now_ms);
                power_valid_ = ok;
                if (!ok) break;  // stale/invalid power authority: enter power-safe (zero propulsion)
                power_state_on_ = (pwr.power_state != 0);
                // ESTOP reset sequence: Power ON, Gear Neutral, speed 0
                if (estop_active_ && power_state_on_ && target_gear_ == can::Gear::N && target_speed_mmps_ == 0) {
                    clear_estop();
                }
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
        comms_timed_out_ = first_frame_seen_ && (now_ms - last_rx_ms_ > kWatchdogTimeoutMs);
        if (comms_timed_out_) comms_healthy_ = false;

        // 2. Evaluate Actuation (fail-safe on latched ESTOP, CAN timeout, or lost power authority)
        // Power authority: derive ignition from a valid power command.
        ignition_on_ = power_valid_ && power_state_on_;
        if (estop_active_ || comms_timed_out_ || !power_valid_) {
            target_speed_mmps_ = 0;
            relays_.set_state(RelayController::State::Off);
            dac_.force_zero();
            return;
        }

        // Mode authority lost: inhibit the drive command (zero propulsion) but keep power.
        if (!mode_valid_) {
            target_speed_mmps_ = 0;
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

        // Directional setpoint sign verification:
        // In Drive (D), speed must be non-negative (0..3000 mm/s). Negative values are rejected.
        // In Reverse (R), canonical 0x204 transmits negative speed (-500..0 mm/s) or legacy positive magnitude (<=500 mm/s).
        // A forward setpoint (>500 mm/s) must never drive Reverse.
        int32_t speed_mag = 0;
        if (drive_enabled) {
            if (target_speed_mmps_ > 0) {
                speed_mag = target_speed_mmps_;
            }
        } else if (reverse_enabled) {
            if (target_speed_mmps_ < 0) {
                speed_mag = -target_speed_mmps_;
            } else if (target_speed_mmps_ > 0 && target_speed_mmps_ <= kMaxReverseSpeedMmps) {
                speed_mag = target_speed_mmps_; // Support legacy positive reverse setpoints <= 500 mm/s
            }
        }

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
        bool inhibited = estop_active_ || comms_timed_out_ || !ignition_on_ || (active_gear_ == can::Gear::N);
        sts.speed_mmps = inhibited ? 0 : static_cast<int16_t>(target_speed_mmps_);
        can::Frame fr;
        can::gen::encode_sys_throttle_sts(sts, fr);
        return fr;
    }

    // Generate 0x206 MTR_MOTOR_FBK (50 Hz)
    can::Frame build_motor_feedback_frame() const {
        can::gen::MtrMotorFbk fbk{};
        bool inhibited = estop_active_ || comms_timed_out_ || !ignition_on_ || (active_gear_ == can::Gear::N);
        fbk.actual_speed_mmps = inhibited ? 0 : static_cast<int16_t>(target_speed_mmps_);
        fbk.gear_state = static_cast<uint8_t>(relays_.current_gear());

        uint8_t flags = 0;
        if (estop_active_) {
            flags |= shared::kMtrFaultEstopActive; // Redundant ESTOP ACK to SYS (Gap #15)
        }
        if (comms_timed_out_) {
            flags |= shared::kMtrFaultCmdTimeout; // Command timeout flag (0x02)
        }
        flags |= shared::kMtrFaultStartupReady; // Bit 4
        fbk.fault_flags = flags;

        can::Frame fr;
        can::gen::encode_mtr_motor_fbk(fbk, fr);
        return fr;
    }

    bool is_estop_active() const { return estop_active_; }
    bool is_comms_timed_out() const { return comms_timed_out_; }
    bool is_comms_healthy() const { return comms_healthy_; }
    int32_t target_speed_mmps() const { return target_speed_mmps_; }
    can::Gear target_gear() const { return target_gear_; }

private:
    uint16_t calculate_dac_code_(int32_t speed_mmps, bool forward, bool reverse) const {
        if (speed_mmps <= 0) return 0;

        // Treat speeds below the shared low-speed threshold as zero to avoid
        // requesting the 0.8 V motor idle voltage (kDacMinCode) for tiny setpoints,
        // which would produce deadband jitter or no motion at all.
        if (speed_mmps < static_cast<int32_t>(shared::kLowSpeedThreshMmps)) return 0;

        int32_t max_speed = forward ? kMaxForwardSpeedMmps : kMaxReverseSpeedMmps;
        float norm = static_cast<float>(speed_mmps) / static_cast<float>(max_speed);
        norm = std::clamp(norm, 0.0f, 1.0f);

        // Use a motion-floor slightly above kDacMinCode to clear the actuator deadband.
        // kDacMinCode (655 = 0.8 V) is the idle threshold; true motion begins at ~0.85 V.
        static constexpr uint16_t kDacActiveFloor = 700; // ~0.855 V
        float code_f = static_cast<float>(kDacActiveFloor)
                     + norm * static_cast<float>(kDacMaxCode - kDacActiveFloor);
        return std::clamp(static_cast<uint16_t>(code_f), kDacMinCode, kDacMaxCode);
    }

    RelayController& relays_;
    DacController& dac_;

    bool estop_active_{false};
    bool comms_timed_out_{false};
    bool comms_healthy_{false};
    bool first_frame_seen_{false};
    uint32_t last_rx_ms_{0};

    int32_t target_speed_mmps_{0};
    can::Gear target_gear_{can::Gear::N};
    can::Gear active_gear_{can::Gear::N};
    can::Mode current_mode_{can::Mode::Manual};
    bool ignition_on_{false};

    // Actuator-authority validity (from SYS 0x110/0x113 rolling-counter streams).
    // mode_valid_ false -> inhibit 0x204 drive command (zero propulsion).
    // power_valid_ false -> power-safe (zero propulsion, relays off).
    bool mode_valid_{false};
    bool power_valid_{false};
    bool power_state_on_{false};
    etrike::protocol::StreamValidity mode_val_;
    etrike::protocol::StreamValidity pwr_val_;
    static constexpr uint32_t kAuthFreshMs = can::gen::SysModeCmd::kCycleMs * 5;  // 100ms cycle -> 500ms

    static constexpr uint32_t kShiftDwellMs{50};
    uint32_t shift_dwell_start_ms_{0};
};

}  // namespace mtr
