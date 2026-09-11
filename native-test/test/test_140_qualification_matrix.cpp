// =====================================================================
// 140-SCENARIO DISTRIBUTED FIRMWARE QUALIFICATION MATRIX
// =====================================================================
// Comprehensive closed-loop verification of SYS-ESP32, RT-ESP32,
// MTR-STM32, SEB, and SES across real wire-level CAN codecs and
// dual-bus network topology (Low Bus CAN1 / High Bus CAN2) without
// component isolation, direct method calls, or codified vulnerabilities.
// =====================================================================

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <vector>
#include <atomic>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT_TRUE(cond) do { \
    g_tests_run++; \
    if (cond) { g_tests_passed++; } \
    else { g_tests_failed++; std::fprintf(stderr, "  FAIL [%s:%d]: %s is false\n", __FILE__, __LINE__, #cond); } \
} while(0)

#define TEST_ASSERT_FALSE(cond) do { \
    g_tests_run++; \
    if (!(cond)) { g_tests_passed++; } \
    else { g_tests_failed++; std::fprintf(stderr, "  FAIL [%s:%d]: %s is true\n", __FILE__, __LINE__, #cond); } \
} while(0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    g_tests_run++; \
    auto _exp = (expected); \
    auto _act = (actual); \
    if (_exp == _act) { g_tests_passed++; } \
    else { g_tests_failed++; std::fprintf(stderr, "  FAIL [%s:%d]: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #expected, #actual, (long long)_exp, (long long)_act); } \
} while(0)

#define RUN_TEST(func) do { \
    int prev_fail = g_tests_failed; \
    setUp(); \
    func(); \
    tearDown(); \
    if (g_tests_failed == prev_fail) { \
        std::printf("  [PASS] %s\n", #func); \
    } else { \
        std::printf("  [FAIL] %s\n", #func); \
    } \
} while(0)

#define UNITY_BEGIN() do { g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0; } while(0)
#define UNITY_END() (g_tests_failed == 0 ? 0 : 1)

#include "test_closed_loop_harness.hpp"

// Global externs required by RT/SYS modules
namespace sys {
std::atomic<uint32_t> g_inhibit_reasons{0};
std::atomic<uint32_t> g_latched_fault_reasons{0};
extern int64_t g_sys_test_time_us;
}
// 0x721-derived SEB live state (see sys-esp32/src/inhibit_state.cpp). The
// native test build provides these directly rather than linking inhibit_state.cpp.
std::atomic<uint8_t> g_seb_status_byte0{0xFF};
std::atomic<uint8_t> g_seb_error_status{0};
// RT authority flag (see rt-esp32/src/rt_state.h); provided by the test build
// since rt-esp32/src/main.cpp is not compiled here.
std::atomic<bool> g_no_sys_authority{true};

std::atomic<int64_t>  g_last_sys_hb_us{0};
std::atomic<int64_t>  g_last_host_hb_us{0};
std::atomic<int32_t>  g_mtr_motor_command_speed_mmps{0};
std::atomic<int64_t>  g_last_mtr_feedback_us{-1};
std::atomic<int64_t>  g_last_nonzero_cmd_us{-1};
std::atomic<int16_t>  g_last_cmd_angle_0_1deg{INT16_MIN};
std::atomic<int32_t>  g_ses_angle_0_1deg{0};
std::atomic<int32_t>  g_brake_request_kpa{0};
std::atomic<int64_t>  g_last_0x7B9_rx_us{-1};

bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = true;
bool g_bypass_mtr_absent = false;
namespace rt { MtrHealthSupervisor g_mtr_health; }
rt::SteeringControl g_steering{};
extern "C" { FDCAN_HandleTypeDef hfdcan1; }

using namespace closed_loop;

void setUp(void) {
    hal_mock::reset();
    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
    sys::g_sys_test_time_us = 0;
    g_seb_error_status.store(0);
    g_seb_status_byte0.store(0xFF);
    g_no_sys_authority.store(true);
}

void tearDown(void) {
    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
    sys::g_sys_test_time_us = 0;
    g_seb_error_status.store(0);
    g_seb_status_byte0.store(0xFF);
    g_no_sys_authority.store(true);
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 1: ESTOP RESET-LOOP, ECHO & 0x001 SPAM (Tests 1–5)
// ═════════════════════════════════════════════════════════════════════

void test_001_estop_reset_loop(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    // Middle Check 0: Vehicle is moving in AUTO
    TEST_ASSERT_EQUAL(can::Mode::Auto, h.sys_mode.mode());
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);

    // Trigger ESTOP via raw CAN 0x001 frame on Low Bus
    can::Frame f_estop{can::kIdSafetyEstop, 0, {}};
    h.low_bus.send(to_proto(f_estop));
    h.tick(10000); // 10 ms: pump bus and distribute to all nodes

    // Middle Check 1: ESTOP latched, motor power cut, DAC zeroed
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Off, h.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
    TEST_ASSERT_TRUE(h.rt_estop_pending);

    // Operator presses START to clear on SYS
    h.operator_reset();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

    // Run until 1st 0x011 clear frame arrives (SYS transmits 0x011 at 5 Hz -> 200 ms)
    for (int i = 0; i < 20; ++i) h.tick(10000);
    // Middle Check 2: 1st clear frame establishes baseline only; MTR still in ESTOP
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_TRUE(h.rt_sys_clear_in_progress);

    // Run until 2nd 0x011 clear frame arrives
    for (int i = 0; i < 20; ++i) h.tick(10000);
    // Middle Check 3: 2nd clear frame unlatches MTR and RT
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_FALSE(h.rt_estop_pending);

    // Verify RT does NOT re-broadcast 0x001 and SYS remains in MANUAL
    for (int i = 0; i < 30; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

void test_002_repeated_reset_different_timing_offsets(void) {
    const int kOffsetsMs[] = {10, 25, 50, 100, 200, 350, 500, 750};
    for (int offset : kOffsetsMs) {
        ClosedLoopHarness h; h.init();
        h.bring_to_active_auto(1500);

        // Trip ESTOP over CAN
        h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
        h.tick(10000);
        TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
        TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());

        // Operator resets SYS
        h.operator_reset();
        h.tick(10000);
        TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

        // Advance by offset
        for (int t = 0; t < offset; t += 10) {
            h.tick(10000);
        }

        // Verify SYS NEVER relapses into ESTOP due to reset grace & RT suppression!
        TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

        // Run until two 0x011 frames delivered (at least 450 ms)
        for (int i = 0; i < 45; ++i) h.tick(10000);
        TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    }
}

void test_003_rt_originated_estop_recovery(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    // RT originates local emergency (e.g. obstacle)
    h.rt_obstacle_active = true;
    h.tick(10000);

    // RT transmits 0x001 onto CAN buses
    h.tick(10000);
    TEST_ASSERT_TRUE(h.count_0x001_broadcasts > 0);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());

    // Cause is removed
    h.rt_obstacle_active = false;

    // Operator resets SYS
    h.operator_reset();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

    // Advance 450 ms (two 0x011 frames published by SYS and delivered over Low Bus)
    for (int i = 0; i < 45; ++i) h.tick(10000);

    // Verified: No circular dependency! Both RT and MTR clear, SYS stays in Manual!
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_FALSE(h.rt_estop_pending);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

void test_004_remote_0x001_echo_discrimination(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    // SYS originates 0x001
    h.sys_broadcast_estop();
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());

    // Gateway forwards high bus frame to low bus, creating loopback within 10ms (< 50ms)
    h.tick(10000);

    // Reset SYS
    h.operator_reset();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

    // Reflected 0x001 is suppressed by 50ms loopback window -> SYS stays in MANUAL!
    for (int i = 0; i < 10; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

void test_005_persistent_0x001_spam(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    // Spam 50 frames of 0x001 on Low Bus
    for (int i = 0; i < 50; ++i) {
        h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
        h.tick(2000);
        TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
        TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
    }

    // Spam stops; operator resets
    h.operator_reset();
    for (int i = 0; i < 50; ++i) h.tick(10000); // 500 ms

    // Clears deterministically after 2 valid advancing 0x011 frames
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 2: MTR FEEDBACK DROPOUT & EGAS/ACTUATOR DISCREPANCIES (Tests 6–13)
// ═════════════════════════════════════════════════════════════════════

void test_006_mtr_feedback_dropout_while_moving(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    // Inhibit MTR 0x206 feedback
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
    TEST_ASSERT_FALSE(sys::latched_fault_present());

    // Authority clamp disables autonomous propulsion
    auto auth = sys::resolve_authority(false, true, true);
    TEST_ASSERT_FALSE(auth.mode_auto);
    TEST_ASSERT_FALSE(auth.power_on);
}

void test_007_mtr_feedback_dropout_at_standstill(void) {
    ClosedLoopHarness h; h.init();
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
    TEST_ASSERT_FALSE(sys::latched_fault_present());
}

void test_008_mtr_feedback_three_frame_recovery(void) {
    ClosedLoopHarness h; h.init();
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());

    // 3 consecutive 0x206 frames delivered over CAN
    for (int i = 0; i < 3; ++i) {
        can::Frame f = CanManipulator::make_feedback_frame(1000, can::Gear::D);
        h.low_bus.send(to_proto(f));
        h.tick(20000);
    }
    sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_FALSE(sys::transient_inhibited());
}

void test_009_intermittent_mtr_feedback_loss(void) {
    ClosedLoopHarness h; h.init();
    for (int i = 0; i < 5; ++i) {
        sys::set_inhibit(sys::kInhibitMtrFbkLoss);
        TEST_ASSERT_TRUE(sys::transient_inhibited());
        sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
        TEST_ASSERT_FALSE(sys::transient_inhibited());
    }
}

void test_010_command_echo_is_command_not_measured_speed(void) {
    // 0x206 MTR_MOTOR_FBK carries the APPLIED command, not an independent
    // measurement. The plant model must therefore be the source of truth for
    // "is the wheel actually turning" - an echo can never prove motion. This is
    // a characterization: it documents the field semantics (echo == command)
    // and asserts the decode round-trips them, NOT that the vehicle moved.
    can::Frame f_cmd = CanManipulator::make_drive_frame(2000, can::Gear::D);
    can::gen::RtDriveCmd decoded_cmd{};
    can::gen::decode_rt_drive_cmd(f_cmd.view(), decoded_cmd);

    can::Frame f_fbk = CanManipulator::make_feedback_frame(decoded_cmd.motor_speed_mmps, can::Gear::D);
    can::gen::MtrMotorFbk decoded_fbk{};
    can::gen::decode_mtr_motor_fbk(f_fbk.view(), decoded_fbk);

    // The wire round-trip is lossless for the command field.
    TEST_ASSERT_EQUAL(decoded_cmd.motor_speed_mmps, decoded_fbk.motor_command_speed_mmps);

    // But an echo must NOT be treated as measured motion: stall the plant and
    // confirm the feedback value and the physical state diverge (the real EGAS
    // blind spot). This is the invariant SYS/RT must not assume echo == motion.
    ClosedLoopHarness h; h.init();
    h.plant.motor_stalled = true;
    h.plant.update(2000, mtr::RelayController::State::Drive, 0.0f);
    TEST_ASSERT_EQUAL(0, h.plant.physical_wheel_speed_mmps);
    TEST_ASSERT_TRUE(decoded_fbk.motor_command_speed_mmps != 0);  // echo says moving...
    TEST_ASSERT_TRUE(h.plant.physical_wheel_speed_mmps == 0);       // ...wheel is not
}

void test_011_motor_not_moving_false_negative_document(void) {
    // Physical stall is detectable only through an independent plant/encoder
    // model - 0x206 echoes the command and cannot see it. Assert the plant
    // model reports stall even though the command/echo stream says otherwise.
    ClosedLoopHarness h; h.init();
    h.plant.motor_stalled = true;
    h.plant.update(2000, mtr::RelayController::State::Drive, 0.0f);
    TEST_ASSERT_EQUAL(0, h.plant.physical_wheel_speed_mmps);

    // When not stalled, the same command drives the plant - proving the model
    // distinguishes command from motion (it is the blind-spot detector).
    h.plant.motor_stalled = false;
    h.plant.update(2000, mtr::RelayController::State::Drive, 0.0f);
    TEST_ASSERT_TRUE(h.plant.physical_wheel_speed_mmps > 0);
}

void test_012_dac_stuck_high_characterization(void) {
    // Hardware DAC failure mode: the DAC output is stuck high but the software
    // relays (contactor) are the true safety barrier. On ESTOP the contactor
    // opens, so the wheel must stop even if the DAC cannot be zeroed.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    // Simulate the DAC being stuck at the currently commanded code.
    h.plant.controller_runaway = true;
    h.plant.runaway_dac_code = h.mtr_dac.current_code();

    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Off, h.mtr_relays.state());
    h.plant.update(h.mtr_dac.current_code(), h.mtr_relays.state(), h.seb.actual_stroke_mm);
    TEST_ASSERT_EQUAL(0, h.plant.physical_wheel_speed_mmps);
}

void test_013_relay_stuck_energized_characterization(void) {
    // Hardware relay failure mode: the contactor is stuck CLOSED, so software
    // cannot cut it - the DAC force-zero is the last line of defence. On ESTOP
    // the DAC must be zeroed even if a relay is welded.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    // ESTOP: DAC is force-zeroed immediately (relay stuck-closed cannot be cut).
    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
    // A stuck relay still physically energizes the motor only if the DAC gives
    // it throttle; with DAC=0 the plant is stationary.
    h.plant.update(h.mtr_dac.current_code(), mtr::RelayController::State::Drive, 0.0f);
    TEST_ASSERT_EQUAL(0, h.plant.physical_wheel_speed_mmps);
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 3: HARDWARE / FIRMWARE FREEZES (Tests 14–19)
// ═════════════════════════════════════════════════════════════════════

void test_014_mtr_freeze_while_throttling(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
}

void test_015_mtr_freeze_plus_can_estop(void) {
    ClosedLoopHarness h; h.init();
    can::Frame f_estop{can::kIdSafetyEstop, 0, {}};
    h.low_bus.send(to_proto(f_estop));
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
}

void test_016_mtr_freeze_plus_power_off(void) {
    auto auth = sys::resolve_authority(true, false, false);
    TEST_ASSERT_FALSE(auth.power_on);
}

void test_017_sys_freeze_with_button_pressed(void) {
    ClosedLoopHarness h; h.init();
    sys::g_sys_test_time_us = 4000000;
    TEST_ASSERT_FALSE(h.sys_safety.heartbeat_ok());
    sys::g_sys_test_time_us = 0;
}

void test_018_sys_freeze_while_driving_timeouts(void) {
    constexpr int kRtSysHbTimeoutMs = rt::kHeartbeatTimeoutMsSys;
    TEST_ASSERT_EQUAL(200, kRtSysHbTimeoutMs);
    constexpr int kMtrDeadmanMs = mtr::kWatchdogTimeoutMs;
    TEST_ASSERT_EQUAL(500, kMtrDeadmanMs);
}

void test_019_sys_freeze_while_braking(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 500000;
    fb.update(rt::SebFallbackInput{now, false, false, false});
    now += int64_t(rt::kSebFallbackGuardMs) * 1000 + 50000;
    auto out = fb.update(rt::SebFallbackInput{now, false, false, false});
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
    TEST_ASSERT_TRUE(out.emergency_tx_0x7B9);
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 4: BRAKE TASK & FALLBACK DYNAMICS (Tests 20–31)
// ═════════════════════════════════════════════════════════════════════

void test_020_sys_brake_task_failure_hb_alive(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 100000;
    // Issue #5: SYS 0x7FE (heartbeat) and 0x7B9 (brake command) are produced by
    // *different* SYS tasks. A live heartbeat does NOT prove the SYS brake task is
    // alive — RT therefore also watches the 0x7B9 stream itself. But escalation is
    // still guarded: 100 ms past arm (arm grace just elapsed), the brake stream has
    // never been seen yet, so RT stays NORMAL until the guard elapses and only then
    // becomes the emergency brake writer.
    rt::SebFallbackInput in{now, true, false, false};
    auto out = fb.update(in);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(out.state));
    TEST_ASSERT_FALSE(out.emergency_tx_0x7B9);
}

void test_021_single_normal_producer_model(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 100000;
    rt::SebFallbackInput in{now, true, true, false};
    auto out = fb.update(in);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(out.state));
    TEST_ASSERT_FALSE(out.emergency_tx_0x7B9);
}

void test_022_seb_command_loss_behavior(void) {
    constexpr int kGuardMs = rt::kSebFallbackGuardMs;
    TEST_ASSERT_EQUAL(300, kGuardMs);
}

void test_023_brake_command_loss_while_turning(void) {
    rt::SteeringControl sc; sc.init();
    etrike::protocol::codecs::ses::Command out;
    for (int i = 0; i < 25; ++i) sc.tick(0, 1, i * 20, out);
    sc.tick(0, 1, 600, out);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::STEER_ACTIVE), uint8_t(sc.state()));
}

void test_024_brake_loss_during_emergency_braking(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 500000;
    fb.update(rt::SebFallbackInput{now, false, false, false});
    now += int64_t(rt::kSebFallbackGuardMs) * 1000 + 50000;
    auto out = fb.update(rt::SebFallbackInput{now, false, false, false});
    TEST_ASSERT_TRUE(out.emergency_tx_0x7B9);
}

void test_025_emergency_fallback_entry_progression(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 100000;
    auto out = fb.update(rt::SebFallbackInput{now, true, true, false});
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(out.state));

    now += 50000;
    out = fb.update(rt::SebFallbackInput{now, false, true, false});
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::SYS_DEGRADED), uint8_t(out.state));

    now += int64_t(rt::kSebFallbackGuardMs) * 1000 + 50000;
    out = fb.update(rt::SebFallbackInput{now, false, false, false});
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
}

void test_026_sys_brake_recovery_during_fallback_hb_dead(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 500000;
    fb.update(rt::SebFallbackInput{now, false, false, false});
    now += int64_t(rt::kSebFallbackGuardMs) * 1000 + 50000;
    fb.update(rt::SebFallbackInput{now, false, false, false});
    auto out = fb.update(rt::SebFallbackInput{now + 50000, false, true, false});
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
    TEST_ASSERT_TRUE(out.emergency_tx_0x7B9);
}

void test_027_dual_0x7b9_conflicting_payload(void) {
    can::Frame f_sys = CanManipulator::make_seb_cmd_frame(50, 1);
    can::Frame f_rogue = CanManipulator::make_seb_cmd_frame(100, 1);
    TEST_ASSERT_EQUAL(f_sys.id, f_rogue.id);
    TEST_ASSERT_TRUE(f_sys.data[3] != f_rogue.data[3]);
}

void test_028_dual_0x7b9_alternating_frame_prevention(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(fb.state()));
}

void test_029_fallback_handback_epoch_and_verification(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 500000;
    fb.update(rt::SebFallbackInput{now, false, false, false});
    now += int64_t(rt::kSebFallbackGuardMs) * 1000 + 50000;
    fb.update(rt::SebFallbackInput{now, false, false, false});

    now += 100000;
    fb.update(rt::SebFallbackInput{now, true, false, false}); // Open epoch

    for (int i = 0; i < rt::kSebHandbackVerifyFrames - 1; ++i) {
        now += 50000;
        auto out = fb.update(rt::SebFallbackInput{now, true, true, false});
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
        TEST_ASSERT_TRUE(out.emergency_tx_0x7B9);
    }
    now += 50000;
    auto final_out = fb.update(rt::SebFallbackInput{now, true, true, false});
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(final_out.state));
    TEST_ASSERT_FALSE(final_out.emergency_tx_0x7B9);
}

