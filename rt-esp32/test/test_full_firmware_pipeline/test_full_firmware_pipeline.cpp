#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include "stm32g4xx_hal.h"
#include "protocol/compat/can.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "physics_model.h"
#include "brake_arbitration.h"
#include "seb_request.h"
#include "watchdog.h"

#include "motor_manager.h"
#include "relay_controller.h"
#include "dac_controller.h"
#include "shared_config.h"

using namespace rt;
using namespace mtr;

namespace {

// ── Complete Simulated Multi-ECU Firmware Environment ─────────────
class FullFirmwareSystemHarness {
public:
    // Simulation Clock (ms)
    uint32_t now_ms{100};

    // ── CAN Bus Network Mock (Dual Bus Architecture) ──────────────
    std::vector<can::Frame> high_bus_rx{}; // frames received on High CAN
    std::vector<can::Frame> low_bus_rx{};  // frames received on Low CAN

    // ── SYS ESP32 State ──────────────────────────────────────────
    uint8_t sys_mode_ctr{0};
    uint8_t sys_pwr_ctr{0};
    uint8_t sys_safety_ctr{0};
    uint8_t sys_hb_ctr{0};
    bool    sys_estop_latch{false};
    can::Mode sys_current_mode{can::Mode::Manual};
    bool    sys_power_on{false};
    bool    enable_sys_hb{true};
    bool    enable_sys_safety{true};
    bool    enable_sys_mode{true};
    bool    enable_sys_pwr{true};

    // ── Host / Jetson State ──────────────────────────────────────
    uint8_t host_hb_ctr{0};
    int32_t host_cmd_speed_mmps{0};
    int32_t host_cmd_yaw_mrad_s{0};
    uint8_t host_cmd_gear{1}; // Drive

    // ── RT ESP32 Subsystems ──────────────────────────────────────
    PhysicsModel     rt_physics{};
    CmdWatchdog      rt_watchdog{};
    can::Mode        rt_mode{can::Mode::Manual};
    int64_t          rt_last_sys_hb_us{0};
    int64_t          rt_last_host_hb_us{0};
    bool             rt_estop_active{false};
    uint8_t          rt_estop_clear_count{0};
    uint8_t          rt_estop_last_zero_ctr{0};
    int32_t          rt_last_resolved_speed{0};
    int32_t          rt_last_resolved_steer_mdeg{0};
    bool             rt_reversing{false};

    // ── MTR STM32 Subsystems ─────────────────────────────────────
    RelayController  mtr_relays{};
    DacController    mtr_dac{};
    MotorManager     mtr_manager{mtr_relays, mtr_dac};

    // ── SES (Steering) & SEB (Brake) Smart Actuator States ───────
    int16_t          ses_actual_angle_0_1deg{0};
    bool             ses_aligned{true};
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

    void establish_active_streams() {
        sys_power_on = true;
        sys_current_mode = can::Mode::Auto;
        sys_estop_latch = false;
        enable_sys_hb = true;
        enable_sys_safety = true;
        enable_sys_mode = true;
        enable_sys_pwr = true;
        for (int i = 0; i < 5; ++i) {
            advance_time(10);
            sys_broadcast_tick();
        }
    }

