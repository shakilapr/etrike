#pragma once
// MCP4725 I2C DAC — 12-bit, VCC=5V → 0-5V output. Architecture.md §8.6.
#include <cstdint>
#include <cstdlib>
#include "config.h"
namespace sys {
class Mcp4725Dac {
public:
    void init() { m_value = 0; }
    void write(uint16_t val) { m_value = (val > 4095) ? 4095 : val; }
    void set_speed_mmps(int16_t speed_mmps) {
        uint32_t v = (uint32_t(abs(speed_mmps)) * 4095) / kThrottleMaxSpeedMmps;
        write(uint16_t(v > 4095 ? 4095 : v));
    }
    uint16_t value() const { return m_value; }
private:
    uint16_t m_value = 0;
};
}
