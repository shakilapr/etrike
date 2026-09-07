#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>

#include "stub/stm32g4xx_hal.h"
#include "protocol/compat/can.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"
#include "physics_model.h"
#include "motor_manager.h"
#include "shared_config.h"

using namespace rt;
using namespace mtr;

namespace {

// Simulated CAN Network Environment to coordinate all active streams
class ActiveNetworkHarness {
public:
    uint8_t sys_mode_ctr{0};
    uint8_t sys_pwr_ctr{0};
    uint8_t sys_safety_ctr{0};
    uint8_t host_alive_ctr{0};
    uint8_t ses_ctr{0};

    uint32_t current_time_ms{100};

    PhysicsModel rt_physics{};
    RelayController mtr_relays{};
    DacController mtr_dac{};
    MotorManager mtr_manager{mtr_relays, mtr_dac};

    void init() {
        hal_mock::reset();
        mtr_manager.init();
    }

    // Step time forward
    void advance_time(uint32_t dt_ms) {
        current_time_ms += dt_ms;
        mtr_manager.tick(current_time_ms);
    }

    // SYS sends 0x011 SYS_SAFETY_STS (CRC-8 protected, rolling counter)
    void send_sys_safety_status(bool estop_active, bool heartbeat_ok = true) {
        can::gen::SysSafetySts msg{};
        msg.estop_active = estop_active;
        msg.heartbeat_ok = heartbeat_ok;
        msg.rolling_counter = sys_safety_ctr++;
        msg.e2e_crc = 0;

        can::Frame tmp{};
        can::gen::encode_sys_safety_sts(msg, tmp);
        msg.e2e_crc = static_cast<std::uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()));

        can::Frame f{};
        can::gen::encode_sys_safety_sts(msg, f);
        mtr_manager.handle_frame(f, current_time_ms);
    }

    // SYS sends 0x110 SYS_MODE_CMD (Mode authority stream)
    void send_sys_mode(can::Mode mode) {
        can::gen::SysModeCmd msg{};
        msg.mode = (mode == can::Mode::Auto);
        msg.rolling_counter = sys_mode_ctr++;

        can::Frame f{};
        can::gen::encode_sys_mode_cmd(msg, f);
        mtr_manager.handle_frame(f, current_time_ms);
    }

    // SYS sends 0x113 SYS_PWR_CMD (Power authority stream)
    void send_sys_power(bool power_on) {
        can::gen::SysPwrCmd msg{};
        msg.power_state = power_on;
        msg.rolling_counter = sys_pwr_ctr++;

        can::Frame f{};
        can::gen::encode_sys_pwr_cmd(msg, f);
        mtr_manager.handle_frame(f, current_time_ms);
    }

    // Host sends 0x7FC HOST_HEARTBEAT
    can::Frame create_host_heartbeat() {
        can::gen::HostHeartbeat msg{};
        msg.alive_ctr = host_alive_ctr++;
        msg.health_flags = 0;
        can::Frame f{};
        can::gen::encode_host_heartbeat(msg, f);
        return f;
    }

    // High CAN 0x300 HOST_DRIVE_CMD -> RT Physics -> Low CAN 0x204 RT_DRIVE_CMD -> MTR
    struct ControlPipelineResult {
        int32_t resolved_motor_speed_mmps;
        int32_t resolved_steer_angle_mdeg;
        bool steer_valid;
        bool steer_saturated;
        bool reversing;
        can::Gear gear;
        uint16_t dac_code;
        float dac_volts;
        RelayController::State relay_state;
    };

    ControlPipelineResult send_host_drive_command(int32_t speed_mmps, int32_t yaw_rate_mrad_s) {
        ControlPipelineResult res{};

        // 1. Encode High-level 0x300 HOST_DRIVE_CMD frame
        can::gen::HostDriveCmd host{};
        host.speed_mmps = speed_mmps;
        host.yaw_rate_mrad_s = yaw_rate_mrad_s;
        host.gear = 0;
        can::Frame high_f{};
        TEST_ASSERT_EQUAL(can::gen::CodecStatus::Ok, can::gen::encode_host_drive_cmd(host, high_f));

        // 2. RT Gateway receives and decodes 0x300
        can::gen::HostDriveCmd decoded_host{};
        TEST_ASSERT_EQUAL(can::gen::CodecStatus::Ok, can::gen::decode_host_drive_cmd(high_f.view(), decoded_host));

        // 3. RT Physics Kinematics Solver
        DriveCmd drive_cmd{decoded_host.speed_mmps, decoded_host.yaw_rate_mrad_s};
        ResolvedSetpoint sp{};
        TEST_ASSERT_TRUE(rt_physics.resolve(drive_cmd, sp));

        res.resolved_motor_speed_mmps = sp.motor_speed_mmps;
        res.resolved_steer_angle_mdeg = sp.steer_angle_mdeg;
        res.steer_valid = sp.steer_valid;
        res.steer_saturated = sp.steer_saturated;
        res.reversing = sp.reversing;

        can::Gear gear = sp.motor_speed_mmps > 0 ? can::Gear::D
                       : sp.motor_speed_mmps < 0 ? can::Gear::R
                       : can::Gear::N;
        res.gear = gear;

        // 4. RT encodes 0x204 RT_DRIVE_CMD setpoint onto Low CAN
        can::gen::RtDriveCmd rt_drive{};
        rt_drive.motor_speed_mmps = sp.motor_speed_mmps;
        rt_drive.gear = static_cast<uint8_t>(gear);
        can::Frame low_204{};
        TEST_ASSERT_EQUAL(can::gen::CodecStatus::Ok, can::gen::encode_rt_drive_cmd(rt_drive, low_204));

        // 5. MTR receives 0x204 and ticks
        mtr_manager.handle_frame(low_204, current_time_ms);
        mtr_manager.tick(current_time_ms);

        res.dac_code = mtr_dac.current_code();
        res.dac_volts = static_cast<float>(res.dac_code) / 4095.0f * 5.0f;
        res.relay_state = mtr_relays.state();
        return res;
    }

    // Establish baseline stream validity for all authority streams
    void establish_active_streams(can::Mode mode = can::Mode::Auto) {
        // StreamValidity requires two consecutive advancing frames to establish authority
        send_sys_safety_status(false);
        send_sys_power(true);
        send_sys_mode(mode);
        advance_time(10);

        send_sys_safety_status(false);
        send_sys_power(true);
        send_sys_mode(mode);
        advance_time(10);
    }
};

} // anonymous namespace

