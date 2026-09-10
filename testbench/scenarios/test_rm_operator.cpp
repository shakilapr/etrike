// Integration tests: exercise rm-esp32-t12d as an operator/source node in the
// testbench (wrapping the real rm::CanEmitter). Verifies the Phase-1/2 findings:
//  - BARE mode now ignites the MTR (0x011 SYS_SAFETY_STS present)
//  - SYS mode drives sys-esp32 seamlessly (AUTO, no estop)
//  - RT mode frames reach rt-esp32 (0x300/0x011/0x110) without estop latch
#include <iostream>
#include "test_bench.hpp"
#include "can_bus.hpp"
#include "nodes/rt_node.hpp"
#include "nodes/mtr_node.hpp"
#include "nodes/rm_operator_model.hpp"
#include "protocol/compat/can.hpp"

namespace testbench {

bool test_rm_bare_ignites_mtr() {
    std::cout << "TEST: rm BARE mode ignites MTR (0x011 + 0x113 rearm edge)\n";
    // Standalone harness: BARE mode emulates the WHOLE supervisor, so there must
    // be no real SYS on the bus (a second 0x011/0x110/0x113 stream would collide
    // with rm's independent rolling counters and invalidate the MTR streams).
    VirtualCanBus low("LOW_CAN");
    MtrNode mtr(low);
    RmOperatorModel rm_op(low);

    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm_op.receive_can("LOW_CAN", f); });

    mtr.init();
    rm_op.init();
    rm_op.set_op_mode(rm::OperatingMode::Bare);
    rm_op.drive(1500);  // forward 1500 mm/s, gear D

    for (uint32_t t = 100; t <= 1200; t += 10) {
        mtr.step(t, 10);
        rm_op.step(t, 10);
        low.tick(t, 10);
    }

    const auto ns = mtr.node_status();
    bool frames_ok = low.has_frame(0x011) && low.has_frame(0x110) && low.has_frame(0x204);
    bool ignition_ok = ns.ready;  // MTR ignition_on_ (real MotorManager)

    std::cout << "  MTR ready=" << (ignition_ok ? "YES" : "NO")
              << " 0x011=" << low.has_frame(0x011)
              << " 0x110=" << low.has_frame(0x110)
              << " 0x204=" << low.has_frame(0x204) << "\n";

    if (!frames_ok) { std::cerr << "  FAIL: rm BARE frames missing on low CAN\n"; return false; }
    if (!ignition_ok) { std::cerr << "  FAIL: MTR did not ignite in BARE mode\n"; return false; }
    return true;
}

bool test_rm_sys_mode_seamless() {
    std::cout << "TEST: rm SYS mode drives sys-esp32 seamlessly (AUTO)\n";
    TestBench tb;
    tb.reset();
    tb.press_start_button();
    tb.rm().set_op_mode(rm::OperatingMode::Sys);
    tb.rm().drive(1500);
    tb.run_for_ms(800, 10);

    bool frames_ok = tb.low_can().has_frame(0x7FD)   // RT heartbeat (SYS freshness)
                  && tb.low_can().has_frame(0x111)   // HMI mode req
                  && tb.low_can().has_frame(0x204);  // drive cmd
    bool auto_ok = (tb.sys().mode() == can::Mode::Auto);
    bool no_estop = !tb.sys().is_estop_latched();

    std::cout << "  SYS mode=" << (auto_ok ? "AUTO" : "NOT-AUTO")
              << " estop_latched=" << (no_estop ? "NO" : "YES")
              << " 0x7FD=" << tb.low_can().has_frame(0x7FD) << "\n";

    if (!frames_ok) { std::cerr << "  FAIL: rm SYS frames missing on low CAN\n"; return false; }
    if (!auto_ok)  { std::cerr << "  FAIL: sys-esp32 did not reach AUTO\n"; return false; }
    if (!no_estop) { std::cerr << "  FAIL: sys-esp32 estop latched\n"; return false; }
    return true;
}

bool test_rm_rt_mode_reaches_rt() {
    std::cout << "TEST: rm RT high-bus frames reach rt but grant NO motion authority\n";
    // rm is a single-bus (High) Host+SYS emulator. Real rt accepts the Host
    // frames (0x300/0x301/0x303) on High but only accepts SYS authority
    // (0x011/0x110) on LOW (rt-esp32/src/can_rx_router.h:28/36/44 vs :64-84).
    // With no real SYS present the frames arrive but authority is NOT granted —
    // this is the honest model that the previous testbench masked.
    VirtualCanBus high("HIGH_CAN");
    VirtualCanBus low("LOW_CAN");
    RtNode rt(high, low);
    RmOperatorModel rm_op(high);

    high.register_node(NodeId::RT, [&](const etrike::protocol::Frame& f) { rt.receive_can("HIGH_CAN", f); });
    high.register_node(NodeId::RM, [&](const etrike::protocol::Frame& f) { rm_op.receive_can("HIGH_CAN", f); });

    rt.init();
    rm_op.init();
    rm_op.set_op_mode(rm::OperatingMode::Rt);
    rm_op.drive(1500);

    for (uint32_t t = 100; t <= 1200; t += 10) {
        rt.step(t, 10);
        rm_op.step(t, 10);
        high.tick(t, 10);
        low.tick(t, 10);
    }

    bool frames_ok = high.has_frame(0x300)   // HOST_DRIVE_CMD
                  && high.has_frame(0x011)   // SYS_SAFETY_STS (emulated SYS, wrong bus)
                  && high.has_frame(0x110);  // SYS_MODE_CMD (emulated SYS, wrong bus)
    bool no_authority = !rt.is_motion_authorized()
                     && !rt.is_safety_stream_ok()
                     && !rt.is_mode_authority_ok()
                     && rt.commanded_speed_mmps() == 0;
    bool not_auto = (rt.active_mode() != can::Mode::Auto);

    std::cout << "  frames(0x300/0x011/0x110)=" << frames_ok
              << " authority=" << (rt.is_motion_authorized() ? "GRANTED" : "NONE")
              << " mode=" << (not_auto ? "NOT-AUTO" : "AUTO")
              << " cmd=" << rt.commanded_speed_mmps() << "\n";

    if (!frames_ok) { std::cerr << "  FAIL: rm RT frames missing on High CAN\n"; return false; }
    if (!no_authority || !not_auto) {
        std::cerr << "  FAIL: rt accepted high-bus SYS authority (must be ignored)\n";
        return false;
    }
    return true;
}

