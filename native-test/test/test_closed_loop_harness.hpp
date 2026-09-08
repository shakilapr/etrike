#pragma once
// ?????????????????????????????????????????????????????????????????????????????
// Closed-Loop Multi-Node Test Harness (SYS + RT + MTR + Actuator/Physics Model)
// ?????????????????????????????????????????????????????????????????????????????

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <algorithm>
#include <atomic>
#include <functional>

#include "protocol/compat/can.hpp"
#include "protocol/compat/can_protocol.hpp"
#include "protocol/compat/e2e.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "shared_config.h"

// SYS Authority & Safety
#include "sys-esp32/src/config.h"
#include "sys-esp32/src/mode_manager.h"
#include "sys-esp32/src/safety_monitor.h"
#include "sys-esp32/src/inhibit_state.h"
#include "sys-esp32/src/brake_control.h"

// RT Motion & Fallback
#include "rt-esp32/src/config.h"
#include "rt-esp32/src/safety_monitor.h"
#include "rt-esp32/src/physics_model.h"
#include "rt-esp32/src/brake_arbitration.h"
#include "rt-esp32/src/brake_fallback.h"
#include "rt-esp32/src/seb_request.h"
#include "rt-esp32/src/steering_control.h"
#include "rt-esp32/src/watchdog.h"
#include "rt-esp32/src/safety_stream_loss.h"

// MTR Actuation
#include "mtr-stm32/src/config.h"
#include "mtr-stm32/src/relay_controller.h"
#include "mtr-stm32/src/dac_controller.h"
#include "mtr-stm32/src/motor_manager.h"
#include "stub/stm32g4xx_hal.h"

// Virtual Bus
#include "can/virtual_can_bus.h"

namespace closed_loop {

// Convert between can::Frame and etrike::protocol::Frame
inline etrike::protocol::Frame to_proto(const can::Frame& f) {
    return f;
}

inline can::Frame to_can(const etrike::protocol::Frame& pf) {
    return pf;
}

// ?? Physical Actuator & Plant Models ?????????????????????????????????????????

struct SebActuatorModel {
    float actual_stroke_mm{0.0f};
    float actual_pressure_kpa{0.0f};
    uint8_t error_status{0};
    uint8_t control_mode{0}; // 0 = stroke, 1 = pressure
    int64_t last_cmd_us{0};

    void update(int64_t now_us, const can::Frame& cmd_frame) {
        if (cmd_frame.id == etrike::protocol::codecs::seb::kCommandId) {
            etrike::protocol::codecs::seb::Command cmd{};
            if (etrike::protocol::codecs::seb::decode_command(cmd_frame.view(), cmd) == can::gen::CodecStatus::Ok) {
                last_cmd_us = now_us;
                control_mode = static_cast<uint8_t>(cmd.control_mode);
                if (cmd.control_mode == etrike::protocol::codecs::seb::ControlMode::Stroke) {
                    float target_stroke = (cmd.stroke_request_raw * 0.05f) - 30.0f;
                    actual_stroke_mm += (target_stroke - actual_stroke_mm) * 0.5f; // simulated cylinder response
                } else {
                    float target_kpa = cmd.pressure_request_raw * 10.0f;
                    actual_pressure_kpa += (target_kpa - actual_pressure_kpa) * 0.5f;
                }
            }
        }
    }

    can::Frame build_status(uint8_t rolling_counter) {
        can::Frame f{};
        f.id = etrike::protocol::codecs::seb::kStatusId;
        f.dlc = 8;
        f.data[0] = static_cast<uint8_t>(0x01 | (control_mode << 2) | (error_status << 6));
        f.data[1] = 0;
        uint16_t s_raw = static_cast<uint16_t>(std::round((actual_stroke_mm + 30.0f) / 0.05f));
        f.data[2] = static_cast<uint8_t>(s_raw & 0xFF);
        f.data[3] = static_cast<uint8_t>((s_raw >> 8) & 0xFF);
        f.data[4] = 0;
        f.data[5] = 0;
        f.data[6] = static_cast<uint8_t>(0x03 | ((rolling_counter & 0x0F) << 4));
        f.data[7] = etrike::protocol::profiles::xor8_ff_v1(f.data.data(), 7);
        return f;
    }
};

struct PhysicalPlantModel {
    int32_t physical_wheel_speed_mmps{0};
    bool motor_stalled{false};
    bool controller_runaway{false};
    uint16_t runaway_dac_code{1200};

