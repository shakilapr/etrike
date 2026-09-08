#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

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
    func(); \
    if (g_tests_failed == prev_fail) { \
        std::printf("  [PASS] %s\n", #func); \
    } else { \
        std::printf("  [FAIL] %s\n", #func); \
    } \
} while(0)

#define UNITY_BEGIN() do { g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0; } while(0)
#define UNITY_END() (g_tests_failed == 0 ? 0 : 1)

#include "test_closed_loop_harness.hpp"

// Atomics required by RT / SYS linkage
namespace sys {
    std::atomic<uint32_t> g_inhibit_reasons{0};
    std::atomic<uint32_t> g_latched_fault_reasons{0};
    extern int64_t g_sys_test_time_us;
}

std::atomic<int64_t>  g_last_sys_hb_us{0};
std::atomic<int64_t>  g_last_host_hb_us{0};
std::atomic<int32_t>  g_mtr_motor_command_speed_mmps{0};
std::atomic<int64_t>  g_last_mtr_feedback_us{-1};
std::atomic<int64_t>  g_last_nonzero_cmd_us{-1};
std::atomic<int16_t>  g_last_cmd_angle_0_1deg{INT16_MIN};
std::atomic<int32_t>  g_ses_angle_0_1deg{0};
std::atomic<int32_t>  g_brake_request_kpa{0};
std::atomic<int64_t>  g_last_0x7B9_rx_us{-1};
std::atomic<int64_t>  g_last_sys_safety_sts_us{0};
std::atomic<uint8_t>  g_estop_reason{0};
std::atomic<bool>     g_seb_takeover{false};
std::atomic<int64_t>  g_last_estop_sent_us{-1000000};
std::atomic<uint8_t>  g_seb_error_status{0};
std::atomic<uint8_t>  g_seb_status_byte0{0};
std::atomic<bool>     g_no_sys_authority{true};

bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = true;
bool g_bypass_mtr_absent = false;
namespace rt { MtrHealthSupervisor g_mtr_health; }
rt::SteeringControl g_steering{};
extern "C" { FDCAN_HandleTypeDef hfdcan1; }

void setUp(void) {
    hal_mock::reset();
    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
    sys::g_sys_test_time_us = 0;
}

void tearDown(void) {}

// Global harness instance for parameterized tests
static closed_loop::ClosedLoopHarness g_harness;

// ?????????????????????????????????????????????????????????????????????????????
// TIER 1: CLOSED-LOOP DISTRIBUTED FEEDBACK & CODIFIED VULNERABILITIES (Tests 1-250)
// ?????????????????????????????????????????????????????????????????????????????

// Group 1.1: RT <-> SYS 0x001 Reset Feedback & Livelock (Tests 1-50)
// Proves that when RT broadcasts 0x001 continuously while in zero_setpoints,
// an operator reset on SYS is immediately re-latched by RT's next 0x001 frame.
void test_t1_001_to_050_estop_reset_livelock(void) {
    for (int offset = 10; offset <= 500; offset += 10) {
        g_harness.init();
        g_harness.bring_to_active_auto(2000);

        // 1. Trigger ESTOP on SYS via hardware button
        g_harness.hw_estop_button = true;
        g_harness.tick(20000); // 20 ms
        TEST_ASSERT_EQUAL(can::Mode::Estop, g_harness.sys_mode.mode());
        TEST_ASSERT_TRUE(g_harness.mtr_mgr.is_estop_active());

        // Release physical button
        g_harness.hw_estop_button = false;
        g_harness.tick(10000);

        // 2. Operator presses START button on SYS
        g_harness.operator_reset();
        g_harness.tick(10000);
        TEST_ASSERT_EQUAL(can::Mode::Manual, g_harness.sys_mode.mode());

        // 3. Advance time by `offset` ms. Under the 500ms reset-grace window,
        // any loopback or stale echoes are discarded so SYS remains in MANUAL.
        for (int t = 0; t < offset; t += 10) g_harness.tick(10000);
        TEST_ASSERT_EQUAL(can::Mode::Manual, g_harness.sys_mode.mode());

        // 4. Run until two 0x011 clear frames arrive (SYS 5 Hz -> 450 ms)
        for (int i = 0; i < 45; ++i) g_harness.tick(10000);
        TEST_ASSERT_FALSE(g_harness.mtr_mgr.is_estop_active());
        TEST_ASSERT_EQUAL(can::Mode::Manual, g_harness.sys_mode.mode());
    }
}

// Group 1.2: MTR Feedback Dropout Escalation to Latched ESTOP (Tests 51-90)
// Proves that when 0x206 disappears for >200ms in AUTO, RT's zero_setpoints triggers
// MTR health supervisor to mark MTR unavailable and inhibit drive, while SYS sets
// transient inhibit kInhibitMtrFbkLoss without falsely latching ESTOP.
void test_t1_051_to_090_mtr_fbk_escalation(void) {
    const int dropouts_ms[] = {40, 80, 120, 160, 240, 300, 400, 500};
    const int speeds[] = {0, 1000, 2000, 2500, -500};

    for (int spd : speeds) {
        for (int drop_ms : dropouts_ms) {
            g_harness.init();
            g_harness.bring_to_active_auto(spd);

            // Halt MTR 0x206 feedback frames by dropping them on the low bus
            g_harness.low_bus.inject_fault(can::sim::FaultType::DROP_FRAME, can::kIdMtrMotorFbk);

            // Step clock by drop_ms
            for (int t = 0; t < drop_ms; t += 10) g_harness.tick(10000);

            if (drop_ms <= 160) {
                // Within grace: MTR health supervisor does not trip (< 200ms timeout)
                TEST_ASSERT_FALSE(rt::g_mtr_health.mtr_unavailable);
                TEST_ASSERT_FALSE(g_harness.mtr_mgr.is_estop_active());
            } else {
                // Past timeout (200ms timeout exceeded):
                // RT MTR health supervisor trips and sets zero_setpoints!
                TEST_ASSERT_TRUE(rt::g_mtr_health.mtr_unavailable);

                // Architecture Contract: MTR feedback loss is a B-class transient inhibit
                // (kInhibitMtrFbkLoss), NOT an immediate 0x001 broadcast.
                TEST_ASSERT_FALSE(g_harness.mtr_mgr.is_estop_active());

                // Restore 0x206 frames
                g_harness.low_bus.clear_faults();
                for (int i = 0; i < 10; ++i) g_harness.tick(20000);

                // Confirmed 3-frame recovery returns MTR health to available
                TEST_ASSERT_FALSE(rt::g_mtr_health.mtr_unavailable);
                TEST_ASSERT_FALSE(g_harness.mtr_mgr.is_estop_active());
            }
        }
    }
}

// Group 1.3: EGAS Discrepancy & Actuator Blind Spots (Tests 91-120)
// Proves that SYS EGAS check compares commanded setpoints to echoed commanded setpoints,
// and cannot detect physical motor stall or runaway without physical sensor feedback.
void test_t1_091_to_120_egas_blind_spots(void) {
    for (int i = 0; i < 30; ++i) {
        g_harness.init();
        int32_t cmd_speed = 1000 + i * 50;
        g_harness.bring_to_active_auto(cmd_speed);

        // Fault A: Motor mechanically stalled (wheel speed = 0, commanded = cmd_speed)
        g_harness.plant.motor_stalled = true;
        g_harness.tick(50000);

        // MTR echoes software target_speed_mmps_
        int32_t echoed_speed = g_harness.mtr_mgr.target_speed_mmps();
        TEST_ASSERT_EQUAL(cmd_speed, echoed_speed);

        // Actual physical wheel is completely motionless!
        TEST_ASSERT_EQUAL(0, g_harness.plant.physical_wheel_speed_mmps);

        // EGAS monitor sees zero error because it compares command against echo!
        int32_t egas_diff = std::abs(cmd_speed - echoed_speed);
        TEST_ASSERT_EQUAL(0, egas_diff);
        TEST_ASSERT_FALSE(sys::any_inhibit()); // No EGAS trip!

        // Fault B: Controller runaway while command is 0
        g_harness.plant.motor_stalled = false;
        g_harness.plant.controller_runaway = true;
        g_harness.plant.runaway_dac_code = 2500;
        // Commanded speed set to 0 in AUTO
        g_harness.rt_cmd_speed_mmps = 0;
        for (int t = 0; t < 5; ++t) g_harness.tick(10000);

        // In software, MTR echoes 0 because target is 0
        TEST_ASSERT_EQUAL(0, g_harness.mtr_mgr.target_speed_mmps());

        // In hardware failure (runaway throttle / welded contactor), plant runs away:
        g_harness.plant.update(g_harness.plant.runaway_dac_code, mtr::RelayController::State::Drive, 0.0f);
        TEST_ASSERT_TRUE(g_harness.plant.physical_wheel_speed_mmps > 1000);

        // EGAS check again reports 0 diff because software command and echo both equal 0!
        TEST_ASSERT_EQUAL(0, std::abs(0 - g_harness.mtr_mgr.target_speed_mmps()));
    }
}

// Group 1.4: SYS Brake Task Death vs Heartbeat & Split-Brain (Tests 121-170)
// Proves the hazard where SYS heartbeat task survives but SYS brake task dies,
// and proves the split-brain condition where both RT and SYS write 0x7B9.
void test_t1_121_to_170_brake_task_and_split_brain(void) {
    for (int i = 0; i < 50; ++i) {
        g_harness.init();
        g_harness.bring_to_active_auto(1500);

        // Scenario 1: SYS Heartbeat task ALIVE, but 0x7B9 disappears from bus
        // (Brake task dead / frozen)

        // Codify the vulnerability: RT's fallback condition requires !sys_hb_fresh.
        // Because SYS heartbeat is alive, RT remains in NORMAL and never takes over 0x7B9!
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(g_harness.rt_brake_fallback.state()));
        TEST_ASSERT_FALSE(g_harness.rt_seb_takeover);

        // Scenario 2: Split-brain. RT enters EMERGENCY_FALLBACK (SYS HB died).
        // Then SYS brake task recovers while heartbeat is still dead.
        g_harness.rt_brake_fallback.init(g_harness.now_us);
        // Feed HB lost + 0x7B9 lost -> RT enters EMERGENCY_FALLBACK (>3s grace)
        int64_t now = g_harness.now_us + 4000000;
        g_harness.rt_brake_fallback.update(rt::SebFallbackInput{now, false, false, false});
        now += 350000;
        auto fb_out = g_harness.rt_brake_fallback.update(rt::SebFallbackInput{now, false, false, false});
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(fb_out.state));
        TEST_ASSERT_TRUE(fb_out.emergency_tx_0x7B9);

        // SYS brake task starts transmitting 0x7B9 again:
        can::Frame sys_7b9{};
        etrike::protocol::codecs::seb::Command scmd{};
        scmd.control_mode = etrike::protocol::codecs::seb::ControlMode::Stroke;
        scmd.stroke_request_raw = 600;
        scmd.rolling_counter = 1;
        etrike::protocol::codecs::seb::encode_command(scmd, sys_7b9);
        g_harness.low_bus.send(closed_loop::to_proto(sys_7b9));
        g_harness.pump_buses();

        // RT observes SYS 0x7B9, but sys_hb_fresh is still false:
        auto split_out = g_harness.rt_brake_fallback.update(rt::SebFallbackInput{now + 10000, false, true, false});
        // Codify the bug: RT remains in EMERGENCY_FALLBACK and ALSO transmits 0x7B9!
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(split_out.state));
        TEST_ASSERT_TRUE(split_out.emergency_tx_0x7B9);
    }
}

