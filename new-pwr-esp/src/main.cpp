// new-pwr-esp — Dedicated DCDC Power Supply Keepalive Controller
// Target: ESP32-WROOM-32UE
// Transceiver: WCMCU-230 (SN65HVD230)
// Pins: Printed TX (GPIO 1) & RX (GPIO 3)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "config.h"
#include "can_driver.h"

[[noreturn]] static void task_keepalive(void*) {
    uint32_t tick_10ms_count = 0;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_10ms = pdMS_TO_TICKS(10);

    while (true) {
        vTaskDelayUntil(&last_wake_time, period_10ms);

        // ── Power Supply / DCDC Keepalive (10 Hz / 100 ms, 29-bit Extended CAN 0x10262B27) ──
        if (tick_10ms_count % 10 == 0) {
            can::Frame dcdc_cmd = can::Frame::extended_frame(0x10262B27u, 8);
            dcdc_cmd.data = {0xFD, 0x78, 0x00, 0xC8, 0x00, 0xFF, 0xFF, 0xFC};
            send_can_frame(dcdc_cmd);

            // Toggle onboard LED pulse every transmit (100 ms) for visual liveness
            static bool s_led_state = false;
            s_led_state = !s_led_state;
            gpio_set_level(static_cast<gpio_num_t>(pwr::kStatusLedGpio), s_led_state ? 1 : 0);
        }

        tick_10ms_count++;
    }
}

extern "C" void app_main() {
    // Configure status LED output
    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = (1ULL << pwr::kStatusLedGpio);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(static_cast<gpio_num_t>(pwr::kStatusLedGpio), 0);

    // Initialize TWAI CAN controller on the printed TX/RX pins
#ifdef CONFIG_CAN_SELF_TEST
    const bool self_test = true;
#else
    const bool self_test = false;
#endif

    g_can.init(pwr::kCanTxGpio, pwr::kCanRxGpio, pwr::kCanBitrateHz, self_test);

    // Run the 100 Hz / 10 ms periodic keepalive task
    xTaskCreatePinnedToCore(task_keepalive, "pwr_keepalive", 3072, nullptr, 5, nullptr, 0);
}
