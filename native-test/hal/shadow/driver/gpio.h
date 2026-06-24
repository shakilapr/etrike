/*
 * driver/gpio.h — Host stub for ESP-IDF GPIO.
 *
 * Virtual pin array with read-back.  Test harness sets input values;
 * firmware reads them. Output values are recorded for inspection.
 */
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef int gpio_num_t;

/* ── pin constants (matching ESP32-S3) ────────────────────────── */
#define GPIO_NUM_0   0
#define GPIO_NUM_1   1
#define GPIO_NUM_4   4
#define GPIO_NUM_5   5
#define GPIO_NUM_7   7
#define GPIO_NUM_8   8
#define GPIO_NUM_10  10
#define GPIO_NUM_11  11
#define GPIO_NUM_12  12
#define GPIO_NUM_13  13
#define GPIO_NUM_14  14
#define GPIO_NUM_17  17
#define GPIO_NUM_18  18
#define GPIO_NUM_21  21
#define GPIO_NUM_36  36
#define GPIO_NUM_37  37
#define GPIO_NUM_38  38
#define GPIO_NUM_39  39
#define GPIO_NUM_40  40
#define GPIO_PIN_MASK  ((1ULL << 48) - 1)

/* ── mode constants ────────────────────────────────────────────── */
typedef int gpio_mode_t;
#define GPIO_MODE_DISABLE   0
#define GPIO_MODE_INPUT     1
#define GPIO_MODE_OUTPUT    2
#define GPIO_MODE_INPUT_OUTPUT 3

/* ── pull constants ────────────────────────────────────────────── */
typedef int gpio_pull_mode_t;
#define GPIO_FLOATING         0
#define GPIO_PULLUP_ONLY      1
#define GPIO_PULLDOWN_ONLY    2
#define GPIO_PULLUP_PULLDOWN  3

/* ── API stubs ─────────────────────────────────────────────────── */
int gpio_set_direction(gpio_num_t pin, gpio_mode_t mode);
int gpio_set_level(gpio_num_t pin, int level);
int gpio_get_level(gpio_num_t pin);
int gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t pull);

/*
 * Test harness API (not in real ESP-IDF):
 *   gpio_test_set_input(pin, level) — sets the value that gpio_get_level() returns.
 *   gpio_test_get_output(pin)       — reads the value last written by gpio_set_level().
 */
void gpio_test_set_input(gpio_num_t pin, int level);
int  gpio_test_get_output(gpio_num_t pin);
void gpio_test_reset(void);

#ifdef __cplusplus
}
#endif
