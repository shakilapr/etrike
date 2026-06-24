/*
 * adc_stubs.cpp — Host implementation: configurable ADC values.
 */
#include "driver/adc.h"
#include <array>
#include <mutex>

static std::mutex g_adc_mutex;
static std::array<int, 16> g_adc_values = {};

extern "C" {

int adc1_config_width(adc_bits_width_t) {
    return ESP_OK;
}

int adc1_config_channel_atten(adc1_channel_t, adc_atten_t) {
    return ESP_OK;
}

int adc1_get_raw(adc1_channel_t channel) {
    if (channel < 0 || channel >= 16) return 0;
    std::lock_guard<std::mutex> lock(g_adc_mutex);
    return g_adc_values[channel];
}

} // extern "C"

/* ── test harness API ──────────────────────────────────────────── */

void adc_test_set(adc1_channel_t channel, int raw_value) {
    if (channel < 0 || channel >= 16) return;
    std::lock_guard<std::mutex> lock(g_adc_mutex);
    g_adc_values[channel] = raw_value;
}

void adc_test_reset() {
    std::lock_guard<std::mutex> lock(g_adc_mutex);
    g_adc_values.fill(0);
}
