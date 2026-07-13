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
    /// Uses 100ms finite timeout — HAL_MAX_DELAY would block forever on
    /// I2C bus disruption, defeating hardware ESTOP (bug 8.2).
    bool write(uint16_t val) {
        if (val > kThrottleDacMaxVal) val = kThrottleDacMaxVal;
        extern I2C_HandleTypeDef hi2c1;
        uint8_t buf[2];
        buf[0] = (val >> 4) & 0xFF;
        buf[1] = (val << 4) & 0xFF;
        constexpr uint32_t kI2cTimeoutMs = 100;  // Finite — never HAL_MAX_DELAY
        HAL_StatusTypeDef st = HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(kThrottleDacI2cAddr << 1),
                                                  0x40, I2C_MEMADD_SIZE_8BIT, buf, 2, kI2cTimeoutMs);
        if (st != HAL_OK) {
            // Retry once — I2C bus may have had a transient glitch
            st = HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(kThrottleDacI2cAddr << 1),
                                   0x40, I2C_MEMADD_SIZE_8BIT, buf, 2, kI2cTimeoutMs);
        }
        if (st != HAL_OK) {
            m_i2c_fail_count++;
        }
        return st == HAL_OK;
    }

    /// Returns consecutive I2C write failure count. Caller should force
    /// throttle to safe state if this exceeds threshold (e.g., 3).
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

};

/// Global DAC instance.
extern Mcp4725Dac g_dac;

}  // namespace mtr
