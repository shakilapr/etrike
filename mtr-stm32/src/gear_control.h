#pragma once
// Gear control — TLP281 optoisolator input read + relay output drives.
//
// TLP281 inputs (active-low):
//   72V present → opto LED on → phototransistor conducts → GPIO LOW
//   72V absent  → phototransistor off → internal pull-up → GPIO HIGH
//
// Relay outputs (active-high):
//   GPIO HIGH → optocoupler LED on → TRIAC/transistor gate on → 72V to ECU
//   GPIO LOW  → relay off → ECU gear line floating
//
// STM32 HAL GPIO access (helpers use the pin encoding from config.h):
//   GPIO_PORT(p) = (p < 16) ? GPIOA : ((p < 32) ? GPIOB : GPIOC)
//   GPIO_PIN(p)  = 1 << (p & 0x0F)

#include <cstdint>
#include "config.h"
#include "can/can_protocol.h"

namespace mtr {

class GearControl {
public:
    /// Initialise gear I/O pins.
    /// Sets relay outputs to OFF (LOW) and configures TLP281 inputs with
    /// internal pull-up (handled by CubeMX MX_GPIO_Init()).
    void init() {
        all_off();
        m_current_gear = can::Gear::N;
    }

    /// Read TLP281 optoisolator inputs.
    /// Returns the gear enum corresponding to the active sense line.
    /// If multiple lines are active, returns N (fail-safe).
    /// If no line is active, returns N.
    can::Gear read_sense() {
        bool d = read_sense_pin(kGearDSense);
        bool s = read_sense_pin(kGearSSense);
        bool r = read_sense_pin(kGearRSense);

        int count = (d ? 1 : 0) + (s ? 1 : 0) + (r ? 1 : 0);
        if (count > 1) {
            // Conflict — multiple gears selected. Fail-safe to N.
            return can::Gear::N;
        }
        if (d) return can::Gear::D;
        if (s) return can::Gear::S;
        if (r) return can::Gear::R;
        return can::Gear::N;
    }

    /// Set relay outputs to match the given gear.
    /// WARNING: Shifting 72V contactors under load can damage hardware.
    /// Caller must ensure vehicle speed < 50 mm/s before calling.
    void set_relays(can::Gear gear) {
        set_relay_pin(kGearDOut, gear == can::Gear::D);
        set_relay_pin(kGearSOut, gear == can::Gear::S);
        set_relay_pin(kGearROut, gear == can::Gear::R);
        m_current_gear = gear;
    }

    /// Pass-through: read TLP281 sense lines, mirror to relay outputs.
    void pass_through() {
        can::Gear g = read_sense();
        set_relays(g);
    }

    /// All relays OFF (neutral / ESTOP).
    void all_off() {
        set_relays(can::Gear::N);
    }

    /// Currently selected gear (last written).
    can::Gear current_gear() const { return m_current_gear; }

private:
    /// Read a single TLP281 sense pin (active-low).
    /// Returns true if the gear is active (GPIO LOW — TLP281 active-low).
    static bool read_sense_pin(int pin) {
        GPIO_TypeDef* port = (pin < 16) ? GPIOA : GPIOB;
        uint16_t mask = 1 << (pin & 0x0F);
        return HAL_GPIO_ReadPin(port, mask) == GPIO_PIN_RESET;
    }

    /// Set a single relay output pin.
    static void set_relay_pin(int pin, bool on) {
        GPIO_TypeDef* port = (pin < 16) ? GPIOA : GPIOB;
        uint16_t mask = 1 << (pin & 0x0F);
        HAL_GPIO_WritePin(port, mask, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    can::Gear m_current_gear = can::Gear::N;
};

/// Global gear control instance.
extern GearControl g_gear;

}  // namespace mtr