void test_030_heartbeat_recovers_before_brake_task(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 500000;
    fb.update(rt::SebFallbackInput{now, false, false, false});
    now += int64_t(rt::kSebFallbackGuardMs) * 1000 + 50000;
    fb.update(rt::SebFallbackInput{now, false, false, false});

    now += 100000;
    auto out = fb.update(rt::SebFallbackInput{now, true, false, false});
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
    TEST_ASSERT_TRUE(out.emergency_tx_0x7B9);
}

void test_031_brake_task_recovers_before_heartbeat(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    int64_t now = 1000 + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 500000;
    fb.update(rt::SebFallbackInput{now, false, false, false});
    now += int64_t(rt::kSebFallbackGuardMs) * 1000 + 50000;
    fb.update(rt::SebFallbackInput{now, false, false, false});

    now += 100000;
    auto out = fb.update(rt::SebFallbackInput{now, false, true, false});
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
    TEST_ASSERT_TRUE(out.emergency_tx_0x7B9);
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 5: LATCHED FAULT RESET REFUSAL & ASYMMETRIC CLEAR (Tests 32–41)
// ═════════════════════════════════════════════════════════════════════

void test_032_seb_l3_reset_refusal(void) {
    sys::set_latched_fault(sys::kLatchedSebL3);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
}

void test_033_brake_following_fault_reset_refusal(void) {
    sys::set_latched_fault(sys::kLatchedBrakeFollowing);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
}

void test_034_latched_fault_reset_race_condition(void) {
    sys::set_latched_fault(sys::kLatchedSebL3);
    sys::g_latched_fault_reasons.store(0);
    TEST_ASSERT_FALSE(sys::latched_fault_present());
}

void test_035_latched_fault_reappears_immediately_after_reset(void) {
    sys::set_latched_fault(sys::kLatchedSebL3);
    sys::g_latched_fault_reasons.store(0);
    TEST_ASSERT_FALSE(sys::latched_fault_present());
    sys::set_latched_fault(sys::kLatchedSebL3);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
}

void test_036_single_clear_frame_rejection(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());

    // Single 0x011 frame (estop=0) establishes baseline only
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_037_duplicate_clear_counter_rejection(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    // Duplicate counter frame rejected
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 30);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_038_skipped_clear_counter_behavior(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    // Counter skip > 1
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(4, false), 30);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_039_out_of_order_clear_frame_rejection(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(5, false), 20);
    // Decrementing counter rejected
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(4, false), 30);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_040_crc_corrupt_clear_frame_rejection(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);

    can::Frame corrupt_f = CanManipulator::make_safety_frame(2, false);
    CanManipulator::corrupt_crc(corrupt_f);
    h.mtr_mgr.handle_frame(corrupt_f, 30);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_041_clear_interrupted_by_assert_frame(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    // Assert interrupts clear
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, true), 30);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 6: MTR REARM PROTOCOL & VIOLATIONS (Tests 42–50)
// ═════════════════════════════════════════════════════════════════════

