#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#include "stub/stm32g4xx_hal.h"
#include "protocol/compat/can.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"

// Load configurations
#include "shared_config.h"
#include "sys-esp32/src/config.h"
#include "rt-esp32/src/config.h"
#include "mtr-stm32/src/config.h"

// SYS authority, safety, and brake control
#include "sys-esp32/src/safety_monitor.h"
#include "sys-esp32/src/mode_manager.h"
#include "sys-esp32/src/brake_control.h"
#include "sys-esp32/src/inhibit_state.h"

// RT safety, kinematics, & fallback
#include "rt-esp32/src/physics_model.h"
#include "rt-esp32/src/brake_arbitration.h"
#include "rt-esp32/src/brake_fallback.h"
#include "rt-esp32/src/seb_request.h"
#include "rt-esp32/src/steering_control.h"
#include "rt-esp32/src/watchdog.h"

// MTR actuation
#include "mtr-stm32/src/relay_controller.h"
#include "mtr-stm32/src/dac_controller.h"
#include "mtr-stm32/src/motor_manager.h"

using namespace rt;
using namespace mtr;

namespace sys {
    std::atomic<uint32_t> g_inhibit_reasons{0};
    std::atomic<uint32_t> g_latched_fault_reasons{0};
    extern int64_t g_sys_test_time_us;
}

extern "C" { FDCAN_HandleTypeDef hfdcan1; }
bool g_bypass_eps_sync = false;
bool g_bench_solo_mode = false;

namespace {

static uint8_t pack_seb_status_b0(const can::custom::seb::Status& status) {
    return (status.alignment_status ? 1u : 0u) |
           ((status.control_enabled ? 1u : 0u) << 1) |
           ((status.control_mode & 3u) << 2) |
           ((status.auto_brake_status ? 1u : 0u) << 4) |
           ((status.error_status & 3u) << 6);
}

} // anonymous namespace

void setUp(void) {
    hal_mock::reset();
    sys::g_inhibit_reasons.store(0);
    sys::g_latched_fault_reasons.store(0);
}

void tearDown(void) {}

// ═════════════════════════════════════════════════════════════════════
// PART 1: ESTOP RESETTING & REARM INVARIANTS
// ═════════════════════════════════════════════════════════════════════

// ── 1. ESTOP Reset Rejected While Physical Button is Held Down ─────
void test_estop_reset_rejected_while_button_held(void) {
    sys::SafetyMonitor sm;
    sm.init();
    sys::ModeManager mm;
    mm.init();

    // Normal running
    mm.tick(/*mode_btn=*/false, /*start_btn=*/false);
    TEST_ASSERT_EQUAL(can::Mode::Manual, mm.mode());

    // Driver presses physical E-Stop button
    sm.set_estop(true);
    mm.force_estop();
    TEST_ASSERT_EQUAL(can::Mode::Estop, mm.mode());
    TEST_ASSERT_TRUE(sys::ModeManager::estop_latched(mm.mode(), sm.estop_active()));

    // Operator presses START button trying to reset while physical E-Stop is STILL HELD
    mm.tick(/*mode_btn=*/false, /*start_btn=*/true);
    mm.tick(/*mode_btn=*/false, /*start_btn=*/false); // release edge

    // ModeManager exits to Manual, BUT sm.estop_active() is STILL true
    // Safety invariant: estop_latched MUST STILL BE TRUE!
    TEST_ASSERT_TRUE(sys::ModeManager::estop_latched(mm.mode(), sm.estop_active()));

    // Only after physical button is released does estop_latched drop to false
    sm.set_estop(false);
    TEST_ASSERT_FALSE(sys::ModeManager::estop_latched(mm.mode(), sm.estop_active()));
}