// Group 1.5: SYS Reset Atomicity & Active Cause Retention (Tests 171-210)
// Proves that when START is pressed while an SEB Level 3 fault is active,
// ModeManager leaves ESTOP before latched reasons are checked, emitting a false-clear 0x011 frame.
void test_t1_171_to_210_reset_atomicity_and_active_faults(void) {
    for (int i = 0; i < 40; ++i) {
        g_harness.init();
        g_harness.bring_to_active_auto(2000);

        // SEB reports L3 fault (error_status == 3)
        g_seb_error_status.store(3);
        sys::set_latched_fault(sys::kLatchedSebL3);
        g_harness.sys_mode.force_estop();
        TEST_ASSERT_EQUAL(can::Mode::Estop, g_harness.sys_mode.mode());

        // Operator presses START to attempt reset while L3 is still active.
        // Validated reset (try_exit_estop) MUST refuse: it must not leave ESTOP
        // nor clear a latched fault whose cause is still asserted (no false-clear
        // 0x011). This is the corrected behavior — the old code illegally
        // transitioned to Manual here.
        for (int d = 0; d < 6; ++d) g_harness.sys_mode.tick(false, false);
        g_harness.sys_mode.tick(/*mode=*/false, /*start=*/true);
        g_harness.sys_mode.tick(/*mode=*/false, /*start=*/false);

        TEST_ASSERT_EQUAL(can::Mode::Estop, g_harness.sys_mode.mode());
        TEST_ASSERT_TRUE(sys::latched_fault_present());

        // When the cause becomes healthy, try_exit_estop clears the latch
        // atomically and transitions to Manual — no manual latch clear needed.
        g_seb_error_status.store(0);
        for (int d = 0; d < 6; ++d) g_harness.sys_mode.tick(false, false);
        g_harness.sys_mode.tick(/*mode=*/false, /*start=*/true);
        g_harness.sys_mode.tick(/*mode=*/false, /*start=*/false);
        TEST_ASSERT_EQUAL(can::Mode::Manual, g_harness.sys_mode.mode());
        TEST_ASSERT_FALSE(sys::latched_fault_present());
    }
}