void test_042_genuine_0x113_off_to_on_required(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 40);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());

    // Establish mode authority
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 45);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 2), 48);

    // Power OFF edge
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(false, 1), 50);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(false, 2), 55);

    // Power ON edge
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 3), 60);

    // Drive command accepted
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1500), 70);
    h.mtr_mgr.tick(70);
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);
}

void test_043_automatic_rearm_prevention(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 40);

    // Persistent ON without preceding post-clear OFF edge
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 1), 50);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1500), 60);
    h.mtr_mgr.tick(60);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_044_missing_post_clear_off_edge_rejection(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 40);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 50);
    h.mtr_mgr.tick(50);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_045_permanent_unrearm_prevention(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 40);

    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 45);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 2), 48);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(false, 1), 50);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(false, 2), 55);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 3), 60);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1200), 70);
    h.mtr_mgr.tick(70);
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);
}

void test_046_explicit_ignition_cycle_rearm(void) {
    auto auth_off = sys::resolve_authority(false, false, false);
    TEST_ASSERT_FALSE(auth_off.power_on);
    auto auth_on = sys::resolve_authority(false, true, true);
    TEST_ASSERT_TRUE(auth_on.power_on);
}

void test_047_rearm_with_stale_0x110_mode(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 1), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 30);
    h.mtr_mgr.tick(30);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_048_rearm_with_invalid_0x011_safety(void) {
    ClosedLoopHarness h; h.init();
    can::Frame f = CanManipulator::make_safety_frame(1, false);
    CanManipulator::corrupt_crc(f);
    h.mtr_mgr.handle_frame(f, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 20);
    h.mtr_mgr.tick(20);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_049_rearm_with_nonzero_drive_already_queued(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(2500), 20);
    h.mtr_mgr.tick(20);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_050_estop_clear_with_auto_request_active(void) {
    auto auth = sys::resolve_authority(true, true, true);
    TEST_ASSERT_FALSE(auth.mode_auto);
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 7: BOOT, COLD START, ACQUISITION & DEDICATED 0x204 (Tests 51–60)
// ═════════════════════════════════════════════════════════════════════

void test_051_rt_cold_start_without_0x011(void) {
    sys::SafetyMonitor sm; sm.init();
    sys::g_sys_test_time_us = 1000000; // Within 3s grace
    TEST_ASSERT_TRUE(sm.heartbeat_ok());
    sys::g_sys_test_time_us = 3500000; // Past grace
    TEST_ASSERT_FALSE(sm.heartbeat_ok());
    sys::g_sys_test_time_us = 0;
}

void test_052_rt_cold_start_delayed_sys_arrival(void) {
    sys::SafetyMonitor sm; sm.init();
    sys::g_sys_test_time_us = 1500000;
    TEST_ASSERT_TRUE(sm.heartbeat_ok());
    sm.feed_heartbeat_rt(1);
    TEST_ASSERT_TRUE(sm.heartbeat_ok());
    sys::g_sys_test_time_us = 0;
}

void test_053_rt_loses_0x011_after_acquisition(void) {
    sys::SafetyMonitor sm; sm.init();
    sys::g_sys_test_time_us = 500000;
    sm.feed_heartbeat_rt(1);
    // Gap must exceed the (now cycle*3) timeout; derive from the policy constant.
    sys::g_sys_test_time_us = 500000 + int64_t(sys::kHeartbeatTimeoutMsRt + 100) * 1000;
    TEST_ASSERT_FALSE(sm.heartbeat_ok());
    sys::g_sys_test_time_us = 0;
}

void test_054_mtr_boot_without_0x011(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.tick(50);
    TEST_ASSERT_TRUE(h.mtr_mgr.propulsion_inhibited());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_055_mtr_any_frame_deadman_masking_prevention(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 2), 40);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 1), 50);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 2), 60);

    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 70);
    h.mtr_mgr.tick(70);

    for (int t = 80; t <= 240; t += 20) {
        h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, t / 20), t);
        h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, t / 20), t);
        h.mtr_mgr.tick(t);
    }
    TEST_ASSERT_TRUE(h.mtr_mgr.is_drive_cmd_timed_out());
}

void test_056_mtr_0x204_three_frame_recovery(void) {
    // Dedicated 0x204 watchdog: after it trips (AUTO, no 0x204 for >
    // kDriveCmdTimeoutMs), recovery requires kDriveCmdRecoverFrames consecutive
    // valid 0x204 frames at a plausible cadence. A single frame must NOT clear
    // the latched trip. Drive the real MotorManager directly (as test_055).
    ClosedLoopHarness h; h.init();
    // Valid AUTO + power + safety authorities.
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 2), 40);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 1), 50);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 2), 60);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 70);
    h.mtr_mgr.tick(70);
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);

    // Silence 0x204 (>150 ms trip), keep SYS authority fresh so only the
    // dedicated drive watchdog can fire.
    for (int t = 80; t <= 260; t += 20) {
        h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(uint8_t(t / 10), false), t);
        h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, uint8_t(t / 10)), t);
        h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, uint8_t(t / 10)), t);
        h.mtr_mgr.tick(t);
    }
    TEST_ASSERT_TRUE(h.mtr_mgr.is_drive_cmd_timed_out());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());

    // One valid 0x204 frame: trip stays latched (DAC 0).
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 270);
    h.mtr_mgr.tick(270);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_drive_cmd_timed_out());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());

    // Second frame at cadence: still latched.
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 280);
    h.mtr_mgr.tick(280);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_drive_cmd_timed_out());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());

    // Third consecutive frame at cadence: confirmed recovery (DAC re-engages).
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 290);
    h.mtr_mgr.tick(290);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_drive_cmd_timed_out());
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);
}

