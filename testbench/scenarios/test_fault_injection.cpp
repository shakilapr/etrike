// Section 8: safety, fault-injection and degraded-communication cases.
//   - RC/SBUS link loss in BARE / SYS / RT (safe stop + recovery)
//   - bus ESTOP (0x001) while rm drives (rm has no CAN RX -> documented limit)
//   - MTR 0x204 drive-command watchdog (150 ms stale -> zero propulsion)
//   - corrupted 0x011 E2E CRC -> MTR drops safety authority
//   - sys RT-heartbeat (0x7FD) loss -> ESTOP
//   - hot mode switch BARE->SYS->RT frame-set correctness + 0x7B9 regression
#include <iostream>
#include <cmath>
#include "can_bus.hpp"
#include "virtual_can_bus.hpp"
#include "nodes/mtr_node.hpp"
#include "nodes/sys_node.hpp"
#include "nodes/rt_node.hpp"
#include "nodes/rm_operator_model.hpp"

namespace testbench {

namespace {
void link_loss(RmOperatorModel& rm) {
    // Mirror rc_decoder failsafe output on signal loss.
    rm.set_signal_valid(false);
    rm.set_park_hold(true);
    rm.set_brake_stroke_mm(15.0f);
}
void link_restore(RmOperatorModel& rm, int32_t speed) {
    rm.set_park_hold(false);
    rm.set_brake_stroke_mm(0.0f);
    rm.drive(speed);
}
}  // namespace

// ── 8.1 RC link loss in BARE -> MTR safe-stops, recovers ─────────────
bool test_fault_rc_loss_bare() {
    std::cout << "TEST: RC link loss BARE -> MTR stop + recovery\n";
    VirtualCanBus low("LOW_CAN");
    MtrNode mtr(low);
    RmOperatorModel rm(low);
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm.receive_can("LOW_CAN", f); });
    mtr.init(); rm.init();
    rm.set_op_mode(rm::OperatingMode::Bare);
    rm.drive(1500);
    for (uint32_t t = 100; t <= 1200; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool driving = mtr.is_traction_enabled();

    link_loss(rm);
    for (uint32_t t = 1210; t <= 1600; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool stopped = !mtr.is_traction_enabled();

    link_restore(rm, 1500);
    for (uint32_t t = 1610; t <= 3000; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool recovered = mtr.is_traction_enabled();

    std::cout << "  driving=" << driving << " stopped_on_loss=" << stopped
              << " recovered=" << recovered << "\n";
    if (!driving || !stopped || !recovered) { std::cerr << "  FAIL: BARE link loss handling\n"; return false; }
    return true;
}

// ── 8.2 RC link loss in SYS -> MTR safe-stops, recovers ──────────────
bool test_fault_rc_loss_sys() {
    std::cout << "TEST: RC link loss SYS -> MTR stop + recovery\n";
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

    link_loss(rm);
    for (uint32_t t = 1510; t <= 2200; t += 10) { sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool stopped = !mtr.is_traction_enabled();

    link_restore(rm, 1500);
    for (uint32_t t = 2210; t <= 3400; t += 10) { sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool recovered = mtr.is_traction_enabled();

    std::cout << "  driving=" << driving << " stopped_on_loss=" << stopped
              << " recovered=" << recovered << "\n";
    if (!driving || !stopped || !recovered) { std::cerr << "  FAIL: SYS link loss handling\n"; return false; }
    return true;
}

// ── 8.3 RC link loss in RT -> MTR safe-stops, recovers ───────────────
bool test_fault_rc_loss_rt() {
    std::cout << "TEST: RC link loss RT -> MTR stop + recovery\n";
    VirtualCanBus high("HIGH_CAN");
    VirtualCanBus low("LOW_CAN");
    RmOperatorModel rm(high);
    RtNode rt(high, low);
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
    sys.press_start_button();
    for (uint32_t t = 100; t <= 1500; t += 10) {
        rt.step(t, 10); sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10);
        high.tick(t, 10); low.tick(t, 10);
    }
    bool driving = mtr.is_traction_enabled();

    link_loss(rm);
    for (uint32_t t = 1510; t <= 2200; t += 10) {
        rt.step(t, 10); sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10);
        high.tick(t, 10); low.tick(t, 10);
    }
    bool stopped = !mtr.is_traction_enabled();

    link_restore(rm, 1500);
    for (uint32_t t = 2210; t <= 3400; t += 10) {
        rt.step(t, 10); sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10);
        high.tick(t, 10); low.tick(t, 10);
    }
    bool recovered = mtr.is_traction_enabled();

    std::cout << "  driving=" << driving << " stopped_on_loss=" << stopped
              << " recovered=" << recovered << "\n";
    if (!driving || !stopped || !recovered) { std::cerr << "  FAIL: RT link loss handling\n"; return false; }
    return true;
}

// ── 8.4 Bus ESTOP (0x001) while rm drives (rm has no CAN RX) ─────────
bool test_fault_rm_ignores_0x001() {
    std::cout << "TEST: bus 0x001 ESTOP while rm BARE drives (rm has no CAN RX)\n";
    VirtualCanBus low("LOW_CAN");
    MtrNode mtr(low);
    RmOperatorModel rm(low);
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm.receive_can("LOW_CAN", f); });
    mtr.init(); rm.init();
    rm.set_op_mode(rm::OperatingMode::Bare);
    rm.drive(1500);
    for (uint32_t t = 100; t <= 1200; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }

    // Continuously assert bus ESTOP 0x001 for 1 s.
    for (uint32_t t = 1210; t <= 2200; t += 10) {
        low.send(NodeId::TEST_HARNESS, can::Frame::standard(can::kIdSafetyEstop, 0));
        mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10);
    }
    bool latched = mtr.is_estop_latched();
    bool still_driving = mtr.is_traction_enabled();

    std::cout << "  mtr_estop_latched=" << latched << " still_driving=" << still_driving << "\n";
    if (!latched && still_driving) {
        std::cerr << "  ISSUE: rm cannot honor a bus 0x001 ESTOP (no CAN RX) - bench keeps driving\n";
    }
    // Informational: document the limitation; the bench is rm-mastered by design.
    return true;
}

