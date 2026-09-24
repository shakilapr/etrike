#pragma once
// RM-ESP32-T12D — Protocol CAN Emitter
// Encodes and emits canonical CAN frames directly using protocol codecs
// across three runtime operating modes: BARE, SYS, and RT.

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "protocol/compat/can.hpp"
#include "protocol/compat/e2e.hpp"
#include "shared_config.h"
#include "config.h"
#include "rc_decoder.h"

namespace rm {

class CanEmitter {
public:
    // Helper: 8-bit Additive Sum Checksum required by physical actuator
    static uint8_t calc_sum8(const uint8_t* data, size_t len) noexcept {
        uint8_t sum = 0;
        for (size_t i = 0; i < len; ++i) sum = static_cast<uint8_t>(sum + data[i]);
        return sum;
    }

    // Called from CAN RX when 0x201 SES_STATUS is received
    void on_ses_status_rx(uint8_t control_mode_status, uint32_t now_ms) noexcept {
        if (!ses_online_) {
            // Reconnected! Trigger 200ms disarm pulse to force a fresh rising edge
            rearm_ses_ticks_ = 20;
        }
        ses_online_ = true;
        ses_mode_status_ = control_mode_status;
        last_0x201_ms_ = now_ms;
    }

    template <typename SendFn>
    void emit_cluster(const RcSnapshot& snap, uint32_t tick_10ms, SendFn&& send) {
        emit_cluster(snap, tick_10ms, 0, send);
    }

    // Sender callback type: returns true if frame successfully queued
    template <typename SendFn>
    void emit_cluster(const RcSnapshot& snap, uint32_t tick_10ms, uint32_t now_ms, SendFn&& send) {
        const bool drive_active = snap.signal_valid && snap.drive_enable_req && !snap.park_hold_req;

        // Auto-detect link loss: if no 0x201 for >250ms, mark offline
        if (now_ms > 0 && last_0x201_ms_ > 0 && (now_ms - last_0x201_ms_ > 250)) {
            ses_online_ = false;
        }

        // Auto re-arm if actuator fell out of Auto mode (e.g. power cycle/reconnect) while drive active
        if (ses_online_ && drive_active && (ses_mode_status_ == 0) && (rearm_ses_ticks_ == 0)) {
            rearm_ses_ticks_ = 20; // 200ms disarm pulse
        }

        if (rearm_ses_ticks_ > 0) {
            --rearm_ses_ticks_;
        }

        switch (snap.op_mode) {
            case OperatingMode::Bare:
                emit_bare(snap, drive_active, tick_10ms, send);
                break;
            case OperatingMode::Sys:
                emit_sys(snap, drive_active, tick_10ms, send);
                break;
            case OperatingMode::Rt:
                emit_rt(snap, drive_active, tick_10ms, send);
                break;
        }
    }