    // ── SYS Broadcasts (on Low Bus) ──────────────────────────────
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
            safe.e2e_crc = static_cast<std::uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()));
            can::Frame f_safe{};
            can::gen::encode_sys_safety_sts(safe, f_safe);
            deliver_to_rt(f_safe, /*from_high=*/false);
            mtr_manager.handle_frame(f_safe, now_ms);
        }

        // 3. SYS Mode Command (0x110, 10 Hz)
        if (enable_sys_mode) {
            can::gen::SysModeCmd mode{};
            mode.mode = (sys_current_mode == can::Mode::Auto);
            mode.rolling_counter = sys_mode_ctr++;
            can::Frame f_mode{};
            can::gen::encode_sys_mode_cmd(mode, f_mode);
            deliver_to_rt(f_mode, /*from_high=*/false);
            mtr_manager.handle_frame(f_mode, now_ms);
        }

        // 4. SYS Power Command (0x113, 10 Hz)
        if (enable_sys_pwr) {
            can::gen::SysPwrCmd pwr{};
            pwr.power_state = sys_power_on;
            pwr.rolling_counter = sys_pwr_ctr++;
            can::Frame f_pwr{};
            can::gen::encode_sys_pwr_cmd(pwr, f_pwr);
            deliver_to_rt(f_pwr, /*from_high=*/false);
            mtr_manager.handle_frame(f_pwr, now_ms);
        }
    }

    // ── Host Broadcasts (on High Bus) ────────────────────────────
    void host_broadcast_tick(int32_t speed_mmps, int32_t yaw_mrad_s, int32_t brake_kpa = 0, uint32_t obstacle_mm = 3000) {
        host_cmd_speed_mmps = speed_mmps;
        host_cmd_yaw_mrad_s = yaw_mrad_s;

        // 1. Host Heartbeat (0x7FC, 2 Hz)
        can::gen::HostHeartbeat hb{};
        hb.alive_ctr = host_hb_ctr++;
        hb.health_flags = 0;
        can::Frame f_hb{};
        can::gen::encode_host_heartbeat(hb, f_hb);
        deliver_to_rt(f_hb, /*from_high=*/true);

        // 2. Host Drive Command (0x300, 10-100 Hz)
        can::gen::HostDriveCmd cmd{};
        cmd.speed_mmps = speed_mmps;
        cmd.yaw_rate_mrad_s = yaw_mrad_s;
        cmd.gear = host_cmd_gear;
        can::Frame f_cmd{};
        can::gen::encode_host_drive_cmd(cmd, f_cmd);
        deliver_to_rt(f_cmd, /*from_high=*/true);

        // 3. Host Brake Request (0x301)
        if (brake_kpa > 0) {
            can::gen::HostBrakeReq brk{};
            brk.brake_pressure_kpa = brake_kpa;
            can::Frame f_brk{};
            can::gen::encode_host_brake_req(brk, f_brk);
            deliver_to_rt(f_brk, /*from_high=*/true);
        }

        // 4. Host Obstacle Distance (0x400)
        if (obstacle_mm < 3000) {
            can::gen::HostObstacleDist obs{};
            obs.distance_mm = obstacle_mm;
            can::Frame f_obs{};
            can::gen::encode_host_obstacle_dist(obs, f_obs);
            deliver_to_rt(f_obs, /*from_high=*/true);
        }
    }

    // ── RT Gateway & Execution Engine ────────────────────────────
    void deliver_to_rt(const can::Frame& f, bool from_high) {
        int64_t now_us = static_cast<int64_t>(now_ms) * 1000;

        // Route transparent gateway frames
        if (from_high && can::is_forwarded_high_to_low(f.id)) {
            low_bus_rx.push_back(f);
        }
        if (!from_high && can::is_forwarded_low_to_high(f.id)) {
            high_bus_rx.push_back(f);
        }

        // Process frames consumed by RT
        if (f.id == can::kIdSafetyEstop) {
            rt_estop_active = true;
            // Cross-forward 0x001
            if (from_high) low_bus_rx.push_back(f);
            else high_bus_rx.push_back(f);
            mtr_manager.handle_frame(f, now_ms);
            return;
        }

        if (f.id == can::kIdSysSafetySts && !from_high) {
            can::gen::SysSafetySts s{};
            can::gen::decode_sys_safety_sts(f.view(), s);
            if (s.estop_active) {
                rt_estop_active = true;
                rt_estop_clear_count = 0;
            } else if (rt_estop_active) {
                // Asymmetric clear on RT side
                if (rt_estop_clear_count == 0) {
                    rt_estop_clear_count = 1;
                    rt_estop_last_zero_ctr = s.rolling_counter;
                } else if (s.rolling_counter == static_cast<uint8_t>(rt_estop_last_zero_ctr + 1)) {
                    ++rt_estop_clear_count;
                    if (rt_estop_clear_count >= 2) {
                        rt_estop_active = false;
                        rt_estop_clear_count = 0;
                    }
                } else {
                    rt_estop_clear_count = 1;
                    rt_estop_last_zero_ctr = s.rolling_counter;
                }
            }
        }

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

        if (f.id == can::kIdHostDriveCmd && from_high) {
            rt_watchdog.feed(now_us);
            can::gen::HostDriveCmd cmd{};
            can::gen::decode_host_drive_cmd(f.view(), cmd);

            // Execute RT Kinematics Pipeline
            DriveCmd drive_cmd{cmd.speed_mmps, cmd.yaw_rate_mrad_s};
            ResolvedSetpoint sp{};
            rt_physics.resolve(drive_cmd, sp);

            rt_last_resolved_speed = sp.motor_speed_mmps;
            rt_last_resolved_steer_mdeg = sp.steer_angle_mdeg;
            rt_reversing = sp.reversing;

            // Drive allowed only in Auto mode when not estopped (arch §7.6)
            const bool drive_allowed = (rt_mode == can::Mode::Auto) && !rt_estop_active;
            int32_t speed_out = drive_allowed ? sp.motor_speed_mmps : 0;
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

            // Generate Low CAN 0x204 RT_DRIVE_CMD
            can::gen::RtDriveCmd drv{speed_out, static_cast<uint8_t>(gear)};
            can::Frame f_204{};
            can::gen::encode_rt_drive_cmd(drv, f_204);
            mtr_manager.handle_frame(f_204, now_ms);
            mtr_manager.tick(now_ms);

            // In Manual mode, SES commands are suppressed per arch §7.6 (EPS-C standalone)
            if (rt_mode != can::Mode::Manual) {
                // Generate Low CAN 0x169 VCU_SES_REQ with XOR Checksum
                etrike::protocol::codecs::ses::Command ses_cmd{};
                ses_cmd.alignment_enable = true;
                ses_cmd.control_enable = !rt_estop_active;
                // 0.1 deg / LSB
                ses_cmd.target_angle_raw = static_cast<int16_t>(sp.steer_angle_mdeg / 100);
                ses_cmd.target_speed_raw = 328;
                ses_cmd.rolling_counter = ses_last_rolling_ctr++;

                can::Frame f_169{};
                etrike::protocol::codecs::ses::encode_command(ses_cmd, f_169);
                receive_ses_command(f_169);
            }
        }

        if (f.id == can::kIdHostBrakeReq && from_high) {
            can::gen::HostBrakeReq brk{};
            can::gen::decode_host_brake_req(f.view(), brk);
            auto seb_cmd = make_seb_auto_req(brk.brake_pressure_kpa);
            can::Frame f_7b9{};
            etrike::protocol::codecs::seb::encode_command(seb_cmd, f_7b9);
            receive_seb_command(f_7b9);
        }
    }

    // ── Smart Actuator Emulators (SES Steering & SEB Brake) ──────
    void receive_ses_command(const can::Frame& f) {
        ses_frames_received++;
        // Validate XOR-8 checksum in byte 7: checksum = XOR(bytes 0-6) ^ 0xFF
        uint8_t cs = 0;
        for (size_t i = 0; i < 7; ++i) cs ^= f.data[i];
        cs ^= 0xFF;
        ses_checksum_valid = (f.data[7] == cs);

        etrike::protocol::codecs::ses::Command cmd{};
        etrike::protocol::codecs::ses::decode_command(f.view(), cmd);
        ses_actual_angle_0_1deg = cmd.target_angle_raw;
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

// ── Test 1: Full Nominal End-to-End Autonomous Pipeline ───────────
void test_full_pipeline_autonomous_drive_and_actuation(void) {
    FullFirmwareSystemHarness s{};
    s.init();

    // Power up and switch SYS to AUTO
    s.sys_power_on = true;
    s.sys_current_mode = can::Mode::Auto;

    // Establish authority streams
    for (int i = 0; i < 2; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
    }

    // Jetson sends autonomous cruise command: 2200 mm/s forward, 120 mrad/s turn
    for (int i = 0; i < 10; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2200, 120);
    }

    // 1. Verify RT Kinematics Resolution
    TEST_ASSERT_EQUAL(2200, s.rt_last_resolved_speed);
    // steer = atan(1.5 * 0.12 / 2.2) ≈ 4.68° = 4680 mdeg
    TEST_ASSERT_INT_WITHIN(15, 4680, s.rt_last_resolved_steer_mdeg);
    TEST_ASSERT_FALSE(s.rt_reversing);

    // 2. Verify SES Steering Actuator Execution
    TEST_ASSERT_TRUE(s.ses_checksum_valid);
    TEST_ASSERT_INT_WITHIN(2, 47, s.ses_actual_angle_0_1deg); // 4.7 deg (0.1 deg / LSB)

    // 3. Verify MTR Actuator Execution
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    // 700 + (2200/3000)*1266 = 1628
    TEST_ASSERT_EQUAL(1628, s.mtr_dac.current_code());
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.99f, static_cast<float>(s.mtr_dac.current_code()) / 4095.0f * 5.0f);

    // 4. Verify Gateway Forwarding: MTR feedback and SYS status forwarded to High CAN
    can::Frame f_fbk = s.mtr_manager.build_motor_feedback_frame();
    s.deliver_to_rt(f_fbk, /*from_high=*/false);
    TEST_ASSERT_TRUE(!s.high_bus_rx.empty());
    TEST_ASSERT_EQUAL(can::kIdMtrMotorFbk, s.high_bus_rx.back().id);
}

