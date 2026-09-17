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
        write_dac_raw(0); // Power up at 0.0 V
        current_code_ = 0;
    }

    // Write safe clamped throttle:
    // When enabled = false or throttle_active = false: sets 0 V.
    // When active: clamps code to [kDacMinCode (655), kDacMaxCode (1966)] (~0.8V to ~2.4V)
    void set_throttle(uint16_t code, bool enabled) {
        uint16_t target = (!enabled || code == 0) ? 0 : std::clamp(code, kDacMinCode, kDacMaxCode);
        if (target == current_code_) {
            return;
        }
        write_dac_raw(target);
        current_code_ = target;
    }

    // Force zero voltage output (ESTOP / Watchdog timeout)
    void force_zero() {
        if (current_code_ == 0) {
            return;
        }
        write_dac_raw(0);
        current_code_ = 0;
    }

    uint16_t current_code() const { return current_code_; }
    uint8_t cached_address() const { return cached_address_; }

    // Direct MCP4725 write routine with address caching and robust payload transmission
    bool write_dac_raw(uint16_t value) {
        if (value > 4095) value = 4095;

        // If cached address is known, attempt write directly
        if (cached_address_ != 0) {
            if (try_write_address_(cached_address_, value)) {
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
            if (try_write_address_(addr, value)) {
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

    bool try_write_address_(uint8_t addr, uint16_t value) {
        i2c_start_();
        uint8_t ack = i2c_write_byte_(addr);

        // Always clock out the 3 MCP4725 payload bytes (0x40 write command, MSB, LSB)
        // regardless of marginal ISO1540 Side 1 VOL (~0.7V) ACK reading.
        (void)i2c_write_byte_(0x40);
        (void)i2c_write_byte_(static_cast<uint8_t>((value >> 4) & 0xFF));
        (void)i2c_write_byte_(static_cast<uint8_t>((value << 4) & 0xF0));
        i2c_stop_();

        return (ack != 0);
    }

    uint16_t current_code_{0};
    uint8_t cached_address_{0};
};

}  // namespace mtr
