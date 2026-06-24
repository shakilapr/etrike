/*
 * esp_timer.h — Host stub for ESP-IDF high-resolution timer.
 *
 * esp_timer_get_time() returns microseconds since boot.
 * On host we use std::chrono::steady_clock.
 */
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns monotonic microseconds since an arbitrary epoch.
 * Resolution: ~1 µs on most platforms.
 */
int64_t esp_timer_get_time(void);

/*
 * Test harness: advance the simulated clock by `us` microseconds.
 * Returns the new time.  Pass 0 to read without advancing.
 */
int64_t esp_timer_test_advance(int64_t us);
void    esp_timer_test_reset(void);   // reset to zero

#ifdef __cplusplus
}
#endif