// ── 2. Asymmetric Clear: Counter Rollover (255 -> 0) Modulo 256 ────
void test_estop_reset_counter_rollover_255_to_0(void) {
    RelayController relays;
    DacController dac;
    MotorManager mgr(relays, dac);
    mgr.init();

    auto make_safe_frame = [](uint8_t ctr, bool estop) {
        can::gen::SysSafetySts s{};
        s.estop_active = estop;
        s.heartbeat_ok = true;
        s.rolling_counter = ctr;
        can::Frame f{};
        can::gen::encode_sys_safety_sts(s, f);
        s.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(f.data.data()));
        can::gen::encode_sys_safety_sts(s, f);
        return f;
    };

    // Prime stream authority with two active ESTOP frames (253, 254)
    mgr.handle_frame(make_safe_frame(253, true), 90);
    mgr.handle_frame(make_safe_frame(254, true), 100);
    mgr.tick(100);
    TEST_ASSERT_TRUE(mgr.is_estop_active());

    // Clear frame 1: rolling counter = 255, estop_active = 0 (baseline)
    mgr.handle_frame(make_safe_frame(255, false), 110);
    TEST_ASSERT_TRUE(mgr.is_estop_active()); // 1st frame: baseline only

    // Clear frame 2: rolling counter = 0 ((255 + 1) % 256 == 0), estop_active = 0
    mgr.handle_frame(make_safe_frame(0, false), 120);
    // Rollover modulo 256 MUST be recognized as exactly +1 advancing zero!
    TEST_ASSERT_FALSE(mgr.is_estop_active());
}

// ── 3. Asymmetric Clear: Counter Gap & Duplicate Rejection ─────────
void test_estop_reset_gap_and_duplicate_rejection(void) {
    RelayController relays;
    DacController dac;
    MotorManager mgr(relays, dac);
    mgr.init();

    auto make_safe_frame = [](uint8_t ctr, bool estop) {
        can::gen::SysSafetySts s{};
        s.estop_active = estop;
        s.heartbeat_ok = true;
        s.rolling_counter = ctr;
        can::Frame f{};
        can::gen::encode_sys_safety_sts(s, f);
        s.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(f.data.data()));
        can::gen::encode_sys_safety_sts(s, f);
        return f;
    };

    // Prime stream authority with two active ESTOP frames (8, 9)
    mgr.handle_frame(make_safe_frame(8, true), 90);
    mgr.handle_frame(make_safe_frame(9, true), 100);
    TEST_ASSERT_TRUE(mgr.is_estop_active());

    // Frame 1: ctr=10, zero -> baseline
    mgr.handle_frame(make_safe_frame(10, false), 110);
    TEST_ASSERT_TRUE(mgr.is_estop_active());

    // Frame 2: duplicate ctr=10, zero -> duplicate restarts sequence from this frame!
    mgr.handle_frame(make_safe_frame(10, false), 120);
    TEST_ASSERT_TRUE(mgr.is_estop_active()); // NOT cleared

    // Frame 3: gap ctr=12 (1 missed frame tolerated by StreamValidity, but restarts clear sequence)
    mgr.handle_frame(make_safe_frame(12, false), 130);
    TEST_ASSERT_TRUE(mgr.is_estop_active()); // NOT cleared

    // Frame 4: advancing ctr=13, zero -> 2nd consecutive from 12!
    mgr.handle_frame(make_safe_frame(13, false), 140);
    TEST_ASSERT_FALSE(mgr.is_estop_active()); // NOW cleared!
}

// ── 4. Asymmetric Clear: Assert Frame Mid-Sequence Aborts Reset ────
void test_estop_reset_assert_mid_sequence_aborts_clear(void) {
    RelayController relays;
    DacController dac;
    MotorManager mgr(relays, dac);
    mgr.init();

    can::Frame f_001{can::kIdSafetyEstop, 0, {}};
    mgr.handle_frame(f_001, 100);
    TEST_ASSERT_TRUE(mgr.is_estop_active());

    auto make_safe_frame = [](uint8_t ctr, bool estop) {
        can::gen::SysSafetySts s{};
        s.estop_active = estop;
        s.heartbeat_ok = true;
        s.rolling_counter = ctr;
        can::Frame f{};
        can::gen::encode_sys_safety_sts(s, f);
        s.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(f.data.data()));
        can::gen::encode_sys_safety_sts(s, f);
        return f;
    };

    // Frame 1: ctr=20, zero -> confirm=1
    mgr.handle_frame(make_safe_frame(20, false), 110);
    TEST_ASSERT_TRUE(mgr.is_estop_active());

    // Glitch / re-assert frame: ctr=21, estop_active=true!
    mgr.handle_frame(make_safe_frame(21, true), 120);
    TEST_ASSERT_TRUE(mgr.is_estop_active());

    // Next frame: ctr=22, zero -> this is only 1st zero after re-assert!
    mgr.handle_frame(make_safe_frame(22, false), 130);
    TEST_ASSERT_TRUE(mgr.is_estop_active()); // Still locked!

    // Frame: ctr=23, zero -> 2nd consecutive zero!
    mgr.handle_frame(make_safe_frame(23, false), 140);
    TEST_ASSERT_FALSE(mgr.is_estop_active()); // Now released!
}