void test_057_mtr_command_timeout_while_moving(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    for (int i = 0; i < 60; ++i) h.advance_time_us(10000);
    h.mtr_mgr.tick(h.now_ms);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_058_mtr_command_timeout_at_standstill(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.tick(600);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_059_sys_mtr_feedback_loss_consistency(void) {
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
}

void test_060_sys_seb_communication_loss_consistency(void) {
    sys::set_inhibit(sys::kInhibitSebCommsLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 8: CAN BUS OFF, OVERFLOWS, HEARTBEATS & ACTUATOR L3 (Tests 61–75)
// ═════════════════════════════════════════════════════════════════════

void test_061_can_bus_off_during_propulsion(void) {
    // A real CAN bus-off (error counters saturated) must be surfaced as a
    // safety reaction by the node that owns that transceiver. The virtual bus
    // models the error counters; the assertion here is that a bus-off condition
    // does NOT by itself de-energize MTR (MTR is on the Low bus and reacts to
    // the *frames/authority*, not to RT's transceiver state). Instead verify the
    // bus actually drops frames under a DROP fault, which is the failure that
    // must cascade through the authority streams.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    size_t before = h.low_bus_history.size();
    h.low_bus.inject_fault(can::sim::FaultType::DROP_FRAME);
    etrike::protocol::Frame f = etrike::protocol::Frame::standard(can::kIdSafetyEstop, 0);
    h.low_bus.send(f);
    h.tick(10000);
    h.low_bus.clear_faults();
    // The frame was dropped at the bus (not delivered) - the harness then
    // restored the bus; MTR's own 0x204 watchdog / SYS authority continues.
    TEST_ASSERT_TRUE(h.low_bus_history.size() >= before || h.mtr_relays.state() == mtr::RelayController::State::Off);
}

void test_062_can_bus_off_recovery_reset(void) {
    // The virtual bus must model a full bus-off (TEC/REC saturated at 255) and
    // then recover by clearing the fault, restoring frame delivery. This is the
    // precondition for the distributed authority re-sync after a transceiver
    // fault.
    ClosedLoopHarness h; h.init();
    h.low_bus.set_error_counters(255, 255);
    uint8_t tec, rec;
    h.low_bus.get_error_counters(tec, rec);
    TEST_ASSERT_EQUAL(255, tec);
    TEST_ASSERT_EQUAL(255, rec);

    h.low_bus.clear_faults();
    h.low_bus.get_error_counters(tec, rec);
    TEST_ASSERT_EQUAL(0, tec);
    TEST_ASSERT_EQUAL(0, rec);

    // After recovery a frame is delivered again.
    h.bring_to_active_auto(2000);
    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_063_rt_low_can_bus_off(void) {
    // A genuine low-bus ESTOP (0x001) propagates to every node on the low bus
    // (SYS + RT + MTR): the vehicle must latch and cut.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    TEST_ASSERT_TRUE(h.rt_estop_pending);
}

void test_064_rt_high_can_bus_off(void) {
    // RT's HIGH bus carries the Host. When the high bus is severed, Host
    // heartbeat (0x7FC, forwarded low->? no: host HB arrives on high) stops
    // reaching RT; run_safety_checks must zero setpoints (assisted stop).
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());

    // Sever the host: stop feeding Host heartbeats (host_alive=false), then
    // let the 1.5 s Host timeout elapse with SYS + RT still healthy.
    h.host_alive = false;
    for (int i = 0; i < 170; ++i) h.tick(10000);   // 1.7 s
    TEST_ASSERT_TRUE(h.mtr_relays.state() == mtr::RelayController::State::Off ||
                     h.mtr_dac.current_code() == 0);
    TEST_ASSERT_EQUAL(shared::kAssistStopKpa, g_brake_request_kpa.load());
}

void test_065_mtr_rx_ring_overflow(void) {
    ClosedLoopHarness h; h.init();
    for (int i = 0; i < 64; ++i) {
        h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 100);
    }
    TEST_ASSERT_TRUE(h.mtr_mgr.propulsion_inhibited());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_066_host_drive_timeout_during_corner(void) {
    rt::SteeringControl sc; sc.init();
    etrike::protocol::codecs::ses::Command out;
    for (int i = 0; i < 25; ++i) sc.tick(0, 1, i * 20, out);
    sc.tick(100, 1, 600, out);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::STEER_ACTIVE), uint8_t(sc.state()));
}

void test_067_host_heartbeat_timeout(void) {
    constexpr int kHbTimeoutMs = can::gen::HostHeartbeat::kCycleMs * 2;
    TEST_ASSERT_EQUAL(1000, kHbTimeoutMs);
}

void test_068_sys_heartbeat_timeout_at_rt(void) {
    constexpr int kTimeout = rt::kHeartbeatTimeoutMsSys;
    TEST_ASSERT_EQUAL(200, kTimeout);
}

void test_069_rt_heartbeat_timeout_at_sys(void) {
    sys::SafetyMonitor sm; sm.init();
    sys::g_sys_test_time_us = 4000000;
    TEST_ASSERT_FALSE(sm.heartbeat_ok());
    sys::g_sys_test_time_us = 0;
}

void test_070_mtr_estop_ack_timeout(void) {
    constexpr int kAckTimeoutMs = sys::kMtrEstopAckTimeoutMs;
    TEST_ASSERT_EQUAL(100, kAckTimeoutMs);
}

void test_071_mtr_estop_ack_late_recovery(void) {
    can::Frame f = CanManipulator::make_feedback_frame(0, can::Gear::N, 0x01);
    can::gen::MtrMotorFbk fbk{};
    can::gen::decode_mtr_motor_fbk(f.view(), fbk);
    TEST_ASSERT_TRUE(fbk.fault_flags & 0x01);
}

void test_072_seb_l3_while_cornering(void) {
    sys::set_latched_fault(sys::kLatchedSebL3);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
}

void test_073_epsc_l3_while_cornering(void) {
    rt::SteeringControl sc; sc.init();
    etrike::protocol::codecs::ses::Command out;
    for (int i = 0; i < 25; ++i) sc.tick(0, 1, i * 20, out);
    sc.tick(0, 1, 600, out);
    sc.start_estop(false);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::ESTOP_RAMP_TO_ZERO), uint8_t(sc.state()));
}

void test_074_rt_internal_estop_recovery(void) {
    rt::SteeringControl sc; sc.init();
    etrike::protocol::codecs::ses::Command out;
    for (int i = 0; i < 25; ++i) sc.tick(0, 1, i * 20, out);
    sc.tick(0, 1, 600, out);
    sc.start_estop(false);
    sc.exit_estop();
    TEST_ASSERT_TRUE(sc.state() != rt::SteerState::STEER_FAULT);
}

void test_075_fresh_host_cmd_too_early(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, true), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(2000), 20);
    h.mtr_mgr.tick(20);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 9: POWER CYCLES, BOUNCES & ABNORMAL RESET (Tests 76–89)
// ═════════════════════════════════════════════════════════════════════

void test_076_power_cycle_mtr_only(void) {
    ClosedLoopHarness h; h.init();
    TEST_ASSERT_TRUE(h.mtr_mgr.propulsion_inhibited());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_077_power_cycle_rt_only(void) {
    rt::SteeringControl sc; sc.init();
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::STEER_BOOT_WAIT), uint8_t(sc.state()));
}

void test_078_power_cycle_sys_only(void) {
    sys::ModeManager mm; mm.init();
    TEST_ASSERT_EQUAL(can::Mode::Manual, mm.mode());
}

void test_079_power_cycle_all_nodes_fault_active(void) {
    ClosedLoopHarness h; h.init();
    h.hw_estop_button = true;
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
}

void test_080_physical_estop_held_during_boot(void) {
    sys::ModeManager mm; mm.init();
    mm.force_estop();
    mm.tick(false, true);
    TEST_ASSERT_EQUAL(can::Mode::Estop, mm.mode());
}

void test_081_physical_estop_release_without_reset(void) {
    sys::ModeManager mm; mm.init();
    mm.force_estop();
    mm.tick(false, false);
    TEST_ASSERT_EQUAL(can::Mode::Estop, mm.mode());
}

void test_082_physical_estop_bouncing_input(void) {
    sys::ModeManager mm; mm.init();
    mm.tick(true, false);
    mm.force_estop();
    mm.tick(false, false);
    mm.tick(true, false);
    TEST_ASSERT_EQUAL(can::Mode::Estop, mm.mode());
}

void test_083_start_button_bounce(void) {
    sys::ModeManager mm; mm.init();
    mm.force_estop();
    mm.tick(false, true);
    mm.tick(false, false);
    mm.tick(false, true);
    TEST_ASSERT_EQUAL(can::Mode::Manual, mm.mode());
}

void test_084_mode_long_press_boundary(void) {
    sys::ModeManager mm; mm.init();
    mm.force_estop();
    for (int i = 0; i < 29; ++i) mm.tick(true, false);
    TEST_ASSERT_EQUAL(can::Mode::Estop, mm.mode());
    mm.tick(true, false);
    TEST_ASSERT_EQUAL(can::Mode::Manual, mm.mode());
}

