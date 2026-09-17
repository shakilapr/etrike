#pragma once
// MTR STM32G431 — Sequential Relay Tester
// Cycles Ignition, Drive, and Reverse relays ON and OFF in strict order.
// Enforces hardware mutual exclusion between Drive and Reverse.

#include <cstdint>
#include "stm32g4xx_hal.h"
#include "config.h"

namespace mtr_test {

class RelayTester {
public:
    enum class Step : uint8_t {
        IgnitionOn = 0,
        IgnitionOffPause,
        DriveOn,
        DriveOffPause,
        ReverseOn,
        ReverseOffPause,
        _Count
    };

    RelayTester() = default;

    void init() {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();

        // Write SET (OFF) to relays before configuring output mode to avoid boot glitch
        HAL_GPIO_WritePin(GPIOA, kRelayRevPin | kRelayDrivePin | kRelayIgnitionPin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOC, kLedPin, GPIO_PIN_SET);

        GPIO_InitTypeDef gpio{};
        gpio.Pin = kRelayRevPin | kRelayDrivePin | kRelayIgnitionPin;
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &gpio);

        gpio.Pin = kLedPin;
        HAL_GPIO_Init(GPIOC, &gpio);

        all_relays_off_();
        current_step_ = Step::IgnitionOn;
        step_start_ms_ = 0;
    }

    void tick(uint32_t now_ms) {
        if (step_start_ms_ == 0) {
            step_start_ms_ = now_ms;
            apply_step_(current_step_);
            return;
        }

        uint32_t step_duration = is_relay_on_step_(current_step_) ? kRelayOnTimeMs : kRelayOffTimeMs;

        if (now_ms - step_start_ms_ >= step_duration) {
            step_start_ms_ = now_ms;
            // Advance to next step in order
            uint8_t next = (static_cast<uint8_t>(current_step_) + 1) % static_cast<uint8_t>(Step::_Count);
            current_step_ = static_cast<Step>(next);
            apply_step_(current_step_);
        }
    }

    Step current_step() const { return current_step_; }

    const char* current_step_name() const {
        switch (current_step_) {
        case Step::IgnitionOn:       return "Ignition ON";
        case Step::IgnitionOffPause: return "Ignition OFF (Pause)";
        case Step::DriveOn:          return "Drive ON";
        case Step::DriveOffPause:    return "Drive OFF (Pause)";
        case Step::ReverseOn:        return "Reverse ON";
        case Step::ReverseOffPause:  return "Reverse OFF (Pause)";
        default:                     return "Unknown";
        }
    }

private:
    static bool is_relay_on_step_(Step s) {
        return (s == Step::IgnitionOn || s == Step::DriveOn || s == Step::ReverseOn);
    }

    void all_relays_off_() {
        // Active-Low: SET = Relay OFF
        HAL_GPIO_WritePin(GPIOA, kRelayRevPin | kRelayDrivePin | kRelayIgnitionPin, GPIO_PIN_SET);
    }

    void apply_step_(Step s) {
        // First ensure all relays are off to guarantee break-before-make
        all_relays_off_();

        switch (s) {
        case Step::IgnitionOn:
            // Ignition ON (RESET), Drive OFF (SET), Reverse OFF (SET)
            HAL_GPIO_WritePin(GPIOA, kRelayIgnitionPin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOC, kLedPin, GPIO_PIN_RESET); // LED ON
            break;

        case Step::DriveOn:
            // Ignition OFF (SET), Drive ON (RESET), Reverse OFF (SET)
            HAL_GPIO_WritePin(GPIOA, kRelayDrivePin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOC, kLedPin, GPIO_PIN_RESET); // LED ON
            break;

        case Step::ReverseOn:
            // Ignition OFF (SET), Drive OFF (SET), Reverse ON (RESET)
            HAL_GPIO_WritePin(GPIOA, kRelayRevPin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOC, kLedPin, GPIO_PIN_RESET); // LED ON
            break;

        case Step::IgnitionOffPause:
        case Step::DriveOffPause:
        case Step::ReverseOffPause:
        default:
            // All relays OFF (SET), LED OFF (SET)
            HAL_GPIO_WritePin(GPIOC, kLedPin, GPIO_PIN_SET);
            break;
        }
    }

    Step current_step_{Step::IgnitionOn};
    uint32_t step_start_ms_{0};
};

}  // namespace mtr_test