void setUp(void) {}
void tearDown(void) {}

// ── Test 1: Full Nominal Active Multi-Command Cycle ──────────────
void test_nominal_active_multi_command_cycle(void) {
    ActiveNetworkHarness harness{};
    harness.init();
    harness.establish_active_streams(can::Mode::Auto);

    // Actively send periodic commands at realistic cadences over 200 ms (20 steps of 10 ms)
    ActiveNetworkHarness::ControlPipelineResult res{};
    for (int step = 0; step < 20; ++step) {
        harness.advance_time(10);

        // SYS safety at 5 Hz (every 200 ms / 20 steps)
        if (step % 20 == 0) {
            harness.send_sys_safety_status(false);
        }
        // SYS mode & power at 10 Hz (every 100 ms / 10 steps)
        if (step % 10 == 0) {
            harness.send_sys_mode(can::Mode::Auto);
            harness.send_sys_power(true);
        }

        // Host Drive commands @ 100 Hz (every 10 ms)
        res = harness.send_host_drive_command(1800, 150);
    }

    // Verify RT Kinematics resolved correctly
    TEST_ASSERT_EQUAL(1800, res.resolved_motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, 7125, res.resolved_steer_angle_mdeg); // atan(1.5*0.15/1.8) ≈ 7.125°
    TEST_ASSERT_TRUE(res.steer_valid);
    TEST_ASSERT_FALSE(res.reversing);

    // Verify MTR execution in AUTO: Relay Drive engaged and DAC producing forward throttle
    TEST_ASSERT_EQUAL(RelayController::State::Drive, res.relay_state);
    // 700 + (1800/3000)*1266 = 1459.6 -> 1459
    TEST_ASSERT_EQUAL(1459, res.dac_code);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.78f, res.dac_volts);
}

