// Minimal dual-board CAN smoke test for ESP32-S3 N16R8 + SN65HVD230.
//
// Build roles (platformio.ini envs):
//   role_rt  → COM9  TX id 0x100 every 200 ms, payload "RT" + seq
//   role_sys → COM5  TX id 0x200 every 200 ms, payload "SY" + seq
//
// Both roles: accept-all RX, print peer frames, log TEC/REC once/sec.
// Watch CANalyst-II low bus (CH1) for 0x100 / 0x200 — that proves wiring.
//
// Optional: -D TWAI_SWAP_TX_RX=1 if CTX/CRX wires may be crossed.

#include <cstdio>
#include <cstring>
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef BOARD_ROLE
#define BOARD_ROLE 0  // 0=RT 1=SYS
#endif
#ifndef TWAI_SWAP_TX_RX
#define TWAI_SWAP_TX_RX 0
#endif

#if TWAI_SWAP_TX_RX
constexpr gpio_num_t kTxGpio = GPIO_NUM_4;
constexpr gpio_num_t kRxGpio = GPIO_NUM_5;
#else
constexpr gpio_num_t kTxGpio = GPIO_NUM_5;
constexpr gpio_num_t kRxGpio = GPIO_NUM_4;
#endif

constexpr int kBitrate = 500'000;
constexpr uint32_t kTxId = (BOARD_ROLE == 0) ? 0x100u : 0x200u;
constexpr const char* kRole = (BOARD_ROLE == 0) ? "RT" : "SYS";
constexpr const char* kTag = "can-smoke";

static twai_timing_config_t timing_500k() {
    twai_timing_config_t t{};
    t.quanta_resolution_hz = 8'000'000;
    t.tseg_1 = 11;
    t.tseg_2 = 4;
    t.sjw = 2;
    return t;
}

static bool twai_start_normal() {
    twai_stop();
    twai_driver_uninstall();

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(
        0, kTxGpio, kRxGpio, TWAI_MODE_NORMAL);
    g.tx_queue_len = 8;
    g.rx_queue_len = 16;
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_timing_config_t t = timing_500k();

    if (twai_driver_install(&g, &t, &f) != ESP_OK) {
        ESP_LOGE(kTag, "twai_driver_install failed");
        return false;
    }
    if (twai_start() != ESP_OK) {
        ESP_LOGE(kTag, "twai_start failed");
        twai_driver_uninstall();
        return false;
    }
    return true;
}

// Phase 0: controller self-test (no ACK / no transceiver required).
static bool self_test_controller() {
    ESP_LOGI(kTag, "--- phase0: TWAI NO_ACK self-test (chip only) ---");
    twai_stop();
    twai_driver_uninstall();

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(
        0, kTxGpio, kRxGpio, TWAI_MODE_NO_ACK);
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_timing_config_t t = timing_500k();
    if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
        ESP_LOGE(kTag, "self-test install/start FAILED");
        return false;
    }

    twai_message_t tx{};
    tx.identifier = 0x7E0;
    tx.data_length_code = 2;
    tx.data[0] = 'T';
    tx.data[1] = '0';
    esp_err_t e = twai_transmit(&tx, pdMS_TO_TICKS(100));
    if (e != ESP_OK) {
        ESP_LOGE(kTag, "self-test TX failed: %s", esp_err_to_name(e));
        return false;
    }
    ESP_LOGI(kTag, "self-test TX OK — controller + pins driven");
    return true;
}

extern "C" void app_main() {
    ESP_LOGI(kTag, "=== CAN SMOKE role=%s TX=GPIO%d RX=GPIO%d id=0x%03lX @500k swap=%d ===",
             kRole, static_cast<int>(kTxGpio), static_cast<int>(kRxGpio),
             static_cast<unsigned long>(kTxId), TWAI_SWAP_TX_RX);

    if (!self_test_controller()) {
        ESP_LOGE(kTag, "Controller self-test failed — stop");
        return;
    }

    ESP_LOGI(kTag, "--- phase1: NORMAL bus traffic ---");
    if (!twai_start_normal()) {
        ESP_LOGE(kTag, "NORMAL mode start failed — stop");
        return;
    }
    ESP_LOGI(kTag, "TWAI NORMAL ready — TX id=0x%03lX every 200ms",
             static_cast<unsigned long>(kTxId));

    uint32_t seq = 0;
    uint32_t tx_ok = 0, tx_fail = 0, rx_n = 0;
    int64_t next_tx = esp_timer_get_time() + 200'000;
    int64_t next_stat = esp_timer_get_time() + 1'000'000;
    int64_t last_recovery = 0;

    while (true) {
        twai_message_t rx{};
        if (twai_receive(&rx, 0) == ESP_OK) {
            rx_n++;
            ESP_LOGI(kTag, "RX id=0x%03lX dlc=%u data=%02X %02X %02X %02X",
                     static_cast<unsigned long>(rx.identifier),
                     rx.data_length_code,
                     rx.data[0], rx.data[1], rx.data[2], rx.data[3]);
        }

        const int64_t now = esp_timer_get_time();
        if (now >= next_tx) {
            twai_message_t tx{};
            tx.identifier = kTxId;
            tx.data_length_code = 4;
            tx.data[0] = static_cast<uint8_t>(kRole[0]);
            tx.data[1] = static_cast<uint8_t>(kRole[1]);
            tx.data[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
            tx.data[3] = static_cast<uint8_t>(seq & 0xFF);
            seq++;

            if (twai_transmit(&tx, pdMS_TO_TICKS(50)) == ESP_OK) {
                tx_ok++;
            } else {
                tx_fail++;
                twai_status_info_t info{};
                if (twai_get_status_info(&info) == ESP_OK) {
                    if (tx_fail <= 5 || (tx_fail % 25) == 0) {
                        ESP_LOGW(kTag, "TX fail n=%lu state=%d tec=%lu rec=%lu",
                                 static_cast<unsigned long>(tx_fail),
                                 static_cast<int>(info.state),
                                 static_cast<unsigned long>(info.tx_error_counter),
                                 static_cast<unsigned long>(info.rx_error_counter));
                    }
                    // Recover from bus-off (state 2) every 1s max
                    if ((info.state == TWAI_STATE_BUS_OFF || info.tx_error_counter >= 128)
                        && (now - last_recovery) > 1'000'000) {
                        last_recovery = now;
                        ESP_LOGW(kTag, "bus recover → reinstall NORMAL");
                        twai_start_normal();
                    }
                }
            }
            next_tx = now + 200'000;
        }

        if (now >= next_stat) {
            twai_status_info_t info{};
            twai_get_status_info(&info);
            ESP_LOGI(kTag, "STAT role=%s tx_ok=%lu tx_fail=%lu rx=%lu tec=%lu rec=%lu state=%d",
                     kRole,
                     static_cast<unsigned long>(tx_ok),
                     static_cast<unsigned long>(tx_fail),
                     static_cast<unsigned long>(rx_n),
                     static_cast<unsigned long>(info.tx_error_counter),
                     static_cast<unsigned long>(info.rx_error_counter),
                     static_cast<int>(info.state));
            next_stat = now + 1'000'000;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
