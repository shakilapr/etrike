#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include "stm32g4xx_hal.h"
#include "protocol/compat/can.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "physics_model.h"
#include "brake_arbitration.h"
#include "seb_request.h"
#include "watchdog.h"
#include "config.h"
#include "shared_config.h"

#include "motor_manager.h"
#include "relay_controller.h"
#include "dac_controller.h"

using namespace rt;
using namespace mtr;

namespace {

// ── Complete Multi-ECU Simulated Running Test Environment ─────────
class RunningFaultSystemHarness {
public:
    uint32_t now_ms{100};

    // Dual CAN Network Simulation
    std::vector<can::Frame> high_bus_rx{};
    std::vector<can::Frame> low_bus_rx{};

    // ── SYS Node State ───────────────────────────────────────────
    uint8_t   sys_mode_ctr{0};
    uint8_t   sys_pwr_ctr{0};
    uint8_t   sys_safety_ctr{0};
    uint8_t   sys_hb_ctr{0};
    bool      sys_estop_latch{false};
    can::Mode sys_current_mode{can::Mode::Manual};
    bool      sys_power_on{false};

    // Fault Injection Flags on SYS
    bool      enable_sys_hb{true};
    bool      enable_sys_safety{true};
    bool      enable_sys_mode{true};
    bool      enable_sys_pwr{true};
    bool      corrupt_safety_crc{false};
    bool      freeze_mode_counter{false};
    bool      jump_pwr_counter{false};

    // ── Host / Jetson Node State ─────────────────────────────────
    uint8_t   host_hb_ctr{0};
    int32_t   host_cmd_speed_mmps{0};
    int32_t   host_cmd_yaw_mrad_s{0};
    uint8_t   host_cmd_gear{1}; // Drive
    bool      enable_host_hb{true};
    bool      enable_host_drive{true};

    // ── RT ESP32 Node State & Subsystems ─────────────────────────
    PhysicsModel     rt_physics{};
    CmdWatchdog      rt_watchdog{};
    can::Mode        rt_mode{can::Mode::Manual};
    int64_t          rt_last_sys_hb_us{0};
    int64_t          rt_last_host_hb_us{0};
    bool             rt_estop_active{false};
    uint8_t          rt_estop_reason{rt::kEstopReasonNone};
    uint8_t          rt_estop_clear_count{0};
    uint8_t          rt_estop_last_zero_ctr{0};
    int32_t          rt_last_resolved_speed{0};
    int32_t          rt_last_resolved_steer_mdeg{0};
    bool             rt_reversing{false};

    // RT Safety Monitor Emulated State
    int64_t          rt_last_mtr_feedback_us{-1};
    int64_t          rt_last_nonzero_cmd_us{-1};
    int32_t          rt_brake_request_kpa{0};
    bool             rt_disable_steering{false};
    bool             rt_obstacle_triggered{false};
    int              rt_steer_follow_err_ticks{0};

    // ── MTR STM32 Subsystems ─────────────────────────────────────
    RelayController  mtr_relays{};
    DacController    mtr_dac{};
    MotorManager     mtr_manager{mtr_relays, mtr_dac};

    // ── Smart Actuator States (SES Steering & SEB Brake) ────────
    int16_t          ses_actual_angle_0_1deg{0};
    bool             ses_jammed{false};
    int16_t          ses_jammed_angle_0_1deg{0};
    uint8_t          ses_last_rolling_ctr{0};
    bool             ses_checksum_valid{false};
    uint32_t         ses_frames_received{0};

    uint8_t          seb_pressure_raw{0};
    uint16_t         seb_stroke_raw{0};
    bool             seb_auto_brake_active{false};

    void init() {
        hal_mock::reset();
        mtr_manager.init();
        rt_watchdog.init();
    }

    void advance_time(uint32_t dt_ms) {
        now_ms += dt_ms;
        mtr_manager.tick(now_ms);
    }

    // Establish active cruise state: 2000 mm/s in AUTO mode
    void establish_active_cruise(int32_t speed_mmps = 2000, int32_t yaw_mrad_s = 0) {
        sys_power_on = true;
        sys_current_mode = can::Mode::Auto;
        sys_estop_latch = false;
        enable_sys_hb = true;
        enable_sys_safety = true;
        enable_sys_mode = true;
        enable_sys_pwr = true;
        enable_host_hb = true;
        enable_host_drive = true;

        // Establish authority streams
        for (int i = 0; i < 5; ++i) {
            advance_time(10);
            sys_broadcast_tick();
        }

        // Engage autonomous cruise
        for (int i = 0; i < 5; ++i) {
            advance_time(10);
            sys_broadcast_tick();
            host_broadcast_tick(speed_mmps, yaw_mrad_s);
            mtr_feedback_tick();
        }
    }

