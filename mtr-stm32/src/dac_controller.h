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

        write_dac_raw(0); // Power up at 0.0 V
    }

    // Write safe clamped throttle:
    // When enabled = false or throttle_active = false: sets 0 V.
    // When active: clamps code to [kDacMinCode (655), kDacMaxCode (1966)] (~0.8V to ~2.4V)
    void set_throttle(uint16_t code, bool enabled) {
        if (!enabled || code == 0) {
            write_dac_raw(0);
            current_code_ = 0;
            return;
        }

        uint16_t clamped = std::clamp(code, kDacMinCode, kDacMaxCode);
        write_dac_raw(clamped);
        current_code_ = clamped;
    }

    // Force zero voltage output (ESTOP / Watchdog timeout)
    void force_zero() {
        write_dac_raw(0);
        current_code_ = 0;
    }

    uint16_t current_code() const { return current_code_; }

    // Direct MCP4725 fast write routine with multi-address scanning and caching
    void write_dac_raw(uint16_t value) {
        if (value > 4095) value = 4095;

        // If cached address is known, attempt write directly
        if (cached_address_ != 0) {
            i2c_start_();
            uint8_t ack = i2c_write_byte_(cached_address_);
            i2c_write_byte_(0x40);
            i2c_write_byte_(static_cast<uint8_t>(value >> 4));
            i2c_write_byte_(static_cast<uint8_t>((value << 4) & 0xF0));
            i2c_stop_();
            if (ack) return;
            cached_address_ = 0; // Invalidate cache on NACK
        }

        // Candidate 7-bit addresses shifted for write form (addr << 1)
        static const uint8_t kAddresses[] = {
            static_cast<uint8_t>(0x60 << 1),
            static_cast<uint8_t>(0x61 << 1),
            static_cast<uint8_t>(0x62 << 1)
        };

        for (uint8_t addr : kAddresses) {
            i2c_start_();
            uint8_t ack = i2c_write_byte_(addr);

            // Fast mode write command (0x40): normal mode, no power-down
            i2c_write_byte_(0x40);
            i2c_write_byte_(static_cast<uint8_t>(value >> 4));
            i2c_write_byte_(static_cast<uint8_t>((value << 4) & 0xF0));
            i2c_stop_();

            if (ack) {
                // Device found and acknowledged
                cached_address_ = addr;
                break;
            }
        }
    }

private:
    static void i2c_delay_() {
        for (volatile int i = 0; i < 40; ++i) {
            __NOP();
        }
    }

    void i2c_start_() {
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_RESET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        i2c_delay_();
    }

    void i2c_stop_() {
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
        i2c_delay_();
    }

    uint8_t i2c_write_byte_(uint8_t byte) {
        for (uint8_t i = 0; i < 8; ++i) {
            if (byte & 0x80) {
                HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
            } else {
                HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_RESET);
            }
            byte <<= 1;
            i2c_delay_();
            HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
            i2c_delay_();
            HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
            i2c_delay_();
        }

        // Release SDA for slave ACK
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
        i2c_delay_();
        uint8_t ack = (HAL_GPIO_ReadPin(GPIOA, kI2cSdaPin) == GPIO_PIN_RESET) ? 1 : 0;
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        i2c_delay_();
        return ack;
    }

    uint16_t current_code_{0};
    uint8_t cached_address_{0};
};

}  // namespace mtr
