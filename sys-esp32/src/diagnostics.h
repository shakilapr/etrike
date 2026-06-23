#pragma once
// System diagnostics — collects health data, sends via CAN 0x600 @ 1 Hz.
#include <cstdint>

namespace sys {

class Diagnostics {
public:
    Diagnostics() = default;

    void init() {}  // no-op (ready immediately)
    void report(uint8_t mode, bool brake_engaged, bool hb_ok, bool estop) const;
};

}  // namespace sys
