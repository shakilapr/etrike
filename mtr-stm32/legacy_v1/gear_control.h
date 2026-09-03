#pragma once
// Gear control — TLP281 optoisolator input read + MOSFET output drives.
//
// TLP281 inputs (active-low):
//   72V present → opto LED on → phototransistor conducts → GPIO LOW
//   72V absent  → phototransistor off → internal pull-up → GPIO HIGH
//
// MOSFET outputs (active-high):
//   GPIO HIGH → optocoupler LED on → TRIAC/transistor gate on → 72V to ECU
//   GPIO LOW  → MOSFET off → ECU gear line floating
//
// STM32 HAL GPIO access (helpers use the pin encoding from config.h):
//   GPIO_PORT(p) = (p < 16) ? GPIOA : ((p < 32) ? GPIOB : GPIOC)
//   GPIO_PIN(p)  = 1 << (p & 0x0F)
#pragma once
// Gear control — logic decoder drives (DEC_A, DEC_B, DEC_EN).
//
// Logic decoder states (DEC_EN active-low):
//   DEC_EN HIGH → Disable (N)
//   DEC_EN LOW  → Enable (D/S/R based on A/B)
//
// STM32 HAL GPIO access (helpers use the pin encoding from config.h):
//   GPIO_PORT(p) = (p < 16) ? GPIOA : ((p < 32) ? GPIOB : GPIOC)
//   GPIO_PIN(p)  = 1 << (p & 0x0F)

#include <cstdint>
#include "config.h"

namespace mtr {

class GearControl {
public:
    /// Initialise gear I/O pins.
    void init() {
        all_off();
        m_current_gear = Gear::N;
    }

    /// Read sense inputs.
    /// V2 hardware does not have discrete optoisolator inputs for gear.
    /// Returns Neutral as fail-safe.
    Gear read_sense() {
        m_gear_conflict = false;
        return Gear::N;
    }

    /// Returns true if gear conflict detected.
    bool gear_conflict_detected() const { return m_gear_conflict; }

    /// Set logic decoder outputs to match the given gear.
    void set_mosfets(Gear gear) {
        bool a = false;
        bool b = false;

        switch (gear) {
            case Gear::N: a = false; b = false; break;
            case Gear::D: a = true;  b = false; break;
            case Gear::S: a = false; b = true;  break;
            case Gear::R: a = true;  b = true;  break;
        }

        set_pin(kGearDecA, a);
        set_pin(kGearDecB, b);

        if (gear == Gear::N) {
            set_pin(kGearDecEn, true); // Disable (active-low)
        } else {
            set_pin(kGearDecEn, false); // Enable
        }
        
        m_current_gear = gear;
    }

    /// Pass-through: no-op in V2 since no hardware sense inputs exist.
    void pass_through() {
        // Without hardware switches, fallback to Neutral
        set_mosfets(Gear::N);
    }

    /// All OFF (neutral / ESTOP).
    void all_off() {
        set_mosfets(Gear::N);
    }

    /// Currently selected gear (last written).
    Gear current_gear() const { return m_current_gear; }

private:
    /// Set a single GPIO output pin.
    static void set_pin(int pin, bool on) {
        GPIO_TypeDef* port = (pin < 16) ? GPIOA : ((pin < 32) ? GPIOB : GPIOC);
        uint16_t mask = 1 << (pin & 0x0F);
        HAL_GPIO_WritePin(port, mask, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    Gear m_current_gear = Gear::N;
    bool m_gear_conflict = false;
};

/// Global gear control instance.
extern GearControl g_gear;

}  // namespace mtr