    void update(uint16_t dac_code, mtr::RelayController::State relay_state, float brake_stroke_mm) {
        if (controller_runaway) {
            dac_code = runaway_dac_code;
        }
        if (motor_stalled || relay_state != mtr::RelayController::State::Drive) {
            physical_wheel_speed_mmps = 0;
            return;
        }
        // 4095 DAC ? 3000 mm/s
        float requested_speed = (float(dac_code) / 4095.0f) * 3000.0f;
        // Hydraulic brake opposing force
        float brake_factor = std::clamp(1.0f - (brake_stroke_mm / 27.0f), 0.0f, 1.0f);
        physical_wheel_speed_mmps = static_cast<int32_t>(requested_speed * brake_factor);
    }
};

struct CanManipulator {
    static can::Frame make_safety_frame(uint8_t rolling_counter, bool estop_active, bool heartbeat_ok = true) {
        can::gen::SysSafetySts msg{};
        msg.estop_active = estop_active;
        msg.heartbeat_ok = heartbeat_ok;
        msg.rolling_counter = rolling_counter;
        msg.e2e_crc = 0;
        can::Frame f;
        can::gen::encode_sys_safety_sts(msg, f);
        msg.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(f.data.data()));
        can::gen::encode_sys_safety_sts(msg, f);
        return f;
    }

    static can::Frame make_mode_frame(can::Mode mode, uint8_t counter) {
        can::gen::SysModeCmd cmd{};
        cmd.mode = (mode == can::Mode::Auto) ? 1 : 0;
        cmd.rolling_counter = counter;
        can::Frame f;
        can::gen::encode_sys_mode_cmd(cmd, f);
        return f;
    }

    static can::Frame make_pwr_frame(bool power_on, uint8_t counter) {
        can::gen::SysPwrCmd cmd{};
        cmd.power_state = power_on ? 1 : 0;
        cmd.rolling_counter = counter;
        can::Frame f;
        can::gen::encode_sys_pwr_cmd(cmd, f);
        return f;
    }

    static can::Frame make_drive_frame(int32_t speed_mmps, can::Gear gear = can::Gear::D) {
        can::gen::RtDriveCmd cmd{};
        cmd.motor_speed_mmps = speed_mmps;
        cmd.gear = static_cast<uint8_t>(gear);
        can::Frame f;
        can::gen::encode_rt_drive_cmd(cmd, f);
        return f;
    }

    static can::Frame make_feedback_frame(int32_t speed_mmps, can::Gear gear, uint8_t fault_flags = 0) {
        can::gen::MtrMotorFbk fbk{};
        fbk.applied_speed_command_mmps = speed_mmps;
        fbk.gear_state = static_cast<uint8_t>(gear);
        fbk.fault_flags = fault_flags;
        can::Frame f;
        can::gen::encode_mtr_motor_fbk(fbk, f);
        return f;
    }

    static can::Frame make_seb_status_frame(uint8_t error_status, uint8_t pressure, uint8_t counter) {
        can::Frame f;
        f.id = etrike::protocol::codecs::seb::kStatusId;
        f.dlc = 8;
        f.data.fill(0);
        f.data[0] = static_cast<uint8_t>(0x03 | ((error_status & 0x03) << 6));
        f.data[3] = pressure;
        f.data[6] = static_cast<uint8_t>(0x03 | ((counter & 0x0F) << 4));
        f.data[7] = etrike::protocol::profiles::xor8_ff_v1(f.data.data(), 7);
        return f;
    }

    static can::Frame make_seb_cmd_frame(uint8_t pressure, uint8_t counter) {
        etrike::protocol::codecs::seb::Command cmd{};
        cmd.control_enable = true;
        cmd.control_mode = etrike::protocol::codecs::seb::ControlMode::Pressure;
        cmd.pressure_request_raw = pressure;
        cmd.rolling_counter = counter;
        etrike::protocol::Frame pf;
        etrike::protocol::codecs::seb::encode_command(cmd, pf);
        can::Frame f;
        f.id = pf.id;
        f.dlc = pf.dlc;
        f.data = pf.data;
        return f;
    }