bool test_rm_rt_low_sys_authority() {
    std::cout << "TEST: rm RT + real SYS authority on LOW bus grants motion\n";
    // Same rm RT Host emulation, but now a real SysNode on the LOW bus grants
    // 0x011 (SAFETY) + 0x110 AUTO (MODE). rt's third bit (HOST) comes from rm's
    // 0x300 on High. All three readiness bits now set -> motion authorized.
    VirtualCanBus high("HIGH_CAN");
    VirtualCanBus low("LOW_CAN");
    RtNode rt(high, low);
    RmOperatorModel rm_op(high);
    SysNode sys(low);

    high.register_node(NodeId::RT, [&](const etrike::protocol::Frame& f) { rt.receive_can("HIGH_CAN", f); });
    high.register_node(NodeId::RM, [&](const etrike::protocol::Frame& f) { rm_op.receive_can("HIGH_CAN", f); });
    low.register_node(NodeId::RT,  [&](const etrike::protocol::Frame& f) { rt.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SYS, [&](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });

    rt.init();
    rm_op.init();
    sys.init();
    rm_op.set_op_mode(rm::OperatingMode::Rt);
    rm_op.drive(1500);
    sys.press_start_button();

    for (uint32_t t = 100; t <= 2000; t += 10) {
        rt.step(t, 10);
        sys.step(t, 10);
        rm_op.step(t, 10);
        high.tick(t, 10);
        low.tick(t, 10);
    }

    bool safety = rt.is_safety_stream_ok();
    bool mode   = rt.is_mode_authority_ok();
    bool host   = rt.is_host_authority_ok();
    bool auth   = rt.is_motion_authorized();
    bool moving = rt.commanded_speed_mmps() == 1500;
    std::cout << "  safety=" << safety << " mode=" << mode << " host=" << host
              << " authorized=" << auth << " cmd=" << rt.commanded_speed_mmps() << "\n";

    if (!safety || !mode || !host || !auth || !moving) {
        std::cerr << "  FAIL: low-bus SYS authority did not grant motion\n";
        return false;
    }
    return true;
}

bool test_rm_rt_low_sys_011_loss() {
    std::cout << "TEST: LOW-bus 0x011 loss -> rt fail-safe (estop latch, motion 0)\n";
    VirtualCanBus high("HIGH_CAN");
    VirtualCanBus low("LOW_CAN");
    RtNode rt(high, low);
    RmOperatorModel rm_op(high);
    SysNode sys(low);

    high.register_node(NodeId::RT, [&](const etrike::protocol::Frame& f) { rt.receive_can("HIGH_CAN", f); });
    high.register_node(NodeId::RM, [&](const etrike::protocol::Frame& f) { rm_op.receive_can("HIGH_CAN", f); });
    low.register_node(NodeId::RT,  [&](const etrike::protocol::Frame& f) { rt.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SYS, [&](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });

    rt.init();
    rm_op.init();
    sys.init();
    rm_op.set_op_mode(rm::OperatingMode::Rt);
    rm_op.drive(1500);
    sys.press_start_button();

    for (uint32_t t = 100; t <= 1000; t += 10) {
        rt.step(t, 10); sys.step(t, 10); rm_op.step(t, 10);
        high.tick(t, 10); low.tick(t, 10);
    }
    bool authorized = rt.is_motion_authorized();
    bool was_moving = (rt.commanded_speed_mmps() == 1500);

    // Drop 0x011 (> 700 ms): ACQUIRED -> LOST must latch ESTOP (fail-safe).
    low.drop(0x011, 200);
    for (uint32_t t = 1010; t <= 2000; t += 10) {
        rt.step(t, 10); sys.step(t, 10); rm_op.step(t, 10);
        high.tick(t, 10); low.tick(t, 10);
    }
    bool lost = !rt.is_safety_stream_ok();
    bool latched = rt.is_estop_latched();
    bool stopped = (rt.commanded_speed_mmps() == 0);

    std::cout << "  authorized=" << authorized << " moving=" << was_moving
              << " after-loss: safety_ok=" << !lost << " estop=" << (latched ? "LATCHED" : "NO")
              << " cmd=" << rt.commanded_speed_mmps() << "\n";

    if (!authorized || !was_moving || !lost || !latched || !stopped) {
        std::cerr << "  FAIL: 0x011 loss did not trip the rt fail-safe\n";
        return false;
    }
    return true;
}

} // namespace testbench
