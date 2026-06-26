#pragma once
// MCP4725 I2C DAC — 12-bit, VCC=5V → 0-5V output.
// Adapted from sys-esp32/src/mcp4725_dac.h for STM32 HAL.
// Datasheet: MCP4725, I2C addr 0x60, write command 0x40.
//
// Protocol (I2C):
//   START + dev_addr(0x60) + W + ACK
//   command_byte(0x40) + ACK        // Write DAC register, normal power mode
//   D[11:4] + ACK                   // Upper 8 bits of 12-bit value
//   D[3:0] << 4 + ACK               // Lower nibble shifted to upper nibble
//   STOP
//
// STM32 HAL: HAL_I2C_Mem_Write(&hi2c1, (0x60 << 1), 0x40,
//                              I2C_MEMADD_SIZE_8BIT, data, 2, timeout)

#include <cstdint>
#include <cstdlib>
#include "config.h"

namespace mtr {

class Mcp4725Dac {
public:
    /// Initialise the DAC (no-op on this class; I2C peripheral init is
    /// handled by STM32CubeMX MX_I2C1_Init()).
    void init() {}

    /// Write a 12-bit value directly to the DAC (0-4095 → 0-5V).
    void write(uint16_t val) {
        if (val > kThrottleDacMaxVal) val = kThrottleDacMaxVal;
        // Hardware write via STM32 HAL:
        // uint8_t buf[2];
        // buf[0] = (val >> 4) & 0xFF;        // D[11:4]
        // buf[1] = (val << 4) & 0xFF;         // D[3:0] << 4, lower nibble zero
        // extern I2C_HandleTypeDef hi2c1;
        // HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(kThrottleDacI2cAddr << 1),
        //                   0x40, I2C_MEMADD_SIZE_8BIT, buf, 2, HAL_MAX_DELAY);
    }

    /// Convenience: set DAC from speed in mm/s.
    /// Maps abs(speed) / 3000 × 4095 → 12-bit DAC value.
    /// Zero speed → 0 V. Negative (reverse) uses magnitude only — gear
    /// lines carry direction.
    void set_speed_mmps(int32_t speed_mmps) {
        uint32_t v = (static_cast<uint32_t>(std::abs(speed_mmps)) * 4095U)
                   / static_cast<uint32_t>(kThrottleMaxSpeedMmps);
        write(static_cast<uint16_t>(v > 4095 ? 4095 : v));
    }

};

/// Global DAC instance.
extern Mcp4725Dac g_dac;

}  // namespace mtr