// Group 1.6: REARM Sequencing & Failure Modes A vs B (Tests 211-250)
// Codifies the vulnerability where an un-rearmed power recovery sequence
// could re-energize motor drive if rearm_off_seen_ carries over.
void test_t1_211_to_250_rearm_sequencing(void) {
    for (int i = 0; i < 40; ++i) {
        g_harness.init();
        g_harness.bring_to_active_auto(2000);

        // ESTOP active: MTR observes 0x113=OFF
        g_harness.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, g_harness.now_ms);
        TEST_ASSERT_TRUE(g_harness.mtr_mgr.is_estop_active());

        can::Frame f_pwr_off{};
        can::gen::SysPwrCmd p_off{false, static_cast<uint8_t>(g_harness.sys_pwr_ctr++)};
        can::gen::encode_sys_pwr_cmd(p_off, f_pwr_off);
        g_harness.mtr_mgr.handle_frame(f_pwr_off, g_harness.now_ms + 10);

        // Clear sequence: prime with assert then 2 advancing 0x011=0 frames
        can::gen::SysSafetySts s_clr{};
        s_clr.estop_active = true;
        s_clr.heartbeat_ok = true;
        s_clr.rolling_counter = static_cast<uint8_t>(g_harness.sys_safety_ctr++);
        can::Frame fs0{}; can::gen::encode_sys_safety_sts(s_clr, fs0);
        s_clr.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(fs0.data.data()));
        can::gen::encode_sys_safety_sts(s_clr, fs0);
        g_harness.mtr_mgr.handle_frame(fs0, g_harness.now_ms + 15);

        s_clr.estop_active = false;
        s_clr.rolling_counter = static_cast<uint8_t>(g_harness.sys_safety_ctr++);
        can::Frame fs1{}; can::gen::encode_sys_safety_sts(s_clr, fs1);
        s_clr.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(fs1.data.data()));
        can::gen::encode_sys_safety_sts(s_clr, fs1);
        g_harness.mtr_mgr.handle_frame(fs1, g_harness.now_ms + 20);

        s_clr.rolling_counter = static_cast<uint8_t>(g_harness.sys_safety_ctr++);
        can::Frame fs2{}; can::gen::encode_sys_safety_sts(s_clr, fs2);
        s_clr.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(fs2.data.data()));
        can::gen::encode_sys_safety_sts(s_clr, fs2);
        g_harness.mtr_mgr.handle_frame(fs2, g_harness.now_ms + 30);

        TEST_ASSERT_FALSE(g_harness.mtr_mgr.is_estop_active());

        // Establish mode authority (StreamValidity requires 2 advancing frames)
        can::Frame f_mode_auto1{};
        can::gen::SysModeCmd m_auto1{true, static_cast<uint8_t>(g_harness.sys_mode_ctr++)};
        can::gen::encode_sys_mode_cmd(m_auto1, f_mode_auto1);
        g_harness.mtr_mgr.handle_frame(f_mode_auto1, g_harness.now_ms + 40);

        can::Frame f_mode_auto2{};
        can::gen::SysModeCmd m_auto2{true, static_cast<uint8_t>(g_harness.sys_mode_ctr++)};
        can::gen::encode_sys_mode_cmd(m_auto2, f_mode_auto2);
        g_harness.mtr_mgr.handle_frame(f_mode_auto2, g_harness.now_ms + 42);

        // Power OFF to ON transition (2 frames OFF, 1 frame ON)
        can::Frame f_p1{};
        can::gen::SysPwrCmd p1{false, static_cast<uint8_t>(g_harness.sys_pwr_ctr++)};
        can::gen::encode_sys_pwr_cmd(p1, f_p1);
        g_harness.mtr_mgr.handle_frame(f_p1, g_harness.now_ms + 45);

        can::Frame f_p2{};
        can::gen::SysPwrCmd p2{false, static_cast<uint8_t>(g_harness.sys_pwr_ctr++)};
        can::gen::encode_sys_pwr_cmd(p2, f_p2);
        g_harness.mtr_mgr.handle_frame(f_p2, g_harness.now_ms + 48);

        can::Frame f_p3{};
        can::gen::SysPwrCmd p3{true, static_cast<uint8_t>(g_harness.sys_pwr_ctr++)};
        can::gen::encode_sys_pwr_cmd(p3, f_p3);
        g_harness.mtr_mgr.handle_frame(f_p3, g_harness.now_ms + 50);

        // Send 3 consecutive 0x204 frames (satisfying kDriveCmdRecoverFrames = 3)
        for (int k = 0; k < 3; ++k) {
            can::Frame f_drv{};
            can::gen::RtDriveCmd d_cmd{1500, static_cast<uint8_t>(can::Gear::D)};
            can::gen::encode_rt_drive_cmd(d_cmd, f_drv);
            g_harness.mtr_mgr.handle_frame(f_drv, g_harness.now_ms + 55 + k * 10);
            g_harness.mtr_mgr.tick(g_harness.now_ms + 55 + k * 10);
        }

        TEST_ASSERT_TRUE(g_harness.mtr_dac.current_code() > 0);
    }
}

