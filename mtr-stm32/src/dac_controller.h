#pragma once
// MTR STM32G431 — Software I2C Driver for MCP4725 12-Bit DAC
// Probes candidate addresses 0x60, 0x61, 0x62 and enforces voltage safety windows.

#include <cstdint>
#include <algorithm>
#include "stm32g4xx_hal.h"
#include "config.h"

namespace mtr {

class DacController {
public:
    DacController() = default;

    // Initialize PA5 (SCL) and PA7 (SDA) as open-drain with internal pull-ups
    void init() {
        __HAL_RCC_GPIOA_CLK_ENABLE();

        HAL_GPIO_WritePin(GPIOA, kI2cSclPin | kI2cSdaPin, GPIO_PIN_SET);

        GPIO_InitTypeDef gpio{};
        gpio.Pin = kI2cSclPin | kI2cSdaPin;
        gpio.Mode = GPIO_MODE_OUTPUT_OD;
        gpio.Pull = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);

        // Power-on bus recovery: clock 9 pulses with SDA high to release any hung slave
        bus_recover_();

        // Allow 50 ms for ISO1540 and MCP4725 5.0 V rail (VCC2) to stabilize after power-up
        HAL_Delay(50);

        current_code_ = 0xFFFF;
        cached_address_ = 0;
        zero_refresh_counter_ = 0;
        eeprom_burned_ = false;

        // Automatically provision MCP4725 non-volatile EEPROM with 0.0 V at boot.
        // Once burned, hardware natively powers up at 0.0 V before MCU code executes.
        if (write_dac_eeprom(0)) {
            current_code_ = 0;
        } else if (write_dac_raw(0)) {
            current_code_ = 0;
        }
    }

    // Write safe clamped throttle:
    // When enabled = false or throttle_active = false: sets 0 V.
    // When active: clamps code to [kDacMinCode (655), kDacMaxCode (1966)] (~0.8V to ~2.4V)
    void set_throttle(uint16_t code, bool enabled) {
        uint16_t target = (!enabled || code == 0) ? 0 : std::clamp(code, kDacMinCode, kDacMaxCode);
        if (target == 0) {
            force_zero();
            return;
        }
        zero_refresh_counter_ = 0;
        if (target == current_code_) {
            return;
        }
        if (write_dac_raw(target)) {
            current_code_ = target;
        }
    }

    // Force zero voltage output (ESTOP / Watchdog timeout / Idle state)
    // Ensures DAC is actively written to 0.0 V even if initial power-on write failed
    // (e.g. late power-on of MTR/DAC) and periodically refreshed in default state.
    void force_zero() {
        if (!eeprom_burned_) {
            // First time DAC is detected/connected, burn 0.0 V into EEPROM so that
            // future hardware power-ups immediately default to 0.0 V (survives module replacement).
            if (write_dac_eeprom(0)) {
                current_code_ = 0;
                zero_refresh_counter_ = 0;
                return;
            }
        }

        if (current_code_ == 0) {
            // Periodic refresh every ~250 ms (50 calls * 5ms) to guarantee 0.0 V is maintained
            // even after hardware brownout, hot-plug, or reset during idle/default state
            if (++zero_refresh_counter_ >= kZeroRefreshInterval) {
                zero_refresh_counter_ = 0;
                write_dac_raw(0);
            }
            return;
        }
        if (write_dac_raw(0)) {
            current_code_ = 0;
            zero_refresh_counter_ = 0;
        }
    }

    uint16_t current_code() const { return current_code_; }
    uint8_t cached_address() const { return cached_address_; }
    bool eeprom_burned() const { return eeprom_burned_; }

    // Burn voltage into MCP4725 non-volatile EEPROM and wait for write cycle (t_prog <= 50 ms)
    bool write_dac_eeprom(uint16_t value) {
        bool ok = write_dac_raw(value, true /* write_eeprom */);
        if (ok) {
            HAL_Delay(50); // MCP4725 internal EEPROM programming time
            eeprom_burned_ = true;
        }
        return ok;
    }

    // Direct MCP4725 write routine with address caching and robust payload transmission
    bool write_dac_raw(uint16_t value, bool write_eeprom = false) {
        if (value > 4095) value = 4095;

        // If cached address is known, attempt write directly
        if (cached_address_ != 0) {
            if (try_write_address_(cached_address_, value, write_eeprom)) {
                return true;
            }
            cached_address_ = 0; // Invalidate cache on NACK
        }

        // Candidate 7-bit addresses: 0x60 (A0=GND, bench hardware default), 0x61 (A0=VCC), 0x62 (A1 variant)
        static const uint8_t kCandidateAddresses[] = {
            static_cast<uint8_t>(0x60 << 1),
            static_cast<uint8_t>(0x61 << 1),
            static_cast<uint8_t>(0x62 << 1)
        };

        for (uint8_t addr : kCandidateAddresses) {
            if (try_write_address_(addr, value, write_eeprom)) {
                cached_address_ = addr;
                return true;
            }
        }
        return false;
    }

private:
    static void i2c_delay_() {
        for (volatile uint32_t i = 0; i < 40; ++i) {
            __NOP();
        }
    }

    void bus_recover_() {
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
        for (int i = 0; i < 9; ++i) {
            HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
            i2c_delay_();
            HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
            i2c_delay_();
        }
        i2c_stop_();
    }

    void i2c_start_() {
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_RESET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        i2c_delay_();
    }

    void i2c_stop_() {
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_RESET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
        i2c_delay_();
    }

    uint8_t i2c_write_byte_(uint8_t byte) {
        uint8_t temp_byte = byte;
        for (uint8_t i = 0; i < 8; ++i) {
            if (temp_byte & 0x80) {
                HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
            } else {
                HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_RESET);
            }
            temp_byte <<= 1;
            i2c_delay_();
            HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
            i2c_delay_();
            HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        }

        // 9th clock cycle: ACK bit
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET); // Release SDA for slave
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET); // SCL high to clock ACK
        i2c_delay_();
        uint8_t ack = (HAL_GPIO_ReadPin(GPIOA, kI2cSdaPin) == GPIO_PIN_RESET) ? 1 : 0;
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        i2c_delay_();
        return ack;
    }

    bool try_write_address_(uint8_t addr, uint16_t value, bool write_eeprom = false) {
        i2c_start_();
        uint8_t ack = i2c_write_byte_(addr);

        // Clock out command byte: 0x60 for DAC Register + EEPROM, or 0x40 for DAC Register only
        uint8_t cmd = write_eeprom ? 0x60 : 0x40;
        (void)i2c_write_byte_(cmd);
        (void)i2c_write_byte_(static_cast<uint8_t>((value >> 4) & 0xFF));
        (void)i2c_write_byte_(static_cast<uint8_t>((value << 4) & 0xF0));
        i2c_stop_();

        return (ack != 0);
    }

    static constexpr uint8_t kZeroRefreshInterval = 50; // 50 * 5 ms = 250 ms periodic refresh
    uint8_t zero_refresh_counter_{0};
    uint16_t current_code_{0xFFFF};
    uint8_t cached_address_{0};
    bool eeprom_burned_{false};
};

}  // namespace mtr
