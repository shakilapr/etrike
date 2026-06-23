#pragma once
// MCP4725 I2C DAC — 12-bit, VCC=5V → 0-5V output. Architecture.md §8.6.
// I2C addr 0x60, SDA=GPIO15, SCL=GPIO16 (config.h).
#include <cstdint>
#include <cstdlib>
#include "driver/i2c.h"
#include "esp_log.h"
#include "config.h"

namespace sys {

class Mcp4725Dac {
public:
    void init() {
        i2c_config_t cfg = {};
        cfg.mode = I2C_MODE_MASTER;
        cfg.sda_io_num = kThrottleI2cSda;
        cfg.scl_io_num = kThrottleI2cScl;
        cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
        cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
        cfg.master.clk_speed = 100000;  // 100 kHz standard mode
        ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &cfg));
        ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
        m_value = 0;
        m_mismatch_count = 0;
    }

    // Write 12-bit value (0-4095) to DAC output. Returns true on ACK.
    bool write(uint16_t val) {
        val = (val > 4095) ? 4095 : val;
        m_value = val;

        // MCP4725 Fast Write command: [C2 C1 PD1 PD0 D11 D10 D9 D8] [D7 D6 D5 D4 D3 D2 D1 D0]
        uint8_t buf[2];
        buf[0] = ((val >> 8) & 0x0F);        // D11-D8, C2=C1=0 (Fast Write), PD1=PD0=0 (normal)
        buf[1] = val & 0xFF;                   // D7-D0

        esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, kThrottleDacI2cAddr,
                                                    buf, sizeof(buf), pdMS_TO_TICKS(10));
        if (ret != ESP_OK) {
            ESP_LOGW("mcp4725", "I2C write failed: %s", esp_err_to_name(ret));
            m_mismatch_count++;
            return false;
        }
        return true;
    }

    // Convert speed (mm/s) to DAC value and write.
    void set_speed_mmps(int32_t speed_mmps) {
        uint32_t v = (uint32_t(std::abs(speed_mmps)) * 4095) / kThrottleMaxSpeedMmps;
        write(uint16_t(v > 4095 ? 4095 : v));
    }

    // Read back DAC register via I2C to verify output. Call periodically (e.g., 1 Hz).
    // MCP4725 read: 3-byte sequence returns RDY/BSY POR EEPROM data + DAC register.
    bool verify() {
        uint8_t buf[5] = {0};
        esp_err_t ret = i2c_master_read_from_device(I2C_NUM_0, kThrottleDacI2cAddr,
                                                     buf, sizeof(buf), pdMS_TO_TICKS(10));
        if (ret != ESP_OK) {
            ESP_LOGW("mcp4725", "I2C readback failed: %s", esp_err_to_name(ret));
            m_mismatch_count++;
            return false;
        }
        // Byte 2-3 contain DAC register [D11-D0] in bits [3:0] of byte 2 and [7:0] of byte 3
        uint16_t readback = ((uint16_t(buf[2] & 0x0F) << 8) | buf[3]);
        if (std::abs(int(readback) - int(m_value)) > 50) {  // ~60 mV tolerance
            ESP_LOGW("mcp4725", "DAC mismatch: wrote %u, read %u", m_value, readback);
            m_mismatch_count++;
            return false;
        }
        m_mismatch_count = 0;
        return true;
    }

    uint16_t value() const { return m_value; }
    int mismatch_count() const { return m_mismatch_count; }

private:
    uint16_t m_value = 0;
    int m_mismatch_count = 0;
};

}  // namespace sys
