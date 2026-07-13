#include "can/can_protocol.h"

#include <cstdio>

int main() {
    const uint32_t low_to_high[] = {0x001, 0x011, 0x120, 0x206, 0x600};
    const uint32_t high_to_low[] = {0x001, 0x111, 0x112, 0x302};
    for (auto id : low_to_high) {
        if (!can::is_forwarded_low_to_high(id)) return 1;
    }
    for (auto id : high_to_low) {
        if (!can::is_forwarded_high_to_low(id)) return 2;
    }
    if (can::is_forwarded_low_to_high(0x7FD) ||
        can::is_forwarded_high_to_low(0x7FD) ||
        can::is_forwarded_low_to_high(0x302) ||
        can::is_forwarded_high_to_low(0x600)) return 3;
    std::puts("PASS: production gateway routes match the canonical contract");
    return 0;
}

