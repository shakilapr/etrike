#include <iostream>
#include <cassert>
#include "test_bench.hpp"

namespace testbench {

bool test_estop_seb_l3_and_rearm_recovery() {
    std::cout << "[SCENARIO] Starting ESTOP SEB L3 & Rearm Recovery Scenario...\n";
    TestBench bench;

    // ── Phase 1: Boot vehicle & verify Standby/Manual readiness ──────
    bench.boot();
    bench.run_for_ms(2000); // 2 seconds simulated in milliseconds

    std::cout << "  [P1] Boot complete. Mode: " << can::mode_name(bench.sys().mode()) << "\n";
    assert(bench.sys().mode() == can::Mode::Manual);
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    // ── Phase 2: Host requests AUTO and drives 2.0 m/s ──────────────
    bench.host().request_mode(can::Mode::Auto);
    bench.host().send_drive_cmd(2000); // 2000 mm/s (2 m/s)
    bench.run_for_ms(500);

    std::cout << "  [P2] Auto Mode Active. RT cmd: " << bench.rt().commanded_speed_mmps()
              << " mm/s, MTR DAC: " << bench.mtr().dac_output() << "\n";
    assert(bench.sys().mode() == can::Mode::Auto);
    assert(bench.rt().commanded_speed_mmps() == 2000);
    assert(bench.mtr().is_traction_enabled());
    assert(bench.mtr().dac_output() > 0);

    // ── Phase 3: Network boundary fault: SEB reports L3 error ────────
    std::cout << "  [P3] Injecting SEB L3 fault on Low CAN...\n";
    bench.seb().inject_l3_fault();
    bench.run_for_ms(100);

    // Verify distributed latching across every node
    std::cout << "  [P3] Checking distributed ESTOP latch across all ECUs:\n";
    std::cout << "       SYS ESTOP: " << (bench.sys().is_estop_latched() ? "LATCHED" : "CLEAR") << "\n";
    std::cout << "       RT  ESTOP: " << (bench.rt().is_estop_latched() ? "LATCHED" : "CLEAR") << "\n";
    std::cout << "       MTR ESTOP: " << (bench.mtr().is_estop_latched() ? "LATCHED" : "CLEAR") << "\n";
    std::cout << "       MTR DAC:   " << bench.mtr().dac_output() << "\n";

    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());
    assert(bench.mtr().dac_output() == 0); // Motor killed immediately

    // ── Phase 4: Clear physical fault, attempt illegal drive commands ─
    std::cout << "  [P4] Clearing SEB fault, attempting drive while ESTOP is latched...\n";
    bench.seb().clear_fault();
    bench.host().send_drive_cmd(2000);
    bench.run_for_ms(1000);

    // Assert motion NEVER returns
    assert(bench.mtr().dac_output() == 0);
    assert(bench.sys().is_estop_latched());
    std::cout << "  [P4] Motion remained suppressed (DAC == 0) as required.\n";

    // ── Phase 5: Operator presses START button ───────────────────────
    std::cout << "  [P5] Operator presses START button to clear SYS/RT/MTR latches...\n";
    bench.press_start_button();
    bench.run_for_ms(200);

    std::cout << "       SYS ESTOP: " << (bench.sys().is_estop_latched() ? "LATCHED" : "CLEAR") << "\n";
    std::cout << "       RT  ESTOP: " << (bench.rt().is_estop_latched() ? "LATCHED" : "CLEAR") << "\n";
    std::cout << "       MTR ESTOP: " << (bench.mtr().is_estop_latched() ? "LATCHED" : "CLEAR") << "\n";

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    // CRITICAL: Even though ESTOP cleared, MTR requires explicit 0x113 REARM edge
    std::cout << "       Checking MTR REARM status (DAC must remain 0)...\n";
    assert(bench.mtr().dac_output() == 0);
    std::cout << "  [P5] Verified: MTR remained unrearmed (DAC == 0) despite ESTOP clear.\n";

    // ── Phase 6: Full legitimate recovery sequence (power cycle) ─────
    std::cout << "  [P6] Executing power cycle OFF -> ON -> AUTO -> drive...\n";
    bench.power_off();
    bench.run_for_ms(200);
    bench.power_on();
    bench.run_for_ms(200);

    bench.host().request_mode(can::Mode::Auto);
    bench.host().send_drive_cmd(2000);
    bench.run_for_ms(500);

    std::cout << "  [P6] Post-rearm MTR DAC: " << bench.mtr().dac_output() << "\n";
    assert(bench.mtr().dac_output() > 0);
    assert(bench.mtr().is_traction_enabled());

    std::cout << "[SCENARIO PASS] ESTOP SEB L3 & Rearm Recovery Scenario completed successfully!\n\n";
    return true;
}

} // namespace testbench
