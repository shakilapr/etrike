#pragma once
#include <cstdint>
#include "can/can_protocol.h"
namespace rt {
class DualHeartbeat {
public:
    void init() { m_ctr=0; }
    void tick_low(can::Frame& out) { out.id=0x7FD;out.dlc=1;out.put_u8(0,++m_ctr); }
    void tick_high(can::Frame& out){ out.id=0x7FD;out.dlc=1;out.put_u8(0,m_ctr); }
    uint8_t ctr() const { return m_ctr; }
private:
    uint8_t m_ctr=0;
};
}
