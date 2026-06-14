// SYS ESP32-S3 — main entry point.  15 FreeRTOS tasks, 2 queues.
// Architecture: architecture.md §8.

#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "config.h"

static const char* TAG = "sys";

// ── queues ─────────────────────────────────────────────────────────
static QueueHandle_t g_can_rx_queue    = nullptr;  // 16 deep, drop-if-full
static QueueHandle_t g_setpoint_queue  = nullptr;  //  4 deep, overwrite

// ── task handles ───────────────────────────────────────────────────
static TaskHandle_t h_can_rx, h_safety, h_dispatch, h_mode, h_motor;
static TaskHandle_t h_throttle, h_gear, h_brake, h_lights, h_dcdc;
static TaskHandle_t h_indicator, h_power, h_can_tx, h_diag, h_hb;

// ── task stubs (empty loops at correct rates, filled in later phases) ─

[[noreturn]] static void task_can_rx(void*) {
    while (1) { vTaskDelay(pdMS_TO_TICKS(10)); }
}
[[noreturn]] static void task_safety(void*) {
    const TickType_t period = pdMS_TO_TICKS(1000 / sys::kSafetyCheckHz);
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_dispatch(void*) {
    while (1) { vTaskDelay(pdMS_TO_TICKS(10)); }
}
[[noreturn]] static void task_mode(void*) {
    const TickType_t period = pdMS_TO_TICKS(100);  // 10 Hz
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_motor(void*) {
    const TickType_t period = pdMS_TO_TICKS(1000 / sys::kControlLoopHz);
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_throttle(void*) {
    const TickType_t period = pdMS_TO_TICKS(10);   // 100 Hz
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_gear(void*) {
    const TickType_t period = pdMS_TO_TICKS(1000 / sys::kGearCheckHz);
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_brake(void*) {
    const TickType_t period = pdMS_TO_TICKS(1000 / sys::kBrakeCmdRateHz);
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_lights(void*) {
    const TickType_t period = pdMS_TO_TICKS(50);   // 20 Hz
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_dcdc(void*) {
    const TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_indicator(void*) {
    const TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_power(void*) {
    const TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_can_tx(void*) {
    const TickType_t period = pdMS_TO_TICKS(200);  // 5 Hz
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_diag(void*) {
    const TickType_t period = pdMS_TO_TICKS(1000); // 1 Hz
    while (1) { vTaskDelayUntil(nullptr, period); }
}
[[noreturn]] static void task_hb(void*) {
    const TickType_t period = pdMS_TO_TICKS(sys::kHeartbeatIntervalMs);
    while (1) { vTaskDelayUntil(nullptr, period); }
}

// ───────────────────────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(TAG, "SYS ESP32-S3 initializing...");

    // 1. CAN driver
    ESP_LOGI(TAG, "CAN init...");

    // 2. Create queues
    g_can_rx_queue   = xQueueCreate(16, sizeof(can::Frame));
    g_setpoint_queue = xQueueCreate( 4, sizeof(int32_t)); // placeholder
    ESP_LOGI(TAG, "Queues created");

    // 3. Create tasks (priority, stack, period from architecture.md §8.7)
    xTaskCreate(task_can_rx,    "can_rx",    4096, nullptr, 5, &h_can_rx);
    xTaskCreate(task_safety,    "safety",    2048, nullptr, 5, &h_safety);
    xTaskCreate(task_dispatch,  "dispatch",  3072, nullptr, 4, &h_dispatch);
    xTaskCreate(task_mode,      "mode",      2048, nullptr, 4, &h_mode);
    xTaskCreate(task_motor,     "motor",     2048, nullptr, 4, &h_motor);
    xTaskCreate(task_throttle,  "throttle",  1536, nullptr, 3, &h_throttle);
    xTaskCreate(task_gear,      "gear",      1536, nullptr, 3, &h_gear);
    xTaskCreate(task_brake,     "brake",     2048, nullptr, 3, &h_brake);
    xTaskCreate(task_lights,    "lights",    1536, nullptr, 3, &h_lights);
    xTaskCreate(task_dcdc,      "dcdc",      1024, nullptr, 3, &h_dcdc);
    xTaskCreate(task_indicator, "indicator", 1024, nullptr, 2, &h_indicator);
    xTaskCreate(task_power,     "power",     1024, nullptr, 2, &h_power);
    xTaskCreate(task_can_tx,    "can_tx",    3072, nullptr, 2, &h_can_tx);
    xTaskCreate(task_diag,      "diag",      2048, nullptr, 1, &h_diag);
    xTaskCreate(task_hb,        "hb",        2048, nullptr, 1, &h_hb);

    ESP_LOGI(TAG, "Ready — 15 tasks running");
    vTaskDelete(nullptr);  // app_main returns, FreeRTOS scheduler owns the show
}
