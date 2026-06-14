#pragma once
// Minimal ESP-IDF stubs for host testing.

#include <cstdint>
#include <cstdio>

static int g_mock_gpio[48] = {};
enum { GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT = 1, GPIO_PULLUP_ONLY = 1 };

inline int gpio_set_direction(int, int) { return 0; }
inline int gpio_set_pull_mode(int, int) { return 0; }

inline int gpio_get_level(int pin) {
    return (pin >= 0 && pin < 48) ? g_mock_gpio[pin] : 0;
}

inline void gpio_set_level(int pin, int level) {
    if (pin >= 0 && pin < 48) g_mock_gpio[pin] = level;
}

// Timer stub
static int64_t g_mock_time_us = 0;

inline int64_t esp_timer_get_time() { return g_mock_time_us; }

// Logging stubs
#define ESP_LOGE(tag, ...) do { std::fprintf(stderr, "E [%s] ", tag); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while(0)
#define ESP_LOGW(tag, ...) do { std::fprintf(stderr, "W [%s] ", tag); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while(0)
#define ESP_LOGI(tag, ...) do { std::fprintf(stderr, "I [%s] ", tag); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while(0)
#define ESP_LOGD(tag, ...) do {} while(0)
#define ESP_ERROR_CHECK(x) (x)

// ADC stub
static int g_mock_adc_raw = 0;
inline int adc1_get_raw(int) { return g_mock_adc_raw; }
inline int adc1_config_width(int) { return 0; }
inline int adc1_config_channel_atten(int, int) { return 0; }
enum { ADC_WIDTH_BIT_12 = 3, ADC_ATTEN_DB_12 = 3 };
using adc1_channel_t = int;

// FreeRTOS stubs
using TickType_t = uint32_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY      0xFFFFFFFF
#define pdTRUE             1
#define pdFALSE            0

// LEDC stubs
enum { LEDC_TIMER_0 = 0, LEDC_TIMER_1 = 1, LEDC_LOW_SPEED_MODE = 1, LEDC_CHANNEL_0 = 0, LEDC_CHANNEL_1 = 1, LEDC_AUTO_CLK = 0, LEDC_TIMER_13_BIT = 13 };
struct ledc_timer_config_t {
    int speed_mode = 0;
    int duty_resolution = 0;
    int timer_num = 0;
    int freq_hz = 0;
    int clk_cfg = 0;
};
struct ledc_channel_config_t {
    int gpio_num = 0;
    int speed_mode = 0;
    int channel = 0;
    int timer_sel = 0;
    int duty = 0;
    int hpoint = 0;
};
static int g_mock_ledc_duty[8] = {};
inline int ledc_timer_config(const ledc_timer_config_t*) { return 0; }
inline int ledc_channel_config(const ledc_channel_config_t*) { return 0; }
inline int ledc_set_duty(int, int channel, int duty) {
    if (channel >= 0 && channel < 8) g_mock_ledc_duty[channel] = duty;
    return 0;
}
inline int ledc_update_duty(int, int) { return 0; }

// esp_system stub
inline uint32_t esp_get_free_heap_size() { return 256 * 1024; }

// esp_rom stub
inline void esp_rom_delay_us(int) {}