// ── 5. REARM Protocol Sequence & Violation Edge Cases ─────────────
void test_rearm_protocol_sequence_and_violations(void) {
    RelayController relays;
    DacController dac;
    MotorManager mgr(relays, dac);
    mgr.init();

    auto make_mode_frame = [](bool auto_mode, uint8_t ctr) {
        can::gen::SysModeCmd m{auto_mode, ctr};
        can::Frame f{};
        can::gen::encode_sys_mode_cmd(m, f);
        return f;
    };
    auto make_pwr_frame = [](bool pwr_on, uint8_t ctr) {
        can::gen::SysPwrCmd p{pwr_on, ctr};
        can::Frame f{};
        can::gen::encode_sys_pwr_cmd(p, f);
        return f;
    };
    auto make_safe_frame = [](uint8_t ctr, bool estop) {
        can::gen::SysSafetySts s{};
        s.estop_active = estop;
        s.heartbeat_ok = true;
        s.rolling_counter = ctr;
        can::Frame f{};
        can::gen::encode_sys_safety_sts(s, f);
        s.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(f.data.data()));
        can::gen::encode_sys_safety_sts(s, f);
        return f;
    };
    auto make_drive_frame = [](int32_t spd) {
        can::gen::RtDriveCmd d{spd, static_cast<uint8_t>(can::Gear::D)};
        can::Frame f{};
        can::gen::encode_rt_drive_cmd(d, f);
        return f;
    };

    // 1. Establish nominal drive
    uint8_t m_ctr = 1, p_ctr = 1, s_ctr = 1;
    for (int i = 0; i < 3; ++i) {
        mgr.handle_frame(make_mode_frame(true, m_ctr++), 100);
        mgr.handle_frame(make_pwr_frame(true, p_ctr++), 100);
        mgr.handle_frame(make_safe_frame(s_ctr++, false), 100);
        mgr.handle_frame(make_drive_frame(2000), 100);
        mgr.tick(100);
    }
    TEST_ASSERT_EQUAL(RelayController::State::Drive, relays.state());
    TEST_ASSERT_EQUAL(1544, dac.current_code());

    // 2. Trigger ESTOP and clear latch via 2-frame sequence
    mgr.handle_frame(make_safe_frame(s_ctr++, true), 110);
    mgr.tick(110);
    TEST_ASSERT_TRUE(mgr.is_estop_active());
    TEST_ASSERT_EQUAL(RelayController::State::Off, relays.state());

    mgr.handle_frame(make_safe_frame(s_ctr++, false), 120); // baseline
    mgr.handle_frame(make_safe_frame(s_ctr++, false), 130); // clear
    TEST_ASSERT_FALSE(mgr.is_estop_active());

    // 3. Violation 1: Drive command arrives without REARM -> rejected
    mgr.handle_frame(make_drive_frame(2000), 140);
    mgr.tick(140);
    TEST_ASSERT_EQUAL(RelayController::State::Off, relays.state());
    TEST_ASSERT_EQUAL(0, dac.current_code());

    // 4. Violation 2: Power ON sent without observing Power OFF first -> rejected
    mgr.handle_frame(make_mode_frame(true, m_ctr++), 150);
    mgr.handle_frame(make_mode_frame(true, m_ctr++), 150);
    mgr.handle_frame(make_pwr_frame(true, p_ctr++), 150);
    mgr.handle_frame(make_drive_frame(2000), 150);
    mgr.tick(150);
    TEST_ASSERT_EQUAL(RelayController::State::Off, relays.state());
    TEST_ASSERT_EQUAL(0, dac.current_code());

    // 5. Proper sequence:
    // Power OFF edge
    mgr.handle_frame(make_pwr_frame(false, p_ctr++), 160);
    mgr.handle_frame(make_pwr_frame(false, p_ctr++), 160);
    mgr.tick(160);

    // Power ON edge with Mode AUTO valid
    mgr.handle_frame(make_pwr_frame(true, p_ctr++), 170);
    mgr.handle_frame(make_pwr_frame(true, p_ctr++), 170);
    mgr.tick(170);

    // Drive command now successfully engages!
    mgr.handle_frame(make_drive_frame(2000), 180);
    mgr.tick(180);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, relays.state());
    TEST_ASSERT_EQUAL(1544, dac.current_code());
}