    void reset_counters() noexcept {
        rearm_ses_ticks_ = 20;
        ses_online_ = false;
        last_0x201_ms_ = 0;
        ses_mode_status_ = 0;
        roll_ses_ = 0;
        roll_seb_ = 0;
        roll_sys_mode_ = 0;
        roll_sys_pwr_ = 0;
        roll_hmi_mode_ = 0;
        roll_hmi_pwr_ = 0;
        roll_host_steer_ = 0;
        alive_ctr_rt_ = 0;
        alive_ctr_host_ = 0;
        rearm_pwr_ticks_ = 2;
    }

private:
    uint32_t rearm_ses_ticks_{20};   // Countdown ticks for Control_Enable rising edge (200ms)
    uint32_t last_0x201_ms_{0};       // Timestamp of last received 0x201 SES_STATUS
    uint8_t  ses_mode_status_{0};     // 0 = Manual/Assist, 1 = Auto/Angle Control
    bool     ses_online_{false};
    // ── Mode 1: BARE (Direct Actuator Control on Low-CAN) ────────────
    template <typename SendFn>
    void emit_bare(const RcSnapshot& snap, bool drive_active, uint32_t tick_10ms, SendFn&& send) {
        // 1. Steering: 0x169 VCU_SES_REQ (50 Hz / 20 ms per SES spec)
        if (tick_10ms % 2 == 0) {
            can::custom::ses::Command ses_cmd{};
            ses_cmd.alignment_enable = false;
            // Control enable requires a clean 0 -> 1 rising edge to engage Angle Control Mode
            ses_cmd.control_enable   = (rearm_ses_ticks_ == 0) && drive_active;
            int16_t angle_raw = static_cast<int16_t>(kSbwAngleOffset);
            if (snap.signal_valid) {
                angle_raw = static_cast<int16_t>(std::round(snap.steering_deg * 10.0f)) + static_cast<int16_t>(kSbwAngleOffset);
                angle_raw = std::clamp(angle_raw, kMinSteerRaw, kMaxSteerRaw);
            }
            ses_cmd.target_angle_raw  = angle_raw;
            ses_cmd.target_speed_raw  = 328; // Standard nominal slew rate (within 400 deg/s rating)
            ses_cmd.rolling_counter   = roll_ses_;
            roll_ses_ = (roll_ses_ + 1) & 0x0F;
            // Note 11: Vehicle speed >= 5 km/h during active drive prevents motor shutdown at 0 deg
            ses_cmd.vehicle_speed_raw = drive_active ? 10 : 0;

            can::Frame ses_fr;
            if (can::custom::ses::encode_command(ses_cmd, ses_fr) == can::gen::CodecStatus::Ok) {
                // Hardware protocol requires 8-bit additive sum checksum over bytes 0..6
                ses_fr.data[7] = calc_sum8(ses_fr.data.data(), 7);
                send(ses_fr);
            }
        }

        // 2. Braking: 0x7B9 VCU_SEB_REQ (50 Hz / 20 ms per SEB specification)
        if (tick_10ms % 2 == 0) {
            can::custom::seb::Command seb_cmd{};
            seb_cmd.alignment_enable = false; // Normal braking: alignment MUST be false (true resets zero datum)
            seb_cmd.control_enable   = true;
            seb_cmd.control_mode     = can::custom::seb::ControlMode::Stroke;
            seb_cmd.auto_brake       = false;

            // Stroke encoding: raw = (mm + 30.0) / 0.05; released (0 mm) = 600, full (27 mm) = 1140
            float commanded_stroke = snap.brake_stroke_mm;
            uint16_t stroke_raw = static_cast<uint16_t>(
                std::clamp(std::round((commanded_stroke + 30.0f) * 20.0f), 500.0f, 1140.0f));
            seb_cmd.stroke_request_raw   = stroke_raw;
            seb_cmd.pressure_request_raw = 0;
            seb_cmd.rolling_counter      = roll_seb_;
            roll_seb_ = (roll_seb_ + 1) & 0x0F;

            can::Frame seb_fr;
            if (can::custom::seb::encode_command(seb_cmd, seb_fr) == can::gen::CodecStatus::Ok) {
                // Checksum: additive sum of bytes 0..6, then XOR with 0xFF
                seb_fr.data[7] = static_cast<uint8_t>(calc_sum8(seb_fr.data.data(), 7) ^ 0xFFu);
                send(seb_fr);
            }
        }

        // 3. Traction: 0x204 RT_DRIVE_CMD (100 Hz)
        can::gen::RtDriveCmd drive_cmd{};
        drive_cmd.motor_speed_mmps = drive_active ? snap.target_speed_mmps : 0;
        drive_cmd.gear = static_cast<uint8_t>(drive_active ? snap.gear : can::Gear::N);

        can::Frame drive_fr;
        if (can::gen::encode_rt_drive_cmd(drive_cmd, drive_fr) == can::gen::CodecStatus::Ok) {
            send(drive_fr);
        }

        // 4. Supervisor Emulation (10 Hz Heartbeat): 0x110 SYS_MODE_CMD + 0x113 SYS_PWR_CMD
        if (tick_10ms % 10 == 0) {
            can::gen::SysModeCmd mode_cmd{};
            // MTR STM32 strictly requires AUTO mode (1) in 0x110 to accept CAN 0x204 RT_DRIVE_CMD
            // and actuate the motor via DAC. In MANUAL mode (0), MTR ignores CAN speed commands
            // and only listens to the physical handlebar throttle ADC.
            // RM mimics the autonomous master, so when drive is active (SWA armed, Park released, link valid),
            // we broadcast AUTO (1); when disarmed/idle, we broadcast MANUAL (0) so motor DAC stays safely zeroed.
            mode_cmd.mode = drive_active ? 1u : 0u;
            mode_cmd.rolling_counter = roll_sys_mode_++;
            can::Frame mode_fr;
            if (can::gen::encode_sys_mode_cmd(mode_cmd, mode_fr) == can::gen::CodecStatus::Ok) {
                send(mode_fr);
            }

            can::gen::SysPwrCmd pwr_cmd{};
            // MTR requires a 0x113 OFF->ON power rearm edge after boot/reset
            // (mtr-stm32/src/motor_manager.h:166-180). Force power_state=0 for
            // the first few emissions so a following ON frame completes REARM.
            const bool force_pwr_off = (rearm_pwr_ticks_ > 0);
            if (force_pwr_off) --rearm_pwr_ticks_;
            pwr_cmd.power_state = force_pwr_off ? 0u : (snap.signal_valid && snap.drive_enable_req);
            pwr_cmd.rolling_counter = roll_sys_pwr_++;
            can::Frame pwr_fr;
            if (can::gen::encode_sys_pwr_cmd(pwr_cmd, pwr_fr) == can::gen::CodecStatus::Ok) {
                send(pwr_fr);
            }

            // Emulated SYS safety authority (0x011 SYS_SAFETY_STS). MTR gates
            // ignition on safety_state_valid_, which is set ONLY by 0x011
            // (mtr-stm32/src/motor_manager.h:313/:194-212). Without it MTR never
            // ignites. estop_active=0 emulates an all-clear supervisor; safe-stop
            // is achieved via 0x204 speed=0 + 0x110 MANUAL on link loss.
            can::gen::SysSafetySts safety_sts{};
            safety_sts.estop_active   = 0;
            safety_sts.heartbeat_ok   = snap.signal_valid ? 1 : 0;
            safety_sts.rolling_counter = roll_sys_safety_++;
            can::Frame safety_fr;
            if (can::gen::encode_sys_safety_sts(safety_sts, safety_fr) == can::gen::CodecStatus::Ok) {
                // SysSafetySts carries an AUTOSAR E2E CRC over bytes[0..3]; MTR/RT
                // reject the frame without it (protocol/compat/e2e.hpp).
                safety_fr.data[4] = can::e2e::sys_safety_sts_crc(safety_fr.data.data());
                send(safety_fr);
            }
        }
    }

