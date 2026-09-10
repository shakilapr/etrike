// End-to-end signal-flow tests: inject operator steering/throttle/brake into the
// rm-esp32-t12d gateway and verify the signals propagate through the intermediate
// controllers (sys-esp32 / rt-esp32 models) to the end units (SES steering, SEB
// brake, MTR motor).
//
//   BARE : rm  --0x169--> SES , --0x7B9--> SEB , --0x204--> MTR   (rm is the source)
//   SYS  : rm  --0x169--> SES , --0x205--> SYS --0x7B9--> SEB , --0x204--> MTR
//   RT   : rm  --0x303--> RT --0x169--> SES
//              --0x301--> RT --0x205--> SYS --0x7B9--> SEB
//              --0x300--> RT --0x204--> MTR
#include <iostream>
#include <cmath>
#include "can_bus.hpp"
#include "virtual_can_bus.hpp"
#include "nodes/ses_model.hpp"
#include "nodes/seb_model.hpp"
#include "nodes/mtr_node.hpp"
#include "nodes/sys_node.hpp"
#include "nodes/rt_node.hpp"
#include "nodes/rm_operator_model.hpp"

namespace testbench {

namespace {
constexpr int16_t kSteer20degRaw = 30000 + 200;  // 20.0 deg -> 30200 raw
constexpr float   kBrake15mm     = 15.0f;
constexpr int32_t kSpeed2000     = 2000;

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
}  // namespace

bool test_signal_flow_bare() {
    std::cout << "TEST: signal flow BARE (rm -> SES/SEB/MTR direct)\n";
    VirtualCanBus low("LOW_CAN");
    SesModel ses(low);
    SebModel seb(low);
    MtrNode  mtr(low);
    RmOperatorModel rm(low);
    low.register_node(NodeId::SES, [&](const etrike::protocol::Frame& f) { ses.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SEB, [&](const etrike::protocol::Frame& f) { seb.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm.receive_can("LOW_CAN", f); });
    ses.init(); seb.init(); mtr.init(); rm.init();

    rm.set_op_mode(rm::OperatingMode::Bare);
    rm.drive(kSpeed2000, 20.0f);
    rm.set_brake_stroke_mm(kBrake15mm);

    for (uint32_t t = 100; t <= 1300; t += 10) {
        ses.step(t, 10); seb.step(t, 10); mtr.step(t, 10); rm.step(t, 10);
        low.tick(t, 10);
    }

    float ses_ang = ses.actual_angle_0_1deg();
    float seb_mm  = seb.actual_stroke_mm();
    bool  mtr_ok  = mtr.node_status().ready && mtr.is_traction_enabled();
    std::cout << "  SES angle=" << ses_ang << " (exp " << kSteer20degRaw << ")"
              << "  SEB stroke=" << seb_mm << "mm (exp " << kBrake15mm << ")"
              << "  MTR traction=" << (mtr_ok ? "YES" : "NO")
              << " dac=" << mtr.dac_output() << " gear=" << static_cast<int>(mtr.current_gear()) << "\n";

    bool ok = near(ses_ang, kSteer20degRaw, 10.0f) && near(seb_mm, kBrake15mm, 0.6f) && mtr_ok;
    if (!ok) std::cerr << "  FAIL: signal did not reach all BARE units\n";
    return ok;
}

bool test_signal_flow_sys() {
    std::cout << "TEST: signal flow SYS (rm -> SYS -> units)\n";
    VirtualCanBus low("LOW_CAN");
    RmOperatorModel rm(low);
    SysNode  sys(low);
    MtrNode  mtr(low);
    SesModel ses(low);
    SebModel seb(low);
    low.register_node(NodeId::RM,  [&](const etrike::protocol::Frame& f) { rm.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SYS, [&](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SES, [&](const etrike::protocol::Frame& f) { ses.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SEB, [&](const etrike::protocol::Frame& f) { seb.receive_can("LOW_CAN", f); });
    rm.init(); sys.init(); mtr.init(); ses.init(); seb.init();

    rm.set_op_mode(rm::OperatingMode::Sys);
    rm.drive(kSpeed2000, 20.0f);
    rm.set_brake_stroke_mm(kBrake15mm);

    for (uint32_t t = 100; t <= 1500; t += 10) {
        // Press START once rm's 2 Hz RT_HEARTBEAT (0x7FD) is flowing, so SYS sees
        // a healthy heartbeat and can leave MANUAL (0x7FD is emitted at t=500).
        if (t == 700) sys.press_start_button();
        sys.step(t, 10); mtr.step(t, 10); ses.step(t, 10); seb.step(t, 10); rm.step(t, 10);
        low.tick(t, 10);
    }

    float ses_ang = ses.actual_angle_0_1deg();
    float seb_mm  = seb.actual_stroke_mm();
    bool  mtr_ok  = mtr.node_status().ready && mtr.is_traction_enabled();
    std::cout << "  SYS mode=" << (sys.mode() == can::Mode::Auto ? "AUTO" : "NOT-AUTO")
              << "  SES angle=" << ses_ang << " (exp " << kSteer20degRaw << ")"
              << "  SEB stroke=" << seb_mm << "mm (exp " << kBrake15mm << ")"
              << "  MTR traction=" << (mtr_ok ? "YES" : "NO") << "\n";

    bool ok = near(ses_ang, kSteer20degRaw, 10.0f) && near(seb_mm, kBrake15mm, 0.6f) && mtr_ok;
    if (!ok) std::cerr << "  FAIL: signal did not reach all SYS-mode units\n";
    return ok;
}

bool test_signal_flow_rt() {
    std::cout << "TEST: signal flow RT (rm -> RT -> SYS -> units)\n";
    VirtualCanBus high("HIGH_CAN");
    VirtualCanBus low("LOW_CAN");
    RmOperatorModel rm(high);
    RtNode  rt(high, low);
    SysNode sys(low);
    MtrNode mtr(low);
    SesModel ses(low);
    SebModel seb(low);
    high.register_node(NodeId::RM, [&](const etrike::protocol::Frame& f) { rm.receive_can("HIGH_CAN", f); });
    high.register_node(NodeId::RT, [&](const etrike::protocol::Frame& f) { rt.receive_can("HIGH_CAN", f); });
    low.register_node(NodeId::RT,  [&](const etrike::protocol::Frame& f) { rt.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SYS, [&](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SES, [&](const etrike::protocol::Frame& f) { ses.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SEB, [&](const etrike::protocol::Frame& f) { seb.receive_can("LOW_CAN", f); });
    rm.init(); rt.init(); sys.init(); mtr.init(); ses.init(); seb.init();

    sys.press_start_button();
    rm.set_op_mode(rm::OperatingMode::Rt);
    rm.drive(kSpeed2000, 20.0f);
    rm.set_brake_stroke_mm(kBrake15mm);

    for (uint32_t t = 100; t <= 1500; t += 10) {
        rt.step(t, 10); sys.step(t, 10); mtr.step(t, 10); ses.step(t, 10); seb.step(t, 10); rm.step(t, 10);
        high.tick(t, 10);
        low.tick(t, 10);
    }

    float ses_ang = ses.actual_angle_0_1deg();
    float seb_mm  = seb.actual_stroke_mm();
    bool  mtr_ok  = mtr.node_status().ready && mtr.is_traction_enabled();
    bool  auth    = rt.is_motion_authorized();
    std::cout << "  RT mode=" << (rt.active_mode() == can::Mode::Auto ? "AUTO" : "NOT-AUTO")
              << "  authority=" << (auth ? "GRANTED" : "NONE")
              << "  RT cmd speed=" << rt.commanded_speed_mmps()
              << "  SES angle=" << ses_ang << " (exp " << kSteer20degRaw << ")"
              << "  SEB stroke=" << seb_mm << "mm (exp " << kBrake15mm << ")"
              << "  MTR traction=" << (mtr_ok ? "YES" : "NO") << "\n";

    // Motion authority must come from the real SYS on the LOW bus (all three
    // readiness bits), not from rm's high-bus SYS emulation.
    bool ok = auth && near(ses_ang, kSteer20degRaw, 10.0f) && near(seb_mm, kBrake15mm, 0.6f) && mtr_ok;
    if (!ok) std::cerr << "  FAIL: signal did not reach all RT-mode units\n";
    return ok;
}

}  // namespace testbench