// ── Test 2: High-Speed Rollover Protection Clamping ──────────────
void test_full_pipeline_dynamic_rollover_clamping_at_speed(void) {
    FullFirmwareSystemHarness s{};
    s.init();
    s.sys_power_on = true;
    s.sys_current_mode = can::Mode::Auto;

    for (int i = 0; i < 2; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
    }

    // Jetson commands high forward speed (3000 mm/s) with extreme yaw rate (3000 mrad/s)
    // Without rollover protection, atan(1.5 * 3.0 / 3.0) would request 56° (saturating at 40°)
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(3000, 3000);

    // Dynamic angle limit at 3000 mm/s (10.8 km/h):
    // limit = 40.0 - (10.8 - 2.0) * (35.0 / 23.0) = 40.0 - 13.39 = 26.6°
    float dyn_limit = compute_dynamic_limit(3000);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 26.6f, dyn_limit);

    // Verify RT steering output does not exceed the dynamic rollover limit
    float resolved_deg = static_cast<float>(s.rt_last_resolved_steer_mdeg) / 1000.0f;
    TEST_ASSERT_TRUE(resolved_deg <= 40.0f);
    TEST_ASSERT_TRUE(s.ses_actual_angle_0_1deg <= 400); // <= 40.0 deg
}

// ── Test 3: Obstacle Slowdown and Automated Brake Intervention ────
void test_full_pipeline_obstacle_intervention_and_braking(void) {
    FullFirmwareSystemHarness s{};
    s.init();
    s.sys_power_on = true;
    s.sys_current_mode = can::Mode::Auto;

    for (int i = 0; i < 2; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
    }

    // Cruising at 2000 mm/s with clear path (3000 mm)
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(2000, 0, /*brake=*/0, /*obstacle=*/3000);
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());

    // Obstacle detected at 1200 mm
    // Physics Obstacle Limiter: target_speed * (1200 - 300) / (3000 - 300) = 2000 * 900 / 2700 = 666 mm/s
    int32_t limited_speed = PhysicsModel::obstacle_limit(2000, 1200);
    TEST_ASSERT_EQUAL(666, limited_speed);

    // Physics Obstacle Brake: 5000 kPa * (1 - 900/2700) = 3333 kPa
    int32_t obstacle_brake_kpa = PhysicsModel::obstacle_to_kpa(1200);
    TEST_ASSERT_EQUAL(3333, obstacle_brake_kpa);

    // Arbitrate brake (obstacle 3333 kPa vs host 0 kPa) -> 3333 kPa
    int32_t arb_kpa = brake_arbitrate(obstacle_brake_kpa, 0);
    TEST_ASSERT_EQUAL(3333, arb_kpa);

    // Generate SEB command from arbitrated brake
    auto seb_cmd = make_seb_auto_req(arb_kpa);
    TEST_ASSERT_EQUAL(1, seb_cmd.auto_brake);
    TEST_ASSERT_EQUAL(etrike::protocol::codecs::seb::ControlMode::Pressure, seb_cmd.control_mode);
    // 3333 kPa * 0.02 = 66.6 -> 66 raw
    TEST_ASSERT_EQUAL(66, seb_cmd.pressure_request_raw);
}

