/*
 * esp_timer_stubs.cpp — Host implementation using std::chrono.
 *
 * Supports two modes:
 *   1. Wall clock (default) — esp_timer_get_time() returns real microseconds.
 *   2. Simulated clock — test harness calls esp_timer_test_advance() to
 *      control time precisely.
 */
#include "esp_timer.h"
#include <chrono>
#include <mutex>
#include <cstdint>

static std::mutex g_time_mutex;
static bool       g_simulated = false;
static int64_t    g_sim_time_us = 0;

static int64_t wall_clock_us() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return duration_cast<microseconds>(steady_clock::now() - t0).count();
}

extern "C" {

int64_t esp_timer_get_time(void) {
    std::lock_guard<std::mutex> lock(g_time_mutex);
    if (g_simulated)
        return g_sim_time_us;
    return wall_clock_us();
}

} // extern "C"

/* ── test harness API ──────────────────────────────────────────── */

int64_t esp_timer_test_advance(int64_t us) {
    std::lock_guard<std::mutex> lock(g_time_mutex);
    g_simulated = true;
    g_sim_time_us += us;
    return g_sim_time_us;
}

void esp_timer_test_reset() {
    std::lock_guard<std::mutex> lock(g_time_mutex);
    g_simulated   = false;
    g_sim_time_us = 0;
}
