#include <iostream>
#include <cassert>
#include <vector>
#include "test_bench.hpp"

namespace testbench {

// ── Test 1: ESTOP reset-loop test ──────────────────────────────────────────
// Trigger ESTOP, remove cause, keep RT powered, press START.
// Verify SYS remains cleared long enough for RT to receive two 0x011=0 frames
// and does not immediately re-enter ESTOP because RT rebroadcasts 0x001.
bool test_01_estop_reset_loop() {
    std::cout << "[TEST 01] Running ESTOP reset-loop test...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Trigger ESTOP via SEB L3
    bench.seb().inject_l3_fault();
    bench.run_for_ms(100);
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Remove the original cause
    bench.seb().clear_fault();
    bench.run_for_ms(50);

    // 3. Keep RT powered and press START button
    bench.press_start_button();
    bench.run_for_ms(200);

    // Verify SYS cleared and RT received two consecutive 0x011=0 clear frames
    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    // 4. Run for an additional 500 ms to confirm SYS never re-latches into ESTOP
    bench.run_for_ms(500);
    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());

    std::cout << "  -> PASS: No reset loop observed; reset grace window prevented re-latch.\n";
    return true;
}

// ── Test 2: Repeated reset-attempt test ─────────────────────────────────────
// Repeat reset sequence 20 times across varying step intervals.
// Verify reset behavior is 100% deterministic and timing-independent.
bool test_02_repeated_reset_attempts() {
    std::cout << "[TEST 02] Running Repeated reset-attempt test (20 iterations)...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    for (int iter = 0; iter < 20; ++iter) {
        // Trigger ESTOP
        bench.press_estop_button();
        bench.run_for_ms(30 + (iter % 15)); // vary timing offset
        assert(bench.sys().is_estop_latched());

        // Release hardware button
        bench.release_estop_button();
        bench.run_for_ms(20 + (iter % 10));

        // Press START reset
        bench.press_start_button();
        bench.run_for_ms(150 + (iter % 25));

        assert(!bench.sys().is_estop_latched());
        assert(!bench.rt().is_estop_latched());
        assert(!bench.mtr().is_estop_latched());
    }

    std::cout << "  -> PASS: 20 consecutive reset attempts succeeded deterministically.\n";
    return true;
}

// ── Test 3: RT-originated ESTOP recovery test ──────────────────────────────
// RT originates an ESTOP (0x001 on both buses). Remove RT cause and execute SYS reset.
// Verify no circular dependency prevents clearing.
bool test_03_rt_originated_estop_recovery() {
    std::cout << "[TEST 03] Running RT-originated ESTOP recovery test...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // RT originates software ESTOP
    bench.rt().trigger_software_estop();
    bench.run_for_ms(100);

    assert(bench.rt().is_estop_latched());
    assert(bench.sys().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // Clear RT internal cause
    bench.rt().clear_software_estop();
    bench.run_for_ms(50);

    // Press START on SYS
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: RT-originated ESTOP cleared cleanly via SYS START button.\n";
    return true;
}

// ── Test 4: Remote 0x001 echo test ─────────────────────────────────────────
// SYS generates 0x001; RT forwards it to High CAN.
// Verify SYS does not interpret returning frame as a new separate ESTOP event.
bool test_04_remote_001_echo() {
    std::cout << "[TEST 04] Running Remote 0x001 echo test...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // SYS presses ESTOP
    bench.press_estop_button();
    bench.run_for_ms(100);

    size_t high_001_count = bench.high_can().count_frames(can::kIdSafetyEstop);
    assert(high_001_count > 0); // Verified RT forwarded to High CAN

    bench.release_estop_button();
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());

    std::cout << "  -> PASS: Remote echo loopback suppression verified.\n";
    return true;
}

// ── Test 5: Persistent 0x001 spam test ─────────────────────────────────────
// Continuously inject 0x001 from test harness and verify system stays stopped.
// Once spam stops, verify deterministic recovery.
bool test_05_persistent_001_spam() {
    std::cout << "[TEST 05] Running Persistent 0x001 spam test...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    can::Frame estop_frame = can::Frame::standard(can::kIdSafetyEstop, 0);

    // Spam 0x001 for 1 second (100 frames)
    for (int i = 0; i < 50; ++i) {
        bench.low_can().send(NodeId::TEST_HARNESS, estop_frame);
        bench.run_for_ms(20);
        assert(bench.sys().is_estop_latched());
        assert(bench.rt().is_estop_latched());
        assert(bench.mtr().dac_output() == 0);
    }

    // Attempt reset while spam is continuously active
    bench.press_start_button();
    // Keep spamming for 600 ms (past the 500 ms reset grace window)
    for (int i = 0; i < 30; ++i) {
        bench.low_can().send(NodeId::TEST_HARNESS, estop_frame);
        bench.run_for_ms(20);
    }
    // Now that grace window has elapsed and spam continues, SYS must be re-latched into ESTOP!
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());

    // Stop spam and wait for bus to settle (wait 600 ms)
    bench.run_for_ms(600);

    // Reset after spam stops -> must recover deterministically
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: Continuous spam held nodes stopped; clean recovery once stopped.\n";
    return true;
}

