#pragma once
// Compatibility shims for host-testing on older compilers.
// ESP-IDF (GCC 8+) has proper C++17 std::clamp.
// This file is ONLY included in test builds, never in firmware.

// std::clamp fallback for GCC < 7
#if __GNUC__ < 7
#include <algorithm>
namespace std {
template<typename T>
constexpr T clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}
}
#endif

// ESP-IDF stubs for host compilation
#define ESP_LOGE(tag, fmt, ...)  (void)0
#define ESP_LOGW(tag, fmt, ...)  (void)0
#define ESP_LOGI(tag, fmt, ...)  (void)0
#define ESP_LOGD(tag, fmt, ...)  (void)0
#define ESP_LOGV(tag, fmt, ...)  (void)0