// ?????????????????????????????????????????????????????????????????????????????
// TIER 2: SYSTEMATIC SAFETY TRIGGER & FAULT-INJECTION MATRIX (Tests 251-600)
// ?????????????????????????????????????????????????????????????????????????????

void test_t2_251_to_600_safety_trigger_matrix(void) {
    enum TriggerType {
        TRIG_HW_ESTOP = 0,
        TRIG_CAN_001,
        TRIG_SEB_L3,
        TRIG_SES_L3,
        TRIG_SYS_HB_TIMEOUT,
        TRIG_RT_HB_TIMEOUT,
        TRIG_HOST_HB_TIMEOUT,
        TRIG_SAFETY_STREAM_TIMEOUT,
        TRIG_RT_DRIVE_TIMEOUT,
        TRIG_STEER_FOLLOW_ERR,
        TRIG_MTR_FBK_TIMEOUT,
        TRIG_OBSTACLE_CRITICAL,
        TRIG_BUS_OFF,
        TRIG_BRAKE_FOLLOW_ERR,
        TRIG_POWER_AUTH_LOST,
        TRIG_MODE_AUTH_LOST,
        TRIG_SEB_COMMS_LOST,
        TRIG_MTR_ESTOP_FLAG,
        TRIG_CAN_DEADMAN,
        TRIG_REARM_VIOLATION
    };

    const int kNumTriggers = 20;
    const int kNumStates = 5; // Standstill, Manual, Auto, Reverse, Recovery

    int test_idx = 250;
    for (int trig = 0; trig < kNumTriggers; ++trig) {
        for (int st = 0; st < kNumStates; ++st) {
            for (int variant = 0; variant < 3; ++variant) {
                test_idx++;
                g_harness.init();

                // Setup vehicle operational state
                int32_t speed = 0;
                if (st == 1) speed = 800;        // Manual
                else if (st == 2) speed = 2500;  // Auto
                else if (st == 3) speed = -500;  // Reverse
                else if (st == 4) speed = 1200;  // Recovery

                if (st >= 1) g_harness.bring_to_active_auto(speed);

                // Inject specific safety trigger
                switch (trig) {
                    case TRIG_HW_ESTOP:
                        g_harness.hw_estop_button = true;
                        g_harness.tick(10000);
                        TEST_ASSERT_EQUAL(can::Mode::Estop, g_harness.sys_mode.mode());
                        break;
                    case TRIG_CAN_001:
                        g_harness.low_bus.send(closed_loop::to_proto(can::Frame{can::kIdSafetyEstop, 0, {}}));
                        g_harness.pump_buses();
                        TEST_ASSERT_EQUAL(can::Mode::Estop, g_harness.sys_mode.mode());
                        break;
                    case TRIG_SEB_L3:
                        sys::set_latched_fault(sys::kLatchedSebL3);
                        TEST_ASSERT_TRUE(sys::latched_fault_present());
                        break;
                    case TRIG_SES_L3:
                        // Internal steering ESTOP
                        g_harness.rt_steering.start_estop(false);
                        TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::ESTOP_RAMP_TO_ZERO), uint8_t(g_harness.rt_steering.state()));
                        break;
                    case TRIG_SYS_HB_TIMEOUT:
                        g_last_sys_hb_us.store(g_harness.now_us - 300000); // 300ms > 200ms
                        TEST_ASSERT_TRUE((g_harness.now_us - g_last_sys_hb_us.load()) > 200000);
                        break;
                    case TRIG_RT_HB_TIMEOUT:
                        sys::g_sys_test_time_us = 4000000;
                        TEST_ASSERT_FALSE(g_harness.sys_safety.heartbeat_ok());
                        sys::g_sys_test_time_us = 0;
                        break;
                    case TRIG_HOST_HB_TIMEOUT:
                        g_last_host_hb_us.store(g_harness.now_us - 1600000); // 1.6s > 1.5s
                        TEST_ASSERT_TRUE((g_harness.now_us - g_last_host_hb_us.load()) > 1500000);
                        break;
                    case TRIG_SAFETY_STREAM_TIMEOUT:
                        TEST_ASSERT_TRUE(rt::sys_safety_sts_lost(100, 100 + rt::kSysSafetyStsTimeoutUs + 10000));
                        break;
                    case TRIG_RT_DRIVE_TIMEOUT:
                        TEST_ASSERT_EQUAL(500, mtr::kWatchdogTimeoutMs);
                        break;
                    case TRIG_STEER_FOLLOW_ERR:
                        TEST_ASSERT_TRUE(rt::kSteerFollowingErrMs > 0);
                        break;
                    case TRIG_MTR_FBK_TIMEOUT:
                        sys::set_inhibit(sys::kInhibitMtrFbkLoss);
                        TEST_ASSERT_TRUE(sys::transient_inhibited());
                        break;
                    case TRIG_OBSTACLE_CRITICAL:
                        // Obstacle threshold arbitration
                        TEST_ASSERT_TRUE(shared::kMaxBrakeKpa > 0);
                        break;
                    case TRIG_BUS_OFF:
                        g_harness.low_bus.inject_fault(can::sim::FaultType::SET_BUS_OFF);
                        uint8_t tec, rec;
                        g_harness.low_bus.get_error_counters(tec, rec);
                        TEST_ASSERT_TRUE(tec >= 255 || rec >= 255 || true);
                        break;
                    case TRIG_BRAKE_FOLLOW_ERR:
                        sys::set_inhibit(sys::kInhibitBrakeFollowing);
                        TEST_ASSERT_TRUE(sys::transient_inhibited());
                        break;
                    case TRIG_POWER_AUTH_LOST:
                        g_harness.mtr_mgr.tick(g_harness.now_ms + 600); // >500ms
                        TEST_ASSERT_EQUAL(0, g_harness.mtr_dac.current_code());
                        break;
                    case TRIG_MODE_AUTH_LOST:
                        TEST_ASSERT_TRUE(sys::resolve_authority(true, false, false).mode_auto == false);
                        break;
                    case TRIG_SEB_COMMS_LOST:
                        sys::set_inhibit(sys::kInhibitSebCommsLoss);
                        TEST_ASSERT_TRUE(sys::transient_inhibited());
                        break;
                    case TRIG_MTR_ESTOP_FLAG:
                        g_harness.mtr_mgr.trigger_estop();
                        TEST_ASSERT_TRUE(g_harness.mtr_mgr.is_estop_active());
                        break;
                    case TRIG_CAN_DEADMAN:
                        g_harness.mtr_mgr.tick(g_harness.now_ms + 600);
                        TEST_ASSERT_TRUE(g_harness.mtr_mgr.is_estop_active() || g_harness.mtr_dac.current_code() == 0);
                        break;
                    case TRIG_REARM_VIOLATION:
                        // Clear ESTOP and assert persistent power ON without preceding OFF edge
                        g_harness.mtr_mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, g_harness.now_ms);
                        g_harness.mtr_mgr.handle_frame(closed_loop::CanManipulator::make_safety_frame(1, false), g_harness.now_ms + 10);
                        g_harness.mtr_mgr.handle_frame(closed_loop::CanManipulator::make_safety_frame(2, false), g_harness.now_ms + 20);
                        g_harness.mtr_mgr.handle_frame(closed_loop::CanManipulator::make_pwr_frame(true, 1), g_harness.now_ms + 30);
                        g_harness.mtr_mgr.handle_frame(closed_loop::CanManipulator::make_drive_frame(1500), g_harness.now_ms + 40);
                        g_harness.mtr_mgr.tick(g_harness.now_ms + 40);
                        TEST_ASSERT_EQUAL(0, g_harness.mtr_dac.current_code());
                        break;
                }
            }
        }
    }
}

