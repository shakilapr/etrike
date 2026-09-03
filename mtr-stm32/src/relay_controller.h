#pragma once
// MTR STM32G431 — Active-Low Relay Actuator Controller
// Controls Ignition, Drive, and Reverse relays with strict mutual exclusion.

#include <cstdint>
#include "stm32g4xx_hal.h"
#include "config.h"
#include "protocol/compat/can.hpp"

namespace mtr {

class RelayController {
public:
    enum class State : uint8_t {
        Off = 0,     // All relays de-energized
        Park = 1,    // Ignition ON, Drive OFF, Reverse OFF (Neutral)
        Drive = 2,   // Ignition ON, Drive ON,  Reverse OFF
        Reverse = 3  // Ignition ON, Drive OFF, Reverse ON
    };

    RelayController() = default;

    // Initialize GPIO pins into safe default state (all relays OFF)
    void init() {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();

        // Write SET (OFF) before configuring pins to avoid boot glitch
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

        apply_state_(State::Off);
    }

    // Set state with hardware mutual exclusion
    void set_state(State new_state) {
        if (state_ == new_state) return;
        apply_state_(new_state);
        // Toggle PC6 status LED on state transition
        HAL_GPIO_TogglePin(GPIOC, kLedPin);
    }

    // Direct mapping from canonical CAN Gear enum
    void set_gear(can::Gear gear, bool ignition_on) {
        if (!ignition_on) {
            set_state(State::Off);
            return;
        }
        switch (gear) {
        case can::Gear::D:
        case can::Gear::S:
            set_state(State::Drive);
            break;
        case can::Gear::R:
            set_state(State::Reverse);
            break;
        case can::Gear::N:
        default:
            set_state(State::Park);
            break;
        }
    }

    State state() const { return state_; }

    can::Gear current_gear() const {
        switch (state_) {
        case State::Drive:   return can::Gear::D;
        case State::Reverse: return can::Gear::R;
        default:             return can::Gear::N;
        }
    }

    bool is_ignition_on() const {
        return state_ != State::Off;
    }

private:
    void apply_state_(State s) {
        state_ = s;
        switch (s) {
        case State::Drive:
            // Ignition ON (RESET), Drive ON (RESET), Reverse OFF (SET)
            HAL_GPIO_WritePin(GPIOA, kRelayRevPin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOA, kRelayIgnitionPin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, kRelayDrivePin, GPIO_PIN_RESET);
            break;
        case State::Reverse:
            // Ignition ON (RESET), Drive OFF (SET), Reverse ON (RESET)
            HAL_GPIO_WritePin(GPIOA, kRelayDrivePin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOA, kRelayIgnitionPin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, kRelayRevPin, GPIO_PIN_RESET);
            break;
        case State::Park:
            // Ignition ON (RESET), Drive OFF (SET), Reverse OFF (SET)
            HAL_GPIO_WritePin(GPIOA, kRelayDrivePin | kRelayRevPin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOA, kRelayIgnitionPin, GPIO_PIN_RESET);
            break;
        case State::Off:
        default:
            // All Relays OFF (SET)
            HAL_GPIO_WritePin(GPIOA, kRelayRevPin | kRelayDrivePin | kRelayIgnitionPin, GPIO_PIN_SET);
            break;
        }
    }

    State state_{State::Off};
};

}  // namespace mtr
