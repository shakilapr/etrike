#pragma once
// MTR STM32G431 — Robust Software I2C Driver for MCP4725 12-Bit DAC
// Works reliably across ISO1540 isolator boundaries (tolerates Side-1 VOL offset).
// Sends DAC updates to candidate addresses 0x60, 0x61, 0x62, 0x63 without aborting on NACK.

#include <cstdint>
#include <algorithm>
#include "stm32g4xx_hal.h"
#include "config.h"

namespace mtr_test {

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

        // Allow 100 ms for ISO1540 and MCP4725 5.0 V rail to stabilize after power-up
        HAL_Delay(100);

        // Power-on bus recovery: clock 9 pulses with SDA high to release any hung slave
        bus_recover_();

        current_code_ = 0xFFFF;
        write_dac_raw(0); // Power up at 0.0 V
        current_code_ = 0;
    }

    // Direct MCP4725 write routine:
    // Sends DAC update to candidate addresses (0x60, 0x61, 0x62, 0x63).
    // Does NOT abort on NACK so ISO1540 Side-1 level offsets (~0.7V) do not block writes.
    bool write_dac_raw(uint16_t value) {
        if (value > 4095) value = 4095;

        static constexpr uint8_t kCandidateAddresses[] = {
            static_cast<uint8_t>(0x60 << 1), // 0xC0 (A0 = GND)
            static_cast<uint8_t>(0x61 << 1), // 0xC2 (A0 = VCC, Adafruit default)
            static_cast<uint8_t>(0x62 << 1), // 0xC4 (A1 variant)
            static_cast<uint8_t>(0x63 << 1)  // 0xC6 (A1 variant with A0 = VCC)
        };

        for (uint8_t addr : kCandidateAddresses) {
            write_frame_(addr, value);
        }

        current_code_ = value;
        return true;
    }

    uint16_t current_code() const { return current_code_; }

private:
    // ~100 NOPs at 16 MHz HSI gives ~6.25 µs half-clock (~80 kHz I2C),
    // ensuring complete rise-time across pull-up resistors and isolator channels.
    static void i2c_delay_() {
        for (volatile uint32_t i = 0; i < 100; ++i) {
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
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        i2c_delay_();
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
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_RESET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
        i2c_delay_();
    }

    bool i2c_write_byte_(uint8_t byte) {
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
        HAL_GPIO_WritePin(GPIOA, kI2cSdaPin, GPIO_PIN_SET);
        i2c_delay_();
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_SET);
        i2c_delay_();
        bool ack = (HAL_GPIO_ReadPin(GPIOA, kI2cSdaPin) == GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOA, kI2cSclPin, GPIO_PIN_RESET);
        i2c_delay_();
        return ack;
    }

    // Full 4-byte MCP4725 write sequence:
    // Byte 1: I2C Address (Write)
    // Byte 2: Command 0x40 (Write DAC Register)
    // Byte 3: Data Bits D11..D4 (High Byte)
    // Byte 4: Data Bits D3..D0  (Low Byte << 4)
    void write_frame_(uint8_t addr, uint16_t value) {
        i2c_start_();
        i2c_write_byte_(addr);
        i2c_write_byte_(0x40);
        i2c_write_byte_(static_cast<uint8_t>((value >> 4) & 0xFF));
        i2c_write_byte_(static_cast<uint8_t>((value << 4) & 0xF0));
        i2c_stop_();
    }

    uint16_t current_code_{0};
};

}  // namespace mtr_test