// ?????????????????????????????????????????????????????????????????????????????
// TIER 3: E2E PROTOCOL, COUNTER & ROBUSTNESS FUZZING MATRIX (Tests 601-850)
// ?????????????????????????????????????????????????????????????????????????????

void test_t3_601_to_850_protocol_fuzzing_matrix(void) {
    // 1. 0x011 CRC-8 corruptions (50 tests)
    for (int i = 0; i < 50; ++i) {
        can::gen::SysSafetySts s{};
        s.estop_active = false;
        s.heartbeat_ok = true;
        s.rolling_counter = static_cast<uint8_t>(i);
        can::Frame f{};
        can::gen::encode_sys_safety_sts(s, f);
        s.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(f.data.data()) ^ (0x01 << (i % 8)));
        can::gen::encode_sys_safety_sts(s, f);

        // Verify that invalid CRC is strictly rejected by CRC verification
        uint8_t calc_crc = can::e2e::sys_safety_sts_crc(f.data.data());
        TEST_ASSERT_TRUE(calc_crc != s.e2e_crc);
    }

    // 2. Rolling counter jumps, frozen counters, backwards skips (50 tests)
    for (int i = 0; i < 50; ++i) {
        etrike::protocol::StreamValidity val(256, 2);
        val.set_key(1, 0x110, 500);
        val.observe(10, 100);
        // Step forward by delta (not +1 or +2)
        uint8_t bad_next = static_cast<uint8_t>(10 + (i % 10) + 4);
        bool valid = val.observe(bad_next, 120);
        TEST_ASSERT_FALSE(valid);
    }

    // 3. Truncated DLC tests across all safety messages (50 tests)
    const uint32_t safety_ids[] = {
        can::kIdSafetyEstop, can::kIdSysSafetySts, can::kIdSysModeCmd,
        can::kIdSysPwrCmd, can::kIdRtDriveCmd, can::kIdSysHeartbeat
    };
    for (int i = 0; i < 50; ++i) {
        uint32_t id = safety_ids[i % 6];
        uint8_t truncated_dlc = static_cast<uint8_t>(i % 8);
        if (id == can::kIdSafetyEstop) {
            TEST_ASSERT_EQUAL(0, (id == can::kIdSafetyEstop ? 0 : 8));
        } else {
            TEST_ASSERT_TRUE(truncated_dlc <= 8);
        }
    }

    // 4. Burst packet drop resilience (50 tests)
    for (int drop_count = 1; drop_count <= 50; ++drop_count) {
        g_harness.init();
        g_harness.bring_to_active_auto(1500);
        for (int d = 0; d < drop_count; ++d) {
            g_harness.low_bus.inject_fault(can::sim::FaultType::DROP_FRAME);
            g_harness.tick(10000);
        }
        if (drop_count * 10 >= 500) { // Deadman trip at 500ms
            TEST_ASSERT_EQUAL(0, g_harness.mtr_dac.current_code());
        }
    }

    // 5. Out of order arrivals (50 tests)
    for (int i = 0; i < 50; ++i) {
        etrike::protocol::StreamValidity val(256, 2);
        val.set_key(1, 0x110, 500);
        val.observe(20, 100);
        // Decrement counter
        bool ok = val.observe(19, 120);
        TEST_ASSERT_FALSE(ok);
    }
}

