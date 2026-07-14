#include "protocol/generated/cpp/etrike_protocol.hpp"

#include <cstdio>

static bool is_forwarded(uint32_t id, const char* from, const char* to) {
    for (const auto& route : etrike::protocol::kRoutes) {
        if (route.from_bus != from || route.to_bus != to ||
            route.semantics != etrike::protocol::RouteSemantics::SameFrame) continue;
        for (const auto& message : etrike::protocol::kMessages) {
            if (message.key == route.message && message.bus == route.from_bus &&
                message.id == id && !message.extended) return true;
        }
    }
    return false;
}

int main() {
    const uint32_t low_to_high[] = {0x001, 0x011, 0x120, 0x206, 0x600};
    const uint32_t high_to_low[] = {0x001, 0x111, 0x112, 0x302};
    for (auto id : low_to_high) {
        if (!is_forwarded(id, "low", "high")) return 1;
    }
    for (auto id : high_to_low) {
        if (!is_forwarded(id, "high", "low")) return 2;
    }
    if (is_forwarded(0x7FD, "low", "high") ||
        is_forwarded(0x7FD, "high", "low") ||
        is_forwarded(0x302, "low", "high") ||
        is_forwarded(0x600, "high", "low")) return 3;
    std::puts("PASS: production gateway routes match the canonical contract");
    return 0;
}
