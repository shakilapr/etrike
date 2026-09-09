#pragma once
// MtrModel — MTR actuator peer (LOW bus only).
//
// PROVISIONAL PEER for Phases A/B/C: consumes RT's 0x204 + SYS authority and
// drives a DAC + relay model, echoing 0x206. Phase C replaces it with the real
// extracted MtrCore (mtr::RelayController/DacController/MotorManager already
// run on-host in native-test/test_vehicle_integration.cpp).
//
// Behavior mirrors the vehicle's documented asymmetric clear + REARM:
//   * 0x001 or 0x011 estop_active=1 -> latched, DAC cut to 0.
//   * 0x011 zero frames clear ONLY on two consecutive advancing zeros.
//   * After an estop clear MTR is REARM_REQUIRED: a 0x113 OFF->ON edge is
//     required before any fresh 0x204 motion command is applied.
//   * 0x206 MTR_MOTOR_FBK echoes the *applied* command at 20 ms cadence.
#include <cstdint>
#include <cstdlib>

#include "protocol/compat/can.hpp"
#include "runtime/sim_clock.h"
#include "runtime/virtual_can.h"

namespace sim {

class MtrModel {
public:
    void boot(int64_t now_us) {
        estop_latched_ = false;
        rearm_required_ = false;
        power_on_ = false;
        auto_mode_ = false;
        dac_ = 0;
        applied_speed_mmps_ = 0;
        applied_gear_ = uint8_t(can::Gear::N);
        last_power_state_ = false;
        next_fbk_us_ = now_us + 20'000;
        roll_fbk_ = 0;
        ssts_roll_ = RollTracker{};
        ssts_latched_ = false;
        ssts_clear_confirm_ = 0;
        ssts_last_zero_ = false;
        ssts_clear_last_ctr_ = 0;
        mode_roll_ = RollTracker{};
        pwr_roll_ = RollTracker{};
    }

    void on_frame(const etrike::protocol::Frame& fr, int64_t now_us) {
        // 0x204 RT_DRIVE_CMD.
        if (fr.id == can::kIdRtDriveCmd) {
            can::gen::RtDriveCmd dc{};
            if (can::decode_frame(fr.view(), dc) != can::gen::CodecStatus::Ok)
                return;
            drive_speed_mmps_ = dc.motor_speed_mmps;
            drive_gear_ = dc.gear;
        }
        // 0x001 SAFETY_ESTOP.
        if (fr.id == can::kIdSafetyEstop) {
            latch_estop();
        }
        // 0x011 SYS_SAFETY_STS (authoritative latch + asymmetric clear).
        if (fr.id == can::kIdSysSafetySts) {
            can::gen::SysSafetySts sts{};
            if (can::decode_frame(fr.view(), sts) != can::gen::CodecStatus::Ok)
                return;
            const uint8_t crc =
                can::e2e::sys_safety_sts_crc(fr.data.data());
            if (crc != sts.e2e_crc) return;
            if (!ssts_roll_.advance(sts.rolling_counter)) return;

            if (sts.estop_active) {
                ssts_latched_ = true;
                ssts_clear_confirm_ = 0;
                ssts_last_zero_ = false;
                latch_estop();
                return;
            }
            if (!ssts_latched_) {
                ssts_clear_confirm_ = 0;
                ssts_last_zero_ = true;
                ssts_clear_last_ctr_ = sts.rolling_counter;
                return;
            }
            const bool first_zero = !ssts_last_zero_;
            const bool advances = sts.rolling_counter ==
                static_cast<uint8_t>(ssts_clear_last_ctr_ + 1u);
            if (first_zero)
                ssts_clear_confirm_ = 1;
            else if (advances)
                ++ssts_clear_confirm_;
            else
                ssts_clear_confirm_ = 1;
            ssts_clear_last_ctr_ = sts.rolling_counter;
            ssts_last_zero_ = true;
            if (ssts_clear_confirm_ >= 2) {
                ssts_latched_ = false;
                estop_latched_ = false;
                // Released into REARM_REQUIRED: still stopped until 0x113 OFF->ON.
                rearm_required_ = true;
            }
        }
        // 0x110 SYS_MODE_CMD: Manual/Auto authority.
        if (fr.id == can::kIdSysModeCmd) {
            can::gen::SysModeCmd msg{};
            if (can::decode_frame(fr.view(), msg) != can::gen::CodecStatus::Ok)
                return;
            if (mode_roll_.advance(msg.rolling_counter))
                auto_mode_ = msg.mode;
        }
        // 0x113 SYS_PWR_CMD: OFF->ON edge satisfies REARM.
        if (fr.id == can::kIdSysPwrCmd) {
            can::gen::SysPwrCmd msg{};
            if (can::decode_frame(fr.view(), msg) != can::gen::CodecStatus::Ok)
                return;
            if (!pwr_roll_.advance(msg.rolling_counter)) return;
            const bool on_edge = msg.power_state && !last_power_state_;
            last_power_state_ = msg.power_state;
            power_on_ = msg.power_state;
            if (!msg.power_state) {
                rearm_required_ = true;  // power drop always re-arms the drive path
            } else if (on_edge && rearm_required_) {
                rearm_required_ = false;
            }
        }
        (void)now_us;
    }