// ── Test 2: Instant ESTOP Failsafe Under Active Driving ───────────
void test_estop_instant_failsafe_under_active_driving(void) {
    ActiveNetworkHarness harness{};
    harness.init();
    harness.establish_active_streams(can::Mode::Auto);

    // Drive actively forward
    auto res = harness.send_host_drive_command(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, res.relay_state);
    TEST_ASSERT_TRUE(res.dac_code > 0);

    // Global ESTOP injected on CAN bus (via 0x001 SAFETY_ESTOP or 0x011 estop_active=1)
    can::Frame estop_001{};
    estop_001.id = can::kIdSafetyEstop; // 0x001
    estop_001.dlc = 0;
    harness.mtr_manager.handle_frame(estop_001, harness.current_time_ms);
    harness.advance_time(5);

    // Verify instantaneous shutdown within 5 ms
    TEST_ASSERT_EQUAL(RelayController::State::Off, harness.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, harness.mtr_dac.current_code());
    TEST_ASSERT_EQUAL(0, harness.mtr_manager.target_speed_mmps());

    // Subsequent host drive commands MUST be rejected while ESTOP is latched
    res = harness.send_host_drive_command(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Off, res.relay_state);
    TEST_ASSERT_EQUAL(0, res.dac_code);
}

// ── Test 3: Asymmetric 2-Frame Clear and REARM Gate Sequence ──────
void test_estop_asymmetric_clear_and_rearm_gate_sequence(void) {
    ActiveNetworkHarness harness{};
    harness.init();
    harness.establish_active_streams(can::Mode::Auto);

    // Inject ESTOP
    harness.send_sys_safety_status(true);
    harness.advance_time(10);
    TEST_ASSERT_EQUAL(RelayController::State::Off, harness.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, harness.mtr_dac.current_code());

    // Step 1: SYS sends ONLY ONE estop_active == 0 frame
    harness.send_sys_safety_status(false);
    harness.advance_time(10);
    // ESTOP MUST NOT CLEAR on a single frame!
    TEST_ASSERT_EQUAL(RelayController::State::Off, harness.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, harness.mtr_dac.current_code());

    // Step 2: SYS sends SECOND consecutive estop_active == 0 frame with advancing counter
    harness.send_sys_safety_status(false);
    harness.advance_time(10);

    // Now ESTOP latch is released, BUT REARM is required!
    // Verify that drive commands CANNOT yet accelerate the vehicle
    auto blocked_res = harness.send_host_drive_command(1500, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Off, blocked_res.relay_state);
    TEST_ASSERT_EQUAL(0, blocked_res.dac_code);

    // Step 3: Execute valid REARM protocol sequence:
    // 1. Establish mode authority (0x110)
    harness.send_sys_mode(can::Mode::Auto);
    harness.send_sys_mode(can::Mode::Auto);
    harness.advance_time(10);

    // 2. Power OFF edge (0x113=false) -> records rearm_off_seen_
    harness.send_sys_power(false);
    harness.send_sys_power(false);
    harness.advance_time(10);

    // 3. Power ON edge (0x113=true with mode_valid_==true) -> sets rearm_observed_
    harness.send_sys_power(true);
    harness.send_sys_power(true);
    harness.advance_time(10);

    // Now send host drive command - propulsion should successfully re-engage!
    auto recovered_res = harness.send_host_drive_command(1500, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, recovered_res.relay_state);
    TEST_ASSERT_EQUAL(1333, recovered_res.dac_code); // 700 + (1500/3000)*1266 = 1333
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.62f, recovered_res.dac_volts);
}

// ── Test 4: Missing Safety Heartbeat Stream Loss & Recovery ──────
void test_safety_heartbeat_stream_loss_timeout_and_recovery(void) {
    ActiveNetworkHarness harness{};
    harness.init();
    harness.establish_active_streams(can::Mode::Auto);

    // Actively driving forward
    auto res = harness.send_host_drive_command(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, res.relay_state);
    TEST_ASSERT_TRUE(res.dac_code > 0);

    // Simulate SYS heartbeat / safety status stream SILENCE (> 700 ms timeout)
    // Drive commands keep arriving, but SYS 0x011 stream is missing!
    for (int step = 0; step < 75; ++step) {
        harness.advance_time(10); // total 750 ms silence on 0x011
        harness.send_host_drive_command(2000, 0);
    }

    // Safety freshness timeout (kSafetyFreshMs = 700 ms) MUST trip!
    // Relays de-energized, DAC code forced to 0
    TEST_ASSERT_EQUAL(RelayController::State::Off, harness.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, harness.mtr_dac.current_code());

    // Stream Recovery: SYS resumes sending 0x011 frames
    harness.send_sys_safety_status(false);
    harness.advance_time(10);
    harness.send_sys_safety_status(false);
    harness.advance_time(10);

    // Also re-fresh power and mode
    harness.send_sys_power(true);
    harness.send_sys_mode(can::Mode::Auto);
    harness.advance_time(10);

    // Vehicle safely recovers propulsion
    auto recover_res = harness.send_host_drive_command(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, recover_res.relay_state);
    TEST_ASSERT_EQUAL(1544, recover_res.dac_code);
}

