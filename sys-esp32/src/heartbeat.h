#pragma once
// SYS heartbeat — sends 0x7FE SYS_HEARTBEAT at 2 Hz with alive counter.

#include <cstdint>
#include "can/can_protocol.h"
#include "config.h"

namespace sys {

class Heartbeat {
public:
    void init() { m_counter = 0; }

    // Call at 2 Hz. Returns the CAN frame to transmit (DLC=1, alive_ctr incremented).
    void tick(can::Frame& out) {
        m_counter = (m_counter + 1) & 0xFF;
        out.id  = can::kIdSysHeartbeat;
        out.dlc = 1;
        out.put_u8(0, m_counter);
    }

    uint8_t counter() const { return m_counter; }

private:
    uint8_t m_counter = 0;
};

}  // namespace sys