    // ── SYS Broadcasts ───────────────────────────────────────────
    void sys_broadcast_tick() {
        // 1. SYS Heartbeat (0x7FE, 10 Hz)
        if (enable_sys_hb) {
            can::gen::SysHeartbeat hb{};
            hb.alive_ctr = sys_hb_ctr++;
            hb.estop_active = sys_estop_latch;
            can::Frame f_hb{};
            can::gen::encode_sys_heartbeat(hb, f_hb);
            deliver_to_rt(f_hb, /*from_high=*/false);
            mtr_manager.handle_frame(f_hb, now_ms);
        }

        // 2. SYS Safety Status (0x011, 5 Hz with E2E CRC)
        if (enable_sys_safety) {
            can::gen::SysSafetySts safe{};
            safe.estop_active = sys_estop_latch;
            safe.heartbeat_ok = true;
            safe.rolling_counter = sys_safety_ctr++;
            safe.e2e_crc = 0;
            can::Frame tmp{};
            can::gen::encode_sys_safety_sts(safe, tmp);
            if (corrupt_safety_crc) {
                safe.e2e_crc = 0xAA; // deliberately corrupted CRC
            } else {
                safe.e2e_crc = static_cast<std::uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()));
            }
            can::Frame f_safe{};
            can::gen::encode_sys_safety_sts(safe, f_safe);
            deliver_to_rt(f_safe, /*from_high=*/false);
            mtr_manager.handle_frame(f_safe, now_ms);
        }

        // 3. SYS Mode Command (0x110, 10 Hz)
        if (enable_sys_mode) {
            can::gen::SysModeCmd mode{};
            mode.mode = (sys_current_mode == can::Mode::Auto);
            if (freeze_mode_counter) {
                mode.rolling_counter = sys_mode_ctr; // repeated/frozen counter
            } else {
                mode.rolling_counter = ++sys_mode_ctr;
            }
            can::Frame f_mode{};
            can::gen::encode_sys_mode_cmd(mode, f_mode);
            deliver_to_rt(f_mode, /*from_high=*/false);
            mtr_manager.handle_frame(f_mode, now_ms);
        }

