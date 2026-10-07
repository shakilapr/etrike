#include <unity.h>
#include <cstdint>
#include "config.h"
#include "bypass_modes.h"
#include "passthrough_pipeline.h"
#include "task_health.h"

// The runtime bypass and ControlPath state are defined by the firmware main.cpp
// (excluded from the native test build). The bypass tests exercise the SHARED pure
// decision function (bypass_modes.h) and pipeline (passthrough_pipeline.h).
etrike::ControlPath g_control_path = etrike::ControlPath::Production;
etrike::HighLevelLossPolicy g_host_loss_policy = etrike::HighLevelLossPolicy::ZeroOutput;
uint32_t g_host_loss_timeout_ms = 500;
bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = false;
bool g_bypass_seb_sync = false;
bool g_bypass_mtr_absent = false;

static void evaluate_bypass_modes(int run_mode, bool override_pin_low) {
    const etrike::BypassState b = etrike::evaluate_run_mode_bypasses(run_mode, override_pin_low);
    g_control_path = b.control_path;
    g_bench_solo_mode = b.bench_solo_mode;
    g_bypass_eps_sync = b.bypass_eps_sync;
    g_bypass_seb_sync = b.bypass_seb_sync;
    g_bypass_mtr_absent = b.bypass_mtr_absent;
}

void setUp(void) {
    g_control_path = etrike::ControlPath::Production;
    g_bench_solo_mode = false;
    g_bypass_eps_sync = false;
    g_bypass_seb_sync = false;
    g_bypass_mtr_absent = false;
}

void tearDown(void) {}

void test_rt_bypass_modes(void) {
    // 1. Vehicle Production Mode -> Safety strictly enforced (ControlPath::Production)
    evaluate_bypass_modes(0, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::Production),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_FALSE(g_bench_solo_mode);
    TEST_ASSERT_FALSE(g_bypass_eps_sync);
    TEST_ASSERT_FALSE(g_bypass_seb_sync);
    TEST_ASSERT_FALSE(g_bypass_mtr_absent);

    // 2. Hardware Bench without Override Jumper -> Safety strictly enforced
    evaluate_bypass_modes(1, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::Production),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_FALSE(g_bench_solo_mode);
    TEST_ASSERT_FALSE(g_bypass_eps_sync);
    TEST_ASSERT_FALSE(g_bypass_seb_sync);
    TEST_ASSERT_FALSE(g_bypass_mtr_absent);

    // 3. Hardware Bench WITH Active-Low Override Jumper -> BenchPassthrough enabled
    evaluate_bypass_modes(1, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::BenchPassthrough),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_TRUE(g_bench_solo_mode);
    TEST_ASSERT_TRUE(g_bypass_eps_sync);
    TEST_ASSERT_TRUE(g_bypass_seb_sync);
    TEST_ASSERT_TRUE(g_bypass_mtr_absent);

    // 4. Bench Simulation Mode -> BenchPassthrough enabled (jumper state irrelevant)
    evaluate_bypass_modes(2, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::BenchPassthrough),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_TRUE(g_bench_solo_mode);

    evaluate_bypass_modes(2, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::BenchPassthrough),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_TRUE(g_bench_solo_mode);
}

// Rule 11 Test: High-level actuator command -> BenchPassthrough -> same actuator command
// regardless of software faults, missing nodes, mode state, START state, limits, or inhibit bits.
void test_rt_bench_passthrough_speed_command_unclamped_and_ungated(void) {
    rt::HostDriveSnapshot host{};
    host.speed_mmps = 8500;  // Well above vehicle maximum limit (6944 mm/s = 25 km/h)
    host.gear_override = uint8_t(can::Gear::D);
    host.timestamp_us = 1'000'000;

    rt::MotionOutputSnapshot out{};
    // Injected: now is fresh (within 500ms), no direct steer override
    rt::compute_passthrough_motion(
        host,
        1'100'000, // 100ms after host packet -> completely fresh
        etrike::HighLevelLossPolicy::ZeroOutput,
        500,
        0, false, 0,
        out
    );

    // Invariant: Exact speed and gear passed through without vehicle limit clamp or steering gate!
    TEST_ASSERT_EQUAL_INT32(8500, out.motor_speed_mmps);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::D), out.motor_gear);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Mode::Auto), out.current_mode);
    TEST_ASSERT_TRUE(out.steer_command_enable);
    TEST_ASSERT_EQUAL_UINT8(0, out.safety_state);
}