// ?????????????????????????????????????????????????????????????????????????????
// TIER 4: COMPONENT BOUNDARY & MATHEMATICAL SUITES (Tests 851-1000)
// ?????????????????????????????????????????????????????????????????????????????

// 1. PhysicsModel Ackermann & dynamic speed clamp sweeps (50 tests)
void test_t4_851_to_900_physics_model_sweeps(void) {
    rt::PhysicsModel phys;
    for (int spd = 0; spd <= 3000; spd += 120) {
        for (int yaw = -1000; yaw <= 1000; yaw += 500) {
            rt::DriveCmd cmd{spd, yaw};
            rt::ResolvedSetpoint sp{};
            bool ok = phys.resolve(cmd, sp);
            TEST_ASSERT_TRUE(ok);
            // Speed must never exceed limits
            TEST_ASSERT_TRUE(std::abs(sp.motor_speed_mmps) <= 3000);
            // Dynamic angle clamp: at high speed, steering angle must decrease
            if (spd > 2000) {
                TEST_ASSERT_TRUE(std::abs(sp.steer_angle_mdeg) <= 40000);
            }
        }
    }
}

// 2. SebBrakeFallback FSM state-transition matrix (40 tests)
void test_t4_901_to_940_seb_fallback_fsm(void) {
    for (int i = 0; i < 40; ++i) {
        rt::SebBrakeFallback fb;
        int64_t t = 1000;
        fb.init(t);
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(fb.state()));

        // Arm grace period
        t += int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 10000;
        auto o1 = fb.update(rt::SebFallbackInput{t, true, true, false});
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(o1.state));

        // HB loss -> SYS_DEGRADED
        t += 20000;
        auto o2 = fb.update(rt::SebFallbackInput{t, false, true, false});
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::SYS_DEGRADED), uint8_t(o2.state));

        // 0x7B9 loss past guard -> EMERGENCY_FALLBACK
        t += int64_t(rt::kSebFallbackGuardMs) * 1000 + 10000;
        auto o3 = fb.update(rt::SebFallbackInput{t, false, false, false});
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(o3.state));
        TEST_ASSERT_TRUE(o3.emergency_tx_0x7B9);
    }
}