    static can::Frame make_ses_cmd_frame(int16_t angle_raw, uint8_t roll) {
        etrike::protocol::codecs::ses::Command cmd{};
        cmd.alignment_enable = true;
        cmd.control_enable = 1;
        cmd.target_angle_raw = angle_raw;
        cmd.target_speed_raw = 150;
        cmd.rolling_counter = roll;
        etrike::protocol::Frame pf;
        etrike::protocol::codecs::ses::encode_command(cmd, pf);
        can::Frame f;
        f.id = pf.id;
        f.dlc = pf.dlc;
        f.data = pf.data;
        return f;
    }

    static can::Frame make_ses_status_frame(int16_t angle_raw, uint8_t status_flags, uint8_t roll) {
        can::Frame f;
        f.id = etrike::protocol::codecs::ses::kStatusId;
        f.dlc = 8;
        f.data.fill(0);
        f.data[0] = status_flags;
        etrike::protocol::write_le_i16(&f.data[2], angle_raw);
        f.data[5] = static_cast<uint8_t>(0x03 | ((roll & 0x0F) << 4));
        f.data[7] = etrike::protocol::profiles::xor8_ff_v1(f.data.data(), 7);
        return f;
    }

    static can::Frame make_host_drive_frame(int32_t speed_mmps, int32_t yaw_mrad_s) {
        can::Frame f;
        f.id = can::kIdHostDriveCmd;
        f.dlc = 8;
        f.data[0] = static_cast<uint8_t>(speed_mmps & 0xFF);
        f.data[1] = static_cast<uint8_t>((speed_mmps >> 8) & 0xFF);
        f.data[2] = static_cast<uint8_t>((speed_mmps >> 16) & 0xFF);
        f.data[3] = static_cast<uint8_t>((speed_mmps >> 24) & 0xFF);
        f.data[4] = static_cast<uint8_t>(yaw_mrad_s & 0xFF);
        f.data[5] = static_cast<uint8_t>((yaw_mrad_s >> 8) & 0xFF);
        f.data[6] = static_cast<uint8_t>((yaw_mrad_s >> 16) & 0xFF);
        f.data[7] = static_cast<uint8_t>((yaw_mrad_s >> 24) & 0xFF);
        return f;
    }

    static can::Frame make_sys_hb_frame(uint8_t counter, bool estop, bool auto_mode) {
        can::gen::SysHeartbeat hb{};
        hb.alive_ctr = counter;
        hb.estop_active = estop;
        hb.mode_auto = auto_mode;
        can::Frame f;
        can::gen::encode_sys_heartbeat(hb, f);
        return f;
    }

    static void corrupt_byte(can::Frame& f, size_t index, uint8_t mask) {
        if (index < f.dlc) f.data[index] ^= mask;
    }

    static void corrupt_crc(can::Frame& f) {
        if (f.id == can::kIdSysSafetySts) {
            f.data[1] ^= 0xA5;
        } else if (f.id == etrike::protocol::codecs::seb::kCommandId ||
                   f.id == etrike::protocol::codecs::seb::kStatusId) {
            f.data[7] ^= 0xFF;
        }
    }

    static void corrupt_counter(can::Frame& f, int8_t delta) {
        if (f.id == can::kIdSysSafetySts) {
            f.data[0] = static_cast<uint8_t>(f.data[0] + (delta << 1));
        } else if (f.id == can::kIdSysModeCmd || f.id == can::kIdSysPwrCmd) {
            f.data[0] = static_cast<uint8_t>(f.data[0] + (delta << 1));
        } else if (f.id == etrike::protocol::codecs::seb::kCommandId) {
            f.data[6] = static_cast<uint8_t>((f.data[6] & 0x0F) | (((f.data[6] >> 4) + delta) << 4));
        }
    }

    static void truncate_dlc(can::Frame& f, uint8_t new_dlc) {
        f.dlc = new_dlc;
    }
};

// ?? Master Closed-Loop Harness ???????????????????????????????????????????????

class ClosedLoopHarness {
public:
    // Simulated time
    uint32_t now_ms{100};
    int64_t  now_us{100000};

    // Virtual dual CAN buses
    can::sim::VirtualCanBus low_bus;
    can::sim::VirtualCanBus high_bus;

    // Real SYS components
    sys::ModeManager      sys_mode;
    sys::SafetyMonitor    sys_safety;
    sys::BrakeControl     sys_brake_ctrl;
    bool hw_estop_button{false};
    uint8_t sys_mode_ctr{0};
    uint8_t sys_pwr_ctr{0};
    uint8_t sys_safety_ctr{0};
    uint8_t sys_hb_ctr{0};
    int64_t last_sys_mode_us{0};
    int64_t last_sys_safety_us{0};
    int64_t last_sys_hb_us{0};
    int64_t last_operator_reset_us{-10000000};
    int64_t last_sys_estop_tx_us{-10000000};