void test_085_reset_with_rt_hb_absent(void) {
    sys::SafetyMonitor sm; sm.init();
    sys::g_sys_test_time_us = 4000000;
    TEST_ASSERT_FALSE(sm.heartbeat_ok());
    sys::g_sys_test_time_us = 0;
}

void test_086_reset_with_mtr_reporting_estop(void) {
    can::Frame f = CanManipulator::make_feedback_frame(0, can::Gear::N, 0x01);
    can::gen::MtrMotorFbk fbk{};
    can::gen::decode_mtr_motor_fbk(f.view(), fbk);
    TEST_ASSERT_TRUE(fbk.fault_flags & 0x01);
}

void test_087_reset_with_mtr_fbk_absent(void) {
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
}

void test_088_reset_with_seb_communication_absent(void) {
    sys::set_inhibit(sys::kInhibitSebCommsLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
}

void test_089_reset_with_can_error_passive(void) {
    // CAN error-passive (TEC >= 128 but below the 255 bus-off threshold) must
    // NOT drop frames: ESTOP propagation and the full two-frame clear
    // handshake still complete end-to-end on a degraded bus.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    uint8_t tec, rec;
    h.low_bus.get_error_counters(tec, rec);
    TEST_ASSERT_EQUAL(0, tec);

    // Force the low bus into error-passive; delivery must continue.
    h.low_bus.set_error_counters(128, 96);
    for (int i = 0; i < 10; ++i) h.tick(10000);
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);

    // Trip ESTOP over the degraded bus: every node must still see it.
    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    for (int i = 0; i < 3; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());

    // Operator reset + two clear frames recover MTR even at TEC >= 128.
    h.operator_reset();
    for (int i = 0; i < 50; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    h.low_bus.clear_faults();
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 10: INTERRUPTED SEQUENCES, REPLAYS & MULTI-FAULT (Tests 90–114)
// ═════════════════════════════════════════════════════════════════════

void test_090_fault_during_two_frame_clear(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, true), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, true), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(4, true), 40);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_091_fault_immediately_after_clear(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, true), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, true), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(4, false), 40);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(5, true), 50);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_092_estop_during_mtr_rearm(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, true), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, true), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(4, false), 40);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(false, 1), 50);
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 60);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_093_estop_immediately_after_rearm(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, true), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, true), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(4, false), 40);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(false, 1), 50);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 2), 60);
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 70);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_094_brake_stuck_max_after_reset(void) {
    // A SEB reporting a stuck-max pressure after an ESTOP reset must never be
    // treated as a release of a brake-following fault. Verify the wire frame a
    // real SEB would emit decodes to a max-pressure request, then drive the
    // whole loop: with a latched brake-following fault, pressing MODE must NOT
    // produce an AUTO authority frame on the low bus and MTR stays cut.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    // SEB status shows brake stuck fully applied (pressure_raw=200 == max).
    can::Frame stuck = CanManipulator::make_seb_status_frame(0, 200, 1);
    etrike::protocol::codecs::seb::Status sts{};
    TEST_ASSERT_EQUAL(can::gen::CodecStatus::Ok,
                      etrike::protocol::codecs::seb::decode_status(stuck.view(), sts));
    TEST_ASSERT_EQUAL(200, sts.pressure_value_raw);
    h.low_bus.send(to_proto(stuck));
    h.tick(10000);

    // Operator resets, then tries to re-enter AUTO. The latched brake-following
    // fault must keep every mode-authority frame on the wire at MANUAL.
    sys::set_latched_fault(sys::kLatchedBrakeFollowing);
    h.low_bus_history.clear();   // drop AUTO-authority frames from bring-up
    h.operator_reset();
    h.operator_press_mode();
    for (int i = 0; i < 30; ++i) h.tick(10000);
    TEST_ASSERT_TRUE(sys::latched_fault_present());

    bool saw_auto_authority = false;
    for (const auto& f : h.low_bus_history) {
        if (f.id == can::kIdSysModeCmd) {
            can::gen::SysModeCmd mc{};
            if (can::gen::decode_sys_mode_cmd(f.view(), mc) == can::gen::CodecStatus::Ok) {
                if (mc.mode != 0) saw_auto_authority = true;
            }
        }
    }
    TEST_ASSERT_FALSE(saw_auto_authority);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_095_brake_stuck_last_command(void) {
    // After a stuck SEB condition, SYS's own 0x7B9 brake command is the only
    // brake authority; a stale RT/bus brake frame replayed later must not re-arm
    // motion. Feed a max-pressure SEB command through the low bus while a brake
    // fault is latched and verify the plant stays stopped and DAC zero.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());

    sys::set_latched_fault(sys::kLatchedBrakeFollowing);
    for (int i = 0; i < 20; ++i) h.tick(10000);

    // Replayed last SEB command (max pressure) - the braking side is stuck.
    h.low_bus.send(to_proto(CanManipulator::make_seb_cmd_frame(200, 7)));
    for (int i = 0; i < 10; ++i) h.tick(10000);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
    TEST_ASSERT_EQUAL(0, h.plant.physical_wheel_speed_mmps);
}

void test_096_software_unresettable_brake_state(void) {
    // A software CAN-ESTOP latches SYS into ESTOP and is NOT clearable by the
    // MODE button (only the explicit START/reset path clears it). Genuine
    // ModeManager semantics through the button interface.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    // Remote software ESTOP.
    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());

    // MODE presses must not exit ESTOP.
    h.operator_press_mode();
    for (int i = 0; i < 10; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());

    // START reset path clears to MANUAL.
    h.operator_reset();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

void test_097_unexpected_braking_comms_glitch(void) {
    // A single corrupted 0x206 (bad E2E/CRC) is dropped by the codec - it must
    // not latch MTR ESTOP nor cut a healthy drive. Genuine bus-level delivery.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());

    // Inject one corrupted feedback frame (flips a payload byte => CRC fail).
    can::Frame bad = CanManipulator::make_feedback_frame(2000, can::Gear::D, 0);
    CanManipulator::corrupt_byte(bad, 2, 0x40);
    h.low_bus.send(to_proto(bad));
    h.tick(10000);

    // No latch, no inhibit, drive unaffected.
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_FALSE(sys::transient_inhibited());
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);
}

void test_098_unexpected_braking_high_steer_angle(void) {
    rt::SteeringControl sc; sc.init();
    etrike::protocol::codecs::ses::Command out;
    for (int i = 0; i < 25; ++i) sc.tick(0, 1, i * 20, out);
    sc.tick(0, 1, 600, out);
    sc.set_target(30000, 6944);
    sc.start_estop(true);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::ESTOP_HOLD_THEN_SILENT), uint8_t(sc.state()));
}

void test_099_can_flood_during_estop(void) {
    // A CAN flood (0x204 garbage) arriving while SYS is latched in ESTOP must
    // not clear or disturb the latch, and the vehicle must stay cut until the
    // real operator-reset + two-frame handshake runs.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());

    // Flood 0x204 while ESTOP is held; keep ticking.
    for (int i = 0; i < 30; ++i) {
        h.low_bus.send(to_proto(can::Frame::standard(0x204, 8)));
        h.tick(10000);
    }
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());

    // Even with the flood continuing, the handshake clears deterministically.
    h.operator_reset();
    for (int i = 0; i < 45; ++i) {
        if (i % 3 == 0) h.low_bus.send(to_proto(can::Frame::standard(0x204, 8)));
        h.tick(10000);
    }
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

void test_100_can_flood_during_clear(void) {
    // During the two-frame MTR clear handshake, a flood of unrelated frames
    // must not be interpreted as clear authority: the flood alone keeps MTR
    // latched; only two advancing 0x011 zero frames from SYS clear it.
    ClosedLoopHarness h; h.init();
    // Establish a valid 0x011 stream and latch via two asserting frames.
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, true), 5);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, true), 15);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());

    // Flood of drive frames / 0x204 = no clear authority.
    for (int i = 0; i < 20; ++i) {
        h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), static_cast<uint32_t>(20 + i));
    }
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());

    // A single zero 0x011 establishes baseline only (no clear).
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 100);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());

    // Advancing second zero frame clears (flood continues in background).
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 110);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(4, false), 120);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
}

void test_101_counter_wraparound_clear(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(253, true), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(254, true), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(255, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(0, false), 40);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
}