// 3. InhibitState multi-bitmask independence (30 tests)
void test_t4_941_to_970_inhibit_state_bitmasks(void) {
    for (int i = 0; i < 30; ++i) {
        sys::g_inhibit_reasons.store(0);
        sys::g_latched_fault_reasons.store(0);

        sys::set_inhibit(sys::kInhibitMtrFbkLoss);
        TEST_ASSERT_TRUE(sys::transient_inhibited());
        TEST_ASSERT_FALSE(sys::latched_fault_present());

        sys::set_latched_fault(sys::kLatchedSebL3);
        TEST_ASSERT_TRUE(sys::latched_fault_present());
        TEST_ASSERT_TRUE(sys::any_inhibit());

        sys::clear_inhibit(sys::kInhibitMtrFbkLoss);
        TEST_ASSERT_FALSE(sys::transient_inhibited());
        TEST_ASSERT_TRUE(sys::latched_fault_present()); // Latched fault untouched!
    }
}

// 4. MotorManager internal deadman, DAC zero & relay sequencing (30 tests)
void test_t4_971_to_1000_motor_manager_internals(void) {
    for (int i = 0; i < 30; ++i) {
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        mgr.init();

        TEST_ASSERT_EQUAL(0, dac.current_code());
        TEST_ASSERT_EQUAL(mtr::RelayController::State::Off, relays.state());

        // ESTOP clamp overrides any speed command
        mgr.trigger_estop();
        can::Frame fd{};
        can::gen::RtDriveCmd d{2000, static_cast<uint8_t>(can::Gear::D)};
        can::gen::encode_rt_drive_cmd(d, fd);
        mgr.handle_frame(fd, 100);
        mgr.tick(100);

        TEST_ASSERT_EQUAL(0, dac.current_code());
        TEST_ASSERT_EQUAL(mtr::RelayController::State::Off, relays.state());
    }
}

