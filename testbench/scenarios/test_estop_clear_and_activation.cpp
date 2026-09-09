#include <iostream>
#include <cassert>
#include "test_bench.hpp"

namespace testbench {

// ── Test 12: In-flight / Transient ESTOP Publishing During Reset ───────────
// When all ECUs are connected and an ESTOP clear is sent, in-flight 0x001 frames
// may still arrive on the bus.
// Verify: The 500 ms reset grace window absorbs transient frames, allows RT/MTR
// to process the two consecutive 0x011=0 clear frames, and clears ESTOP properly.
bool test_estop_clear_with_transient_inflight_publishing() {
    std::cout << "[TEST 12] ESTOP Clear with In-Flight / Transient 0x001 Publishing...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Initial ESTOP trigger
    bench.press_estop_button();
    bench.run_for_ms(50);
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Remove physical cause
    bench.release_estop_button();
    bench.run_for_ms(50);

    // 3. Operator initiates reset
    bench.press_start_button();

    // Simulate in-flight / trailing 0x001 frame arriving at +20 ms (e.g. delayed network transit)
    can::Frame fr = can::Frame::standard(can::kIdSafetyEstop, 0);
    bench.low_can().send(NodeId::TEST_HARNESS, fr);
    bench.run_for_ms(30);

    // Another trailing 0x001 arriving at +50 ms
    bench.low_can().send(NodeId::TEST_HARNESS, fr);
    bench.run_for_ms(200);

    // Verify: SYS ignored trailing in-flight frames within the grace window,
    // transmitted 0x011 with estop_active=0, RT and MTR received 2 clear frames
    assert(!bench.sys().is_estop_latched());
    assert(bench.sys().mode() == can::Mode::Manual);
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    // Verify no delayed re-latch occurs after grace window expires
    bench.run_for_ms(500);
    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: In-flight publishing absorbed by reset grace; unlatched cleanly.\n";
    return true;
}

// ── Test 13: Continuous ESTOP Publishing During Reset (Stuck / Spammed) ────
// When an ECU or sensor continuously keeps publishing ESTOP frames:
// Verify: Reset cannot clear the vehicle; system re-latches into ESTOP once
// the grace window passes, and propulsion authority is never granted.
bool test_estop_clear_refused_when_continuously_publishing() {
    std::cout << "[TEST 13] ESTOP Clear Under Continuous / Stuck ESTOP Publishing...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Initial ESTOP latch
    can::Frame fr = can::Frame::standard(can::kIdSafetyEstop, 0);
    bench.low_can().send(NodeId::TEST_HARNESS, fr);
    bench.run_for_ms(50);
    assert(bench.sys().is_estop_latched());

    // 2. Continuous publisher keeps firing 0x001 every 20 ms
    // Operator attempts reset while frames are actively and continuously publishing
    bench.press_start_button();

    // Stream continuous frames for 800 ms (past 500 ms grace window)
    for (int t = 0; t < 40; ++t) {
        bench.low_can().send(NodeId::TEST_HARNESS, fr);
        bench.run_for_ms(20);

        // Invariant: throughout this entire duration, MTR must NEVER produce propulsion!
        assert(bench.mtr().dac_output() == 0);
        assert(bench.mtr().is_propulsion_inhibited());
    }

    // After 800 ms (> 500 ms grace), the continuous frames have re-latched SYS
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());
    assert(bench.mtr().dac_output() == 0);

    std::cout << "  -> PASS: Continuous publishing prevented vehicle unlatching; safety preserved.\n";
    return true;
}