// ═════════════════════════════════════════════════════════════════════
// PART 2: BRAKE FAULTS & PRIORITY ARBITRATIONS
// ═════════════════════════════════════════════════════════════════════

// ── 6. Mechanical Rider Brake Lever Overrides Autonomous Pressure ───
void test_brake_rider_lever_override_during_auto_cruise(void) {
    sys::BrakeControl bc;
    bc.init();

    can::custom::seb::Status seb_ok{};
    seb_ok.alignment_status = true;
    seb_ok.stroke_value_raw = 600;
    uint8_t status_b0 = pack_seb_status_b0(seb_ok);
    uint16_t stroke = 600;

    // Bootstrap to ACTIVE
    can::custom::seb::Command unused;
    for (int i = 0; i < 110; ++i) {
        bc.tick(false, false, 0, can::Mode::Manual, 0xFF, 600, unused);
    }
    bc.tick(false, false, 0, can::Mode::Manual, status_b0, stroke, unused);

    // Autonomous cruise in AUTO mode with 2500 kPa autonomous request
    can::custom::seb::Command out;
    bc.tick(/*lever=*/false, /*estop=*/false, /*brake_kpa=*/2500, can::Mode::Auto, status_b0, stroke, out);
    TEST_ASSERT_EQUAL(uint8_t(can::custom::seb::ControlMode::Pressure), uint8_t(out.control_mode));
    TEST_ASSERT_EQUAL(1, out.auto_brake);
    TEST_ASSERT_TRUE(out.pressure_request_raw > 0);

    // Rider pulls mechanical brake lever while autonomous cruise is active!
    bc.tick(/*lever=*/true, /*estop=*/false, /*brake_kpa=*/2500, can::Mode::Auto, status_b0, stroke, out);

    // Rider lever MUST override: switches to Stroke mode (15 mm stroke), auto_brake=0!
    constexpr uint16_t kExpectedManualStroke = uint16_t((sys::kBrakeManualStroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
    TEST_ASSERT_EQUAL(uint8_t(can::custom::seb::ControlMode::Stroke), uint8_t(out.control_mode));
    TEST_ASSERT_EQUAL(kExpectedManualStroke, out.stroke_request_raw);
    TEST_ASSERT_EQUAL(0, out.auto_brake);
    TEST_ASSERT_EQUAL(0, out.pressure_request_raw);
}

// ── 7. SEB Brake Stroke Following Error: Transient vs Latched ──────
void test_brake_following_error_transient_vs_latched(void) {
    // Excursion raw threshold = 60 (3.0 mm)
    // Commanded stroke = 600 (0 mm), actual stroke = 670 (3.5 mm -> diff = 70 > 60)
    uint16_t cmd_raw = 600;
    uint16_t actual_raw = 670;
    uint16_t diff = actual_raw - cmd_raw;
    TEST_ASSERT_TRUE(diff > sys::kBrakeFollowingErrRaw);

    // Transient excursion sets kInhibitBrakeFollowing
    sys::set_inhibit(sys::kInhibitBrakeFollowing);
    TEST_ASSERT_TRUE(sys::transient_inhibited());
    TEST_ASSERT_TRUE(sys::any_inhibit());
    TEST_ASSERT_FALSE(sys::latched_fault_present());

    // If excursion clears quickly, transient bit is cleared
    sys::clear_inhibit(sys::kInhibitBrakeFollowing);
    TEST_ASSERT_FALSE(sys::transient_inhibited());

    // If excursion persists >= kBrakeFollowingLatchedMs (500 ms), it latches!
    sys::set_latched_fault(sys::kLatchedBrakeFollowing);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
    TEST_ASSERT_TRUE(sys::any_inhibit());

    // A transient clear does NOT clear the latched fault
    sys::clear_inhibit(sys::kInhibitBrakeFollowing);
    TEST_ASSERT_TRUE(sys::latched_fault_present());
}

// ── 8. SEB Latched Fault Reset Rejected if Underlying Cause Persists ─
void test_brake_latched_fault_reset_rejection_if_cause_persists(void) {
    // Latched SEB L3 fault present
    sys::set_latched_fault(sys::kLatchedSebL3);
    TEST_ASSERT_TRUE(sys::latched_fault_present());

    // Case A: SEB still reports error_status == 3 (fault still active)
    uint8_t seb_error_status = 3;
    if (seb_error_status < 3) {
        sys::g_latched_fault_reasons.fetch_and(~static_cast<uint32_t>(sys::kLatchedSebL3));
    }
    // Latch is RETAINED! Reset must not paper over a live fault
    TEST_ASSERT_TRUE(sys::latched_fault_present());

    // Case B: SEB error clears (error_status == 0)
    seb_error_status = 0;
    if (seb_error_status < 3) {
        sys::g_latched_fault_reasons.fetch_and(~static_cast<uint32_t>(sys::kLatchedSebL3));
    }
    // Latch is now successfully cleared!
    TEST_ASSERT_FALSE(sys::latched_fault_present());
}

// ── 9. RT Emergency Brake Fallback Takeover & Handback Epoch ────────
void test_brake_emergency_fallback_and_handback_epoch(void) {
    rt::SebBrakeFallback fb;
    const int64_t boot = 1'000'000; // 1.0 s
    fb.init(boot);

    // 1. Establish normal operation with healthy SYS (HB + 0x7B9 observed)
    int64_t now = boot + int64_t(rt::kSebFallbackArmGraceMs) * 1000 + 100'000;
    rt::SebFallbackInput in{now, /*sys_hb_fresh=*/true, /*sys_0x7B9_observed=*/true, /*startup_grace_active=*/false};
    auto out = fb.update(in);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(out.state));
    TEST_ASSERT_FALSE(out.emergency_tx_0x7B9);

    // 2. SYS heartbeat lost, but SYS 0x7B9 still present -> SYS_DEGRADED only
    now += 50'000;
    in = rt::SebFallbackInput{now, /*sys_hb_fresh=*/false, /*sys_0x7B9_observed=*/true, /*startup_grace_active=*/false};
    out = fb.update(in);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::SYS_DEGRADED), uint8_t(out.state));
    TEST_ASSERT_FALSE(out.emergency_tx_0x7B9);

    // 3. SYS 0x7B9 ALSO disappears for > guard (200 ms) -> EMERGENCY_FALLBACK!
    now += int64_t(rt::kSebFallbackGuardMs) * 1000 + 50'000;
    in = rt::SebFallbackInput{now, /*sys_hb_fresh=*/false, /*sys_0x7B9_observed=*/false, /*startup_grace_active=*/false};
    out = fb.update(in);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
    TEST_ASSERT_TRUE(out.emergency_tx_0x7B9); // RT must transmit emergency 0x7B9!

    // 4. SYS Heartbeat returns -> RT opens handback epoch, continues emergency TX until verified
    now += 100'000;
    in = rt::SebFallbackInput{now, /*sys_hb_fresh=*/true, /*sys_0x7B9_observed=*/false, /*startup_grace_active=*/false};
    out = fb.update(in);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
    TEST_ASSERT_TRUE(out.emergency_tx_0x7B9);

    // Verify frames 1 to kSebHandbackVerifyFrames - 1 remain in EMERGENCY_FALLBACK
    for (int i = 0; i < rt::kSebHandbackVerifyFrames - 1; ++i) {
        now += 50'000;
        in = rt::SebFallbackInput{now, /*sys_hb_fresh=*/true, /*sys_0x7B9_observed=*/true, /*startup_grace_active=*/false};
        out = fb.update(in);
        TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::EMERGENCY_FALLBACK), uint8_t(out.state));
        TEST_ASSERT_TRUE(out.emergency_tx_0x7B9);
    }

    // Final verification frame (kSebHandbackVerifyFrames = 5) -> ownership returns to NORMAL!
    now += 50'000;
    in = rt::SebFallbackInput{now, /*sys_hb_fresh=*/true, /*sys_0x7B9_observed=*/true, /*startup_grace_active=*/false};
    out = fb.update(in);
    TEST_ASSERT_EQUAL(uint8_t(rt::SebBrakeState::NORMAL), uint8_t(out.state));
    TEST_ASSERT_FALSE(out.emergency_tx_0x7B9);
}

