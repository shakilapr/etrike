#pragma once
// Gear control — TLP281 inputs + relay outputs. Architecture.md §8.6.
#include <cstdint>
#include "config.h"
#include "can/can_protocol.h"
namespace sys {
class GearControl {
public:
    void init() { m_gear = can::Gear::N; }
    // Call @ 50 Hz. sense: 3-bit (D|S|R). mode: current. setpoint_gear: from CAN.
    uint8_t tick(can::Mode mode, uint8_t sense_bits, uint8_t setpoint_gear) {
        if (mode == can::Mode::Estop) { m_gear = can::Gear::N; return 0; }
        if (mode == can::Mode::Manual) {
            // mirror: if D→D, S→S, R→R. Conflict → N.
            int count=(sense_bits&1)+((sense_bits>>1)&1)+((sense_bits>>2)&1);
            if (count==1) {
                if (sense_bits&1) m_gear=can::Gear::D;
                else if (sense_bits&2) m_gear=can::Gear::S;
                else m_gear=can::Gear::R;
            } else m_gear=can::Gear::N;
        } else {
            m_gear = (setpoint_gear<=3)?can::Gear(setpoint_gear):can::Gear::N;
        }
        return gear_out_bits();
    }
    can::Gear gear() const { return m_gear; }
    uint8_t gear_out_bits() const {
        switch(m_gear){case can::Gear::D:return 1;case can::Gear::S:return 2;
        case can::Gear::R:return 4;default:return 0;}
    }
private:
    can::Gear m_gear = can::Gear::N;
};
}
