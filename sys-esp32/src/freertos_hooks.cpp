// FreeRTOS runtime hooks — stack overflow + malloc failed.
// These are called by the FreeRTOS kernel when fatal conditions are detected.
// Both log the error then spin forever (safe state: no further task scheduling).

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "freertos";

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    ESP_LOGE(TAG, "STACK OVERFLOW: task=%s", pcTaskName ? pcTaskName : "?");
    for (;;) {}
}

extern "C" void vApplicationMallocFailedHook(void) {
    ESP_LOGE(TAG, "MALLOC FAILED -- heap exhausted");
    for (;;) {}
}