void test_rt_bench_passthrough_steer_command_unclamped(void) {
    rt::HostDriveSnapshot host{};
    host.speed_mmps = 8000;
    host.direct_steer_0_1deg = 380; // 38.0° (Dynamic angle clamp at 8 m/s would clamp to 5.0°!)
    host.direct_steer_valid = true;
    host.timestamp_us = 1'000'000;

    rt::MotionOutputSnapshot out{};
    rt::compute_passthrough_motion(
        host,
        1'100'000,
        etrike::HighLevelLossPolicy::ZeroOutput,
        500,
        380, true, 1500 /* 1500 kPa brake */,
        out
    );

    // Invariant: Steer angle and brake pressure pass through completely unclamped!
    TEST_ASSERT_EQUAL_INT16(380, out.steer_angle_0_1deg);
    TEST_ASSERT_EQUAL_INT32(1500, out.brake_kpa);
}

// Rule 10 Test: High-level command loss policy ZeroOutput
void test_rt_bench_passthrough_loss_policy_zero_output(void) {
    rt::HostDriveSnapshot host{};
    host.speed_mmps = 5000;
    host.gear_override = uint8_t(can::Gear::D);
    host.direct_steer_0_1deg = 200;
    host.direct_steer_valid = true;
    host.timestamp_us = 1'000'000;

    rt::MotionOutputSnapshot out{};
    // Stream lost: 600 ms since last packet (> 500 ms loss timeout)
    rt::compute_passthrough_motion(
        host,
        1'600'001,
        etrike::HighLevelLossPolicy::ZeroOutput,
        500,
        200, true, 500,
        out
    );

    // ZeroOutput policy zeroes all commands on loss
    TEST_ASSERT_EQUAL_INT32(0, out.motor_speed_mmps);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::N), out.motor_gear);
    TEST_ASSERT_EQUAL_INT16(0, out.steer_angle_0_1deg);
    TEST_ASSERT_EQUAL_INT32(0, out.brake_kpa);
}

// Rule 10 Test: High-level command loss policy HoldLast
void test_rt_bench_passthrough_loss_policy_hold_last(void) {
    rt::HostDriveSnapshot host{};
    host.speed_mmps = 5000;
    host.gear_override = uint8_t(can::Gear::D);
    host.direct_steer_0_1deg = 200;
    host.direct_steer_valid = true;
    host.timestamp_us = 1'000'000;

    rt::MotionOutputSnapshot out{};
    // Stream lost: 600 ms since last packet, but policy is HoldLast
    rt::compute_passthrough_motion(
        host,
        1'600'001,
        etrike::HighLevelLossPolicy::HoldLast,
        500,
        200, true, 1000,
        out
    );

    // HoldLast policy preserves previous setpoint
    TEST_ASSERT_EQUAL_INT32(5000, out.motor_speed_mmps);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::D), out.motor_gear);
    TEST_ASSERT_EQUAL_INT16(200, out.steer_angle_0_1deg);
    TEST_ASSERT_EQUAL_INT32(1000, out.brake_kpa);
}

void test_rt_task_health_reflects_actual_task_liveness(void) {
    constexpr int64_t now = 1'000'000;
    constexpr int64_t timeout = 500'000;
    TEST_ASSERT_EQUAL_UINT8(0x0F, rt::task_health_from_timestamps(
        now, now, now, now, timeout));
    TEST_ASSERT_EQUAL_UINT8(0x06, rt::task_health_from_timestamps(
        now, now - timeout - 1, now, now, timeout));
    TEST_ASSERT_EQUAL_UINT8(0x05, rt::task_health_from_timestamps(
        now, now, now - timeout - 1, now, timeout));
    TEST_ASSERT_EQUAL_UINT8(0x03, rt::task_health_from_timestamps(
        now, now, now, now - timeout - 1, timeout));
}

