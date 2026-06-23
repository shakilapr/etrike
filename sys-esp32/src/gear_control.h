#pragma once
// Gear control — TLP281 inputs + relay outputs. Architecture.md §8.6.
#include <cstdint>
#include "config.h"
#include "can/can_protocol.h"
namespace sys {
// Bit positions in the 3-bit sense mask: D=b0, S=b1, R=b2
constexpr uint8_t kGearSenseD = 1u << 0;
constexpr uint8_t kGearSenseS = 1u << 1;
constexpr uint8_t kGearSenseR = 1u << 2;

// Output bit for each gear (relay drive)
constexpr uint8_t kGearOutD = 1u;
constexpr uint8_t kGearOutS = 2u;
constexpr uint8_t kGearOutR = 4u;

class GearControl {
public:
    void init() { m_gear = can::Gear::N; }
    // Call @ 50 Hz. sense: 3-bit (D|S|R). mode: current. setpoint_gear: from CAN.
    uint8_t tick(can::Mode mode, uint8_t sense_bits, uint8_t setpoint_gear) {
        if (mode == can::Mode::Estop) { m_gear = can::Gear::N; return 0; }
        if (mode == can::Mode::Manual) {
            // mirror: if D→D, S→S, R→R. Conflict → N.
            bool d = (sense_bits & kGearSenseD) != 0;
            bool s = (sense_bits & kGearSenseS) != 0;
            bool r = (sense_bits & kGearSenseR) != 0;
            int count = int(d) + int(s) + int(r);
            if (count == 1) {
                if (d) m_gear = can::Gear::D;
                else if (s) m_gear = can::Gear::S;
                else m_gear = can::Gear::R;
            } else {
                m_gear = can::Gear::N;
            }
        } else {
            bool valid = (setpoint_gear <= static_cast<uint8_t>(can::Gear::R));
            m_gear = valid ? can::Gear(setpoint_gear) : can::Gear::N;
        }
        return gear_out_bits();
    }
    can::Gear gear() const { return m_gear; }
    uint8_t gear_out_bits() const {
        switch (m_gear) {
        case can::Gear::D: return kGearOutD;
        case can::Gear::S: return kGearOutS;
        case can::Gear::R: return kGearOutR;
        default:           return 0;
        }
    }
private:
    can::Gear m_gear = can::Gear::N;
};
}
