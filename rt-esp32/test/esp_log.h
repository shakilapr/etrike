#pragma once
// ESP-IDF logging stubs for host testing.
#include <cstdio>
#include "stubs.h"

#define ESP_LOGE(tag, ...) do { std::fprintf(stderr, "E [%s] ", tag); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while(0)
#define ESP_LOGW(tag, ...) do { std::fprintf(stderr, "W [%s] ", tag); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while(0)
#define ESP_LOGI(tag, ...) do { std::fprintf(stderr, "I [%s] ", tag); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while(0)
#define ESP_LOGD(tag, ...) do {} while(0)
#define ESP_ERROR_CHECK(x) (x)