    // Real RT components
    rt::PhysicsModel      rt_physics;
    rt::SteeringControl   rt_steering;
    rt::SebBrakeFallback  rt_brake_fallback;
    bool rt_estop_pending{false};
    bool rt_seb_takeover{false};
    bool rt_obstacle_active{false};
    // Obstacle distance RT passes to run_safety_checks. Default UINT32_MAX (no
    // obstacle). Tests drive obstacle scenarios via rt_obstacle_mm or the Host
    // 0x400 frames; it must NEVER default to 0 (0 mm <= kObstacleStopMM would
    // make RT treat every moving scenario as an imminent collision).
    uint32_t rt_obstacle_mm{UINT32_MAX};
    // RT's commanded longitudinal setpoint (mirrors what t_control resolves from
    // HOST_DRIVE_CMD). RT's low-CAN TX task (t_can_tx_low) turns this into 0x204:
    //   AUTO + no ESTOP -> commanded speed; MANUAL/ESTOP -> {0,N} keep-alive.
    // step_rt() publishes that gated 0x204 every 10 ms so the MTR 0x204 watchdog
    // stays fed in AUTO and the throttle is cut the moment the mode leaves AUTO.
    int32_t rt_cmd_speed_mmps{0};
    bool    rt_steer_ready{true};   // RT steering lockout (STEER_ACTIVE etc.)
    int64_t last_rt_drive_us{0};
    int64_t last_rt_control_us{0};
    int64_t last_rt_hb_us{0};
    uint8_t rt_hb_ctr{0};
    int64_t last_rt_0x001_sent_us{-1000000};
    uint8_t rt_clear_confirm_count{0};
    bool rt_sys_clear_in_progress{false};

    // Real MTR components
    mtr::RelayController mtr_relays;
    mtr::DacController   mtr_dac;
    mtr::MotorManager    mtr_mgr{mtr_relays, mtr_dac};
    int64_t last_mtr_tick_us{0};
    int64_t last_mtr_fbk_us{0};
    uint8_t mtr_fbk_ctr{0};

    // Simulated Plants & Peripherals
    SebActuatorModel   seb;
    PhysicalPlantModel plant;
    uint8_t seb_ctr{0};
    int64_t last_seb_status_us{0};
    uint8_t ses_ctr{0};
    int64_t last_ses_status_us{0};
    int16_t ses_actual_angle_0_1deg{0};
    bool ses_aligned{true};
    int64_t last_host_hb_tx_us{-1000000};
    bool host_alive{true};
    int64_t last_sys_seb_cmd_us{-1000000};
    uint8_t sys_seb_ctr{0};
    bool sys_enable_0x7b9{true};

    // Telemetry capture
    std::deque<can::Frame> low_bus_history;
    std::deque<can::Frame> high_bus_history;
    size_t count_0x001_broadcasts{0};
    size_t count_0x7b9_sys_tx{0};
    size_t count_0x7b9_rt_tx{0};