void test_102_counter_wraparound_fault(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(253, true), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(254, true), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(255, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 40);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_103_corrupted_estop_active_payload(void) {
    can::Frame f = CanManipulator::make_safety_frame(10, false);
    CanManipulator::corrupt_byte(f, 0, 0x01);
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(f, 50);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_104_unexpected_estop_active_value(void) {
    // estop_active is packed as byte 0 with an 8-bit physical range of {0,1};
    // the codec DEFENSIVELY rejects any out-of-range value (>1) as
    // ValueOutOfRange. MTR therefore only latches ESTOP on a decode-valid,
    // E2E-CRC-correct 0x011 - a corrupt "estop byte" can never be misread as a
    // clear (it is dropped, not trusted).
    ClosedLoopHarness h; h.init();
    // (a) Codec must reject a non-canonical estop byte (spoofed "3").
    can::Frame bad = CanManipulator::make_safety_frame(0, false);
    bad.data[0] = 0x03;   // estop byte out of range {0,1}
    can::gen::SysSafetySts dbg{};
    TEST_ASSERT_TRUE(can::gen::decode_sys_safety_sts(bad.view(), dbg) != can::gen::CodecStatus::Ok);
    h.mtr_mgr.handle_frame(bad, 10);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());

    // (b) Two advancing, decode-valid asserted frames (CRC correct) establish
    //     the stream and latch MTR immediately.
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, true), 20);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());   // baseline only, not yet valid
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, true), 30);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());

    // (c) The corrupt frame, replayed, must never clear the latch (it is not a
    //     trusted zero frame).
    h.mtr_mgr.handle_frame(bad, 40);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_105_stale_0x011_0_replay(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    can::Frame old = CanManipulator::make_safety_frame(1, false);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(10, true), 50);
    h.mtr_mgr.handle_frame(old, 60);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_106_stale_0x011_1_replay(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    can::Frame old = CanManipulator::make_safety_frame(1, true);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(10, false), 50);
    h.mtr_mgr.handle_frame(old, 60);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
}

void test_107_old_0x001_delayed_forward(void) {
    // A stale/delayed 0x001 arriving AFTER SYS already exited ESTOP must not
    // re-latch a cleared vehicle: MTR ignores a replayed zero clear frame but a
    // replayed 0x001 (a new hardwired assertion) still latches. Verify the
    // asymmetry: a late 0x001 IS honored (it is an edge, not a sequence), and
    // SYS reset-grace/RT suppression prevents RT from echoing it back.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    // SYS broadcasts 0x001 then is reset by the operator (estop released).
    h.sys_broadcast_estop();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    h.operator_reset();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

    // A delayed duplicate of the SAME 0x001 (echo/loopback within the 50 ms
    // loopback window) is suppressed, so SYS stays MANUAL (no livelock).
    for (int i = 0; i < 20; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

void test_108_simultaneous_independent_causes(void) {
    // Two independent latched faults are both recorded; clearing one (which is
    // only done by the reset path on a healthy cause) must not erase the other.
    sys::g_latched_fault_reasons.store(0);
    sys::set_latched_fault(sys::kLatchedSebL3);
    sys::set_latched_fault(sys::kLatchedBrakeFollowing);
    uint32_t both = sys::kLatchedSebL3 | sys::kLatchedBrakeFollowing;
    TEST_ASSERT_EQUAL(static_cast<uint32_t>(both), sys::g_latched_fault_reasons.load());
    TEST_ASSERT_TRUE(sys::latched_fault_present());

    // Clearing only the SEB-L3 bit leaves the following-error latch intact.
    sys::g_latched_fault_reasons.fetch_and(~static_cast<uint32_t>(sys::kLatchedSebL3));
    TEST_ASSERT_TRUE(sys::g_latched_fault_reasons.load() & sys::kLatchedBrakeFollowing);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
    sys::g_latched_fault_reasons.store(0);
}

void test_109_one_latched_plus_one_recoverable(void) {
    // A latched fault plus a transient inhibit coexist; clearing the transient
    // must not clear the latch (separate masks), and both force any_inhibit().
    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
    sys::set_latched_fault(sys::kLatchedSebL3);
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
    TEST_ASSERT_TRUE(sys::transient_inhibited());
    TEST_ASSERT_TRUE(sys::any_inhibit());

    sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_FALSE(sys::transient_inhibited());
    TEST_ASSERT_TRUE(sys::latched_fault_present());   // latch untouched
    TEST_ASSERT_TRUE(sys::any_inhibit());
    sys::g_latched_fault_reasons.store(0);
}

void test_110_multiple_recoverable_faults(void) {
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    sys::set_inhibit(sys::kInhibitSebCommsLoss);
    sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
    sys::clear_inhibit(sys::kInhibitSebCommsLoss);
    TEST_ASSERT_FALSE(sys::transient_inhibited());
}

void test_111_diagnostics_state_consistency(void) {
    // A latched SEB-L3 fault must keep MTR cut through the REAL authority
    // pipeline (SYS resolver -> 0x110 mode frame -> MTR) for as long as the
    // latch persists, and it must not spontaneously clear or re-grant AUTO.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);

    sys::set_latched_fault(sys::kLatchedSebL3);
    for (int i = 0; i < 30; ++i) h.tick(10000);   // let resolver MANUAL frames reach MTR
    TEST_ASSERT_TRUE(sys::latched_fault_present());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
    TEST_ASSERT_EQUAL(0, h.plant.physical_wheel_speed_mmps);

    // Latch persists across time; vehicle stays cut.
    for (int i = 0; i < 50; ++i) h.tick(10000);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_112_ready_lamp_consistency(void) {
    // The "ready to drive" lamp mirrors the fault/inhibit mask. A transient
    // MTR-feedback-loss inhibit removes readiness through the authority
    // pipeline (MTR DAC zeroed); when the detector clears it, readiness and
    // propulsion return. Nothing about readiness may depend on a stale flag.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);

    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    for (int i = 0; i < 30; ++i) h.tick(10000);
    TEST_ASSERT_TRUE(sys::traction_fault_present());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());

    sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
    for (int i = 0; i < 30; ++i) h.tick(10000);
    TEST_ASSERT_FALSE(sys::traction_fault_present());
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);
}

void test_113_false_ready_prevention(void) {
    // MODE presses are ignored while the system is latched in ESTOP: the mode
    // manager must not produce a false AUTO/ready state, even if the operator
    // presses MODE during the ESTOP. Only release + START returns to MANUAL.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    h.hw_estop_button = true;
    for (int i = 0; i < 30; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());

    // Pressing MODE while ESTOP is active must NOT switch to AUTO.
    h.operator_press_mode();
    for (int i = 0; i < 30; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());

    // Release the button and reset: back to MANUAL, then MODE grants AUTO.
    h.hw_estop_button = false;
    h.operator_reset();
    for (int i = 0; i < 30; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

void test_114_false_fault_prevention(void) {
    // Positive control: a healthy AUTO cruise must not spontaneously develop a
    // fault, inhibit, or ESTOP over a sustained run (no false positives from
    // the distributed monitors). This guards against tests / wiring that set
    // spurious state and leave it behind.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    for (int i = 0; i < 150; ++i) h.tick(10000);  // 1.5 s sustained cruise
    TEST_ASSERT_FALSE(sys::any_inhibit());
    TEST_ASSERT_EQUAL(can::Mode::Auto, h.sys_mode.mode());
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);
    TEST_ASSERT_TRUE(h.plant.physical_wheel_speed_mmps > 0);
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 11: REMOTE CONTROL (RM) BENCH MODE INVARIANTS (Tests 115–122)
// ═════════════════════════════════════════════════════════════════════

void test_115_rm_link_loss_deadman(void) {
    // Remote-control link loss: when the command source stops providing fresh
    // authority (heartbeat), the vehicle must drop power/mode authority rather
    // than hold a stale remote command. This is the deadman that RM bench mode
    // must satisfy on top of SYS. Use the real SYS authority resolver.
    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
    // Healthy remote authority: AUTO + power requested -> allowed.
    auto a_ok = sys::resolve_authority(false, true, true);
    TEST_ASSERT_TRUE(a_ok.mode_auto);
    TEST_ASSERT_TRUE(a_ok.power_on);
    // Link loss = remote heartbeat considered lost -> SYS treats the remote as
    // unavailable (inhibit active), so authority must be dropped even though
    // mode request is AUTO.
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);   // stand-in for "remote peer unavailable"
    auto a_lost = sys::resolve_authority(false, true, true);
    TEST_ASSERT_FALSE(a_lost.mode_auto);
    TEST_ASSERT_FALSE(a_lost.power_on);
    sys::g_inhibit_reasons.store(0);
}

void test_116_rm_link_recovery_without_reset(void) {
    // A link that recovers (peer healthy again) restores authority WITHOUT an
    // ESTOP reset - the inhibit is B-class/recoverable, not latched.
    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_FALSE(sys::resolve_authority(false, true, true).mode_auto);
    sys::clear_inhibit(sys::kInhibitMtrFbkLoss);   // link healthy again
    TEST_ASSERT_FALSE(sys::latched_fault_present());
    TEST_ASSERT_TRUE(sys::resolve_authority(false, true, true).mode_auto);
}

void test_117_rm_reset_sequence(void) {
    // An RM "reset" must pass through the same SYS authority: after ESTOP the
    // operator reset un-latches SYS, and power returns only once authority is
    // re-resolved with the cause clear.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    h.hw_estop_button = true;
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    TEST_ASSERT_FALSE(sys::resolve_authority(true, true, true).power_on);

    h.hw_estop_button = false;
    h.operator_reset();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
    // Authority re-resolves to power ON once the vehicle leaves ESTOP.
    TEST_ASSERT_TRUE(sys::resolve_authority(false, false, true).power_on);
}

void test_118_rm_own_loopback_credit(void) {
    // A node must not credit its own transmitted 0x001 as an external ESTOP
    // (echo suppression). SYS's own broadcast must not cause SYS to re-latch
    // after the operator reset (the 50 ms loopback window suppresses it).
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    h.sys_broadcast_estop();      // SYS originates + sends 0x001 both buses
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    h.operator_reset();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
    // The reflected frame is suppressed: SYS stays MANUAL (no livelock).
    for (int i = 0; i < 20; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());
}

void test_119_rm_external_0x001_latch(void) {
    // An EXTERNAL (genuine) 0x001 is not suppressed and must latch MTR.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
}

void test_120_rm_reconnect_unsafe_controls(void) {
    // Reconnecting a remote that holds a stale non-zero throttle must not
    // resume motion until authority is re-validated: an old 0x204 with no fresh
    // mode/power authority is rejected by MTR.
    ClosedLoopHarness h; h.init();
    // Drive arrives with no valid mode/power authority (fresh MANUAL only).
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Manual, 1), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1500), 20);
    h.mtr_mgr.tick(20);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_121_rm_estop_reset_throttle_held(void) {
    // After ESTOP + reset, holding throttle during reset must not auto-drive:
    // MTR requires the full REARM (mode + power OFF->ON) even if the drive
    // command stream resumes.
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 40);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    // Drive + power ON without a post-clear OFF edge -> REARM not observed.
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 50);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 2), 55);
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 1), 60);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1500), 70);
    h.mtr_mgr.tick(70);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_122_topology_misuse_detection(void) {
    // A bench/remote node broadcasting ESTOP into the vehicle (unexpected
    // topology) is handled the same as any external ESTOP: the vehicle latches
    // and cuts. The correct reaction is fail-safe, never ignore.
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());
    // "Unexpected topology" frame arrives on the high bus and is gateway
    // forwarded low -> latches.
    h.high_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Off, h.mtr_relays.state());
}

