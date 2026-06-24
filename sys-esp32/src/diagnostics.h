#pragma once
// System diagnostics — collects health data, sends via CAN 0x600 @ 1 Hz.
#include <cstdint>
#include "can/can_driver.h"

namespace sys {

class Diagnostics {
public:
    Diagnostics() = default;

    void init() {}  // no-op (ready immediately)
    void set_can_driver(can::CanDriver* drv) { m_can = drv; }
    void report(uint8_t mode, bool brake_engaged, bool hb_ok, bool estop) const;
private:
    can::CanDriver* m_can = nullptr;
};

}  // namespace sys