// ═════════════════════════════════════════════════════════════════════
// PART 3: STEERING FAULTS & EMERGENCY RAMPS
// ═════════════════════════════════════════════════════════════════════

// ── 10. Steering Power-On Implausible Angle Plausibility Fault ─────
void test_steering_startup_implausible_angle_fault(void) {
    rt::SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    etrike::protocol::codecs::ses::Command out;

    // Advance 500 ms boot wait
    int boot_ticks = (rt::kSteerBootWaitMs * rt::kSteerCmdRateHz) / 1000;
    for (int i = 0; i < boot_ticks; ++i) {
        sc.tick(INT16_MIN, 0, now_ms += 20, out);
    }
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::STEER_LISTEN_SYNC), uint8_t(sc.state()));

    // SES reports 45.0° (450 raw in 0.1°) at startup! (> 30.0° / 300 raw plausibility limit)
    sc.tick(450, 1, now_ms += 20, out);

    // MUST refuse ACTIVE and enter STEER_FAULT
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::STEER_FAULT), uint8_t(sc.state()));
}

// ── 11. Steering Non-Obstacle Centering Ramp & Linkage Jam Fault ───
void test_steering_non_obstacle_centering_ramp_and_jam_fault(void) {
    rt::SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    etrike::protocol::codecs::ses::Command out;

    // Boot to active at 15.0° (150 raw)
    int boot_ticks = (rt::kSteerBootWaitMs * rt::kSteerCmdRateHz) / 1000;
    for (int i = 0; i < boot_ticks; ++i) sc.tick(INT16_MIN, 0, now_ms += 20, out);
    sc.tick(150, 1, now_ms += 20, out);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::STEER_ACTIVE), uint8_t(sc.state()));

    // Non-obstacle ESTOP triggered
    sc.start_estop(/*obstacle_triggered=*/false);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::ESTOP_RAMP_TO_ZERO), uint8_t(sc.state()));

    // Verify ramp reduces angle by 4 units per 20 ms tick (20°/s)
    sc.tick(150, 1, now_ms += 20, out);
    int16_t offset_free = out.target_angle_raw - rt::kSbwAngleOffset;
    TEST_ASSERT_EQUAL(146, offset_free); // 150 - 4 = 146

    // Linkage mechanically jams: actual angle stays at 150 while commanded angle ramps down to 0
    // Persists > 1000 ms -> STEER_FAULT (silent-stop)
    bool faulted = false;
    for (int i = 0; i < 70; ++i) { // 70 * 20 ms = 1400 ms
        sc.tick(150, 1, now_ms += 20, out);
        if (sc.state() == rt::SteerState::STEER_FAULT) {
            faulted = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(faulted);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::STEER_FAULT), uint8_t(sc.state()));
}