    void init() {
        now_ms = 100;
        now_us = 100000;
        sys::g_sys_test_time_us = now_us;
        low_bus.clear_faults();
        high_bus.clear_faults();
        low_bus_history.clear();
        high_bus_history.clear();
        count_0x001_broadcasts = 0;
        count_0x7b9_sys_tx = 0;
        count_0x7b9_rt_tx = 0;

        hal_mock::reset();
        sys_mode.init();
        sys_safety.init();
        mtr_mgr.init();
        rt_steering.init();
        for (int i = 0; i < 25; ++i) {
            etrike::protocol::codecs::ses::Command dummy;
            rt_steering.tick(0, 1, i * 20, dummy);
        }
        {
            etrike::protocol::codecs::ses::Command dummy;
            rt_steering.tick(0, 1, 600, dummy);
        }
        rt_brake_fallback.init(now_us);
        rt_estop_pending = false;
        rt_seb_takeover = false;
        rt_obstacle_active = false;
        rt_obstacle_mm = UINT32_MAX;
        rt_cmd_speed_mmps = 0;
        rt_steer_ready = true;
        last_rt_drive_us = -1000000;
        rt_clear_confirm_count = 0;
        rt_sys_clear_in_progress = false;

        hw_estop_button = false;
        last_operator_reset_us = -10000000;
        last_sys_estop_tx_us = -10000000;

        sys::g_inhibit_reasons.store(0);
        sys::g_latched_fault_reasons.store(0);
        g_last_sys_hb_us.store(now_us);
        g_last_host_hb_us.store(now_us);
        g_last_mtr_feedback_us.store(-1);
        g_last_nonzero_cmd_us.store(-1);
        g_brake_request_kpa.store(0);
        g_mtr_applied_speed_command_mmps.store(0);
        rt::g_mtr_health.reset();

        seb = SebActuatorModel{};
        plant = PhysicalPlantModel{};
        ses_ctr = 0;
        last_ses_status_us = -1000000;
        ses_actual_angle_0_1deg = 0;
        ses_aligned = true;

        last_sys_mode_us = -1000000;
        last_sys_safety_us = -1000000;
        last_sys_hb_us = -1000000;
        last_rt_control_us = -1000000;
        last_rt_hb_us = -1000000;
        last_mtr_tick_us = -1000000;
        last_mtr_fbk_us = -1000000;
        last_seb_status_us = -1000000;
        last_host_hb_tx_us = -1000000;
        host_alive = true;
        sys_safety.feed_heartbeat_rt(0);
        last_sys_seb_cmd_us = -1000000;
        sys_seb_ctr = 0;
        sys_enable_0x7b9 = true;
        g_last_0x7B9_rx_us.store(now_us);
    }

    void advance_time_us(int64_t dt_us) {
        now_us += dt_us;
        now_ms = static_cast<uint32_t>(now_us / 1000);
        sys::g_sys_test_time_us = now_us;
    }

    // Deliver frames from virtual buses to target ECU handlers
    void pump_buses() {
        // Pump Low Bus
        etrike::protocol::Frame pf;
        while (low_bus.receive(pf)) {
            can::Frame f = to_can(pf);
            low_bus_history.push_back(f);
            if (low_bus_history.size() > 500) low_bus_history.pop_front();

            if (f.id == can::kIdSafetyEstop) {
                count_0x001_broadcasts++;
                // 1. Loopback discrimination (50 ms window)
                bool is_loopback = (now_us - last_sys_estop_tx_us) < 50000;
                // 2. Operator reset grace window (500 ms window)
                bool in_reset_grace = (now_us - last_operator_reset_us) < 500000;
                if (!is_loopback && !in_reset_grace) {
                    sys_safety.set_estop(true);
                    sys_mode.force_estop();
                }
                rt_estop_pending = true;
                mtr_mgr.handle_frame(f, now_ms);
            } else if (f.id == can::kIdSysSafetySts) {
                mtr_mgr.handle_frame(f, now_ms);
                // RT safety stream observation
                can::gen::SysSafetySts smsg{};
                if (can::gen::decode_sys_safety_sts(f.view(), smsg) == can::gen::CodecStatus::Ok) {
                    if (!smsg.estop_active) {
                        rt_sys_clear_in_progress = true;
                        rt_clear_confirm_count++;
                        if (rt_clear_confirm_count >= 2) {
                            rt_estop_pending = false;
                            rt_sys_clear_in_progress = false;
                        }
                    } else {
                        rt_clear_confirm_count = 0;
                        rt_sys_clear_in_progress = false;
                        rt_estop_pending = true;
                    }
                }
            } else if (f.id == can::kIdSysModeCmd || f.id == can::kIdSysPwrCmd || f.id == can::kIdRtDriveCmd) {
                mtr_mgr.handle_frame(f, now_ms);
            } else if (f.id == can::kIdSysHeartbeat) {
                g_last_sys_hb_us.store(now_us);
            } else if (f.id == can::kIdMtrMotorFbk) {
                g_last_mtr_feedback_us.store(now_us);
                can::gen::MtrMotorFbk fmsg{};
                if (can::gen::decode_mtr_motor_fbk(f.view(), fmsg) == can::gen::CodecStatus::Ok) {
                    g_mtr_applied_speed_command_mmps.store(fmsg.applied_speed_command_mmps);
                }
            } else if (f.id == etrike::protocol::codecs::seb::kCommandId) {
                seb.update(now_us, f);
                g_last_0x7B9_rx_us.store(now_us);
            } else if (f.id == etrike::protocol::codecs::ses::kStatusId) {
                etrike::protocol::codecs::ses::Status smsg{};
                if (etrike::protocol::codecs::ses::decode_status(f.view(), smsg) == can::gen::CodecStatus::Ok) {
                    g_ses_angle_0_1deg.store(smsg.steering_angle_raw);
                }
            } else if (f.id == can::kIdRtHeartbeatLow) {
                can::gen::RtHeartbeat hb{};
                if (can::gen::decode_rt_heartbeat(f.view(), hb) == can::gen::CodecStatus::Ok) {
                    sys_safety.feed_heartbeat_rt(hb.alive_ctr);
                }
            }
        }

        // Pump High Bus & Gateway forwarding (High -> Low for 0x001, 0x300, etc.)
        while (high_bus.receive(pf)) {
            can::Frame f = to_can(pf);
            high_bus_history.push_back(f);
            if (f.id == can::kIdSafetyEstop) {
                low_bus.send(to_proto(f)); // Gateway forward
            } else if (f.id == 0x7FC) { // Host Heartbeat
                g_last_host_hb_us.store(now_us);
            }
        }
    }