    // Called each quantum; emits 0x206 at 20 ms and recomputes actuation.
    void step(int64_t now_us, VirtualCanBus& bus) {
        if (!estop_latched_ && !rearm_required_ && power_on_ && auto_mode_ &&
            drive_speed_mmps_ != 0) {
            applied_speed_mmps_ = drive_speed_mmps_;
            applied_gear_ = drive_gear_;
            dac_ = std::abs(drive_speed_mmps_);  // indicator: commanded actuation
        } else {
            applied_speed_mmps_ = 0;
            applied_gear_ = uint8_t(can::Gear::N);
            dac_ = 0;
        }
        if (now_us >= next_fbk_us_) {
            next_fbk_us_ += 20'000;
            can::gen::MtrMotorFbk fbk{};
            fbk.motor_command_speed_mmps =
                static_cast<int16_t>(applied_speed_mmps_);
            fbk.gear_state = applied_gear_;
            fbk.fault_flags = 0;
            can::Frame fr;
            if (can::encode_frame(fbk, fr) == can::gen::CodecStatus::Ok)
                bus.transmit(Bus::Low, fr);
            ++roll_fbk_;
        }
    }

    bool is_estop_active() const noexcept { return estop_latched_; }
    bool rearm_required() const noexcept { return rearm_required_; }
    bool power_on() const noexcept { return power_on_; }
    int  dac() const noexcept { return dac_; }
    int32_t applied_speed_mmps() const noexcept { return applied_speed_mmps_; }

private:
    struct RollTracker {
        bool   first = true;
        uint8_t last = 0;
        bool advance(uint8_t counter) noexcept {
            if (first) {
                first = false;
                last = counter;
                return true;
            }
            const uint8_t delta = static_cast<uint8_t>(counter - last);
            last = counter;
            return delta == 1 || delta == 2;
        }
    };

    void latch_estop() noexcept {
        if (!estop_latched_) {
            estop_latched_ = true;
            rearm_required_ = true;
        }
    }

    bool estop_latched_ = false;
    bool rearm_required_ = false;
    bool power_on_ = false;
    bool auto_mode_ = false;
    bool last_power_state_ = false;
    int  dac_ = 0;
    int32_t drive_speed_mmps_ = 0;
    uint8_t drive_gear_ = uint8_t(can::Gear::N);
    int32_t applied_speed_mmps_ = 0;
    uint8_t applied_gear_ = uint8_t(can::Gear::N);
    int64_t next_fbk_us_ = 0;
    uint8_t roll_fbk_ = 0;

    RollTracker ssts_roll_;
    bool   ssts_latched_ = false;
    uint8_t ssts_clear_confirm_ = 0;
    bool   ssts_last_zero_ = false;
    uint8_t ssts_clear_last_ctr_ = 0;
    RollTracker mode_roll_;
    RollTracker pwr_roll_;
};

}  // namespace sim