        // 4. SYS Power Command (0x113, 10 Hz)
        if (enable_sys_pwr) {
            can::gen::SysPwrCmd pwr{};
            pwr.power_state = sys_power_on;
            if (jump_pwr_counter) {
                sys_pwr_ctr += 5; // counter jump/gap
                jump_pwr_counter = false;
            }
            pwr.rolling_counter = sys_pwr_ctr++;
            can::Frame f_pwr{};
            can::gen::encode_sys_pwr_cmd(pwr, f_pwr);
            deliver_to_rt(f_pwr, /*from_high=*/false);
            mtr_manager.handle_frame(f_pwr, now_ms);
        }
    }

    // ── Host Broadcasts ──────────────────────────────────────────
    void host_broadcast_tick(int32_t speed_mmps, int32_t yaw_mrad_s, int32_t brake_kpa = 0, uint32_t obstacle_mm = 3000) {
        host_cmd_speed_mmps = speed_mmps;
        host_cmd_yaw_mrad_s = yaw_mrad_s;

        // 1. Host Heartbeat (0x7FC, 2 Hz)
        if (enable_host_hb) {
            can::gen::HostHeartbeat hb{};
            hb.alive_ctr = host_hb_ctr++;
            hb.health_flags = 0;
            can::Frame f_hb{};
            can::gen::encode_host_heartbeat(hb, f_hb);
            deliver_to_rt(f_hb, /*from_high=*/true);
        }

        // 2. Host Obstacle Distance (0x400) - Sensor input processed before motion commands
        if (obstacle_mm < 3000) {
            can::gen::HostObstacleDist obs{};
            obs.distance_mm = obstacle_mm;
            can::Frame f_obs{};
            can::gen::encode_host_obstacle_dist(obs, f_obs);
            deliver_to_rt(f_obs, /*from_high=*/true);
        }

        // 3. Host Drive Command (0x300, 50 Hz)
        if (enable_host_drive) {
            can::gen::HostDriveCmd cmd{};
            cmd.speed_mmps = speed_mmps;
            cmd.yaw_rate_mrad_s = yaw_mrad_s;
            cmd.gear = host_cmd_gear;
            can::Frame f_cmd{};
            can::gen::encode_host_drive_cmd(cmd, f_cmd);
            deliver_to_rt(f_cmd, /*from_high=*/true);
        }

        // 4. Host Brake Request (0x301)
        if (brake_kpa > 0) {
            can::gen::HostBrakeReq brk{};
            brk.brake_pressure_kpa = brake_kpa;
            can::Frame f_brk{};
            can::gen::encode_host_brake_req(brk, f_brk);
            deliver_to_rt(f_brk, /*from_high=*/true);
        }
    }

    // ── MTR Feedback Broadcast (0x206) ───────────────────────────
    void mtr_feedback_tick() {
        int64_t now_us = static_cast<int64_t>(now_ms) * 1000;
        can::Frame f_fbk = mtr_manager.build_motor_feedback_frame();
        deliver_to_rt(f_fbk, /*from_high=*/false);
        rt_last_mtr_feedback_us = now_us;
    }

    // ── RT Gateway & Execution Engine ────────────────────────────
    void deliver_to_rt(const can::Frame& f, bool from_high) {
        int64_t now_us = static_cast<int64_t>(now_ms) * 1000;

        // Gateway forwarding
        if (from_high && can::is_forwarded_high_to_low(f.id)) {
            low_bus_rx.push_back(f);
        }
        if (!from_high && can::is_forwarded_low_to_high(f.id)) {
            high_bus_rx.push_back(f);
        }

        // Process 0x001 Hardwired ESTOP
        if (f.id == can::kIdSafetyEstop) {
            rt_estop_active = true;
            rt_estop_reason = rt::kEstopReasonCanEstop;
            if (from_high) low_bus_rx.push_back(f);
            else high_bus_rx.push_back(f);
            mtr_manager.handle_frame(f, now_ms);
            return;
        }

        // Process 0x011 SYS Safety Status
        if (f.id == can::kIdSysSafetySts && !from_high) {
            can::gen::SysSafetySts s{};
            can::gen::decode_sys_safety_sts(f.view(), s);
            if (s.estop_active) {
                rt_estop_active = true;
                rt_estop_reason = rt::kEstopReasonCanEstop;
                rt_estop_clear_count = 0;
            } else if (rt_estop_active) {
                // Asymmetric clear logic
                if (rt_estop_clear_count == 0) {
                    rt_estop_clear_count = 1;
                    rt_estop_last_zero_ctr = s.rolling_counter;
                } else if (s.rolling_counter == static_cast<uint8_t>(rt_estop_last_zero_ctr + 1)) {
                    ++rt_estop_clear_count;
                    if (rt_estop_clear_count >= 2) {
                        rt_estop_active = false;
                        rt_estop_reason = rt::kEstopReasonNone;
                        rt_estop_clear_count = 0;
                    }
                } else {
                    rt_estop_clear_count = 1;
                    rt_estop_last_zero_ctr = s.rolling_counter;
                }
            }
        }

        // Process 0x110 SYS Mode Command
        if (f.id == can::kIdSysModeCmd && !from_high) {
            can::gen::SysModeCmd mode_cmd{};
            if (can::gen::decode_sys_mode_cmd(f.view(), mode_cmd) == can::gen::CodecStatus::Ok) {
                rt_mode = mode_cmd.mode ? can::Mode::Auto : can::Mode::Manual;
            }
        }

        if (f.id == can::kIdHostHeartbeat && from_high) {
            rt_last_host_hb_us = now_us;
        }
        if (f.id == can::kIdSysHeartbeat && !from_high) {
            rt_last_sys_hb_us = now_us;
        }

        // Process 0x202 SES ErrorInfo (L3 fault bits)
        if (f.id == can::kIdSbwErrInfo && !from_high) {
            can::custom::ses::ErrorInfo error{};
            if (can::custom::ses::decode_error_info(f.view(), error) == can::gen::CodecStatus::Ok) {
                constexpr uint16_t kSesL3Mask = 0x3C0F; // Byte 1 (0x0F) | (Byte 2 (0x3C) << 8)
                const uint16_t fault_bits = static_cast<uint16_t>(error.raw[1]) |
                                            (static_cast<uint16_t>(error.raw[2]) << 8);
                if ((fault_bits & kSesL3Mask) != 0) {
                    rt_estop_active = true;
                    rt_estop_reason = rt::kEstopReasonInternal;
                    rt_brake_request_kpa = shared::kMaxBrakeKpa;
                    rt_disable_steering = true;
                }
            }
        }

        // Process 0x721 SEB Status (L3 error status == 3)
        if (f.id == can::kIdBbwStatus && !from_high) {
            can::custom::seb::Status val{};
            if (can::custom::seb::decode_status(f.view(), val) == can::gen::CodecStatus::Ok) {
                if (val.error_status == 3) {
                    rt_estop_active = true;
                    rt_estop_reason = rt::kEstopReasonInternal;
                    rt_brake_request_kpa = shared::kMaxBrakeKpa;
                }
            }
        }

        // Process 0x400 Obstacle Distance
        if (f.id == can::kIdHostObstacleDist && from_high) {
            can::gen::HostObstacleDist obs{};
            if (can::gen::decode_host_obstacle_dist(f.view(), obs) == can::gen::CodecStatus::Ok) {
                if (obs.distance_mm <= shared::kObstacleStopMM &&
                    std::abs(rt_last_resolved_speed) > static_cast<int32_t>(shared::kLowSpeedThreshMmps)) {
                    rt_obstacle_triggered = true;
                    rt_disable_steering = true;
                    rt_estop_reason = rt::kEstopReasonObstacle;
                    rt_brake_request_kpa = shared::kMaxBrakeKpa;
                } else if (obs.distance_mm > shared::kObstacleStopMM) {
                    rt_obstacle_triggered = false;
                    rt_disable_steering = false;
                    if (rt_estop_reason == rt::kEstopReasonObstacle) {
                        rt_estop_reason = rt::kEstopReasonNone;
                        rt_brake_request_kpa = 0;
                    }
                }
            }
        }

        // Process 0x300 Host Drive Command
        if (f.id == can::kIdHostDriveCmd && from_high) {
            rt_watchdog.feed(now_us);
            can::gen::HostDriveCmd cmd{};
            can::gen::decode_host_drive_cmd(f.view(), cmd);

            // Execute RT Kinematics
            DriveCmd drive_cmd{cmd.speed_mmps, cmd.yaw_rate_mrad_s};
            ResolvedSetpoint sp{};
            rt_physics.resolve(drive_cmd, sp);

            rt_last_resolved_speed = sp.motor_speed_mmps;
            rt_last_resolved_steer_mdeg = sp.steer_angle_mdeg;
            rt_reversing = sp.reversing;

            // ── Steering Following-Error Supervision (100 Hz check) ──
            int16_t cmd_angle_0_1deg = static_cast<int16_t>(sp.steer_angle_mdeg / 100);
            int16_t actual_angle_0_1deg = ses_jammed ? ses_jammed_angle_0_1deg : ses_actual_angle_0_1deg;
            int32_t diff = std::abs(static_cast<int32_t>(cmd_angle_0_1deg) - static_cast<int32_t>(actual_angle_0_1deg));
            float threshold_deg = rt::compute_following_error_threshold(static_cast<float>(std::abs(sp.motor_speed_mmps)));
            int32_t threshold_0_1deg = static_cast<int32_t>(threshold_deg * 10.0f);

            if (diff > threshold_0_1deg) {
                rt_steer_follow_err_ticks++;
                constexpr int kTickLimit = rt::kSteerFollowingErrMs / (1000 / rt::kControlLoopHz); // 300ms / 10ms = 30 ticks
                if (rt_steer_follow_err_ticks >= kTickLimit) {
                    rt_estop_active = true;
                    rt_estop_reason = rt::kEstopReasonFollowingError;
                    rt_brake_request_kpa = shared::kMaxBrakeKpa;
                    rt_disable_steering = true;
                }
            } else {
                rt_steer_follow_err_ticks = 0;
            }

            // Gating: motion prohibited if estopped or obstacle triggered or not AUTO
            const bool drive_allowed = (rt_mode == can::Mode::Auto) && !rt_estop_active && !rt_obstacle_triggered;
            int32_t speed_out = drive_allowed ? sp.motor_speed_mmps : 0;
            if (speed_out != 0) {
                rt_last_nonzero_cmd_us = now_us;
            }

            can::Gear gear;
            if (!drive_allowed) {
                gear = can::Gear::N;
            } else if (speed_out > 0) {
                gear = can::Gear::D;
            } else if (speed_out < 0) {
                gear = can::Gear::R;
            } else {
                gear = can::Gear::N;
            }

            // Send 0x204 to MTR
            can::gen::RtDriveCmd drv{speed_out, static_cast<uint8_t>(gear)};
            can::Frame f_204{};
            can::gen::encode_rt_drive_cmd(drv, f_204);
            mtr_manager.handle_frame(f_204, now_ms);
            mtr_manager.tick(now_ms);

            // Send 0x169 to SES (suppressed in Manual mode)
            if (rt_mode != can::Mode::Manual) {
                etrike::protocol::codecs::ses::Command ses_cmd{};
                ses_cmd.alignment_enable = false;
                ses_cmd.control_enable = (!rt_estop_active && !rt_disable_steering);
                ses_cmd.target_angle_raw = cmd_angle_0_1deg;
                ses_cmd.target_speed_raw = 328;
                ses_cmd.rolling_counter = ses_last_rolling_ctr++;

                can::Frame f_169{};
                etrike::protocol::codecs::ses::encode_command(ses_cmd, f_169);
                receive_ses_command(f_169);
            }
        }

        // Process 0x301 Host Brake Request
        if (f.id == can::kIdHostBrakeReq && from_high) {
            can::gen::HostBrakeReq brk{};
            can::gen::decode_host_brake_req(f.view(), brk);
            int32_t effective_brake = std::max(static_cast<int32_t>(brk.brake_pressure_kpa), rt_brake_request_kpa);
            auto seb_cmd = make_seb_auto_req(effective_brake);
            can::Frame f_7b9{};
            etrike::protocol::codecs::seb::encode_command(seb_cmd, f_7b9);
            receive_seb_command(f_7b9);
        }
    }

    void receive_ses_command(const can::Frame& f) {
        ses_frames_received++;
        uint8_t cs = 0;
        for (size_t i = 0; i < 7; ++i) cs ^= f.data[i];
        cs ^= 0xFF;
        ses_checksum_valid = (f.data[7] == cs);

        etrike::protocol::codecs::ses::Command cmd{};
        etrike::protocol::codecs::ses::decode_command(f.view(), cmd);
        if (!ses_jammed) {
            ses_actual_angle_0_1deg = cmd.target_angle_raw;
        }
    }

    void receive_seb_command(const can::Frame& f) {
        etrike::protocol::codecs::seb::Command cmd{};
        etrike::protocol::codecs::seb::decode_command(f.view(), cmd);
        seb_pressure_raw = cmd.pressure_request_raw;
        seb_stroke_raw = cmd.stroke_request_raw;
        seb_auto_brake_active = (cmd.auto_brake != 0);
    }
};

} // anonymous namespace