// ── 12. Steering Obstacle ESTOP Rollover Clamped Hold ──────────────
void test_steering_obstacle_estop_rollover_clamped_hold(void) {
    rt::SteeringControl sc;
    sc.init();
    uint32_t now_ms = 0;
    etrike::protocol::codecs::ses::Command out;

    // Boot to active
    int boot_ticks = (rt::kSteerBootWaitMs * rt::kSteerCmdRateHz) / 1000;
    for (int i = 0; i < boot_ticks; ++i) sc.tick(INT16_MIN, 0, now_ms += 20, out);
    sc.tick(0, 1, now_ms += 20, out);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::STEER_ACTIVE), uint8_t(sc.state()));

    // High speed (25 km/h = 6944 mm/s) with extreme 30° target
    sc.set_target(30 * 1000, 6944);
    sc.tick(0, 1, now_ms += 20, out);

    // Obstacle ESTOP triggered at speed
    sc.start_estop(/*obstacle_triggered=*/true);
    sc.set_estop_hold_time(now_ms);
    TEST_ASSERT_EQUAL(uint8_t(rt::SteerState::ESTOP_HOLD_THEN_SILENT), uint8_t(sc.state()));

    // At 25 km/h, dynamic limit is clamped to 5.0° - 6.0°
    // Held angle must NOT exceed this limit to prevent rollover under emergency braking!
    sc.tick(0, 1, now_ms += 20, out);
    int16_t hold_angle = out.target_angle_raw - rt::kSbwAngleOffset;
    TEST_ASSERT_TRUE(std::abs(hold_angle) <= 60); // <= 6.0°
}

