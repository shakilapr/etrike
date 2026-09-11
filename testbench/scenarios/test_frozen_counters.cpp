// Section 10: frozen rolling-counter / heartbeat supervision.
//
// A real CAN peer that keeps transmitting but whose rolling counter never
// advances ("stuck producer") must be detected and drive the fail-safe. The
// VirtualCanBus freeze_payload() fault replays the first payload seen for an
// ID; these tests assert each supervised stream reacts.
#include <iostream>
#include "can_bus.hpp"
#include "virtual_can_bus.hpp"
#include "protocol/compat/can.hpp"
#include "nodes/rt_node.hpp"
#include "nodes/sys_node.hpp"
#include "nodes/rm_operator_model.hpp"

namespace testbench {

namespace {
// rt (Host/SYS emulation) + real SYS authority on the low bus.
struct RtSysHarness {
    VirtualCanBus high{"HIGH_CAN"};
    VirtualCanBus low{"LOW_CAN"};
    RtNode rt{high, low};
    SysNode sys{low};
    RmOperatorModel rm{high};

    RtSysHarness() {
        high.register_node(NodeId::RT, [this](const etrike::protocol::Frame& f) { rt.receive_can("HIGH_CAN", f); });
        high.register_node(NodeId::RM, [this](const etrike::protocol::Frame& f) { rm.receive_can("HIGH_CAN", f); });
        low.register_node(NodeId::RT,  [this](const etrike::protocol::Frame& f) { rt.receive_can("LOW_CAN", f); });
        low.register_node(NodeId::SYS, [this](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });
        rt.init();
        sys.init();
        rm.init();
        rm.set_op_mode(rm::OperatingMode::Rt);
        rm.drive(1500);
        sys.press_start_button();
    }

    void run(uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += 10) {
            rt.step(t, 10);
            sys.step(t, 10);
            rm.step(t, 10);
            high.tick(t, 10);
            low.tick(t, 10);
        }
    }
};
}  // namespace

// 10.1 Frozen 0x110 (SYS_MODE_CMD) counter -> rt mode authority lost.
bool test_frozen_rt_mode_cmd() {
    std::cout << "TEST: frozen 0x110 counter -> rt mode authority lost\n";
    RtSysHarness h;
    h.run(100, 1200);
    bool authorized = h.rt.is_motion_authorized() && h.rt.is_mode_authority_ok();

    h.low.freeze_payload(0x110);
    h.run(1210, 2200);

    bool mode_lost = !h.rt.is_mode_authority_ok();
    bool stopped = (h.rt.commanded_speed_mmps() == 0);
    std::cout << "  authorized=" << authorized << " mode_ok_after_freeze="
              << h.rt.is_mode_authority_ok() << " cmd=" << h.rt.commanded_speed_mmps() << "\n";

    if (!authorized || !mode_lost || !stopped) {
        std::cerr << "  FAIL: frozen 0x110 did not revoke mode authority\n";
        return false;
    }
    return true;
}

// 10.2 Frozen 0x011 (SYS_SAFETY_STS) counter -> fail-safe estop latch.
bool test_frozen_rt_safety_sts() {
    std::cout << "TEST: frozen 0x011 counter -> rt fail-safe (estop latch)\n";
    RtSysHarness h;
    h.run(100, 1200);
    bool authorized = h.rt.is_motion_authorized() && h.rt.is_safety_stream_ok();

    // Chain: 0x011 counter frozen -> StreamValidity invalidates (~700 ms) ->
    // SafetyStreamSupervisor declares LOST (~another 700 ms) -> fail-safe.
    h.low.freeze_payload(0x011);
    h.run(1210, 3400);

    bool safety_lost = !h.rt.is_safety_stream_ok();
    bool latched = h.rt.is_estop_latched();
    bool stopped = (h.rt.commanded_speed_mmps() == 0);
    std::cout << "  authorized=" << authorized << " safety_ok_after=" << h.rt.is_safety_stream_ok()
              << " estop=" << (latched ? "LATCHED" : "NO") << " cmd=" << h.rt.commanded_speed_mmps() << "\n";

    if (!authorized || !safety_lost || !latched || !stopped) {
        std::cerr << "  FAIL: frozen 0x011 did not trip the fail-safe\n";
        return false;
    }
    return true;
}

// 10.3 Frozen 0x7FC (HOST_HEARTBEAT) counter -> assisted stop after 1500 ms.
bool test_frozen_rt_host_heartbeat() {
    std::cout << "TEST: frozen 0x7FC counter -> rt assisted stop\n";
    RtSysHarness h;
    h.run(100, 1200);
    bool authorized = h.rt.is_motion_authorized();

    h.high.freeze_payload(0x7FC);
    h.run(1210, 3400);

    bool hb_lost = h.rt.is_host_heartbeat_lost();
    bool stopped = (h.rt.commanded_speed_mmps() == 0);
    std::cout << "  authorized=" << authorized << " host_hb_lost=" << hb_lost
              << " cmd=" << h.rt.commanded_speed_mmps() << "\n";

    if (!authorized || !hb_lost || !stopped) {
        std::cerr << "  FAIL: frozen 0x7FC did not trigger the assisted stop\n";
        return false;
    }
    return true;
}

// 10.4 Frozen 0x7FD (RT_HEARTBEAT) alive counter -> SYS heartbeat loss -> ESTOP.
bool test_frozen_sys_rt_heartbeat() {
    std::cout << "TEST: frozen 0x7FD counter -> SYS RT-heartbeat loss -> ESTOP\n";
    VirtualCanBus high("HIGH_CAN");
    VirtualCanBus low("LOW_CAN");
    RtNode rt(high, low);
    SysNode sys(low);
    high.register_node(NodeId::RT, [&](const etrike::protocol::Frame& f) { rt.receive_can("HIGH_CAN", f); });
    low.register_node(NodeId::RT,  [&](const etrike::protocol::Frame& f) { rt.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SYS, [&](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });
    rt.init();
    sys.init();

    auto run = [&](uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += 10) {
            rt.step(t, 10);
            sys.step(t, 10);
            high.tick(t, 10);
            low.tick(t, 10);
        }
    };

    run(100, 1500);
    bool hb_ok_before = sys.safety_monitor().heartbeat_ok();

    low.freeze_payload(0x7FD);
    run(1510, 5000);

    bool hb_ok_after = sys.safety_monitor().heartbeat_ok();
    bool estop = sys.is_estop_latched() || (sys.mode() == can::Mode::Estop);
    std::cout << "  hb_ok_before=" << hb_ok_before << " hb_ok_after=" << hb_ok_after
              << " estop=" << (estop ? "YES" : "NO") << "\n";

    if (!hb_ok_before || hb_ok_after || !estop) {
        std::cerr << "  FAIL: frozen 0x7FD did not escalate to ESTOP\n";
        return false;
    }
    return true;
}

}  // namespace testbench
