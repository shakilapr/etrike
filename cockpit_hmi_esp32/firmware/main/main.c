/**
 * @file main.c
 * @brief Waveshare ESP32-S3 Cockpit HMI Firmware Entry Point
 *
 * Dual-core FreeRTOS architecture:
 * - CPU Core 0: Real-Time CAN Bus (TWAI) ingestion task @ 500 kbps
 * - CPU Core 1: LVGL 800x480 UI rendering task @ 60 FPS
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "board_waveshare_43b.h"
#include "can_decoder.h"
#include "ui_bridge.h"

static const char *TAG = "COCKPIT_MAIN";

/**
 * @brief High-priority TWAI CAN ingestion task pinned to CPU Core 0
 */
static void can_rx_task(void *arg) {
    ESP_LOGI(TAG, "CAN RX task started on Core %d", xPortGetCoreID());
    twai_message_t rx_msg;

    while (1) {
        esp_err_t res = twai_receive(&rx_msg, pdMS_TO_TICKS(100));
        if (res == ESP_OK) {
            can_decoder_process_frame(&rx_msg);
        } else if (res == ESP_ERR_TIMEOUT) {
            // Bus idle tick
        } else {
            ESP_LOGW(TAG, "TWAI receive error: 0x%x", res);
            twai_status_info_t status;
            twai_get_status_info(&status);
            if (status.state == TWAI_STATE_BUS_OFF) {
                ESP_LOGE(TAG, "TWAI Bus-Off detected, initiating auto-recovery...");
                twai_initiate_recovery();
            }
        }
    }
}

/**
 * @brief LVGL rendering and UI event loop pinned to CPU Core 1
 */
static void lvgl_ui_task(void *arg) {
    ESP_LOGI(TAG, "LVGL UI task started on Core %d", xPortGetCoreID());

    // Initialize LVGL library
    lv_init();

    // Initialize UI Bridge & load primary Cockpit view
    ui_bridge_init();

    vehicle_telemetry_t telemetry_snapshot;

    while (1) {
        // Fetch atomic snapshot of vehicle CAN telemetry
        can_decoder_get_telemetry(&telemetry_snapshot);

        // Update all UI widgets
        ui_bridge_update_telemetry(&telemetry_snapshot);

        // Run LVGL timer handler
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5) delay_ms = 5;
        if (delay_ms > 20) delay_ms = 20;

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=====================================================");
    ESP_LOGI(TAG, "   Waveshare ESP32-S3-Touch-LCD-4.3B Cockpit HMI    ");
    ESP_LOGI(TAG, "   Model: SKU 28141 | 800x480 IPS | CAN 2.0 (TWAI)  ");
    ESP_LOGI(TAG, "=====================================================");

    // 1. Initialize board hardware (Display, Touch, Expander, TWAI)
    ESP_ERROR_CHECK(board_hardware_init());

    // 2. Initialize CAN telemetry decoder
    can_decoder_init();

    // 3. Create real-time CAN RX task on Core 0 (Priority 10)
    xTaskCreatePinnedToCore(can_rx_task, "can_rx_task", 4096, NULL, 10, NULL, 0);

    // 4. Create LVGL rendering task on Core 1 (Priority 5)
    xTaskCreatePinnedToCore(lvgl_ui_task, "lvgl_ui_task", 8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "System initialization finished successfully.");
}