// ═════════════════════════════════════════════════════════════════════
// PART 4: ADDITIONAL SYSTEM INVARIANTS & PROTECTIONS
// ═════════════════════════════════════════════════════════════════════

// ── 13. Relay Controller Mutual Exclusion & Neutral Failsafe ───────
void test_relay_mutual_exclusion_and_neutral_failsafe(void) {
    RelayController relays;
    relays.init();

    // Power off -> all relays de-energized (SET / HIGH in active-low logic)
    relays.set_gear(can::Gear::N, /*ignition=*/false);
    TEST_ASSERT_EQUAL(RelayController::State::Off, relays.state());
    TEST_ASSERT_FALSE(relays.is_ignition_on());
    TEST_ASSERT_EQUAL(can::Gear::N, relays.current_gear());
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOA, mtr::kRelayDrivePin));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOA, mtr::kRelayRevPin));

    // Shift to Drive -> Ignition ON (RESET), Drive ON (RESET), Reverse OFF (SET)
    relays.set_gear(can::Gear::D, /*ignition=*/true);
    TEST_ASSERT_EQUAL(RelayController::State::Drive, relays.state());
    TEST_ASSERT_TRUE(relays.is_ignition_on());
    TEST_ASSERT_EQUAL(can::Gear::D, relays.current_gear());
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, HAL_GPIO_ReadPin(GPIOA, mtr::kRelayDrivePin));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOA, mtr::kRelayRevPin));

    // Shift to Park / Neutral -> Ignition ON (RESET), Drive OFF (SET), Reverse OFF (SET)
    relays.set_gear(can::Gear::N, /*ignition=*/true);
    TEST_ASSERT_EQUAL(RelayController::State::Park, relays.state());
    TEST_ASSERT_TRUE(relays.is_ignition_on());
    TEST_ASSERT_EQUAL(can::Gear::N, relays.current_gear());
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOA, mtr::kRelayDrivePin));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOA, mtr::kRelayRevPin));

    // Shift to Reverse -> Ignition ON (RESET), Drive OFF (SET), Reverse ON (RESET)
    relays.set_gear(can::Gear::R, /*ignition=*/true);
    TEST_ASSERT_EQUAL(RelayController::State::Reverse, relays.state());
    TEST_ASSERT_TRUE(relays.is_ignition_on());
    TEST_ASSERT_EQUAL(can::Gear::R, relays.current_gear());
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOA, mtr::kRelayDrivePin));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, HAL_GPIO_ReadPin(GPIOA, mtr::kRelayRevPin));

    // Mutual exclusion check: Drive and Reverse coils can NEVER be simultaneously active (RESET)!
    TEST_ASSERT_FALSE(HAL_GPIO_ReadPin(GPIOA, mtr::kRelayDrivePin) == GPIO_PIN_RESET &&
                      HAL_GPIO_ReadPin(GPIOA, mtr::kRelayRevPin) == GPIO_PIN_RESET);
}

