#pragma once
#include <cstdint>
#include "can/can_protocol.h"
namespace rt {
class DualHeartbeat {
public:
    void init() { m_ctr_low=0; m_ctr_high=0; }

    /// Build heartbeat frame. health_flags: bit0=internal_fault, bit1=can_warning,
    /// bit2=can_passive, bit3=peer_hb_lost, bit4=degraded, bit5-7=reserved.
    void tick_low(can::Frame& out, uint8_t health_flags=0) {
        out.id=can::kIdRtHeartbeatLow; out.dlc=2;
        out.put_u8(0, ++m_ctr_low);
        out.put_u8(1, health_flags);
    }
    void tick_high(can::Frame& out, uint8_t health_flags=0) {
        out.id=can::kIdRtHeartbeatHigh; out.dlc=2;
        out.put_u8(0, ++m_ctr_high);
        out.put_u8(1, health_flags);
    }
    uint8_t ctr_low() const { return m_ctr_low; }
    uint8_t ctr_high() const { return m_ctr_high; }
private:
    uint8_t m_ctr_low=0;
    uint8_t m_ctr_high=0;
};
}