// Realistic Condition 1: Streaming with realistic clock jitter (+/- 3ms) and transient packet drops (40ms)
void test_rt_bench_passthrough_jittery_streaming_and_intermittent_drops(void) {
    rt::HostDriveSnapshot host{};
    rt::MotionOutputSnapshot out{};
    int64_t current_time_us = 1'000'000; // Start at 1.0s

    // Jitter profile representing real scheduler latency on high-level compute: 8ms, 12ms, 9ms, 13ms, 10ms
    const int32_t jitter_steps_us[] = { 8'000, 12'000, 9'000, 13'000, 10'000, 11'000, 7'000, 12'000 };
    const size_t num_jitter = sizeof(jitter_steps_us) / sizeof(jitter_steps_us[0]);

    // Stream 50 cycles of cruise at 3200 mm/s with jitter
    for (int i = 0; i < 50; ++i) {
        current_time_us += jitter_steps_us[i % num_jitter];
        host.speed_mmps = 3200;
        host.gear_override = uint8_t(can::Gear::D);
        host.direct_steer_0_1deg = 120; // 12.0 deg
        host.direct_steer_valid = true;
        host.timestamp_us = current_time_us;

        rt::compute_passthrough_motion(
            host, current_time_us,
            etrike::HighLevelLossPolicy::ZeroOutput,
            500, 120, true, 0, out
        );

        TEST_ASSERT_EQUAL_INT32(3200, out.motor_speed_mmps);
        TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::D), out.motor_gear);
        TEST_ASSERT_EQUAL_INT16(120, out.steer_angle_0_1deg);
    }

    // Now inject a 40ms transient network/CAN drop (4 missed frames, << 500ms timeout)
    current_time_us += 40'000;
    // Host packet timestamp NOT updated!
    rt::compute_passthrough_motion(
        host, current_time_us,
        etrike::HighLevelLossPolicy::ZeroOutput,
        500, 120, true, 0, out
    );

    // Invariant: Transient drop under timeout must NOT glitch to zero or interrupt motion!
    TEST_ASSERT_EQUAL_INT32(3200, out.motor_speed_mmps);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::D), out.motor_gear);
    TEST_ASSERT_EQUAL_INT16(120, out.steer_angle_0_1deg);
}

// Realistic Condition 2: Dynamic multi-phase maneuver with high-speed slalom angles
// Proves angle is NEVER clamped to dynamic rollover limit (5.0 deg at high speed)
void test_rt_bench_passthrough_dynamic_motion_trajectory_sweeps(void) {
    rt::HostDriveSnapshot host{};
    rt::MotionOutputSnapshot out{};
    int64_t t_us = 10'000'000; // 10.0s baseline

    // Phase 1: Rapid acceleration 0 -> 4500 mm/s (well beyond 25 km/h vehicle limit)
    for (int32_t spd = 0; spd <= 4500; spd += 150) {
        t_us += 10'000; // 100 Hz
        host.speed_mmps = spd;
        host.gear_override = spd > 0 ? uint8_t(can::Gear::D) : uint8_t(can::Gear::N);
        host.timestamp_us = t_us;

        rt::compute_passthrough_motion(
            host, t_us, etrike::HighLevelLossPolicy::ZeroOutput, 500,
            0, false, 0, out
        );

        TEST_ASSERT_EQUAL_INT32(spd, out.motor_speed_mmps);
        TEST_ASSERT_EQUAL_UINT8(spd > 0 ? uint8_t(can::Gear::D) : uint8_t(can::Gear::N), out.motor_gear);
    }

    // Phase 2: High-speed slalom sweep -35.0 deg to +35.0 deg while travelling at 4500 mm/s
    // In Production mode, dynamic limit would clamp this to [-5.0 deg, +5.0 deg]!
    for (int16_t ang = -350; ang <= 350; ang += 25) {
        t_us += 10'000;
        host.speed_mmps = 4500;
        host.direct_steer_0_1deg = ang;
        host.direct_steer_valid = true;
        host.timestamp_us = t_us;

        rt::compute_passthrough_motion(
            host, t_us, etrike::HighLevelLossPolicy::ZeroOutput, 500,
            ang, true, 0, out
        );

        // Passthrough preserves EXACT requested angle across entire sweep
        TEST_ASSERT_EQUAL_INT16(ang, out.steer_angle_0_1deg);
        TEST_ASSERT_EQUAL_INT32(4500, out.motor_speed_mmps);
    }

    // Phase 3: Emergency service braking ramp (0 -> 3500 kPa) with motor cut
    for (int32_t p_kpa = 500; p_kpa <= 3500; p_kpa += 500) {
        t_us += 10'000;
        host.speed_mmps = 0;
        host.gear_override = uint8_t(can::Gear::N);
        host.timestamp_us = t_us;

        rt::compute_passthrough_motion(
            host, t_us, etrike::HighLevelLossPolicy::ZeroOutput, 500,
            0, true, p_kpa, out
        );

        TEST_ASSERT_EQUAL_INT32(0, out.motor_speed_mmps);
        TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::N), out.motor_gear);
        TEST_ASSERT_EQUAL_INT32(p_kpa, out.brake_kpa);
    }
}

