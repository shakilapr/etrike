/*
 * esp_err.h — Host stub for ESP-IDF error types.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;

#define ESP_OK                    0
#define ESP_FAIL                  -1
#define ESP_ERR_INVALID_ARG       -2
#define ESP_ERR_INVALID_STATE     -3
#define ESP_ERR_TIMEOUT           -4
#define ESP_ERR_NO_MEM            -5
#define ESP_ERR_NOT_SUPPORTED     -6

#ifdef __cplusplus
}
#endif
