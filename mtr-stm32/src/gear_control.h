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

/// GPIO port from combined pin number (0-15=PORTA, 16-31=PORTB, 32-47=PORTC).
inline void* gpio_port(int pin) {
    // STM32: returns GPIOA, GPIOB, or GPIOC based on pin range.
    // Cast to void* to avoid requiring stm32 HAL header here;
    // consumers reinterpret_cast to GPIO_TypeDef*.
    return reinterpret_cast<void*>(
        pin < 16 ? 0x40010800U :        // GPIOA base (STM32F103)
        (pin < 32 ? 0x40010C00U :       // GPIOB base
                    0x40011000U));      // GPIOC base
}

inline uint16_t gpio_pin_mask(int pin) {
    return static_cast<uint16_t>(1U << (pin & 0x0F));
}

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
    /// Returns true if the gear is active (GPIO LOW).
    static bool read_sense_pin(int pin) {
        // STM32 HAL implementation:
        // GPIO_TypeDef* port = static_cast<GPIO_TypeDef*>(gpio_port(pin));
        // uint16_t mask = gpio_pin_mask(pin);
        // return HAL_GPIO_ReadPin(port, mask) == GPIO_PIN_RESET;
        (void)pin;
        return false;  // stub
    }

    /// Set a single relay output pin.
    static void set_relay_pin(int pin, bool on) {
        // STM32 HAL implementation:
        // GPIO_TypeDef* port = static_cast<GPIO_TypeDef*>(gpio_port(pin));
        // uint16_t mask = gpio_pin_mask(pin);
        // HAL_GPIO_WritePin(port, mask, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
        (void)pin;
        (void)on;
    }

    can::Gear m_current_gear = can::Gear::N;
};

/// Global gear control instance.
extern GearControl g_gear;

}  // namespace mtr