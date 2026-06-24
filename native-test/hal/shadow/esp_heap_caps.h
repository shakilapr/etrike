/*
 * esp_heap_caps.h — Host stub for ESP-IDF heap capability queries.
 */
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define MALLOC_CAP_DEFAULT   0
#define MALLOC_CAP_8BIT      1
#define MALLOC_CAP_32BIT     2
#define MALLOC_CAP_INTERNAL  4
#define MALLOC_CAP_SPIRAM    8

inline uint32_t esp_get_free_heap_size(void)  { return 512 * 1024 * 1024; }  // 512 MB
inline uint32_t esp_get_minimum_free_heap_size(void) { return 256 * 1024 * 1024; }

#ifdef __cplusplus
}
#endif