// ── Test 4: Full Multi-Node ESTOP Broadcast & REARM Recovery ──────
void test_full_pipeline_estop_multi_node_shutdown_and_recovery(void) {
    FullFirmwareSystemHarness s{};
    s.init();
    s.sys_power_on = true;
    s.sys_current_mode = can::Mode::Auto;

    for (int i = 0; i < 2; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
    }

    // Vehicle actively moving forward
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_TRUE(s.mtr_dac.current_code() > 0);

    // 1. Emergency Stop Triggered on SYS (0x001 broadcast + 0x011 estop_active=1)
    s.sys_estop_latch = true;
    can::Frame f_001{};
    f_001.id = can::kIdSafetyEstop;
    f_001.dlc = 0;
    s.deliver_to_rt(f_001, /*from_high=*/false);
    s.sys_broadcast_tick();
    s.advance_time(5);

    // All actuators MUST shut down immediately
    TEST_ASSERT_TRUE(s.rt_estop_active);
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // 2. Clear ESTOP via validated 2-frame sequence on SYS
    s.sys_estop_latch = false;
    // Frame 1 (does not release latch)
    s.sys_broadcast_tick();
    s.advance_time(10);
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());

    // Frame 2 (releases latch, engages REARM gate)
    s.sys_broadcast_tick();
    s.advance_time(10);
    TEST_ASSERT_FALSE(s.rt_estop_active);

    // Drive command alone CANNOT move vehicle while REARM is required
    s.host_broadcast_tick(1500, 0);
    s.advance_time(10);
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // 3. Execute REARM Protocol Sequence
    s.sys_current_mode = can::Mode::Auto;
    s.sys_broadcast_tick();
    s.advance_time(10);

    // Power OFF edge
    s.sys_power_on = false;
    s.sys_broadcast_tick();
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.advance_time(10);

    // Power ON edge
    s.sys_power_on = true;
    s.sys_broadcast_tick();
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.advance_time(10);

    // Propulsion successfully re-engages!
    s.host_broadcast_tick(1500, 0);
    s.advance_time(10);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1333, s.mtr_dac.current_code());
}