    // ── Mode 2: SYS (Targeting sys-esp32 on Low-CAN) ─────────────────
    template <typename SendFn>
    void emit_sys(const RcSnapshot& snap, bool drive_active, uint32_t tick_10ms, SendFn&& send) {
        // Direct actuator setpoints (50 Hz / 20 ms per SES spec)
        if (tick_10ms % 2 == 0) {
            can::custom::ses::Command ses_cmd{};
            ses_cmd.alignment_enable = false;
            // Control enable requires a clean 0 -> 1 rising edge to engage Angle Control Mode
            ses_cmd.control_enable   = (rearm_ses_ticks_ == 0) && drive_active;
            int16_t angle_raw = static_cast<int16_t>(kSbwAngleOffset);
            if (snap.signal_valid) {
                angle_raw = static_cast<int16_t>(std::round(snap.steering_deg * 10.0f)) + static_cast<int16_t>(kSbwAngleOffset);
                angle_raw = std::clamp(angle_raw, kMinSteerRaw, kMaxSteerRaw);
            }
            ses_cmd.target_angle_raw  = angle_raw;
            ses_cmd.target_speed_raw  = 328;
            ses_cmd.rolling_counter   = roll_ses_;
            roll_ses_ = (roll_ses_ + 1) & 0x0F;
            // Note 11: Vehicle speed >= 5 km/h during active drive prevents motor shutdown at 0 deg
            ses_cmd.vehicle_speed_raw = drive_active ? 10 : 0;

            can::Frame ses_fr;
            if (can::custom::ses::encode_command(ses_cmd, ses_fr) == can::gen::CodecStatus::Ok) {
                // Hardware protocol requires 8-bit additive sum checksum over bytes 0..6
                ses_fr.data[7] = calc_sum8(ses_fr.data.data(), 7);
                send(ses_fr);
            }
        }

        // Braking: 0x205 RT_BRAKE_CMD (RT brake *intent* in kPa). In SYS mode the
        // real sys-esp32 is the sole 0x7B9 producer (seb.yaml sender=SYS) and
        // applies this 0x205 intent to SEB (sys-esp32/src/main.cpp:892-906). rm
        // emulates RT here, so it must NOT emit 0x7B9 (that would collide with SYS).
        can::gen::RtBrakeCmd brake_cmd{};
        float stroke_fraction = snap.brake_stroke_mm / kMaxBrakeStrokeMm;
        stroke_fraction = std::clamp(stroke_fraction, 0.0f, 1.0f);
        brake_cmd.brake_pressure_kpa =
            static_cast<int32_t>(std::round(stroke_fraction * static_cast<float>(kMaxBrakePressureKpa)));
        brake_cmd.brake_pressure_kpa =
            std::clamp<int32_t>(brake_cmd.brake_pressure_kpa, 0, kMaxBrakePressureKpa);

        can::Frame brake_fr;
        if (can::gen::encode_rt_brake_cmd(brake_cmd, brake_fr) == can::gen::CodecStatus::Ok) {
            send(brake_fr);
        }

        can::gen::RtDriveCmd drive_cmd{};
        drive_cmd.motor_speed_mmps = drive_active ? snap.target_speed_mmps : 0;
        drive_cmd.gear = static_cast<uint8_t>(drive_active ? snap.gear : can::Gear::N);

        can::Frame drive_fr;
        if (can::gen::encode_rt_drive_cmd(drive_cmd, drive_fr) == can::gen::CodecStatus::Ok) {
            send(drive_fr);
        }

        // HMI Requests: 0x111 HMI_MODE_REQ + 0x112 HMI_PWR_REQ (10 Hz)
        if (tick_10ms % 10 == 0) {
            can::gen::HmiModeReq hmi_mode{};
            hmi_mode.req_mode = drive_active;
            hmi_mode.rolling_counter = roll_hmi_mode_++;
            can::Frame hmi_mode_fr;
            if (can::gen::encode_hmi_mode_req(hmi_mode, hmi_mode_fr) == can::gen::CodecStatus::Ok) {
                send(hmi_mode_fr);
            }

            can::gen::HmiPwrReq hmi_pwr{};
            hmi_pwr.req_start = snap.signal_valid && snap.drive_enable_req;
            hmi_pwr.rolling_counter = roll_hmi_pwr_++;
            can::Frame hmi_pwr_fr;
            if (can::gen::encode_hmi_pwr_req(hmi_pwr, hmi_pwr_fr) == can::gen::CodecStatus::Ok) {
                send(hmi_pwr_fr);
            }
        }

        // RT Heartbeat: 0x7FD RT_HEARTBEAT (2 Hz / 500 ms)
        if (tick_10ms % 50 == 0) {
            can::gen::RtHeartbeat rt_hb{};
            rt_hb.alive_ctr = alive_ctr_rt_++;
            rt_hb.health_flags = snap.signal_valid ? 0 : 1;
            can::Frame rt_hb_fr;
            if (can::gen::encode_rt_heartbeat(rt_hb, rt_hb_fr) == can::gen::CodecStatus::Ok) {
                send(rt_hb_fr);
            }
        }
    }

