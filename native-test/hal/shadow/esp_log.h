/*
 * esp_log.h — Host stub for ESP-IDF logging.
 *
 * Routes ESP_LOGx macros to stderr with tag prefix and level.  Keeping logs
 * off stdout preserves the simulator's JSON-Lines IPC contract.
 */
#pragma once

#include <cstdio>
#include <cstdarg>

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_log_level_t;

#define ESP_LOG_NONE    0
#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5

inline void _host_esp_log(const char* level, const char* tag, const char* fmt, ...) {
    fprintf(stderr, "[%s] %s: ", level, tag ? tag : "-");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

#define ESP_LOGE(tag, fmt, ...)  _host_esp_log("E", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...)  _host_esp_log("W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...)  _host_esp_log("I", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)  _host_esp_log("D", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...)  _host_esp_log("V", tag, fmt, ##__VA_ARGS__)
#define ESP_ERROR_CHECK(x)       (void)(x)

#ifdef __cplusplus
}
#endif