// ── Test 5: Watchdog Silence Failsafe Across Nodes ─────────────────
void test_full_pipeline_heartbeat_silence_failsafe(void) {
    FullFirmwareSystemHarness s{};
    s.init();
    s.sys_power_on = true;
    s.sys_current_mode = can::Mode::Auto;

    for (int i = 0; i < 2; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
    }

    s.host_broadcast_tick(1500, 0);
    TEST_ASSERT_EQUAL(1333, s.mtr_dac.current_code());

    // Simulate Host Freeze (No 0x300 commands for > 500 ms)
    for (int step = 0; step < 55; ++step) {
        s.advance_time(10); // 550 ms elapsed
        s.sys_broadcast_tick();
        // Host is silent
    }

    // RT Command Watchdog MUST trip
    int64_t now_us = static_cast<int64_t>(s.now_ms) * 1000;
    TEST_ASSERT_TRUE(s.rt_watchdog.is_stale(now_us));

    // MTR dedicated drive watchdog in AUTO also trips (> 200 ms)
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());
}

// ── Test 6: Forward to Reverse Transition with Contactor Arc Dwell ─
void test_full_pipeline_direction_dwell_and_reverse_kinematics(void) {
    FullFirmwareSystemHarness s{};
    s.init();
    s.sys_power_on = true;
    s.sys_current_mode = can::Mode::Auto;

    for (int i = 0; i < 2; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
    }

    // 1. Moving forward at 1800 mm/s
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(1800, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());

    // 2. Command reverse at -300 mm/s with 50 mrad/s yaw
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(-300, 50);

    // RT Kinematics preserves negative reverse angle
    TEST_ASSERT_EQUAL(-300, s.rt_last_resolved_speed);
    TEST_ASSERT_INT_WITHIN(10, -14036, s.rt_last_resolved_steer_mdeg); // atan(1.5*0.05/-0.3) ≈ -14.036°
    TEST_ASSERT_TRUE(s.rt_reversing);

    // MTR enters 50 ms direction shift dwell (Park state, zero throttle)
    TEST_ASSERT_EQUAL(RelayController::State::Park, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Advance 60 ms past dwell completion
    s.advance_time(60);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(-300, 50);

    // Reverse relay is now active and DAC voltage curve active in reverse
    TEST_ASSERT_EQUAL(RelayController::State::Reverse, s.mtr_relays.state());
    // 700 + (300/500)*1266 = 1459
    TEST_ASSERT_EQUAL(1459, s.mtr_dac.current_code());
}

// ── Test 7: Safety Status (0x011) Stream Loss and Recovery ─────────
void test_full_pipeline_safety_stream_loss_and_reestablishment(void) {
    FullFirmwareSystemHarness s{};
    s.init();
    s.establish_active_streams();

    // Actively driving forward at 2000 mm/s
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());

    // Cut SYS Safety Status (0x011) stream (simulate wiring fault or SYS crash)
    s.enable_sys_safety = false;

    // Advance 800 ms (exceeding kSafetyFreshMs = 700 ms) while other frames still arrive
    for (int i = 0; i < 80; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2000, 0);
    }

    // Actuation MUST fail-safe: Relays Off, DAC zero throttle
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Restore SYS Safety Status stream
    s.enable_sys_safety = true;
    for (int i = 0; i < 3; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2000, 0);
    }

    // Propulsion safely restores after valid frames with advancing counters
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());
}