    void operator_reset() {
        last_operator_reset_us = now_us;
        sys_safety.set_estop(false);
        for (int i = 0; i < 6; ++i) sys_mode.tick(false, false);
        sys_mode.tick(false, true);
        sys_mode.tick(false, false);
    }

    void operator_press_mode() {
        for (int i = 0; i < 6; ++i) sys_mode.tick(false, false);
        sys_mode.tick(true, false);
        sys_mode.tick(false, false);
    }

    void sys_broadcast_estop() {
        last_sys_estop_tx_us = now_us;
        sys_safety.set_estop(true);
        sys_mode.force_estop();
        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        low_bus.send(to_proto(f001));
        high_bus.send(to_proto(f001));
    }

    // SYS periodic execution
    void step_sys() {
        // HW Button evaluation
        sys_safety.set_estop(hw_estop_button);
        if (sys_safety.estop_active() && sys_mode.mode() != can::Mode::Estop) {
            sys_mode.force_estop();
        }

        const bool estop = (sys_mode.mode() == can::Mode::Estop) || hw_estop_button;
        const bool mode_auto = (sys_mode.mode() == can::Mode::Auto);

        // 10 Hz: Mode & Power authority
        if (now_us - last_sys_mode_us >= 100000) {
            last_sys_mode_us = now_us;
            auto auth = sys::resolve_authority(estop, mode_auto, /*power_requested=*/true);

            can::gen::SysModeCmd mmsg{};
            mmsg.mode = auth.mode_auto ? 1u : 0u;
            mmsg.rolling_counter = sys_mode_ctr++;
            can::Frame fm; can::gen::encode_sys_mode_cmd(mmsg, fm);
            low_bus.send(to_proto(fm));

            can::gen::SysPwrCmd pmsg{};
            pmsg.power_state = auth.power_on ? 1u : 0u;
            pmsg.rolling_counter = sys_pwr_ctr++;
            can::Frame fp; can::gen::encode_sys_pwr_cmd(pmsg, fp);
            low_bus.send(to_proto(fp));
        }

        // 5 Hz: Safety Status 0x011
        if (now_us - last_sys_safety_us >= 200000) {
            last_sys_safety_us = now_us;
            can::gen::SysSafetySts smsg{};
            smsg.estop_active = estop;
            smsg.heartbeat_ok = sys_safety.heartbeat_ok();
            smsg.rolling_counter = sys_safety_ctr++;
            smsg.e2e_crc = 0;
            can::Frame tmp; can::gen::encode_sys_safety_sts(smsg, tmp);
            smsg.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()));
            can::Frame fs; can::gen::encode_sys_safety_sts(smsg, fs);
            low_bus.send(to_proto(fs));
        }

        // 10 Hz: Heartbeat 0x7FE
        if (now_us - last_sys_hb_us >= 100000) {
            last_sys_hb_us = now_us;
            can::gen::SysHeartbeat hb{};
            hb.alive_ctr = sys_hb_ctr++;
            hb.heartbeat_ok = sys_safety.heartbeat_ok();
            hb.estop_active = estop;
            hb.mode_auto = mode_auto;
            can::Frame fh; can::gen::encode_sys_heartbeat(hb, fh);
            low_bus.send(to_proto(fh));
        }

        // 50 Hz: SEB Brake Command 0x7B9
        if (sys_enable_0x7b9 && (now_us - last_sys_seb_cmd_us >= 20000)) {
            last_sys_seb_cmd_us = now_us;
            etrike::protocol::codecs::seb::Command cmd{};
            cmd.control_enable = true;
            cmd.control_mode = etrike::protocol::codecs::seb::ControlMode::Pressure;
            cmd.pressure_request_raw = estop ? 40 : 0;
            cmd.rolling_counter = sys_seb_ctr++;
            etrike::protocol::Frame pf;
            if (etrike::protocol::codecs::seb::encode_command(cmd, pf) == can::gen::CodecStatus::Ok) {
                low_bus.send(pf);
                count_0x7b9_sys_tx++;
            }
        }
    }

    // RT periodic execution (100 Hz control loop)
    void step_rt() {
        if (now_us - last_rt_control_us < 10000) return;
        last_rt_control_us = now_us;

        // Run safety checks
        bool takeover_dummy = rt_seb_takeover;
        auto sr = ::run_safety_checks(now_us, false, rt_obstacle_mm, rt_estop_pending,
                                      sys_mode.mode() == can::Mode::Auto ? 1 : 0,
                                      takeover_dummy);

        // Fallback machine update
        rt::SebFallbackInput fbi{};
        fbi.now_us = now_us;
        fbi.sys_hb_fresh = (now_us - g_last_sys_hb_us.load()) <= 200000;
        fbi.sys_0x7B9_observed = (now_us - g_last_0x7B9_rx_us.load()) <= 100000;
        fbi.startup_grace_active = false;
        auto fbo = rt_brake_fallback.update(fbi);
        rt_seb_takeover = fbo.emergency_tx_0x7B9;

        // Broadcast 0x001 ONLY when RT actively originates an unhandled local emergency trip.
        // Never broadcast 0x001 when reacting to external CAN ESTOP (CanEstop / Mode==Estop),
        // nor when SYS clear is in progress, nor for soft disables (MTR fbk loss, host timeout).
        const bool is_active_local_trip = (rt_obstacle_active ||
                                           sr.obstacle_triggered ||
                                           sr.estop_reason == rt::kEstopReasonFollowingError ||
                                           sr.estop_reason == rt::kEstopReasonBusOff ||
                                           sr.estop_reason == rt::kEstopReasonInternal ||
                                           rt_seb_takeover);

        if (is_active_local_trip && !rt_sys_clear_in_progress) {
            if ((now_us - last_rt_0x001_sent_us) >= 250000) {
                last_rt_0x001_sent_us = now_us;
                can::Frame f001{can::kIdSafetyEstop, 0, {}};
                low_bus.send(to_proto(f001));
                high_bus.send(to_proto(f001));
            }
        } else {
            if (!is_active_local_trip) {
                last_rt_0x001_sent_us = -1000000;
            }
        }

        // RT emergency 0x7B9 transmission
        if (rt_seb_takeover) {
            count_0x7b9_rt_tx++;
            can::Frame fb_cmd{};
            auto seb_cmd = rt::make_seb_takeover_req();
            seb_cmd.rolling_counter = sys_mode_ctr++;
            etrike::protocol::codecs::seb::encode_command(seb_cmd, fb_cmd);
            low_bus.send(to_proto(fb_cmd));
        }

        // 20 Hz: RT Heartbeat 0x7FD
        if (now_us - last_rt_hb_us >= 50000) {
            last_rt_hb_us = now_us;
            can::gen::RtHeartbeat hb{};
            hb.alive_ctr = rt_hb_ctr++;
            hb.health_flags = rt_estop_pending ? 0x01 : 0x00;
            can::Frame fh;
            can::gen::encode_rt_heartbeat(hb, fh);
            low_bus.send(to_proto(fh));
        }

        // RT low-CAN TX 0x204 RT_DRIVE_CMD at 100 Hz (mirrors t_can_tx_low):
        // only AUTO may command motion. MANUAL/ESTOP publish a keep-alive {0,N}
        // so the MTR 0x204 watchdog stays fed but the throttle is zeroed the
        // instant the mode leaves AUTO (this is what cuts an in-flight throttle
        // on SYS AUTO->MANUAL — MTR alone cannot know it must stop).
        // run_safety_checks may ALSO force zero_setpoints (host/SYS heartbeat
        // loss, MTR-health trip, ESTOP): RT must then emit {0,N}, not motion.
        if (now_us - last_rt_drive_us >= 10000) {
            last_rt_drive_us = now_us;
            const bool mode_auto = (sys_mode.mode() == can::Mode::Auto);
            const bool motion_allowed =
                mode_auto && !rt_estop_pending && rt_steer_ready && !sr.zero_setpoints;
            int32_t speed_out = motion_allowed ? rt_cmd_speed_mmps : 0;
            uint8_t gear_out;
            if (speed_out > 0) gear_out = static_cast<uint8_t>(can::Gear::D);
            else if (speed_out < 0) gear_out = static_cast<uint8_t>(can::Gear::R);
            else gear_out = static_cast<uint8_t>(can::Gear::N);
            can::gen::RtDriveCmd dc{};
            dc.motor_speed_mmps = speed_out;
            dc.gear = gear_out;
            can::Frame f204;
            can::gen::encode_rt_drive_cmd(dc, f204);
            low_bus.send(to_proto(f204));
        }
    }

    void step_host() {
        if (!host_alive) return;
        if (now_us - last_host_hb_tx_us >= 100000) {
            last_host_hb_tx_us = now_us;
            can::Frame f_hb = can::Frame::standard(0x7FC, 0);
            high_bus.send(to_proto(f_hb));
        }
    }

    // MTR periodic execution (200 Hz evaluation + 50 Hz feedback)
    void step_mtr() {
        if (now_us - last_mtr_tick_us >= 5000) {
            last_mtr_tick_us = now_us;
            mtr_mgr.tick(now_ms);
            plant.update(mtr_dac.current_code(), mtr_relays.state(), seb.actual_stroke_mm);
        }

        // 50 Hz: MTR 0x206 feedback
        if (now_us - last_mtr_fbk_us >= 20000) {
            last_mtr_fbk_us = now_us;
            can::gen::MtrMotorFbk fbk{};
            fbk.applied_speed_command_mmps = mtr_mgr.target_speed_mmps();
            fbk.gear_state = static_cast<uint8_t>(mtr_relays.state() == mtr::RelayController::State::Drive ? can::Gear::D : can::Gear::N);
            fbk.fault_flags = mtr_mgr.is_estop_active() ? 0x01 : 0x00;
            can::Frame ff; can::gen::encode_mtr_motor_fbk(fbk, ff);
            low_bus.send(to_proto(ff));
        }
    }

    // SEB periodic execution (50 Hz status)
    void step_seb() {
        if (now_us - last_seb_status_us >= 20000) {
            last_seb_status_us = now_us;
            can::Frame f_stat = seb.build_status(seb_ctr++);
            low_bus.send(to_proto(f_stat));
        }
    }

    // SES periodic execution (50 Hz status)
    void step_ses() {
        if (now_us - last_ses_status_us >= 20000) {
            last_ses_status_us = now_us;
            can::Frame f = CanManipulator::make_ses_status_frame(ses_actual_angle_0_1deg, ses_aligned ? 0x01 : 0x00, ses_ctr++);
            low_bus.send(to_proto(f));
        }
    }

    // Single synchronized discrete-event step
    void tick(int64_t step_us = 10000) {
        advance_time_us(step_us);
        step_host();
        pump_buses();
        step_sys();
        step_rt();
        step_mtr();
        step_seb();
        step_ses();
        pump_buses();
    }

    // Bring whole vehicle to active AUTO driving state.
    // Mirrors the real chain: SYS broadcasts MANUAL authority during boot; the
    // MODE button flips to AUTO; RT then commands motion via its gated 0x204
    // producer (step_rt). RT keeps the 0x204 watchdog fed from the first AUTO
    // tick so MTR never fails-safe for a missing drive stream.
    void bring_to_active_auto(int32_t speed_mmps = 2000) {
        for (int i = 0; i < 60; ++i) tick(10000); // 600 ms boot grace (MANUAL)

        // Clear debounce and press MODE button to switch to AUTO
        operator_press_mode();

        // Command motion once in AUTO. step_rt() emits gated 0x204 every 10 ms.
        rt_cmd_speed_mmps = speed_mmps;
        for (int i = 0; i < 60; ++i) tick(10000); // 600 ms of AUTO driving

        // Leave the plant actually moving (a few more ticks after authority /
        // safety-streams are fully established).
        for (int i = 0; i < 20; ++i) tick(10000);
    }
};

} // namespace closed_loop
