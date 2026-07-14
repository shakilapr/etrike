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

#include <cstdint>
#include "config.h"

namespace mtr {

class GearControl {
public:
    /// Initialise gear I/O pins.
    /// Sets MOSFET outputs to OFF (LOW) and configures TLP281 inputs with
    /// internal pull-up (handled by CubeMX MX_GPIO_Init()).
    void init() {
        all_off();
        m_current_gear = Gear::N;
    }

    /// Read TLP281 optoisolator inputs.
    /// Returns the gear enum corresponding to the active sense line.
    /// If multiple lines are active, returns N (fail-safe) and sets
    /// the conflict-detected flag (query via gear_conflict_detected()).
    /// If no line is active, returns N.
    Gear read_sense() {
        bool d = read_sense_pin(kGearDSense);
        bool s = read_sense_pin(kGearSSense);
        bool r = read_sense_pin(kGearRSense);

        int count = (d ? 1 : 0) + (s ? 1 : 0) + (r ? 1 : 0);
        if (count > 1) {
            // Conflict — multiple gears selected. Fail-safe to N.
            m_gear_conflict = true;
            return Gear::N;
        }
        m_gear_conflict = false;
        if (d) return Gear::D;
        if (s) return Gear::S;
        if (r) return Gear::R;
        return Gear::N;
    }

    /// Returns true if the last read_sense() detected a gear conflict
    /// (multiple gear lines active simultaneously). Caller should set
    /// the kMtrFaultGearConflict flag in g_fault_flags.
    bool gear_conflict_detected() const { return m_gear_conflict; }

    /// Set MOSFET outputs to match the given gear.
    /// WARNING: Shifting 72V contactors under load can damage hardware.
    /// Caller must ensure vehicle speed < 50 mm/s before calling.
    void set_mosfets(Gear gear) {
        set_mosfet_pin(kGearDOut, gear == Gear::D);
        set_mosfet_pin(kGearSOut, gear == Gear::S);
        set_mosfet_pin(kGearROut, gear == Gear::R);
        m_current_gear = gear;
    }

    /// Pass-through: read TLP281 sense lines, mirror to MOSFET outputs.
    void pass_through() {
        Gear g = read_sense();
        set_mosfets(g);
    }

    /// All MOSFETs OFF (neutral / ESTOP).
    void all_off() {
        set_mosfets(Gear::N);
    }

    /// Currently selected gear (last written).
    Gear current_gear() const { return m_current_gear; }

private:
    /// Read a single TLP281 sense pin (active-low).
    /// Returns true if the gear is active (GPIO LOW — TLP281 active-low).
    static bool read_sense_pin(int pin) {
        GPIO_TypeDef* port = (pin < 16) ? GPIOA : ((pin < 32) ? GPIOB : GPIOC);
        uint16_t mask = 1 << (pin & 0x0F);
        return HAL_GPIO_ReadPin(port, mask) == GPIO_PIN_RESET;
    }

    /// Set a single MOSFET output pin.
    static void set_mosfet_pin(int pin, bool on) {
        GPIO_TypeDef* port = (pin < 16) ? GPIOA : ((pin < 32) ? GPIOB : GPIOC);
        uint16_t mask = 1 << (pin & 0x0F);
        HAL_GPIO_WritePin(port, mask, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    Gear m_current_gear = Gear::N;
    bool       m_gear_conflict = false;
};

/// Global gear control instance.
extern GearControl g_gear;

}  // namespace mtr