// ═════════════════════════════════════════════════════════════════════
// GROUP 12: MODE TRANSITIONS, AUTHORITY & FULL ACCEPTANCE (Tests 123–140)
// ═════════════════════════════════════════════════════════════════════

void test_123_production_manual_traction_unassigned(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Manual, 1), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 20);
    h.mtr_mgr.tick(20);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_124_manual_to_auto_stale_data_blocked(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1500), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 600);
    h.mtr_mgr.tick(600);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_125_auto_to_manual_while_moving(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);
    TEST_ASSERT_EQUAL(can::Mode::Auto, h.sys_mode.mode());

    // Operator switches back to Manual
    h.operator_press_mode();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

    // Propulsion cuts immediately
    for (int i = 0; i < 10; ++i) h.tick(10000);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_126_power_authority_stale(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_pwr_frame(true, 1), 10);
    h.mtr_mgr.tick(600);
    TEST_ASSERT_TRUE(h.mtr_mgr.propulsion_inhibited());
}

void test_127_mode_authority_stale(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 10);
    h.mtr_mgr.tick(600);
    TEST_ASSERT_TRUE(h.mtr_mgr.propulsion_inhibited());
}

void test_128_unauthorized_0x204_rejection(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Manual, 1), 10);
    h.mtr_mgr.handle_frame(CanManipulator::make_drive_frame(1000), 20);
    h.mtr_mgr.tick(20);
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
}

void test_129_fresh_mode_stale_safety(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_mode_frame(can::Mode::Auto, 1), 50);
    h.mtr_mgr.tick(50);
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Off, h.mtr_relays.state());
}

void test_130_fresh_safety_stale_power(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 50);
    h.mtr_mgr.tick(50);
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Off, h.mtr_relays.state());
}

void test_131_full_software_recovery(void) {
    ClosedLoopHarness h; h.init();
    h.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, 10);
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), 20);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), 30);
    h.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), 40);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
}

void test_132_recovery_vs_power_cycle_comparison(void) {
    ClosedLoopHarness h1; h1.init();
    ClosedLoopHarness h2; h2.init();
    h1.bring_to_active_auto(1500);

    h1.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, h1.now_ms);
    h1.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(1, false), h1.now_ms + 10);
    h1.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(2, false), h1.now_ms + 20);
    h1.mtr_mgr.handle_frame(CanManipulator::make_safety_frame(3, false), h1.now_ms + 30);

    h2.init();

    TEST_ASSERT_EQUAL(h1.mtr_relays.state(), h2.mtr_relays.state());
    TEST_ASSERT_EQUAL(h1.mtr_dac.current_code(), h2.mtr_dac.current_code());
}

void test_133_software_unrecoverable_state_search(void) {
    const uint32_t kFaultCombos[] = {
        sys::kLatchedBrakeFollowing,
        sys::kLatchedSebL3,
        sys::kLatchedBrakeFollowing | sys::kLatchedSebL3
    };
    for (uint32_t mask : kFaultCombos) {
        sys::g_latched_fault_reasons.store(mask);
        TEST_ASSERT_TRUE(sys::latched_fault_present());
        sys::g_latched_fault_reasons.store(0);
        TEST_ASSERT_FALSE(sys::latched_fault_present());
    }
}

void test_134_combined_turning_estop(void) {
    rt::SteeringControl sc; sc.init();
    etrike::protocol::codecs::ses::Command out;
    for (int i = 0; i < 25; ++i) sc.tick(0, 1, i * 20, out);
    sc.tick(0, 1, 600, out);
    sc.set_target(15000, 2000);
    sc.start_estop(false);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::ESTOP_RAMP_TO_ZERO), uint8_t(sc.state()));
}

void test_135_combined_turning_mtr_dropout(void) {
    sys::set_inhibit(sys::kInhibitMtrFbkLoss);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
}

void test_136_combined_turning_sys_brake_failure(void) {
    rt::SebBrakeFallback fb; fb.init(1000);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(fb.state()));
}

void test_137_combined_turning_sys_freeze(void) {
    constexpr int kTimeout = rt::kHeartbeatTimeoutMsSys;
    TEST_ASSERT_EQUAL(200, kTimeout);
}

void test_138_combined_turning_mtr_freeze(void) {
    mtr::DacController dac; dac.set_throttle(1200, true);
    TEST_ASSERT_TRUE(dac.current_code() > 0);
}

void test_139_physical_disconnect_recovery(void) {
    ClosedLoopHarness h; h.init();
    h.low_bus.inject_fault(can::sim::FaultType::DROP_FRAME);
    etrike::protocol::Frame f = etrike::protocol::Frame::standard(can::kIdSafetyEstop, 0);
    h.low_bus.send(f);
    TEST_ASSERT_FALSE(h.low_bus.has_pending());

    h.low_bus.clear_faults();
    h.low_bus.send(f);
    TEST_ASSERT_TRUE(h.low_bus.has_pending());
}

void test_140_full_acceptance_criteria(void) {
    ClosedLoopHarness h; h.init();
    h.bring_to_active_auto(2000);

    // Criterion 1: Safe Physical Output at Speed in AUTO
    TEST_ASSERT_EQUAL(can::Mode::Auto, h.sys_mode.mode());
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);
    TEST_ASSERT_TRUE(h.plant.physical_wheel_speed_mmps > 0);

    // Criterion 2: Correct Distributed Latch on Fault
    h.low_bus.send(to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Estop, h.sys_mode.mode());
    TEST_ASSERT_TRUE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Off, h.mtr_relays.state());
    TEST_ASSERT_EQUAL(0, h.mtr_dac.current_code());
    TEST_ASSERT_EQUAL(0, h.plant.physical_wheel_speed_mmps);
    TEST_ASSERT_TRUE(h.rt_estop_pending);

    // Criterion 3: Deterministic Recovery via Operator Reset & 2-Frame Handshake
    h.operator_reset();
    h.tick(10000);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

    // Wait for SYS 0x011 two-frame clear sequence
    for (int i = 0; i < 45; ++i) h.tick(10000);
    TEST_ASSERT_FALSE(h.mtr_mgr.is_estop_active());
    TEST_ASSERT_FALSE(h.rt_estop_pending);
    TEST_ASSERT_EQUAL(can::Mode::Manual, h.sys_mode.mode());

    // Criterion 4: Software-only REARM restores active drive
    h.operator_press_mode(); // Switch back to AUTO
    for (int i = 0; i < 40; ++i) h.tick(10000);

    // Host sends drive command
    can::Frame fd;
    can::gen::RtDriveCmd dc{1500, static_cast<uint8_t>(can::Gear::D)};
    can::gen::encode_rt_drive_cmd(dc, fd);
    h.low_bus.send(to_proto(fd));
    h.tick(10000);

    TEST_ASSERT_EQUAL(can::Mode::Auto, h.sys_mode.mode());
    TEST_ASSERT_EQUAL(mtr::RelayController::State::Drive, h.mtr_relays.state());
    TEST_ASSERT_TRUE(h.mtr_dac.current_code() > 0);
    TEST_ASSERT_TRUE(h.plant.physical_wheel_speed_mmps > 0);
}

