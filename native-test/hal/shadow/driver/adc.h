/*
 * driver/adc.h — Host stub for ESP-IDF ADC1.
 *
 * Returns configurable test values set by the test harness.
 */
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* ── types ─────────────────────────────────────────────────────── */
typedef int adc1_channel_t;
typedef int adc_atten_t;
typedef int adc_bits_width_t;

#define ADC1_CHANNEL_0  0
#define ADC1_CHANNEL_1  1
#define ADC1_CHANNEL_5  5
#define ADC1_CHANNEL_6  6
#define ADC1_CHANNEL_7  7
#define ADC_WIDTH_BIT_12  12
#define ADC_ATTEN_DB_11   11
#define ADC_ATTEN_DB_0    0

/* ── API stubs ─────────────────────────────────────────────────── */
int adc1_config_width(adc_bits_width_t);
int adc1_config_channel_atten(adc1_channel_t, adc_atten_t);
int adc1_get_raw(adc1_channel_t);

/*
 * Test harness API:
 *   adc_test_set(channel, raw_value) — sets the ADC reading for that channel.
 *   adc_test_reset() — clears all channels to 0.
 */
void adc_test_set(adc1_channel_t channel, int raw_value);
void adc_test_reset(void);

#ifdef __cplusplus
}
#endif