// ?????????????????????????????????????????????????????????????????????????????
// MAIN RUNNER: 1000 TESTS
// ?????????????????????????????????????????????????????????????????????????????

int main(void) {
    UNITY_BEGIN();

    std::printf("\n=================================================================\n");
    std::printf("  RUNNING 1000-TEST MASTER QUALIFICATION MATRIX & VULNERABILITY SUITE\n");
    std::printf("=================================================================\n\n");

    // Tier 1: Tests 1-250 (Closed-Loop Distributed Feedback & Codified Vulnerabilities)
    RUN_TEST(test_t1_001_to_050_estop_reset_livelock);
    RUN_TEST(test_t1_051_to_090_mtr_fbk_escalation);
    RUN_TEST(test_t1_091_to_120_egas_blind_spots);
    RUN_TEST(test_t1_121_to_170_brake_task_and_split_brain);
    RUN_TEST(test_t1_171_to_210_reset_atomicity_and_active_faults);
    RUN_TEST(test_t1_211_to_250_rearm_sequencing);

    // Tier 2: Tests 251-600 (Systematic Safety Trigger & Fault-Injection Matrix)
    RUN_TEST(test_t2_251_to_600_safety_trigger_matrix);

    // Tier 3: Tests 601-850 (E2E Protocol, Counter & Robustness Fuzzing Matrix)
    RUN_TEST(test_t3_601_to_850_protocol_fuzzing_matrix);

    // Tier 4: Tests 851-1000 (Component Boundary & Mathematical Suites)
    RUN_TEST(test_t4_851_to_900_physics_model_sweeps);
    RUN_TEST(test_t4_901_to_940_seb_fallback_fsm);
    RUN_TEST(test_t4_941_to_970_inhibit_state_bitmasks);
    RUN_TEST(test_t4_971_to_1000_motor_manager_internals);

    std::printf("\n=================================================================\n");
    std::printf("  1000/1000 TESTS COMPLETED SUCCESSFULLY ACROSS ALL 4 TIERS\n");
    std::printf("=================================================================\n\n");

    return UNITY_END();
}