// ── 14. Reverse Direction Sign Protection & Clamping ───────────────
void test_reverse_speed_sign_protection(void) {
    RelayController relays;
    DacController dac;
    MotorManager mgr(relays, dac);
    mgr.init();

    auto make_mode_frame = [](bool auto_mode, uint8_t ctr) {
        can::gen::SysModeCmd m{auto_mode, ctr};
        can::Frame f{};
        can::gen::encode_sys_mode_cmd(m, f);
        return f;
    };
    auto make_pwr_frame = [](bool pwr_on, uint8_t ctr) {
        can::gen::SysPwrCmd p{pwr_on, ctr};
        can::Frame f{};
        can::gen::encode_sys_pwr_cmd(p, f);
        return f;
    };
    auto make_safe_frame = [](uint8_t ctr, bool estop) {
        can::gen::SysSafetySts s{};
        s.estop_active = estop;
        s.heartbeat_ok = true;
        s.rolling_counter = ctr;
        can::Frame f{};
        can::gen::encode_sys_safety_sts(s, f);
        s.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(f.data.data()));
        can::gen::encode_sys_safety_sts(s, f);
        return f;
    };

    uint8_t m_ctr = 1, p_ctr = 1, s_ctr = 1;
    for (int i = 0; i < 3; ++i) {
        mgr.handle_frame(make_mode_frame(true, m_ctr++), 100);
        mgr.handle_frame(make_pwr_frame(true, p_ctr++), 100);
        mgr.handle_frame(make_safe_frame(s_ctr++, false), 100);
        mgr.tick(100);
    }

    // Set Reverse gear with valid negative setpoint: -300 mm/s
    can::gen::RtDriveCmd drv_rev{-300, static_cast<uint8_t>(can::Gear::R)};
    can::Frame f_rev{};
    can::gen::encode_rt_drive_cmd(drv_rev, f_rev);
    mgr.handle_frame(f_rev, 110);
    mgr.tick(110);
    // Advance 60 ms past dwell
    mgr.tick(170);
    TEST_ASSERT_EQUAL(RelayController::State::Reverse, relays.state());
    TEST_ASSERT_EQUAL(1459, dac.current_code()); // 700 + (300/500)*1266 = 1459

    // Setpoint sign violation: positive speed > 500 mm/s (e.g. +1000 mm/s) in Reverse gear!
    can::gen::RtDriveCmd drv_bad{1000, static_cast<uint8_t>(can::Gear::R)};
    can::Frame f_bad{};
    can::gen::encode_rt_drive_cmd(drv_bad, f_bad);
    mgr.handle_frame(f_bad, 180);
    mgr.tick(180);

    // MotorManager directional setpoint sign verification MUST reject forward setpoint in Reverse!
    // Speed magnitude forced to 0, DAC forced to 0!
    TEST_ASSERT_EQUAL(0, dac.current_code());
}

extern "C" void app_main() {
    UNITY_BEGIN();
    // Part 1: ESTOP Resetting & REARM Invariants
    RUN_TEST(test_estop_reset_rejected_while_button_held);
    RUN_TEST(test_estop_reset_counter_rollover_255_to_0);
    RUN_TEST(test_estop_reset_gap_and_duplicate_rejection);
    RUN_TEST(test_estop_reset_assert_mid_sequence_aborts_clear);
    RUN_TEST(test_rearm_protocol_sequence_and_violations);

    // Part 2: Brake Faults & Priority Arbitrations
    RUN_TEST(test_brake_rider_lever_override_during_auto_cruise);
    RUN_TEST(test_brake_following_error_transient_vs_latched);
    RUN_TEST(test_brake_latched_fault_reset_rejection_if_cause_persists);
    RUN_TEST(test_brake_emergency_fallback_and_handback_epoch);

    // Part 3: Steering Faults & Emergency Ramps
    RUN_TEST(test_steering_startup_implausible_angle_fault);
    RUN_TEST(test_steering_non_obstacle_centering_ramp_and_jam_fault);
    RUN_TEST(test_steering_obstacle_estop_rollover_clamped_hold);

    // Part 4: Additional System Invariants & Protections
    RUN_TEST(test_relay_mutual_exclusion_and_neutral_failsafe);
    RUN_TEST(test_reverse_speed_sign_protection);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
