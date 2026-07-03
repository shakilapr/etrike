#pragma once
// gear control — SYS_OWNS_MOTOR bench-only legacy. MTR owns all gear in vehicle.. Architecture.md §8.6.
#include <cstdint>
#include "driver/gpio.h"
#include "config.h"
#include "can/can_protocol.h"
namespace sys {
// Bit positions in the 3-bit sense mask: D=b0, S=b1, R=b2
constexpr uint8_t kGearSenseD = 1u << 0;
constexpr uint8_t kGearSenseS = 1u << 1;
constexpr uint8_t kGearSenseR = 1u << 2;

// Output bit for each gear (MOSFET drive (bench-only SYS_OWNS_MOTOR legacy))
constexpr uint8_t kGearOutD = 1u;
constexpr uint8_t kGearOutS = 2u;
constexpr uint8_t kGearOutR = 4u;

class GearControl {
public:
    void init() {
        m_gear = can::Gear::N;
        // Initialize output GPIOs (bench-only legacy)
        gpio_set_direction(static_cast<gpio_num_t>(sys::kGearDOut), GPIO_MODE_OUTPUT);
        gpio_set_direction(static_cast<gpio_num_t>(sys::kGearSOut), GPIO_MODE_OUTPUT);
        gpio_set_direction(static_cast<gpio_num_t>(sys::kGearROut), GPIO_MODE_OUTPUT);
        // All off on init
        gpio_set_level(static_cast<gpio_num_t>(sys::kGearDOut), 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kGearSOut), 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kGearROut), 0);
    }
    // Call @ 50 Hz. sense: 3-bit (D|S|R). mode: current. setpoint_gear: from CAN.
    // Actuates output GPIOs (bench-only legacy).
    void tick(can::Mode mode, uint8_t sense_bits, uint8_t setpoint_gear) {
        if (mode == can::Mode::Estop) { m_gear = can::Gear::N; apply(); return; }
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
        apply();
    }
    can::Gear gear() const { return m_gear; }
private:
    can::Gear m_gear = can::Gear::N;

    void apply() {
        gpio_set_level(static_cast<gpio_num_t>(sys::kGearDOut), (m_gear == can::Gear::D) ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kGearSOut), (m_gear == can::Gear::S) ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(sys::kGearROut), (m_gear == can::Gear::R) ? 1 : 0);
    }
};
}