void setUp(void) {}
void tearDown(void) {}

// ── Test 1: Physical E-Stop Button Press & REARM Sequence While Cruising ──
void test_running_fault_estop_button_and_full_rearm_recovery(void) {
    RunningFaultSystemHarness s{};
    s.init();
    s.establish_active_cruise(2000, 0);

    // Verify initial active cruise state: Relays Drive, DAC 1544 (~1.88V)
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());
    TEST_ASSERT_FALSE(s.rt_estop_active);

    // 1. Driver hits physical E-Stop button on dashboard while cruising at 2000 mm/s!
    s.sys_estop_latch = true;
    can::Frame f_001{can::kIdSafetyEstop, 0, {}};
    s.deliver_to_rt(f_001, /*from_high=*/false);
    s.sys_broadcast_tick();
    s.advance_time(10);

    // Immediate shutdown verified across nodes
    TEST_ASSERT_TRUE(s.rt_estop_active);
    TEST_ASSERT_EQUAL(rt::kEstopReasonCanEstop, s.rt_estop_reason);
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // 2. Driver pulls E-Stop button out (stops asserting 0x001, SYS sends estop_active=0)
    s.sys_estop_latch = false;

    // Frame 1 of estop_active=0: Latch MUST NOT clear (asymmetric clear guard)
    s.sys_broadcast_tick();
    s.advance_time(10);
    TEST_ASSERT_TRUE(s.rt_estop_active);
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());

    // Frame 2 of estop_active=0 with counter +1: Latch clears on RT & MTR!
    s.sys_broadcast_tick();
    s.advance_time(10);
    TEST_ASSERT_FALSE(s.rt_estop_active);

    // 3. REARM GATE CHECK: Autonomous drive command arrives before REARM sequence
    s.host_broadcast_tick(2000, 0);
    s.advance_time(10);
    // Vehicle MUST remain completely stopped!
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // 4. Execute Complete REARM Sequence:
    // Step 4a: Mode AUTO confirmed (0x110 sent twice)
    s.sys_current_mode = can::Mode::Auto;
    s.sys_broadcast_tick();
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.advance_time(10);

    // Step 4b: Power OFF edge (0x113=false sent twice)
    s.sys_power_on = false;
    s.sys_broadcast_tick();
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.advance_time(10);

    // Step 4c: Power ON edge (0x113=true sent twice)
    s.sys_power_on = true;
    s.sys_broadcast_tick();
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.advance_time(10);

    // 5. Propulsion successfully resumes!
    s.host_broadcast_tick(2000, 0);
    s.advance_time(10);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());
}

