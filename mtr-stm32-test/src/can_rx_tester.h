#pragma once
// MTR STM32G431 test firmware — CAN Receive Path Tester (TEST_MODE 2).
// Passive listener: accepts all standard frames and reports bus reception
// activity on the PC6 status LED. Relays are held OFF; the LED is owned
// entirely by this tester. Verifies PA11 / transceiver RX stage / wiring
// independently of the production firmware.
//
// LED coding:
//   Solid ON   — frames are being received (traffic within the last
//                kRxActivityWindowMs)
//   Slow blink — FDCAN up and listening, but no frames received
//                (1 Hz, 500 ms on / 500 ms off)
//   Fast blink — FDCAN init failure
//                (5 Hz, 100 ms on / 100 ms off)

#include <cstdint>
#include "stm32g4xx_hal.h"
#include "config.h"
#include "can_bus.h"

namespace mtr_test {

class CanRxTester {
public:
    void init() {
        init_gpio_();

        can_ok_ = bus_.init(true);
        led_ = can_ok_ ? Led::Listening : Led::Fail;
    }

    // Call every loop pass (~1 ms)
    void tick(uint32_t now_ms) {
        if (can_ok_) {
            bus_.service();

            // Drain the RX FIFO; any frame counts as traffic
            uint32_t id = 0;
            uint8_t dlc = 0;
            while (bus_.poll_rx(id, dlc)) {
                last_rx_ms_ = now_ms;
                if (++rx_count_ == 0u) {
                    ++rx_count_;  // keep nonzero on wrap
                }
            }

            if (rx_count_ != 0u && (now_ms - last_rx_ms_) <= kRxActivityWindowMs) {
                led_ = Led::Traffic;
            } else {
                led_ = Led::Listening;
            }
        }

        render_led_(now_ms);
    }

private:
    enum class Led : uint8_t { Traffic, Listening, Fail };

    void init_gpio_() {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();

        HAL_GPIO_WritePin(GPIOA, kRelayRevPin | kRelayDrivePin | kRelayIgnitionPin, GPIO_PIN_SET);
        GPIO_InitTypeDef gpio{};
        gpio.Pin = kRelayRevPin | kRelayDrivePin | kRelayIgnitionPin;
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &gpio);

        HAL_GPIO_WritePin(GPIOC, kLedPin, GPIO_PIN_SET);
        gpio.Pin = kLedPin;
        HAL_GPIO_Init(GPIOC, &gpio);
    }

    void render_led_(uint32_t now_ms) {
        bool on = false;
        switch (led_) {
        case Led::Traffic:   on = true; break;
        case Led::Listening: on = (now_ms % 1000u) < 500u; break;  // 1 Hz
        case Led::Fail:      on = (now_ms % 200u) < 100u; break;   // 5 Hz
        }
        HAL_GPIO_WritePin(GPIOC, kLedPin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }

    CanBus bus_{};
    bool can_ok_{false};
    uint32_t last_rx_ms_{0};
    uint32_t rx_count_{0};
    Led led_{Led::Fail};
};

}  // namespace mtr_test
