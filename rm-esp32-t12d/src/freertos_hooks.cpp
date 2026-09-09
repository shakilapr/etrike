// FreeRTOS runtime hooks — stack overflow + malloc failed.
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