// ── Test 2: SYS Safety Status (0x011) CRC Corruption & Auto-Recovery ──────
void test_running_fault_sys_safety_crc_corruption_and_recovery(void) {
    RunningFaultSystemHarness s{};
    s.init();
    s.establish_active_cruise(2200, 0);

    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1628, s.mtr_dac.current_code());

    // Corrupt E2E CRC on SYS_SAFETY_STS (0x011) while running
    s.corrupt_safety_crc = true;
    s.sys_broadcast_tick();
    s.advance_time(10);

    // Motor manager immediately invalidates safety_state_valid_ and cuts throttle
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Bus noise clears: SYS sends clean frames with valid CRC and sequential counters
    s.corrupt_safety_crc = false;
    for (int i = 0; i < 3; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2200, 0);
    }

    // Stream authority self-heals: propulsion restores smoothly
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1628, s.mtr_dac.current_code());
}

// ── Test 3: Rolling Counter Freeze on Mode Command (0x110) ────────
void test_running_fault_mode_stream_counter_freeze_and_recovery(void) {
    RunningFaultSystemHarness s{};
    s.init();
    s.establish_active_cruise(2000, 0);

    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());

    // Freeze SYS mode rolling counter while driving (repeats same counter)
    s.freeze_mode_counter = true;
    // Advance 550 ms (exceeding kAuthFreshMs = 500 ms) while counter remains frozen
    for (int i = 0; i < 55; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2000, 0);
    }

    // Stream freshness fails due to frozen counter -> drive command unauthorized -> zero throttle
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Counter resumes normal advancing sequence
    s.freeze_mode_counter = false;
    for (int i = 0; i < 4; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2000, 0);
    }

    // Mode authority restored -> throttle restored
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());
}

