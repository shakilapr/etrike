#pragma once
// MCP4725 I2C DAC — 12-bit, VCC=5V → 0-5V output.
// Adapted from sys-esp32/src/mcp4725_dac.h for STM32 HAL.
// Datasheet: MCP4725, I2C addr 0x61 (A0=VCC), write command 0x40.
//
// Protocol (I2C):
//   START + dev_addr(0x60) + W + ACK
//   command_byte(0x40) + ACK        // Write DAC register, normal power mode
//   D[11:4] + ACK                   // Upper 8 bits of 12-bit value
//   D[3:0] << 4 + ACK               // Lower nibble shifted to upper nibble
//   STOP
//
// STM32 HAL: HAL_I2C_Mem_Write(&hi2c1, (0x61 << 1), 0x40,
//                              I2C_MEMADD_SIZE_8BIT, data, 2, timeout)
//
// Protocol (I2C):
//   START + dev_addr(0x60) + W + ACK
//   command_byte(0x40) + ACK        // Write DAC register, normal power mode
//   D[11:4] + ACK                   // Upper 8 bits of 12-bit value
//   D[3:0] << 4 + ACK               // Lower nibble shifted to upper nibble
//   STOP

#include <cstdint>
#include "config.h"

namespace mtr {

class Mcp4725Dac {
public:
    /// Initialise the DAC and explicitly command zero so a retained DAC
    /// output cannot survive an MCU reset into the control loop.
    void init() { (void)write(0); }

    /// Write a 12-bit value directly to the DAC (0-4095 → 0-5V).
    /// Returns true on success, false if I2C write failed after retry.
    bool write(uint16_t val) {
        if (val > kThrottleDacMaxVal) val = kThrottleDacMaxVal;
        
        bool success = do_write(val);
        if (!success) {
            // Retry once
            success = do_write(val);
        }
        
        if (!success) {
            m_i2c_fail_count++;
        }
        return success;
    }

    /// Returns consecutive I2C write failure count.
    uint32_t i2c_failures() const { return m_i2c_fail_count; }

    /// Convenience: set DAC from speed in mm/s.
    /// Maps abs(speed) / 3000 × 4095 → 12-bit DAC value.
    /// Zero speed → 0 V. Negative (reverse) uses magnitude only — gear
    /// lines carry direction.
    /// Speed is clamped to [-kThrottleMaxSpeedMmps, kThrottleMaxSpeedMmps]
    /// before conversion to guard against corrupt CAN 0x204 values.
    /// Returns true if DAC write succeeded.
    bool set_speed_mmps(int32_t speed_mmps) {
        // Clamp to valid range — prevents UB from std::abs(INT32_MIN) and
        // guards against corrupt CAN frames producing arbitrary DAC output.
        if (speed_mmps > kThrottleMaxSpeedMmps) speed_mmps = kThrottleMaxSpeedMmps;
        else if (speed_mmps < -kThrottleMaxSpeedMmps) speed_mmps = -kThrottleMaxSpeedMmps;
        // Branch-based abs to avoid std::abs(INT32_MIN) undefined behavior
        uint32_t abs_speed = speed_mmps < 0 ? uint32_t(-speed_mmps) : uint32_t(speed_mmps);
        uint32_t v = (abs_speed * 4095U) / static_cast<uint32_t>(kThrottleMaxSpeedMmps);
        return write(static_cast<uint16_t>(v > 4095 ? 4095 : v));
    }

private:
    uint32_t m_i2c_fail_count = 0;

    static GPIO_TypeDef* get_port(int pin) {
        return (pin < 16) ? GPIOA : ((pin < 32) ? GPIOB : GPIOC);
    }
    static uint16_t get_mask(int pin) {
        return 1 << (pin & 0x0F);
    }

    static void set_pin(int pin, bool state) {
        HAL_GPIO_WritePin(get_port(pin), get_mask(pin), state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    static bool read_pin(int pin) {
        return HAL_GPIO_ReadPin(get_port(pin), get_mask(pin)) == GPIO_PIN_SET;
    }

    static void delay() {
        for (volatile int i = 0; i < 50; i++);
    }

    static void start() {
        set_pin(kThrottleI2cSda, true);
        delay();
        set_pin(kThrottleI2cScl, true);
        delay();
        set_pin(kThrottleI2cSda, false);
        delay();
        set_pin(kThrottleI2cScl, false);
        delay();
    }

    static void stop() {
        set_pin(kThrottleI2cSda, false);
        delay();
        set_pin(kThrottleI2cScl, true);
        delay();
        set_pin(kThrottleI2cSda, true);
        delay();
    }

    static bool write_byte(uint8_t byte) {
        for (uint8_t i = 0; i < 8; i++) {
            set_pin(kThrottleI2cSda, (byte & 0x80) != 0);
            byte <<= 1;
            delay();
            set_pin(kThrottleI2cScl, true);
            delay();
            set_pin(kThrottleI2cScl, false);
        }
        
        // Read ACK
        set_pin(kThrottleI2cSda, true);
        delay();
        set_pin(kThrottleI2cScl, true);
        delay();
        
        bool ack = !read_pin(kThrottleI2cSda);
        
        set_pin(kThrottleI2cScl, false);
        delay();
        
        return ack;
    }

    bool do_write(uint16_t val) {
        // Try addresses 0x60 and 0x61
        uint8_t addrs[] = {0x60 << 1, 0x61 << 1};
        for (int i = 0; i < 2; i++) {
            start();
            if (write_byte(addrs[i])) {
                write_byte(0x40);
                write_byte((val >> 4) & 0xFF);
                write_byte((val << 4) & 0xFF);
                stop();
                return true;
            }
            stop();
        }
        return false;
    }
};

/// Global DAC instance.
extern Mcp4725Dac g_dac;

}  // namespace mtr
