// CAN-TEST — minimal TWAI send/receive test (no RTOS tasks, bare loop).
// Flashes to ESP32-S3. TX=GPIO5, RX=GPIO4, 500 kbit/s.
// Prints received frames, sends a test frame every 500ms, tracks errors.
// Connect two boards via CAN transceiver + twisted pair + 120Ω termination.
// If only one board: place CAN transceiver in loopback mode or expect TX failures.

#include <cstdio>
#include <cstring>
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ── TWAI config ──────────────────────────────────────────────────────────
constexpr gpio_num_t kTxGpio = GPIO_NUM_5;
constexpr gpio_num_t kRxGpio = GPIO_NUM_4;
constexpr int        kBitrate = 500'000;  // 500 kbit/s

// Timing for 500 kbit/s @ 8 MHz resolution
// TQ = 1/8MHz = 125ns.  Bit time = 2µs = 16 TQ.
// Sync=1, PropSeg=7, PS1=4, PS2=4, SJW=2 → total 16 TQ.
static const twai_timing_config_t kTiming = {
    .quanta_resolution_hz = 8'000'000,
    .tseg_1 = 11,   // PropSeg + PS1 = 7+4 = 11
    .tseg_2 = 4,    // PS2 = 4
    .sjw    = 2,
};

// ── helpers ───────────────────────────────────────────────────────────────

static void print_frame(const char* dir, const twai_message_t& msg) {
    printf("  %-4s id=0x%03lX dlc=%d ext=%d data=",
           dir, (unsigned long)msg.identifier, msg.data_length_code, msg.extd);
    for (int i = 0; i < msg.data_length_code && i < 8; ++i)
        printf("%02X ", msg.data[i]);
    printf("\n");
}

static void print_error_counters() {
    twai_status_info_t info;
    if (twai_get_status_info(&info) == ESP_OK) {
        printf("  [TEC=%3lu REC=%3lu tx_q=%lu rx_q=%lu tx_fail=%lu rx_miss=%lu bus_err=%lu arb_lost=%lu]\n",
               (unsigned long)info.tx_error_counter, (unsigned long)info.rx_error_counter,
               (unsigned long)info.msgs_to_tx, (unsigned long)info.msgs_to_rx,
               (unsigned long)info.tx_failed_count, (unsigned long)info.rx_missed_count,
               (unsigned long)info.bus_error_count, (unsigned long)info.arb_lost_count);
    }
}

// ── app_main — bare loop (FreeRTOS init only, no tasks) ──────────────────

extern "C" void app_main() {
    printf("\n=== CAN-TEST: TWAI TX=%d RX=%d @ %d kbit/s ===\n\n",
           kTxGpio, kRxGpio, kBitrate / 1000);

    // 1. Install TWAI driver
    twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT_V2(
        0, kTxGpio, kRxGpio, TWAI_MODE_NORMAL);
    twai_filter_config_t f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_cfg, &kTiming, &f_cfg);
    if (err != ESP_OK) {
        printf("FATAL: twai_driver_install failed: %s\n", esp_err_to_name(err));
        return;
    }
    printf("[init] driver installed\n");

    err = twai_start();
    if (err != ESP_OK) {
        printf("FATAL: twai_start failed: %s\n", esp_err_to_name(err));
        return;
    }
    printf("[init] TWAI started\n\n");

    // 2. Main loop — receive + periodic transmit
    uint32_t seq = 0;
    int64_t  next_tx_us = esp_timer_get_time() + 1'000'000;  // first TX at 1s
    int      rx_count = 0, tx_count = 0, tx_fail = 0;
    int64_t  last_status_us = 0;

    while (1) {
        // ── Receive (non-blocking poll) ──────────────────────────────
        twai_message_t rx_msg;
        if (twai_receive(&rx_msg, 0) == ESP_OK) {
            rx_count++;
            print_frame("RX", rx_msg);
        }

        // ── Transmit every 500ms (after first 1s) ────────────────────
        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_tx_us) {
            twai_message_t tx_msg = {};
            tx_msg.identifier       = 0x555;  // test CAN ID
            tx_msg.data_length_code = 4;
            tx_msg.data[0] = (seq >> 24) & 0xFF;
            tx_msg.data[1] = (seq >> 16) & 0xFF;
            tx_msg.data[2] = (seq >>  8) & 0xFF;
            tx_msg.data[3] = (seq      ) & 0xFF;
            seq++;

            err = twai_transmit(&tx_msg, pdMS_TO_TICKS(50));
            if (err == ESP_OK) {
                tx_count++;
                print_frame("TX", tx_msg);
            } else {
                tx_fail++;
                if (tx_fail <= 3 || tx_fail % 100 == 0) {
                    printf("  TX-FAIL #%d: %s\n", tx_fail, esp_err_to_name(err));
                }
            }

            next_tx_us = now_us + 500'000;  // every 500ms
        }

        // ── Status every 5s ──────────────────────────────────────────
        if (now_us - last_status_us > 5'000'000) {
            print_error_counters();
            printf("  [rx=%d tx=%d fail=%d]\n\n", rx_count, tx_count, tx_fail);
            last_status_us = now_us;
        }

        vTaskDelay(pdMS_TO_TICKS(1));  // 1ms poll interval
    }
}