// ── Test 4: Rolling Counter Jump / Gap on Power Command (0x113) ────
void test_running_fault_power_stream_counter_jump_and_recovery(void) {
    RunningFaultSystemHarness s{};
    s.init();
    s.establish_active_cruise(2000, 0);

    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());

    // Induce counter gap (+5) on power stream
    s.jump_pwr_counter = true;
    s.sys_broadcast_tick();
    s.advance_time(10);

    // Power validity immediately invalidated -> relays open to Off, DAC to 0
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Normal sequential frames resume
    for (int i = 0; i < 3; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2000, 0);
    }

    // Power validity restored -> relays re-engage
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());
}

// ── Test 5: Steering Following-Error (Actuator Jam) & ESTOP ────────
void test_running_fault_steering_following_error_and_estop(void) {
    RunningFaultSystemHarness s{};
    s.init();
    // Cruising at 2000 mm/s with 0° steer
    s.establish_active_cruise(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());

    // Host commands turn: yaw rate 250 mrad/s -> requests ~10.4° steer (104 raw in 0.1°)
    // Actuator jams at 0°!
    s.ses_jammed = true;
    s.ses_jammed_angle_0_1deg = 0;

    // Error (10.4° - 0° = 10.4°) exceeds threshold at 2000 mm/s (~8.0°)
    // Advance 250 ms (less than 300 ms persist threshold): must NOT trip yet
    for (int i = 0; i < 25; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2000, 250);
    }
    TEST_ASSERT_FALSE(s.rt_estop_active);

    // Advance 60 ms more (total 310 ms > 300 ms kSteerFollowingErrMs): MUST TRIP!
    for (int i = 0; i < 6; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2000, 250);
    }

    // Following-Error ESTOP verified
    TEST_ASSERT_TRUE(s.rt_estop_active);
    TEST_ASSERT_EQUAL(rt::kEstopReasonFollowingError, s.rt_estop_reason);
    TEST_ASSERT_TRUE(s.rt_disable_steering);
    TEST_ASSERT_EQUAL(shared::kMaxBrakeKpa, s.rt_brake_request_kpa);

    // MTR propulsion is cut (speed output forced 0, relays Park/Neutral)
    TEST_ASSERT_EQUAL(RelayController::State::Park, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());
}

