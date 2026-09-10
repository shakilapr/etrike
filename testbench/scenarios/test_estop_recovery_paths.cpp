// ESTOP reset/recovery scenarios across the operator paths. Probes for issues:
//   1. Host ESTOP -> START reset -> re-command AUTO: does motion recover without
//      an unnecessary full power cycle?
//   2. ESTOP reset must be REFUSED while the originating fault is still active.
//   3. rm BARE actuator bench: ESTOP (0x001) then automatic clear/rearm.
//   4. rm SYS path: ESTOP -> operator reset -> recovery.
#include <iostream>
#include <cmath>
#include "test_bench.hpp"
#include "can_bus.hpp"
#include "virtual_can_bus.hpp"
#include "nodes/mtr_node.hpp"
#include "nodes/sys_node.hpp"
#include "nodes/rm_operator_model.hpp"

namespace testbench {

namespace {
void bring_up_auto(TestBench& tb) {
    tb.reset();
    tb.host().request_mode(can::Mode::Auto);
    tb.host().request_power(true);
    tb.run_for_ms(200, 10);
    tb.press_start_button();
    tb.run_for_ms(300, 10);
}
}  // namespace

// 1. Host ESTOP -> reset -> resume by re-commanding (no power cycle).
bool test_estop_reset_recovery_host() {
    std::cout << "TEST: ESTOP reset/recovery HOST (START then re-command AUTO)\n";
    TestBench tb;
    bring_up_auto(tb);
    tb.host().set_gear(can::Gear::D);
    tb.host().send_drive_cmd(1500);
    tb.run_for_ms(800, 10);
    bool driving = tb.mtr().is_traction_enabled();

    tb.press_estop_button();
    tb.run_for_ms(400, 10);
    bool stopped = !tb.mtr().is_traction_enabled() && tb.sys().is_estop_latched();

    // Operator reset: release the button, press START.
    tb.release_estop_button();
    tb.press_start_button();
    tb.run_for_ms(400, 10);
    bool cleared = !tb.sys().is_estop_latched();
    bool mode_after = (tb.sys().mode() == can::Mode::Auto);

    // Re-command AUTO + drive WITHOUT a power cycle.
    tb.host().request_mode(can::Mode::Auto);
    tb.host().send_drive_cmd(1500);
    tb.run_for_ms(1000, 10);
    bool resumed = tb.mtr().is_traction_enabled();

    std::cout << "  driving=" << driving << " stopped=" << stopped
              << " cleared=" << cleared << " mode_after_start="
              << (mode_after ? "AUTO" : "MANUAL")
              << " resumed_without_powercycle=" << resumed << "\n";
    if (!driving || !stopped || !cleared) { std::cerr << "  FAIL: basic ESTOP/reset broken\n"; return false; }
    if (!resumed) std::cerr << "  ISSUE: motion did NOT resume after reset+re-command (power cycle required?)\n";
    return driving && stopped && cleared;  // ISSUE reported but not fatal here
}

// 2. Reset must be refused while the originating fault is still asserted.
bool test_estop_reset_blocked_by_fault() {
    std::cout << "TEST: ESTOP reset refused while SEB L3 fault still active\n";
    TestBench tb;
    bring_up_auto(tb);
    tb.host().send_drive_cmd(1500);
    tb.seb().inject_l3_fault();
    tb.run_for_ms(300, 10);
    bool latched = tb.sys().is_estop_latched();

    // Attempt reset with the fault STILL present.
    tb.press_start_button();
    tb.run_for_ms(300, 10);
    bool refused = tb.sys().is_estop_latched();

    // Clear the fault and let the cleared SEB status propagate, THEN reset.
    tb.seb().clear_fault();
    tb.run_for_ms(300, 10);
    tb.press_start_button();
    tb.run_for_ms(300, 10);
    bool cleared = !tb.sys().is_estop_latched();

    std::cout << "  latched=" << latched << " refused_while_faulted=" << refused
              << " cleared_after_fault_gone=" << cleared << "\n";
    if (!latched)  { std::cerr << "  FAIL: SEB L3 did not latch ESTOP\n"; return false; }
    if (!refused)  { std::cerr << "  ISSUE: ESTOP reset succeeded while fault active\n"; return false; }
    if (!cleared)  { std::cerr << "  ISSUE: ESTOP did not clear after fault removed\n"; return false; }
    return true;
}

// 3. rm BARE bench: MTR ESTOP via 0x001, then does rm's continuous 0x011 clear it?
bool test_estop_recovery_bare() {
    std::cout << "TEST: rm BARE ESTOP (0x001) -> automatic clear/rearm\n";
    VirtualCanBus low("LOW_CAN");
    MtrNode mtr(low);
    RmOperatorModel rm(low);
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm.receive_can("LOW_CAN", f); });
    mtr.init(); rm.init();
    rm.set_op_mode(rm::OperatingMode::Bare);
    rm.drive(1500);

    for (uint32_t t = 100; t <= 1200; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool ignites = mtr.is_traction_enabled();

    // Inject ESTOP 0x001 onto the low bus.
    low.send(NodeId::TEST_HARNESS, can::Frame::standard(can::kIdSafetyEstop, 0));
    for (uint32_t t = 1210; t <= 1600; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool stopped = !mtr.is_traction_enabled() || mtr.is_estop_latched();

    // Let rm keep running (it emits 0x011 estop_active=0 continuously).
    for (uint32_t t = 1610; t <= 3000; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool recovered = mtr.is_traction_enabled();
    bool still_latched = mtr.is_estop_latched();

    std::cout << "  ignites=" << ignites << " stopped=" << stopped
              << " recovered=" << recovered << " estop_latched_after=" << still_latched << "\n";
    if (!ignites) { std::cerr << "  FAIL: BARE never drove initially\n"; return false; }
    if (!stopped) { std::cerr << "  FAIL: 0x001 did not stop MTR\n"; return false; }
    if (!recovered) std::cerr << "  ISSUE: MTR did not recover in BARE after 0x001 ESTOP\n";
    return ignites && stopped;
}

// 4. rm SYS path: ESTOP at SYS, operator reset, recovery.
bool test_estop_recovery_rm_sys() {
    std::cout << "TEST: ESTOP reset/recovery via rm SYS path\n";
    VirtualCanBus low("LOW_CAN");
    RmOperatorModel rm(low);
    SysNode sys(low);
    MtrNode mtr(low);
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SYS, [&](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    rm.init(); sys.init(); mtr.init();
    rm.set_op_mode(rm::OperatingMode::Sys);
    rm.drive(1500);

    for (uint32_t t = 100; t <= 1500; t += 10) {
        if (t == 700) sys.press_start_button();
        sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10);
    }
    bool driving = mtr.is_traction_enabled();

    sys.press_estop_button();
    for (uint32_t t = 1510; t <= 1900; t += 10) { sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool stopped = sys.is_estop_latched() && !mtr.is_traction_enabled();

    sys.release_estop_button();
    for (uint32_t t = 1910; t <= 2300; t += 10) { sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    sys.press_start_button();
    // Re-command AUTO via rm HMI (rm keeps emitting 0x111 AUTO).
    for (uint32_t t = 2310; t <= 3200; t += 10) { sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }

    bool cleared = !sys.is_estop_latched();
    bool recovered = mtr.is_traction_enabled();
    std::cout << "  driving=" << driving << " stopped=" << stopped
              << " cleared=" << cleared << " recovered=" << recovered << "\n";
    if (!driving || !stopped) { std::cerr << "  FAIL: SYS ESTOP path broken\n"; return false; }
    if (!cleared)    std::cerr << "  ISSUE: SYS ESTOP not cleared by START\n";
    if (!recovered)  std::cerr << "  ISSUE: MTR did not recover via rm SYS path\n";
    return driving && stopped;
}

// 5. rm RT path: software ESTOP originated at RT, cleared, recovery.
bool test_estop_recovery_rm_rt() {
    std::cout << "TEST: ESTOP reset/recovery via rm RT path\n";
    VirtualCanBus high("HIGH_CAN");
    VirtualCanBus low("LOW_CAN");
    RmOperatorModel rm(high);
    RtNode  rt(high, low);
    SysNode sys(low);
    MtrNode mtr(low);
    high.register_node(NodeId::RM, [&](const etrike::protocol::Frame& f) { rm.receive_can("HIGH_CAN", f); });
    high.register_node(NodeId::RT, [&](const etrike::protocol::Frame& f) { rt.receive_can("HIGH_CAN", f); });
    low.register_node(NodeId::RT,  [&](const etrike::protocol::Frame& f) { rt.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SYS, [&](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    rm.init(); rt.init(); sys.init(); mtr.init();
    rm.set_op_mode(rm::OperatingMode::Rt);
    rm.drive(1500);

    auto run = [&](uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += 10) {
            rt.step(t, 10); sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10);
            high.tick(t, 10); low.tick(t, 10);
        }
    };
    sys.press_start_button();
    run(100, 1500);
    bool driving = mtr.is_traction_enabled();

    rt.trigger_software_estop();  // 0x001 on both buses
    run(1510, 1900);
    bool stopped = rt.is_estop_latched() && sys.is_estop_latched() && !mtr.is_traction_enabled();

    // Recovery: clear the software latch, release hw, reset SYS, then rt needs
    // two fresh 0x011 clear frames from SYS.
    rt.clear_software_estop();
    sys.release_estop_button();
    run(1910, 2300);
    sys.press_start_button();
    run(2310, 3400);

    bool sys_cleared = !sys.is_estop_latched();
    bool rt_cleared  = !rt.is_estop_latched();
    bool recovered   = mtr.is_traction_enabled();
    std::cout << "  driving=" << driving << " stopped=" << stopped
              << " sys_cleared=" << sys_cleared << " rt_cleared=" << rt_cleared
              << " recovered=" << recovered << "\n";
    if (!driving || !stopped) { std::cerr << "  FAIL: RT ESTOP path broken\n"; return false; }
    if (!rt_cleared) std::cerr << "  ISSUE: rt-esp32 did not clear ESTOP after SYS reset (2-frame 0x011)\n";
    if (!recovered)  std::cerr << "  ISSUE: MTR did not recover via rm RT path\n";
    return driving && stopped;
}

}  // namespace testbench