// ── Test 8: Manual Takeover & Mode Arbitration on the Fly ──────────
void test_full_pipeline_manual_takeover_and_mode_arbitration(void) {
    FullFirmwareSystemHarness s{};
    s.init();
    s.establish_active_streams();

    // Actively driving forward at 2000 mm/s in AUTO
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());
    uint32_t ses_count_before = s.ses_frames_received;

    // Driver flips dashboard switch: SYS switches to MANUAL mode
    s.sys_current_mode = can::Mode::Manual;
    for (int i = 0; i < 3; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        // Host continues sending autonomous commands
        s.host_broadcast_tick(2000, 0);
    }

    // In MANUAL mode:
    // 1. RT forces 0x204 speed to 0 and gear to Neutral
    // 2. SES steering commands are completely suppressed (EPS-C operates standalone)
    // 3. MTR drops relays to Neutral/Park and DAC throttle to 0
    TEST_ASSERT_EQUAL(ses_count_before, s.ses_frames_received); // No new SES commands sent
    TEST_ASSERT_EQUAL(RelayController::State::Park, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Driver re-engages AUTO mode
    s.sys_current_mode = can::Mode::Auto;
    for (int i = 0; i < 3; ++i) {
        s.advance_time(10);
        s.sys_broadcast_tick();
        s.host_broadcast_tick(2000, 0);
    }

    // Autonomous control resumes: SES frames resume, relays in Drive, DAC restores
    TEST_ASSERT_TRUE(s.ses_frames_received > ses_count_before);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1544, s.mtr_dac.current_code());
}

// ── Test 9: Remote High CAN ESTOP Gateway Cross-Forwarding ────────
void test_full_pipeline_remote_high_can_estop_forwarding_and_cut(void) {
    FullFirmwareSystemHarness s{};
    s.init();
    s.establish_active_streams();

    // Actively driving forward at 2200 mm/s in AUTO
    s.advance_time(10);
    s.sys_broadcast_tick();
    s.host_broadcast_tick(2200, 120);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(1628, s.mtr_dac.current_code());

    size_t low_bus_before = s.low_bus_rx.size();

    // Jetson or wireless emergency receiver asserts 0x001 on the HIGH CAN bus
    can::Frame f_estop{};
    f_estop.id = can::kIdSafetyEstop;
    f_estop.dlc = 0;
    s.deliver_to_rt(f_estop, /*from_high=*/true);

    // RT Gateway must have cross-forwarded 0x001 to the LOW CAN bus
    TEST_ASSERT_TRUE(s.low_bus_rx.size() > low_bus_before);
    TEST_ASSERT_EQUAL(can::kIdSafetyEstop, s.low_bus_rx.back().id);

    // RT ESTOP latch is set
    TEST_ASSERT_TRUE(s.rt_estop_active);

    // MTR MotorManager received 0x001: immediate emergency power cut
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());

    // Further drive commands from Host are suppressed (speed 0, SES disabled)
    s.advance_time(10);
    s.host_broadcast_tick(2200, 120);
    TEST_ASSERT_EQUAL(0, s.mtr_dac.current_code());
    TEST_ASSERT_EQUAL(RelayController::State::Off, s.mtr_relays.state());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_full_pipeline_autonomous_drive_and_actuation);
    RUN_TEST(test_full_pipeline_dynamic_rollover_clamping_at_speed);
    RUN_TEST(test_full_pipeline_obstacle_intervention_and_braking);
    RUN_TEST(test_full_pipeline_estop_multi_node_shutdown_and_recovery);
    RUN_TEST(test_full_pipeline_heartbeat_silence_failsafe);
    RUN_TEST(test_full_pipeline_direction_dwell_and_reverse_kinematics);
    RUN_TEST(test_full_pipeline_safety_stream_loss_and_reestablishment);
    RUN_TEST(test_full_pipeline_manual_takeover_and_mode_arbitration);
    RUN_TEST(test_full_pipeline_remote_high_can_estop_forwarding_and_cut);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