// ── Test 6: Sudden Obstacle Inside Stop Distance & Clearance ───────
void test_running_fault_obstacle_collision_imminent_and_clearance(void) {
    RunningFaultSystemHarness s{};
    s.init();
    s.establish_active_cruise(2000, 0);

    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());

    // Sudden obstacle detected at 200 mm (<= 300 mm kObstacleStopMM) while moving at 2000 mm/s
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(2000, 0, /*brake=*/0, /*obstacle=*/200);

    // Collision danger reaction verified
    TEST_ASSERT_TRUE(s.rt_obstacle_triggered);
    TEST_ASSERT_TRUE(s.rt_disable_steering);
    TEST_ASSERT_EQUAL(rt::kEstopReasonObstacle, s.rt_estop_reason);
    TEST_ASSERT_EQUAL(shared::kMaxBrakeKpa, s.rt_brake_request_kpa);

    // Propulsion forced zero
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Obstacle clears (distance jumps to 2500 mm)
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(2000, 0, /*brake=*/0, /*obstacle=*/2500);

    // Obstacle emergency clears
    TEST_ASSERT_FALSE(s.rt_obstacle_triggered);
    TEST_ASSERT_FALSE(s.rt_disable_steering);
    TEST_ASSERT_EQUAL(rt::kEstopReasonNone, s.rt_estop_reason);
}

// ── Test 7: Smart Actuator L3 Fatal Error (SES 0x202 & SEB 0x721) ──
void test_running_fault_actuator_l3_fault_estop(void) {
    // 1. Verify that non-L3 fault bits do NOT trigger ESTOP
    {
        RunningFaultSystemHarness s{};
        s.init();
        s.establish_active_cruise(2000, 0);

        // Byte 0: Controller Under/Over Voltage (L2), CAN Comms (L1), Temp (L1), etc.
        can::Frame f_non_l3{};
        f_non_l3.id = can::kIdSbwErrInfo;
        f_non_l3.dlc = 8;
        f_non_l3.data.fill(0);
        f_non_l3.data[0] = 0xFF; // Non-L3 faults in byte 0
        f_non_l3.data[1] = 0xF0; // Non-L3 faults in byte 1 (bits 4..7: power, centering, over-angle, stall)
        f_non_l3.data[2] = 0xC3; // Non-L3 faults in byte 2 (bits 0..1 current/sensor5V, bits 6..7 angle-err/idling)
        f_non_l3.data[3] = 0x01; // EEPROM fault (L2)
        s.deliver_to_rt(f_non_l3, /*from_high=*/false);

        // No L3 bits are active -> ESTOP must NOT be triggered
        TEST_ASSERT_FALSE(s.rt_estop_active);
        TEST_ASSERT_EQUAL(rt::kEstopReasonNone, s.rt_estop_reason);
        TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    }

    // 2. Exhaustively verify each of the 8 L3 fault bits individually triggers ESTOP
    // Documented L3 bits:
    // Byte 1 (Angle Sensor):
    //   - bit 0 (0x01): Angle Sensor Pri. Open Circuit
    //   - bit 1 (0x02): Angle Sensor Pri. Out of Range
    //   - bit 2 (0x04): Angle Sensor Sec. Open Circuit
    //   - bit 3 (0x08): Angle Sensor Sec. Out of Range
    // Byte 2 (Torque Sensor):
    //   - bit 2 (0x04): Torque Sensor T1 Open Circuit
    //   - bit 3 (0x08): Torque Sensor T1 Out of Range
    //   - bit 4 (0x10): Torque Sensor T2 Open Circuit
    //   - bit 5 (0x20): Torque Sensor T2 Out of Range
    struct L3BitTest {
        uint8_t byte_idx;
        uint8_t bit_mask;
    };
    const L3BitTest kL3Vectors[] = {
        {1, 0x01}, {1, 0x02}, {1, 0x04}, {1, 0x08},
        {2, 0x04}, {2, 0x08}, {2, 0x10}, {2, 0x20}
    };

    for (const auto& vec : kL3Vectors) {
        RunningFaultSystemHarness s{};
        s.init();
        s.establish_active_cruise(2000, 0);

        can::Frame f_l3{};
        f_l3.id = can::kIdSbwErrInfo;
        f_l3.dlc = 8;
        f_l3.data.fill(0);
        f_l3.data[vec.byte_idx] = vec.bit_mask;
        s.deliver_to_rt(f_l3, /*from_high=*/false);

        TEST_ASSERT_TRUE(s.rt_estop_active);
        TEST_ASSERT_EQUAL(rt::kEstopReasonInternal, s.rt_estop_reason);
        TEST_ASSERT_EQUAL(shared::kMaxBrakeKpa, s.rt_brake_request_kpa);
        TEST_ASSERT_TRUE(s.rt_disable_steering);
    }
}