// ── Test 14: ESTOP Signal Clearing != Controllers in Active State ─────────
// Demonstrates that when 0x011.estop_active drops to 0, controllers are NOT
// yet in an active driving state. Tests telemetry verification of active state.
bool test_estop_clear_does_not_mean_controllers_active() {
    std::cout << "[TEST 14] ESTOP Signal Clearing Does NOT Mean Controllers Are in Active State...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Put vehicle in AUTO driving mode before ESTOP
    bench.host().request_mode(can::Mode::Auto);
    bench.host().send_drive_cmd(2000);
    bench.run_for_ms(500);
    assert(bench.sys().mode() == can::Mode::Auto);
    assert(bench.mtr().is_traction_enabled());
    assert(bench.mtr().dac_output() > 0);

    // 2. Trigger ESTOP via SEB L3 fault
    bench.seb().inject_l3_fault();
    bench.run_for_ms(250);

    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());
    assert(bench.mtr().dac_output() == 0);
    assert(bench.seb().actual_stroke_mm() > 20.0f); // Emergency brake clamped

    // 3. Clear the SEB L3 fault cause
    bench.seb().clear_fault();
    bench.run_for_ms(50);

    // 4. Operator presses START button -> ESTOP signal clears
    bench.press_start_button();
    bench.run_for_ms(50); // Just enough for 2 frames of 0x011=0 to arrive

    // ── INSPECT CONTROLLER STATES IMMEDIATELY AFTER ESTOP CLEAR ───────
    // At this instant: 0x011.estop_active == 0 (ESTOP signal is CLEARED)
    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  [CHECK AT ESTOP SIGNAL CLEAR]\n";

    // CONTROLLER 1: SYS State
    std::cout << "    1. SYS Mode: " << can::mode_name(bench.sys().mode()) << " (EXPECTED: MANUAL)\n";
    assert(bench.sys().mode() == can::Mode::Manual); // NOT AUTO!
    assert(bench.sys().mode() != can::Mode::Auto);

    // CONTROLLER 2: MTR State
    can::gen::MtrNodeStatus mtr_ns = bench.mtr().node_status();
    std::cout << "    2. MTR Status:\n"
              << "       - Node State: " << static_cast<int>(mtr_ns.node_state)
              << " (0=Standby, 1=Estop, 2=Recover, 3=Active, 4=Inhibited)\n"
              << "       - Recovery Pending: " << (mtr_ns.recovery_pending ? "YES" : "NO") << "\n"
              << "       - Ready: " << (mtr_ns.ready ? "YES" : "NO") << "\n"
              << "       - Propulsion Inhibited: " << (bench.mtr().is_propulsion_inhibited() ? "YES" : "NO") << "\n"
              << "       - DAC Output: " << bench.mtr().dac_output() << "\n";

    // Invariants: MTR is NOT active! It is in RECOVER / INHIBITED mode!
    assert(mtr_ns.node_state != can::gen::MtrNodeStatus::kNodeStateActive);
    assert(mtr_ns.recovery_pending == true); // REARM is required!
    assert(mtr_ns.ready == false);
    assert(bench.mtr().is_propulsion_inhibited() == true);
    assert(bench.mtr().dac_output() == 0);
    assert(bench.mtr().current_gear() == can::Gear::N);

    // CONTROLLER 3: RT State
    std::cout << "    3. RT Commanded Speed: " << bench.rt().commanded_speed_mmps() << " mm/s (EXPECTED: 0)\n";
    assert(bench.rt().active_mode() == can::Mode::Manual);
    assert(bench.rt().commanded_speed_mmps() == 0); // No autonomous speed command

    // CONTROLLER 4: SEB Brake State
    std::cout << "    4. SEB Actual Stroke: " << bench.seb().actual_stroke_mm()
              << " mm (Cylinder still retracting; brakes mechanically dragged)\n";
    assert(bench.seb().actual_stroke_mm() > 1.0f); // Not fully retracted yet!

    // ── HOW DO WE KNOW WHEN ALL CONTROLLERS ARE IN ACTIVE STATE? ──────
    std::cout << "  [EXECUTING MULTI-CONTROLLER ACTIVE TRANSITION SEQUENCE]\n";

    // Step A: Allow SEB hydraulic brake cylinder to physically retract
    bench.run_for_ms(300);
    std::cout << "    Step A: SEB stroke retracted to " << bench.seb().actual_stroke_mm() << " mm (< 1.0 mm)\n";
    assert(bench.seb().actual_stroke_mm() < 1.0f);

    // Step B: Re-arm MTR via power cycle sequence (0x112 / 0x113 OFF -> ON)
    bench.power_off();
    bench.run_for_ms(200);
    bench.power_on();
    bench.run_for_ms(200);

    // Step C: Operator / HMI requests AUTO mode
    bench.host().request_mode(can::Mode::Auto);
    bench.host().send_drive_cmd(2000);
    bench.run_for_ms(300);

    // Verify all controllers are now verified to be in ACTIVE state
    can::gen::MtrNodeStatus mtr_active_ns = bench.mtr().node_status();
    std::cout << "  [CHECK POST-REARM ACTIVE STATE]\n"
              << "    - SYS Mode: " << can::mode_name(bench.sys().mode()) << " (ACTIVE AUTO)\n"
              << "    - MTR Node State: " << static_cast<int>(mtr_active_ns.node_state) << " (ACTIVE)\n"
              << "    - MTR Ready: " << (mtr_active_ns.ready ? "YES" : "NO") << "\n"
              << "    - MTR Recovery Pending: " << (mtr_active_ns.recovery_pending ? "YES" : "NO") << "\n"
              << "    - MTR Gear: " << can::gear_name(bench.mtr().current_gear()) << "\n"
              << "    - RT Commanded Speed: " << bench.rt().commanded_speed_mmps() << " mm/s\n"
              << "    - MTR DAC Output: " << bench.mtr().dac_output() << "\n";

    assert(bench.sys().mode() == can::Mode::Auto);
    assert(mtr_active_ns.node_state == can::gen::MtrNodeStatus::kNodeStateActive);
    assert(mtr_active_ns.ready == true);
    assert(mtr_active_ns.recovery_pending == false);
    assert(bench.mtr().is_traction_enabled());
    assert(bench.mtr().dac_output() > 0);
    assert(bench.rt().commanded_speed_mmps() == 2000);

    std::cout << "  -> PASS: Confirmed ESTOP clear is only a prerequisite; active state requires multi-node rearm.\n";
    return true;
}

} // namespace testbench
