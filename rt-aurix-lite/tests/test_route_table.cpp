// Route table policy tests — pure, deterministic.

#include <cstdio>
#include <cstdlib>

#include "protocol/route_table.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::Bus;
using rta::RouteAction;
using rta::classify_route;

void test_estop_bridge() {
    auto lo = classify_route(Bus::Low, 0x001u);
    CHECK(lo.action == RouteAction::ForwardLowToHigh);
    CHECK(lo.is_estop);
    auto hi = classify_route(Bus::High, 0x001u);
    CHECK(hi.action == RouteAction::ForwardHighToLow);
    CHECK(hi.is_estop);
}

void test_l2h_transparent() {
    CHECK(classify_route(Bus::Low, 0x120u).action == RouteAction::ForwardLowToHigh);
    CHECK(classify_route(Bus::Low, 0x206u).action == RouteAction::ForwardLowToHigh);
    // Same IDs on high bus are NOT re-forwarded (they arrive via forward).
    CHECK(classify_route(Bus::High, 0x120u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::High, 0x206u).action == RouteAction::BusLocal);
}

void test_h2l_transparent() {
    CHECK(classify_route(Bus::High, 0x302u).action == RouteAction::ForwardHighToLow);
    CHECK(classify_route(Bus::Low, 0x302u).action == RouteAction::BusLocal);
}

void test_consume_regenerate() {
    CHECK(classify_route(Bus::High, 0x300u).action == RouteAction::ConsumeAndRegenerate);
    CHECK(classify_route(Bus::High, 0x301u).action == RouteAction::ConsumeAndRegenerate);
    CHECK(classify_route(Bus::High, 0x303u).action == RouteAction::ConsumeAndRegenerate);
}

void test_bus_local() {
    // RT telemetry + actuator feedback + HMI are bus-local.
    CHECK(classify_route(Bus::Low, 0x169u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::Low, 0x7B9u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::Low, 0x201u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::Low, 0x721u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::High, 0x210u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::High, 0x600u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::High, 0x111u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::Low, 0x204u).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::High, 0x7FD).action == RouteAction::BusLocal);
    CHECK(classify_route(Bus::Low, 0x7FD).action == RouteAction::BusLocal);
}

}  // namespace

int main() {
    test_estop_bridge();
    test_l2h_transparent();
    test_h2l_transparent();
    test_consume_regenerate();
    test_bus_local();
    if (g_failures) {
        std::printf("route_table: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("route_table: all tests passed\n");
    return 0;
}