// Realistic Condition 3: Full wire-format CAN frame encoding and roundtrip decoding
void test_rt_bench_passthrough_wire_format_codec_roundtrip(void) {
    // 1. Simulate High CAN 0x300 and 0x303 frames arriving on wire
    can::gen::HostDriveCmd host_drive_wire{};
    host_drive_wire.speed_mmps = 2850;
    host_drive_wire.yaw_rate_mrad_s = 0;
    host_drive_wire.gear = uint8_t(can::Gear::D);
    can::Frame f_300{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::encode_frame(host_drive_wire, f_300)));

    can::gen::HostSteerCmd host_steer_wire{};
    host_steer_wire.steer_angle_0_1deg = 275; // 27.5 deg
    host_steer_wire.angle_valid = true;
    host_steer_wire.reserved = 0;
    host_steer_wire.rolling_counter = 5;
    can::Frame f_303{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::encode_frame(host_steer_wire, f_303)));

    // Decode from wire
    can::gen::HostDriveCmd decoded_drive{};
    can::gen::HostSteerCmd decoded_steer{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::decode_frame(f_300, decoded_drive)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::decode_frame(f_303, decoded_steer)));

    // Populate snapshot and execute passthrough pipeline
    rt::HostDriveSnapshot snap{};
    snap.speed_mmps = decoded_drive.speed_mmps;
    snap.gear_override = decoded_drive.gear;
    snap.direct_steer_0_1deg = decoded_steer.steer_angle_0_1deg;
    snap.direct_steer_valid = decoded_steer.angle_valid;
    snap.timestamp_us = 5'000'000;

    rt::MotionOutputSnapshot out{};
    rt::compute_passthrough_motion(
        snap, 5'005'000, etrike::HighLevelLossPolicy::ZeroOutput, 500,
        decoded_steer.steer_angle_0_1deg, decoded_steer.angle_valid, 800, out
    );

    // 2. Encode to outgoing Low CAN wire frames:
    // 0x204 (RT_DRIVE_CMD)
    can::gen::RtDriveCmd out_drive{out.motor_speed_mmps, out.motor_gear};
    can::Frame f_204{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::encode_frame(out_drive, f_204)));

    // 0x169 (VCU_SES_REQ with vendor offset 30000)
    can::custom::ses::Command out_ses{};
    out_ses.target_angle_raw = static_cast<int16_t>(out.steer_angle_0_1deg + 30000);
    out_ses.control_enable = true;
    out_ses.alignment_enable = false;
    out_ses.target_speed_raw = 328;
    can::Frame f_169{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::custom::ses::encode_command(out_ses, f_169)));

    // 0x205 (RT_BRAKE_CMD)
    can::gen::RtBrakeCmd out_brake{out.brake_kpa};
    can::Frame f_205{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::encode_frame(out_brake, f_205)));

    // 3. Decode outgoing wire frames and verify exact physical values
    can::gen::RtDriveCmd dec_204{};
    can::gen::RtBrakeCmd dec_205{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::decode_frame(f_204, dec_204)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::decode_frame(f_205, dec_205)));

    TEST_ASSERT_EQUAL_INT32(2850, dec_204.motor_speed_mmps);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::D), dec_204.gear);
    TEST_ASSERT_EQUAL_INT32(800, dec_205.brake_pressure_kpa);

    can::custom::ses::Command dec_ses{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::custom::ses::decode_command(f_169, dec_ses)));
    TEST_ASSERT_EQUAL_INT16(30275, dec_ses.target_angle_raw); // 275 + 30000
    TEST_ASSERT_TRUE(dec_ses.control_enable);
}

