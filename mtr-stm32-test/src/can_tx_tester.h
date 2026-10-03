#pragma once
// MTR STM32G431 test firmware — CAN Transmit Path Tester (TEST_MODE 1).
// Sends one test frame per second and reports the FDCAN transmit outcome on
// the PC6 status LED — the only output channel available while the TX path
// itself is broken (a failing frame cannot carry its own error report onto
// the bus). Relays are held OFF; the LED is owned entirely by this tester.
//
// LED verdict coding (judge >= 3 s after power-on; LED starts solid):
//   Solid ON   — frame transmitted, TXBRP drained, TEC unchanged, LEC clean
//                => TX path proven; re-enable publishing in mtr-stm32
//   Slow blink — LEC 3: ACK error (frame reached the bus, nobody
//                acknowledged — partner missing, not ACK-capable, or
//                termination wrong)
//                (1 Hz, 500 ms on / 500 ms off)
//   Fast blink — LEC 1/2/4/5/6 or stuck TX pending or FDCAN init failure:
//                bus-level error — transceiver TX stage dead, silent, or
//                miswired (PA12 -> TXD)  (5 Hz, 100 ms on / 100 ms off)
//   Dark       — Bus-Off latched (controller disconnected itself)

#include <cstdint>
#include "stm32g4xx_hal.h"
#include "config.h"
#include "can_bus.h"

namespace mtr_test {

class CanTxTester {
public:
    void init() {
        init_gpio_();

        can_ok_ = bus_.init(false);
        led_ = can_ok_ ? Led::Solid : Led::Fast;  // Fast = FDCAN init failure
    }

    // Call every loop pass (~1 ms)
    void tick(uint32_t now_ms) {
        if (can_ok_) {
            bus_.service();
        }

        render_led_(now_ms);

        if (!can_ok_) {
            return;
        }

        if (awaiting_) {
            // One 500 kbit/s frame completes in ~250 us; 10 ms also lets
            // service() refresh the cached health with this frame's outcome.
            if (now_ms - sent_ms_ >= kTxTestSampleMs) {
                awaiting_ = false;
                const auto h = bus_.health();
                const bool clean_lec = (h.lec == 0u || h.lec == 7u);
                if (h.bus_off) {
                    led_ = Led::Dark;
                } else if (!bus_.tx_pending() && clean_lec && h.tec <= tec_at_send_) {
                    led_ = Led::Solid;
                } else if (h.lec == 3u) {
                    led_ = Led::Slow;
                } else {
                    led_ = Led::Fast;
                }
            }
            return;
        }

        if (now_ms - last_send_ms_ >= kTxTestPeriodMs) {
            last_send_ms_ = now_ms;
            tec_at_send_ = bus_.health().tec;
            if (bus_.send(kTxTestFrameId, kTxTestFrameDlc)) {
                awaiting_ = true;
            } else {
                led_ = Led::Fast;  // TX FIFO rejected the frame
            }
        }
    }

private:
    enum class Led : uint8_t { Solid, Slow, Fast, Dark };

    void init_gpio_() {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();

        // Relays to safe OFF before configuring outputs (no boot glitch)
        HAL_GPIO_WritePin(GPIOA, kRelayRevPin | kRelayDrivePin | kRelayIgnitionPin, GPIO_PIN_SET);
        GPIO_InitTypeDef gpio{};
        gpio.Pin = kRelayRevPin | kRelayDrivePin | kRelayIgnitionPin;
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &gpio);

        // Status LED, active-low; starts solid (provisional until first verdict)
        HAL_GPIO_WritePin(GPIOC, kLedPin, GPIO_PIN_RESET);
        gpio.Pin = kLedPin;
        HAL_GPIO_Init(GPIOC, &gpio);
    }

    void write_led_(bool on) {
        HAL_GPIO_WritePin(GPIOC, kLedPin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }

    void render_led_(uint32_t now_ms) {
        switch (led_) {
        case Led::Solid: write_led_(true); break;
        case Led::Dark:  write_led_(false); break;
        case Led::Slow:  write_led_((now_ms % 1000u) < 500u); break;  // 1 Hz
        case Led::Fast:  write_led_((now_ms % 200u) < 100u); break;   // 5 Hz
        }
    }

    CanBus bus_{};
    bool can_ok_{false};
    bool awaiting_{false};
    uint32_t last_send_ms_{0};
    uint32_t sent_ms_{0};
    uint8_t tec_at_send_{0};
    Led led_{Led::Solid};
};

}  // namespace mtr_test