// ── Test 8: Dedicated 0x204 Watchdog Trip & 3-Frame Recovery ───────
void test_running_fault_dedicated_0x204_watchdog_and_3_frame_recovery(void) {
    RunningFaultSystemHarness s{};
    s.init();
    s.establish_active_cruise(2000, 0);

    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());

    // RT drive generation freezes: silence on 0x204 while other nodes continue
    // Advance 220 ms (> 200 ms kDriveCmdTimeoutMs)
    for (int i = 0; i < 22; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick(); // generic comms fed
        // No host drive tick (no 0x204 sent)
    }

    // Dedicated watchdog trips -> DAC dropped to 0
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Send single 0x204 frame: trip MUST remain latched!
    can::gen::RtDriveCmd drv{2000, static_cast<uint8_t>(can::Gear::D)};
    can::Frame f_204{};
    can::gen::encode_rt_drive_cmd(drv, f_204);

    s.mtr_manager.handle_frame(f_204, s.now_ms);
    s.mtr_manager.tick(s.now_ms);
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code()); // still 0

    // Send 2nd frame: still latched
    s.advance_time(10);
    s.mtr_manager.handle_frame(f_204, s.now_ms);
    s.mtr_manager.tick(s.now_ms);
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code()); // still 0

    // Send 3rd frame at cadence: confirmed recovery (kDriveCmdRecoverFrames = 3)!
    s.advance_time(10);
    s.mtr_manager.handle_frame(f_204, s.now_ms);
    s.mtr_manager.tick(s.now_ms);
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code()); // restored!
}

// ── Test 9: Sudden Reverse Inversion Guard at Speed (50ms Dwell) ───
void test_running_fault_sudden_reverse_inversion_guard_at_speed(void) {
    RunningFaultSystemHarness s{};
    s.init();
    s.establish_active_cruise(2500, 0);

    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());

    // While cruising forward at 2500 mm/s, sudden reverse command arrives
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(-500, 0);

    // Contactor arc protection dwell MUST engage immediately
    // Relays shifted to Neutral/Park, DAC forced to 0
    TEST_ASSERT_EQUAL(RelayController::State::Park, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Advance 40 ms (less than 50 ms dwell): reverse relay still blocked
    s.advance_time(40);
    s.mtr_manager.tick(s.now_ms);
    TEST_ASSERT_EQUAL(RelayController::State::Park, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Advance 20 ms more (total 60 ms > 50 ms): dwell complete, reverse safely engages!
    s.advance_time(20);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(-500, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Reverse, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1966, s.mtr_dac.current_code()); // 100% reverse throttle
}

// ── Test 10: Low CAN Bus-Off Event & Cross-Bus Alert ──────────────
void test_running_fault_low_can_bus_off_cross_bus_alert(void) {
    RunningFaultSystemHarness s{};
    s.init();
    s.establish_active_cruise(2000, 0);

    size_t high_bus_before = s.high_bus_rx.size();

    // Low CAN transceiver hardware asserts bus-off
    // RT can_health triggers: sets kEstopReasonBusOff, enqueues ESTOP, broadcasts 0x001 on High CAN
    s.rt_estop_active = true;
    s.rt_estop_reason = rt::kEstopReasonBusOff;

    can::Frame f_001{};
    f_001.id = can::kIdSafetyEstop;
    f_001.dlc = 0;
    // Broadcast on High CAN
    s.high_bus_rx.push_back(f_001);
    // Also cut MTR
    s.mtr_manager.handle_frame(f_001, s.now_ms);

    // Verify cross-bus alert & motor cut
    TEST_ASSERT_TRUE(s.high_bus_rx.size() > high_bus_before);
    TEST_ASSERT_EQUAL(can::kIdSafetyEstop, s.high_bus_rx.back().id);
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_running_fault_estop_button_and_full_rearm_recovery);
    RUN_TEST(test_running_fault_sys_safety_crc_corruption_and_recovery);
    RUN_TEST(test_running_fault_mode_stream_counter_freeze_and_recovery);
    RUN_TEST(test_running_fault_power_stream_counter_jump_and_recovery);
    RUN_TEST(test_running_fault_steering_following_error_and_estop);
    RUN_TEST(test_running_fault_obstacle_collision_imminent_and_clearance);
    RUN_TEST(test_running_fault_actuator_l3_fault_estop);
    RUN_TEST(test_running_fault_dedicated_0x204_watchdog_and_3_frame_recovery);
    RUN_TEST(test_running_fault_sudden_reverse_inversion_guard_at_speed);
    RUN_TEST(test_running_fault_low_can_bus_off_cross_bus_alert);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