// ── 8.5 MTR 0x204 drive-command watchdog (150 ms stale) ──────────────
bool test_fault_mtr_drive_timeout() {
    std::cout << "TEST: MTR 0x204 drive watchdog (drop 0x204 -> zero propulsion)\n";
    VirtualCanBus low("LOW_CAN");
    MtrNode mtr(low);
    RmOperatorModel rm(low);
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm.receive_can("LOW_CAN", f); });
    mtr.init(); rm.init();
    rm.set_op_mode(rm::OperatingMode::Bare);
    rm.drive(1500);
    for (uint32_t t = 100; t <= 1200; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool driving = mtr.is_traction_enabled();

    low.drop(0x204, 40);  // ~400 ms of 0x204 suppressed (> 150 ms timeout)
    for (uint32_t t = 1210; t <= 1500; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool timed_out = !mtr.is_traction_enabled();

    for (uint32_t t = 1510; t <= 3000; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool recovered = mtr.is_traction_enabled();

    std::cout << "  driving=" << driving << " timed_out=" << timed_out
              << " recovered=" << recovered << "\n";
    if (!driving || !timed_out || !recovered) { std::cerr << "  FAIL: MTR drive watchdog\n"; return false; }
    return true;
}

// ── 8.6 Corrupted 0x011 E2E CRC -> MTR drops safety authority ────────
bool test_fault_011_crc_rejected() {
    std::cout << "TEST: corrupt 0x011 E2E CRC -> MTR drops authority\n";
    VirtualCanBus low("LOW_CAN");
    MtrNode mtr(low);
    RmOperatorModel rm(low);
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm.receive_can("LOW_CAN", f); });
    mtr.init(); rm.init();
    rm.set_op_mode(rm::OperatingMode::Bare);
    rm.drive(1500);
    for (uint32_t t = 100; t <= 1200; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool driving = mtr.is_traction_enabled();

    // Corrupt the CRC byte (4) of each 0x011 for 200 ms.
    for (uint32_t t = 1210; t <= 1400; t += 10) {
        low.corrupt_byte(0x011, 4, 0xFF);
        mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10);
    }
    bool dropped = !mtr.node_status().ready || !mtr.is_traction_enabled();

    // Stop corrupting -> fresh good 0x011 frames restore authority.
    for (uint32_t t = 1410; t <= 2600; t += 10) { mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool recovered = mtr.is_traction_enabled();

    std::cout << "  driving=" << driving << " dropped_on_bad_crc=" << dropped
              << " recovered=" << recovered << "\n";
    if (!driving || !dropped || !recovered) { std::cerr << "  FAIL: 0x011 CRC rejection\n"; return false; }
    return true;
}

// ── 8.7 sys RT-heartbeat (0x7FD) loss -> ESTOP ───────────────────────
bool test_fault_sys_rt_heartbeat_loss() {
    std::cout << "TEST: sys 0x7FD RT-heartbeat loss -> ESTOP\n";
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
    bool was_auto = (sys.mode() == can::Mode::Auto);

    low.drop(0x7FD, 8);  // drop ~2 s of RT heartbeats (> 1000 ms timeout)
    for (uint32_t t = 1510; t <= 3200; t += 10) { sys.step(t, 10); mtr.step(t, 10); rm.step(t, 10); low.tick(t, 10); }
    bool latched = sys.is_estop_latched();
    bool stopped = !mtr.is_traction_enabled();

    std::cout << "  was_auto=" << was_auto << " estop_latched=" << latched
              << " mtr_stopped=" << stopped << "\n";
    if (!was_auto || !latched || !stopped) { std::cerr << "  FAIL: sys heartbeat timeout handling\n"; return false; }
    return true;
}

// ── 8.8 Hot mode switch + 0x7B9 regression ───────────────────────────
bool test_fault_hot_mode_switch() {
    std::cout << "TEST: hot mode switch BARE->SYS->RT frame sets\n";
    VirtualCanBus bus("LOW_CAN");
    RmOperatorModel rm(bus);
    rm.init();
    rm.drive(1500);  // enables rm, op_mode defaults Bare

    auto window = [&](uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += 10) { rm.step(t, 10); bus.tick(t, 10); }
    };

    window(100, 400);
    bool bare_ok = bus.has_frame(0x7B9) && bus.has_frame(0x110) && bus.has_frame(0x113)
                && !bus.has_frame(0x205);
    bus.clear_trace();

    rm.set_op_mode(rm::OperatingMode::Sys);
    window(410, 700);
    // SYS mode must emit 0x205 (brake intent) and NEVER 0x7B9 (regression).
    bool sys_ok = bus.has_frame(0x205) && bus.has_frame(0x111) && bus.has_frame(0x7FD)
               && !bus.has_frame(0x7B9);
    bus.clear_trace();

    rm.set_op_mode(rm::OperatingMode::Rt);
    window(710, 1000);
    bool rt_ok = bus.has_frame(0x300) && bus.has_frame(0x301) && bus.has_frame(0x303)
              && bus.has_frame(0x7FC) && !bus.has_frame(0x204) && !bus.has_frame(0x169);
    bus.clear_trace();

    std::cout << "  bare_ok=" << bare_ok << " sys_ok=" << sys_ok << " rt_ok=" << rt_ok << "\n";
    if (!bare_ok || !sys_ok || !rt_ok) { std::cerr << "  FAIL: hot mode switch frame sets\n"; return false; }
    return true;
}

}  // namespace testbench