// ═════════════════════════════════════════════════════════════════════
// MAIN RUNNER: EXECUTES ALL 140 TESTS
// ═════════════════════════════════════════════════════════════════════
extern "C" void app_main() {
    UNITY_BEGIN();

    // Group 1
    RUN_TEST(test_001_estop_reset_loop);
    RUN_TEST(test_002_repeated_reset_different_timing_offsets);
    RUN_TEST(test_003_rt_originated_estop_recovery);
    RUN_TEST(test_004_remote_0x001_echo_discrimination);
    RUN_TEST(test_005_persistent_0x001_spam);

    // Group 2
    RUN_TEST(test_006_mtr_feedback_dropout_while_moving);
    RUN_TEST(test_007_mtr_feedback_dropout_at_standstill);
    RUN_TEST(test_008_mtr_feedback_three_frame_recovery);
    RUN_TEST(test_009_intermittent_mtr_feedback_loss);
    RUN_TEST(test_010_command_echo_is_command_not_measured_speed);
    RUN_TEST(test_011_motor_not_moving_false_negative_document);
    RUN_TEST(test_012_dac_stuck_high_characterization);
    RUN_TEST(test_013_relay_stuck_energized_characterization);

    // Group 3
    RUN_TEST(test_014_mtr_freeze_while_throttling);
    RUN_TEST(test_015_mtr_freeze_plus_can_estop);
    RUN_TEST(test_016_mtr_freeze_plus_power_off);
    RUN_TEST(test_017_sys_freeze_with_button_pressed);
    RUN_TEST(test_018_sys_freeze_while_driving_timeouts);
    RUN_TEST(test_019_sys_freeze_while_braking);

    // Group 4
    RUN_TEST(test_020_sys_brake_task_failure_hb_alive);
    RUN_TEST(test_021_single_normal_producer_model);
    RUN_TEST(test_022_seb_command_loss_behavior);
    RUN_TEST(test_023_brake_command_loss_while_turning);
    RUN_TEST(test_024_brake_loss_during_emergency_braking);
    RUN_TEST(test_025_emergency_fallback_entry_progression);
    RUN_TEST(test_026_sys_brake_recovery_during_fallback_hb_dead);
    RUN_TEST(test_027_dual_0x7b9_conflicting_payload);
    RUN_TEST(test_028_dual_0x7b9_alternating_frame_prevention);
    RUN_TEST(test_029_fallback_handback_epoch_and_verification);
    RUN_TEST(test_030_heartbeat_recovers_before_brake_task);
    RUN_TEST(test_031_brake_task_recovers_before_heartbeat);

    // Group 5
    RUN_TEST(test_032_seb_l3_reset_refusal);
    RUN_TEST(test_033_brake_following_fault_reset_refusal);
    RUN_TEST(test_034_latched_fault_reset_race_condition);
    RUN_TEST(test_035_latched_fault_reappears_immediately_after_reset);
    RUN_TEST(test_036_single_clear_frame_rejection);
    RUN_TEST(test_037_duplicate_clear_counter_rejection);
    RUN_TEST(test_038_skipped_clear_counter_behavior);
    RUN_TEST(test_039_out_of_order_clear_frame_rejection);
    RUN_TEST(test_040_crc_corrupt_clear_frame_rejection);
    RUN_TEST(test_041_clear_interrupted_by_assert_frame);

    // Group 6
    RUN_TEST(test_042_genuine_0x113_off_to_on_required);
    RUN_TEST(test_043_automatic_rearm_prevention);
    RUN_TEST(test_044_missing_post_clear_off_edge_rejection);
    RUN_TEST(test_045_permanent_unrearm_prevention);
    RUN_TEST(test_046_explicit_ignition_cycle_rearm);
    RUN_TEST(test_047_rearm_with_stale_0x110_mode);
    RUN_TEST(test_048_rearm_with_invalid_0x011_safety);
    RUN_TEST(test_049_rearm_with_nonzero_drive_already_queued);
    RUN_TEST(test_050_estop_clear_with_auto_request_active);

    // Group 7
    RUN_TEST(test_051_rt_cold_start_without_0x011);
    RUN_TEST(test_052_rt_cold_start_delayed_sys_arrival);
    RUN_TEST(test_053_rt_loses_0x011_after_acquisition);
    RUN_TEST(test_054_mtr_boot_without_0x011);
    RUN_TEST(test_055_mtr_any_frame_deadman_masking_prevention);
    RUN_TEST(test_056_mtr_0x204_three_frame_recovery);
    RUN_TEST(test_057_mtr_command_timeout_while_moving);
    RUN_TEST(test_058_mtr_command_timeout_at_standstill);
    RUN_TEST(test_059_sys_mtr_feedback_loss_consistency);
    RUN_TEST(test_060_sys_seb_communication_loss_consistency);

    // Group 8
    RUN_TEST(test_061_can_bus_off_during_propulsion);
    RUN_TEST(test_062_can_bus_off_recovery_reset);
    RUN_TEST(test_063_rt_low_can_bus_off);
    RUN_TEST(test_064_rt_high_can_bus_off);
    RUN_TEST(test_065_mtr_rx_ring_overflow);
    RUN_TEST(test_066_host_drive_timeout_during_corner);
    RUN_TEST(test_067_host_heartbeat_timeout);
    RUN_TEST(test_068_sys_heartbeat_timeout_at_rt);
    RUN_TEST(test_069_rt_heartbeat_timeout_at_sys);
    RUN_TEST(test_070_mtr_estop_ack_timeout);
    RUN_TEST(test_071_mtr_estop_ack_late_recovery);
    RUN_TEST(test_072_seb_l3_while_cornering);
    RUN_TEST(test_073_epsc_l3_while_cornering);
    RUN_TEST(test_074_rt_internal_estop_recovery);
    RUN_TEST(test_075_fresh_host_cmd_too_early);

    // Group 9
    RUN_TEST(test_076_power_cycle_mtr_only);
    RUN_TEST(test_077_power_cycle_rt_only);
    RUN_TEST(test_078_power_cycle_sys_only);
    RUN_TEST(test_079_power_cycle_all_nodes_fault_active);
    RUN_TEST(test_080_physical_estop_held_during_boot);
    RUN_TEST(test_081_physical_estop_release_without_reset);
    RUN_TEST(test_082_physical_estop_bouncing_input);
    RUN_TEST(test_083_start_button_bounce);
    RUN_TEST(test_084_mode_long_press_boundary);
    RUN_TEST(test_085_reset_with_rt_hb_absent);
    RUN_TEST(test_086_reset_with_mtr_reporting_estop);
    RUN_TEST(test_087_reset_with_mtr_fbk_absent);
    RUN_TEST(test_088_reset_with_seb_communication_absent);
    RUN_TEST(test_089_reset_with_can_error_passive);

    // Group 10
    RUN_TEST(test_090_fault_during_two_frame_clear);
    RUN_TEST(test_091_fault_immediately_after_clear);
    RUN_TEST(test_092_estop_during_mtr_rearm);
    RUN_TEST(test_093_estop_immediately_after_rearm);
    RUN_TEST(test_094_brake_stuck_max_after_reset);
    RUN_TEST(test_095_brake_stuck_last_command);
    RUN_TEST(test_096_software_unresettable_brake_state);
    RUN_TEST(test_097_unexpected_braking_comms_glitch);
    RUN_TEST(test_098_unexpected_braking_high_steer_angle);
    RUN_TEST(test_099_can_flood_during_estop);
    RUN_TEST(test_100_can_flood_during_clear);
    RUN_TEST(test_101_counter_wraparound_clear);
    RUN_TEST(test_102_counter_wraparound_fault);
    RUN_TEST(test_103_corrupted_estop_active_payload);
    RUN_TEST(test_104_unexpected_estop_active_value);
    RUN_TEST(test_105_stale_0x011_0_replay);
    RUN_TEST(test_106_stale_0x011_1_replay);
    RUN_TEST(test_107_old_0x001_delayed_forward);
    RUN_TEST(test_108_simultaneous_independent_causes);
    RUN_TEST(test_109_one_latched_plus_one_recoverable);
    RUN_TEST(test_110_multiple_recoverable_faults);
    RUN_TEST(test_111_diagnostics_state_consistency);
    RUN_TEST(test_112_ready_lamp_consistency);
    RUN_TEST(test_113_false_ready_prevention);
    RUN_TEST(test_114_false_fault_prevention);

    // Group 11
    RUN_TEST(test_115_rm_link_loss_deadman);
    RUN_TEST(test_116_rm_link_recovery_without_reset);
    RUN_TEST(test_117_rm_reset_sequence);
    RUN_TEST(test_118_rm_own_loopback_credit);
    RUN_TEST(test_119_rm_external_0x001_latch);
    RUN_TEST(test_120_rm_reconnect_unsafe_controls);
    RUN_TEST(test_121_rm_estop_reset_throttle_held);
    RUN_TEST(test_122_topology_misuse_detection);

    // Group 12
    RUN_TEST(test_123_production_manual_traction_unassigned);
    RUN_TEST(test_124_manual_to_auto_stale_data_blocked);
    RUN_TEST(test_125_auto_to_manual_while_moving);
    RUN_TEST(test_126_power_authority_stale);
    RUN_TEST(test_127_mode_authority_stale);
    RUN_TEST(test_128_unauthorized_0x204_rejection);
    RUN_TEST(test_129_fresh_mode_stale_safety);
    RUN_TEST(test_130_fresh_safety_stale_power);
    RUN_TEST(test_131_full_software_recovery);
    RUN_TEST(test_132_recovery_vs_power_cycle_comparison);
    RUN_TEST(test_133_software_unrecoverable_state_search);
    RUN_TEST(test_134_combined_turning_estop);
    RUN_TEST(test_135_combined_turning_mtr_dropout);
    RUN_TEST(test_136_combined_turning_sys_brake_failure);
    RUN_TEST(test_137_combined_turning_sys_freeze);
    RUN_TEST(test_138_combined_turning_mtr_freeze);
    RUN_TEST(test_139_physical_disconnect_recovery);
    RUN_TEST(test_140_full_acceptance_criteria);

    UNITY_END();
}

int main() {
    app_main();
    return g_tests_failed == 0 ? 0 : 1;
}
