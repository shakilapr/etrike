#pragma once
#include <cstdint>
#include "can/can_protocol.h"
namespace rt {
class DualHeartbeat {
public:
    void init() { m_ctr_low=0; m_ctr_high=0; }
    void tick_low(can::Frame& out) { out.id=0x7FD;out.dlc=1;out.put_u8(0,++m_ctr_low); }
    void tick_high(can::Frame& out){ out.id=0x7FD;out.dlc=1;out.put_u8(0,++m_ctr_high); }
    uint8_t ctr_low() const { return m_ctr_low; }
    uint8_t ctr_high() const { return m_ctr_high; }
private:
    uint8_t m_ctr_low=0;
    uint8_t m_ctr_high=0;
};
}