    // ── Mode 3: RT (Targeting rt-esp32 on High-CAN) ──────────────────
    template <typename SendFn>
    void emit_rt(const RcSnapshot& snap, bool drive_active, uint32_t tick_10ms, SendFn&& send) {
        // 1. Host Steer: 0x303 HOST_STEER_CMD (100 Hz)
        can::gen::HostSteerCmd steer_cmd{};
        int16_t steer_0_1deg = static_cast<int16_t>(std::round(snap.steering_deg * 10.0f));
        steer_cmd.steer_angle_0_1deg = std::clamp<int16_t>(steer_0_1deg, -450, 450);
        steer_cmd.angle_valid = snap.signal_valid;
        steer_cmd.reserved = 0;
        steer_cmd.rolling_counter = roll_host_steer_++;

        can::Frame steer_fr;
        if (can::gen::encode_host_steer_cmd(steer_cmd, steer_fr) == can::gen::CodecStatus::Ok) {
            send(steer_fr);
        }

        // 2. Host Brake: 0x301 HOST_BRAKE_REQ (100 Hz)
        can::gen::HostBrakeReq brake_cmd{};
        float stroke_fraction = snap.brake_stroke_mm / kMaxBrakeStrokeMm;
        stroke_fraction = std::clamp(stroke_fraction, 0.0f, 1.0f);
        int32_t pressure_kpa = static_cast<int32_t>(std::round(stroke_fraction * static_cast<float>(kMaxBrakePressureKpa)));
        brake_cmd.brake_pressure_kpa = std::clamp<int32_t>(pressure_kpa, 0, kMaxBrakePressureKpa);

        can::Frame brake_fr;
        if (can::gen::encode_host_brake_req(brake_cmd, brake_fr) == can::gen::CodecStatus::Ok) {
            send(brake_fr);
        }

        // 3. Host Drive: 0x300 HOST_DRIVE_CMD (100 Hz)
        can::gen::HostDriveCmd drive_cmd{};
        drive_cmd.speed_mmps = drive_active ? snap.target_speed_mmps : 0;
        drive_cmd.speed_mmps = std::clamp<int32_t>(drive_cmd.speed_mmps, -kSpeedRevMaxMmps, kSpeedFwdMaxMmps);
        drive_cmd.yaw_rate_mrad_s = 0;
        drive_cmd.gear = drive_active ? static_cast<uint8_t>(snap.gear) : static_cast<uint8_t>(can::Gear::N);

        can::Frame drive_fr;
        if (can::gen::encode_host_drive_cmd(drive_cmd, drive_fr) == can::gen::CodecStatus::Ok) {
            send(drive_fr);
        }

        // 4. HMI Requests: 0x111 HMI_MODE_REQ + 0x112 HMI_PWR_REQ (10 Hz)
        if (tick_10ms % 10 == 0) {
            can::gen::HmiModeReq hmi_mode{};
            hmi_mode.req_mode = drive_active;
            hmi_mode.rolling_counter = roll_hmi_mode_++;
            can::Frame hmi_mode_fr;
            if (can::gen::encode_hmi_mode_req(hmi_mode, hmi_mode_fr) == can::gen::CodecStatus::Ok) {
                send(hmi_mode_fr);
            }

            can::gen::HmiPwrReq hmi_pwr{};
            hmi_pwr.req_start = snap.signal_valid && snap.drive_enable_req;
            hmi_pwr.rolling_counter = roll_hmi_pwr_++;
            can::Frame hmi_pwr_fr;
            if (can::gen::encode_hmi_pwr_req(hmi_pwr, hmi_pwr_fr) == can::gen::CodecStatus::Ok) {
                send(hmi_pwr_fr);
            }
        }

        // NOTE: RT mode emulates the autonomous Host ONLY. SYS-owned frames
        // (0x011 SYS_SAFETY_STS / 0x110 SYS_MODE_CMD, contracts owner=sys) are
        // deliberately NOT emitted here: rt-esp32 consumes them from its LOW bus
        // (can_rx_router.h:64-84), while the Host lives on HIGH. Building these
        // on HIGH is ignored by real rt and would collide with a real SYS.
        // Motion authority therefore requires a real sys-esp32 on rt's low bus.

        // 5. Host Heartbeat: 0x7FC HOST_HEARTBEAT (2 Hz / 500 ms)
        if (tick_10ms % 50 == 0) {
            can::gen::HostHeartbeat host_hb{};
            host_hb.alive_ctr = alive_ctr_host_++;
            host_hb.health_flags = snap.signal_valid ? 0 : 1;
            can::Frame host_hb_fr;
            if (can::gen::encode_host_heartbeat(host_hb, host_hb_fr) == can::gen::CodecStatus::Ok) {
                send(host_hb_fr);
            }
        }
    }

    uint8_t roll_ses_{0};
    uint8_t roll_seb_{0};
    uint8_t roll_sys_mode_{0};
    uint8_t roll_sys_pwr_{0};
    uint8_t roll_sys_safety_{0};   // 0x011 SYS_SAFETY_STS rolling counter (BARE/RT emulated supervisor)
    uint8_t rearm_pwr_ticks_{2};   // power rearm edge: force 0x113 power_state=0 for first N emissions after boot/reset
    uint8_t roll_hmi_mode_{0};
    uint8_t roll_hmi_pwr_{0};
    uint8_t roll_host_steer_{0};
    uint8_t alive_ctr_rt_{0};
    uint8_t alive_ctr_host_{0};
};

}  // namespace rm