// ── Test 5: Dedicated 0x204 Drive Command Watchdog Timeout & Confirmation Recovery
void test_drive_command_watchdog_timeout_in_auto_and_recovery(void) {
    ActiveNetworkHarness harness{};
    harness.init();
    harness.establish_active_streams(can::Mode::Auto);

    // Actively driving forward
    auto res = harness.send_host_drive_command(1500, 0);
    TEST_ASSERT_EQUAL(1333, res.dac_code);

    // Keep generic traffic (0x011, 0x110, 0x113) alive, but STOP 0x204 drive commands!
    // Advance time past kDriveCmdStaleMs (200 ms)
    for (int step = 0; step < 25; ++step) {
        harness.advance_time(10); // 250 ms of no 0x204
        if (step % 5 == 0) {
            harness.send_sys_safety_status(false);
            harness.send_sys_mode(can::Mode::Auto);
            harness.send_sys_power(true);
        }
    }

    // Dedicated drive watchdog MUST trip: DAC throttled to 0 even though generic comms are alive!
    TEST_ASSERT_EQUAL(0, harness.mtr_dac.current_code());

    // Confirmed Recovery requires kDriveCmdRecoverFrames (3) consecutive valid 0x204 frames
    // at plausible cadence (<= 50 ms)
    for (int i = 0; i < 3; ++i) {
        harness.advance_time(10);
        res = harness.send_host_drive_command(1500, 0);
    }

    // Watchdog cleared, throttle restored to 1333!
    TEST_ASSERT_EQUAL(RelayController::State::Drive, res.relay_state);
    TEST_ASSERT_EQUAL(1333, res.dac_code);
}

// ── Test 6: Forward-to-Reverse Direction Arc Protection Dwell ─────
void test_direction_dwell_protection_under_active_control(void) {
    ActiveNetworkHarness harness{};
    harness.init();
    harness.establish_active_streams(can::Mode::Auto);

    // 1. Actively driving forward at 2000 mm/s
    auto fwd_res = harness.send_host_drive_command(2000, 0);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, fwd_res.relay_state);
    TEST_ASSERT_EQUAL(1544, fwd_res.dac_code);

    // 2. Command reverse at -400 mm/s with 50 mrad/s yaw
    auto rev_start = harness.send_host_drive_command(-400, 50);

    // RT Physics resolves reverse setpoint with negative angle
    TEST_ASSERT_EQUAL(-400, rev_start.resolved_motor_speed_mmps);
    TEST_ASSERT_TRUE(rev_start.reversing);
    TEST_ASSERT_INT_WITHIN(10, -10620, rev_start.resolved_steer_angle_mdeg); // atan(1.5*0.05/-0.4) ≈ -10.620°

    // MTR enters 50 ms direction shift dwell:
    // Drive relay opens, Reverse relay does NOT close yet (Park state), DAC forced to 0
    TEST_ASSERT_EQUAL(RelayController::State::Park, rev_start.relay_state);
    TEST_ASSERT_EQUAL(0, rev_start.dac_code);

    // Advance 30 ms (still inside 50 ms dwell)
    harness.advance_time(30);
    auto rev_mid = harness.send_host_drive_command(-400, 50);
    TEST_ASSERT_EQUAL(RelayController::State::Park, rev_mid.relay_state);
    TEST_ASSERT_EQUAL(0, rev_mid.dac_code);

    // Advance past 50 ms dwell (30 ms more = 60 ms total)
    harness.advance_time(30);
    auto rev_done = harness.send_host_drive_command(-400, 50);

    // Reverse relay is now safely energized, DAC throttle active in reverse!
    TEST_ASSERT_EQUAL(RelayController::State::Reverse, rev_done.relay_state);
    // Reverse scaling: 700 + (400/500)*1266 = 1712.8 -> 1712
    TEST_ASSERT_EQUAL(1712, rev_done.dac_code);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 2.09f, rev_done.dac_volts);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_nominal_active_multi_command_cycle);
    RUN_TEST(test_estop_instant_failsafe_under_active_driving);
    RUN_TEST(test_estop_asymmetric_clear_and_rearm_gate_sequence);
    RUN_TEST(test_safety_heartbeat_stream_loss_timeout_and_recovery);
    RUN_TEST(test_drive_command_watchdog_timeout_in_auto_and_recovery);
    RUN_TEST(test_direction_dwell_protection_under_active_control);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