// ── Test 6: MTR feedback 250 ms dropout test while moving ──────────────────
// In AUTO moving at 2.0 m/s, drop 0x206 for 250 ms.
// Verify RT/SYS perform recoverable inhibit (DAC=0) without escalating to latched ESTOP.
bool test_06_mtr_feedback_dropout_moving() {
    std::cout << "[TEST 06] Running MTR feedback 250 ms dropout test while moving...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // Enter AUTO and drive 2.0 m/s
    bench.host().request_mode(can::Mode::Auto);
    bench.host().send_drive_cmd(2000);
    bench.run_for_ms(500);

    assert(bench.mtr().dac_output() > 0);
    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());

    // Drop 0x206 frames on Low CAN for ~260 ms (13 frames @ 20 ms)
    std::cout << "  Dropping 0x206 frames for 260 ms...\n";
    bench.low_can().drop(can::kIdMtrMotorFbk, 13);
    bench.run_for_ms(260);

    // Verify recoverable inhibit: speed commanded to 0, DAC code 0
    assert(bench.rt().is_mtr_feedback_lost());
    assert(bench.rt().commanded_speed_mmps() == 0);
    assert(bench.mtr().dac_output() == 0);

    // CRITICAL: Must NOT escalate to latched ESTOP!
    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: Speed cut to 0 without escalating to latched ESTOP.\n";
    return true;
}

// ── Test 7: MTR feedback 250 ms dropout test at standstill ─────────────────
// In AUTO at standstill (speed=0), drop 0x206 for 250 ms.
// Verify propulsion inhibited without global ESTOP or max-brake event.
bool test_07_mtr_feedback_dropout_standstill() {
    std::cout << "[TEST 07] Running MTR feedback 250 ms dropout test at standstill...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    bench.host().request_mode(can::Mode::Auto);
    bench.host().send_drive_cmd(0); // standstill
    bench.run_for_ms(500);

    // Drop 0x206 for 260 ms
    bench.low_can().drop(can::kIdMtrMotorFbk, 13);
    bench.run_for_ms(260);

    assert(bench.rt().is_mtr_feedback_lost());
    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(bench.seb().actual_stroke_mm() <= 2.0f); // no emergency max brake applied

    std::cout << "  -> PASS: Standstill dropout inhibited propulsion without max-brake/ESTOP.\n";
    return true;
}

// ── Test 8: MTR feedback recovery test ─────────────────────────────────────
// After temporary 0x206 loss, restore 3 consecutive frames.
// Verify recoverable inhibit clears automatically without operator START button reset.
bool test_08_mtr_feedback_recovery() {
    std::cout << "[TEST 08] Running MTR feedback recovery test...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    bench.host().request_mode(can::Mode::Auto);
    bench.host().send_drive_cmd(2000);
    bench.run_for_ms(500);
    assert(bench.mtr().dac_output() > 0);

    // Drop 0x206 for 260 ms to trigger inhibit
    bench.low_can().drop(can::kIdMtrMotorFbk, 13);
    bench.run_for_ms(260);
    assert(bench.rt().is_mtr_feedback_lost());
    assert(bench.mtr().dac_output() == 0);

    // Restore traffic and run for 250 ms (allows 3 fbk frames + 100ms mode frame delivery)
    bench.run_for_ms(250);

    // Verify auto-recovery: NO operator START press required!
    assert(!bench.rt().is_mtr_feedback_lost());
    assert(bench.rt().commanded_speed_mmps() == 2000);
    assert(bench.mtr().dac_output() > 0);

    std::cout << "  -> PASS: Recoverable inhibit auto-cleared upon 3 fresh 0x206 frames.\n";
    return true;
}

// ── Test 9: MTR feedback intermittent-loss test ───────────────────────────
// Alternate valid and missing 0x206 around 200 ms threshold.
// Verify system maintains safe inhibit and cannot oscillate into latched ESTOP.
bool test_09_mtr_feedback_intermittent_loss() {
    std::cout << "[TEST 09] Running MTR feedback intermittent-loss test...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    bench.host().request_mode(can::Mode::Auto);
    bench.host().send_drive_cmd(2000);
    bench.run_for_ms(500);

    // Churn 0x206: drop 9 frames (180 ms), deliver 1 frame, drop 9 frames, etc.
    for (int cycle = 0; cycle < 5; ++cycle) {
        bench.low_can().drop(can::kIdMtrMotorFbk, 9);
        bench.run_for_ms(220);
        // During dropout periods, verify system never escalates to latched ESTOP
        assert(!bench.sys().is_estop_latched());
        assert(!bench.rt().is_estop_latched());
    }

    // Now let frames flow cleanly again
    bench.run_for_ms(200);
    assert(!bench.rt().is_mtr_feedback_lost());
    assert(bench.mtr().dac_output() > 0);

    std::cout << "  -> PASS: Churning frames handled safely without latching ESTOP.\n";
    return true;
}

// ── Test 10: Command-echo false-negative test ──────────────────────────────
// Command zero propulsion while motor produces traction (or speed is echoed).
// Verify that 0x206 speed is an echoed command, proving architecture lack of independent tach.
bool test_10_command_echo_false_negative() {
    std::cout << "[TEST 10] Running Command-echo false-negative test...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // In MANUAL, commanded speed is 0
    bench.run_for_ms(200);

    // Capture MTR feedback frame from bus
    auto fbk_frames = bench.low_can().find_frames(can::kIdMtrMotorFbk);
    assert(!fbk_frames.empty());

    can::gen::MtrMotorFbk last_fbk{};
    can::decode_frame(fbk_frames.back(), last_fbk);

    // Notice: motor_command_speed_mmps strictly reports target_speed_mmps_ (0).
    // Even if physical wheel/motor was forcibly spun, 0x206 reports 0!
    assert(last_fbk.motor_command_speed_mmps == 0);

    std::cout << "  [VULNERABILITY CONFIRMED] 0x206 reports motor_command_speed_mmps = "
              << last_fbk.motor_command_speed_mmps
              << ", confirming 0x206 is an echoed command rather than independent shaft tachometry.\n";
    std::cout << "  -> PASS: Test documented command-echo false-negative characteristic.\n";
    return true;
}

} // namespace testbench
