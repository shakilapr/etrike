#pragma once
#include <cstdint>
#include "can/codec_transport.h"
namespace rt {
class DualHeartbeat {
public:
    void init() { m_ctr_low=0; m_ctr_high=0; }

    /// Build heartbeat frame. health_flags bits (shared in can_protocol.h):
    /// bit0=heartbeat_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok, bit4-7=reserved.
    void tick_low(can::Frame& out, uint8_t health_flags=0) {
        can::gen::RtHeartbeat message{++m_ctr_low, health_flags};
        (void)can::encode_frame(message, out);
    }
    void tick_high(can::Frame& out, uint8_t health_flags=0) {
        can::gen::RtHeartbeat message{++m_ctr_high, health_flags};
        (void)can::encode_frame(message, out);
    }
    uint8_t ctr_low() const { return m_ctr_low; }
    uint8_t ctr_high() const { return m_ctr_high; }
private:
    uint8_t m_ctr_low=0;
    uint8_t m_ctr_high=0;
};
}
