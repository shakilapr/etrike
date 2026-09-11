// Section 12: realistic bus timing/arbitration (opt-in) and duplicate-ID conflicts.
#include <iostream>
#include <vector>
#include <utility>
#include "can_bus.hpp"
#include "virtual_can_bus.hpp"
#include "protocol/compat/can.hpp"
#include "nodes/sys_node.hpp"
#include "nodes/rm_operator_model.hpp"

namespace testbench {

namespace {
std::vector<std::pair<uint32_t, uint32_t>> g_deliveries;  // (time_ms, id)

can::Frame frame_std(uint32_t id, uint8_t dlc = 8) {
    return can::Frame::standard(id, dlc);
}
}  // namespace

// 12.1 A high-priority frame (0x001) is delivered before a lower-priority burst.
bool test_realistic_priority() {
    std::cout << "TEST: realistic bus prioritizes low CAN IDs (0x001 first)\n";
    VirtualCanBus bus("LOW_CAN");
    g_deliveries.clear();
    bus.set_realistic(true);
    bus.register_node(NodeId::MTR, [](const etrike::protocol::Frame& f) {
        g_deliveries.emplace_back(0, f.id);
    });

    bus.send(NodeId::RT, frame_std(0x300));
    bus.send(NodeId::RT, frame_std(0x001, 0));
    bus.tick(100, 10);

    bool estop_first = !g_deliveries.empty() && g_deliveries.front().second == 0x001;
    std::cout << "  first_delivered=";
    if (g_deliveries.empty()) std::cout << "none\n";
    else std::cout << std::hex << g_deliveries.front().second << std::dec << "\n";

    if (!estop_first) {
        std::cerr << "  FAIL: low-ID frame did not win arbitration\n";
        return false;
    }
    return true;
}

// 12.2 Frames are serialized: a burst spreads across successive bus slots.
bool test_realistic_serialization() {
    std::cout << "TEST: realistic bus serializes a burst over time\n";
    VirtualCanBus bus("LOW_CAN");
    int delivered = 0;
    bus.set_realistic(true);
    bus.register_node(NodeId::MTR, [&](const etrike::protocol::Frame&) { ++delivered; });

    bus.tick(100, 10);  // advance the bus clock so sends are stamped at t=100
    for (int i = 0; i < 5; ++i) bus.send(NodeId::RT, frame_std(0x300));
    bus.tick(100, 10);
    int at_100 = delivered;
    bus.tick(101, 10);
    bus.tick(102, 10);
    bus.tick(103, 10);
    bus.tick(104, 10);

    std::cout << "  delivered@100=" << at_100 << " delivered@104=" << delivered << "\n";
    if (at_100 != 1 || delivered != 5) {
        std::cerr << "  FAIL: serialization did not spread the burst\n";
        return false;
    }
    return true;
}

// 12.3 Self-reception (loopback) is optional and off by default.
bool test_self_reception() {
    std::cout << "TEST: optional self-reception\n";
    VirtualCanBus bus("LOW_CAN");
    int rx = 0;
    bus.set_realistic(true);
    bus.register_node(NodeId::RT, [&](const etrike::protocol::Frame&) { ++rx; });

    bus.send(NodeId::RT, frame_std(0x204));
    bus.tick(100, 10);
    bool off_ok = (rx == 0);

    bus.set_self_reception(true);
    bus.send(NodeId::RT, frame_std(0x204));
    bus.tick(110, 10);
    bool on_ok = (rx == 1);

    std::cout << "  default_rx=" << (off_ok ? 0 : 1) << " self_rx=" << rx << "\n";
    if (!off_ok || !on_ok) {
        std::cerr << "  FAIL: self-reception behavior incorrect\n";
        return false;
    }
    return true;
}

// 12.4 Duplicate-ID detection (two producers on one bus).
bool test_duplicate_id_conflict() {
    std::cout << "TEST: duplicate-ID producer conflict detection\n";
    VirtualCanBus bus("LOW_CAN");
    bus.send(NodeId::HOST, frame_std(0x300));
    bus.send(NodeId::RM,   frame_std(0x300));
    bus.send(NodeId::SYS,  frame_std(0x011, 5));

    auto c = bus.conflicts();
    bool conflict_found = (c.count(0x300) == 1 && c[0x300] == 2);
    bool single_ok = (c.count(0x011) == 0);
    std::cout << "  0x300 producers=" << (c.count(0x300) ? (int)c[0x300] : 0)
              << " 0x011 conflict=" << (c.count(0x011) ? "yes" : "no") << "\n";

    if (!conflict_found || !single_ok) {
        std::cerr << "  FAIL: duplicate-ID conflict detection incorrect\n";
        return false;
    }
    return true;
}

// 12.5 Real SYS + rm RT on ONE low bus => duplicate 0x011/0x110 producers.
bool test_sys_rm_authority_collision() {
    std::cout << "TEST: real SYS + rm on same bus -> 0x011/0x110 collision flagged\n";
    VirtualCanBus low("LOW_CAN");
    SysNode sys(low);
    RmOperatorModel rm(low);
    sys.init();
    rm.init();
    rm.set_op_mode(rm::OperatingMode::Rt);   // rm RT emulates SYS authority on the same bus
    rm.drive(1500);

    for (uint32_t t = 100; t <= 500; t += 10) {
        sys.step(t, 10);
        rm.step(t, 10);
        low.tick(t, 10);
    }

    auto c = low.conflicts();
    bool se = c.count(0x011) == 1 && c[0x011] >= 2;
    bool md = c.count(0x110) == 1 && c[0x110] >= 2;
    std::cout << "  0x011 producers=" << (c.count(0x011) ? (int)c[0x011] : 0)
              << " 0x110 producers=" << (c.count(0x110) ? (int)c[0x110] : 0) << "\n";

    if (!se || !md) {
        std::cerr << "  FAIL: same-bus authority collision not detected\n";
        return false;
    }
    return true;
}

}  // namespace testbench