// Realistic Condition 4: Communication blackout followed by dynamic re-acquisition
void test_rt_bench_passthrough_blackout_and_dynamic_reacquisition(void) {
    rt::HostDriveSnapshot host{};
    rt::MotionOutputSnapshot out{};
    int64_t t_us = 1'000'000;

    // Normal streaming
    host.speed_mmps = 2400;
    host.gear_override = uint8_t(can::Gear::D);
    host.direct_steer_0_1deg = 150;
    host.direct_steer_valid = true;
    host.timestamp_us = t_us;

    rt::compute_passthrough_motion(
        host, t_us, etrike::HighLevelLossPolicy::ZeroOutput, 500,
        150, true, 0, out
    );
    TEST_ASSERT_EQUAL_INT32(2400, out.motor_speed_mmps);

    // Blackout: host goes completely silent for 700ms (> 500ms loss timeout)
    t_us += 700'000;
    rt::compute_passthrough_motion(
        host, t_us, etrike::HighLevelLossPolicy::ZeroOutput, 500,
        150, true, 0, out
    );
    // Verified: zeroed during blackout
    TEST_ASSERT_EQUAL_INT32(0, out.motor_speed_mmps);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::N), out.motor_gear);
    TEST_ASSERT_EQUAL_INT16(0, out.steer_angle_0_1deg);

    // Host suddenly resumes with a new setpoint (-1200 mm/s Reverse, -18.0 deg)
    t_us += 10'000;
    host.speed_mmps = -1200;
    host.gear_override = uint8_t(can::Gear::R);
    host.direct_steer_0_1deg = -180;
    host.direct_steer_valid = true;
    host.timestamp_us = t_us;

    rt::compute_passthrough_motion(
        host, t_us, etrike::HighLevelLossPolicy::ZeroOutput, 500,
        -180, true, 0, out
    );

    // Verified: immediately re-engages and follows new trajectory seamlessly without reboot
    TEST_ASSERT_EQUAL_INT32(-1200, out.motor_speed_mmps);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::Gear::R), out.motor_gear);
    TEST_ASSERT_EQUAL_INT16(-180, out.steer_angle_0_1deg);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_rt_bypass_modes);
    RUN_TEST(test_rt_bench_passthrough_speed_command_unclamped_and_ungated);
    RUN_TEST(test_rt_bench_passthrough_steer_command_unclamped);
    RUN_TEST(test_rt_bench_passthrough_loss_policy_zero_output);
    RUN_TEST(test_rt_bench_passthrough_loss_policy_hold_last);
    RUN_TEST(test_rt_bench_passthrough_jittery_streaming_and_intermittent_drops);
    RUN_TEST(test_rt_bench_passthrough_dynamic_motion_trajectory_sweeps);
    RUN_TEST(test_rt_bench_passthrough_wire_format_codec_roundtrip);
    RUN_TEST(test_rt_bench_passthrough_blackout_and_dynamic_reacquisition);
    RUN_TEST(test_rt_task_health_reflects_actual_task_liveness);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
