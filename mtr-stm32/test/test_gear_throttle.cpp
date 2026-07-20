// g++ -std=c++17 -DTESTING -I. -I../src -I../.. -I../../shared test_gear_throttle.cpp -o test_gt && ./test_gt
//
// Verifies: gear_control.h (conflict detection, pass-through) and
//           throttle_input.h (dead zone, linear mapping).

#include <cstdio>
#include <cstdint>

// ── Minimal STM32 HAL stubs for native testing ──
// These replace stm32f1xx_hal_gpio.h / stm32f1xx_hal_adc.h on host.
typedef int GPIO_TypeDef;  // dummy
#define GPIOA ((GPIO_TypeDef*)1)
#define GPIOB ((GPIO_TypeDef*)2)
#define GPIOC ((GPIO_TypeDef*)3)
#define GPIO_PIN_RESET  0
#define GPIO_PIN_SET    1

static uint16_t g_gpio_state[4] = {0, 0, 0, 0};  // bits represent pin levels for Port A, B, C

static int HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t mask) {
    int p = (port == GPIOA) ? 1 : (port == GPIOB) ? 2 : (port == GPIOC) ? 3 : 0;
    return (g_gpio_state[p] & mask) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
static void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t mask, int state) {
    int p = (port == GPIOA) ? 1 : (port == GPIOB) ? 2 : (port == GPIOC) ? 3 : 0;
    if (state == GPIO_PIN_SET) g_gpio_state[p] |= mask;
    else g_gpio_state[p] &= ~mask;
}

// ADC stub
typedef int ADC_HandleTypeDef;  // dummy
#define HAL_MAX_DELAY 0xFFFFFFFF
static ADC_HandleTypeDef hadc1;
static uint16_t g_adc_value = 0;
static void HAL_ADC_Start(ADC_HandleTypeDef*) {}
static int  HAL_ADC_PollForConversion(ADC_HandleTypeDef*, uint32_t) { return 0; }
static uint16_t HAL_ADC_GetValue(ADC_HandleTypeDef*) { return g_adc_value; }
static void HAL_ADC_Stop(ADC_HandleTypeDef*) {}

#include "gear_control.h"
#include "throttle_input.h"
#include "config.h"

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { if(cond){pass++;}else{fail++;fprintf(stderr,"FAIL %s\n",msg);} } while(0)

using namespace mtr;

int main() {
    printf("\n=== MTR — Gear Control & Throttle Native Tests ===\n\n");

    // ═══ GearControl ═══
    printf("-- GearControl: init defaults to N --\n");
    {
        mtr::GearControl gc; gc.init();
        CHECK(gc.current_gear() == Gear::N, "init → N");
    }

    printf("-- GearControl: read_sense (V2) --\n");
    {
        mtr::GearControl gc; gc.init();
        Gear g = gc.read_sense();
        CHECK(g == Gear::N, "V2 has no sense hardware → always N");
        CHECK(!gc.gear_conflict_detected(), "no conflict");
    }

    printf("-- GearControl: set_mosfets and all_off --\n");
    {
        mtr::GearControl gc; gc.init();
        gc.set_mosfets(Gear::D);
        CHECK(gc.current_gear() == Gear::D, "set to D");
        CHECK((g_gpio_state[1] | g_gpio_state[2] | g_gpio_state[3]) != 0, "output pins set");
        gc.all_off();
        CHECK(gc.current_gear() == Gear::N, "all_off → N");
    }

    // ═══ ThrottleInput ═══
    printf("-- Throttle: dead zone --\n");
    {
        mtr::ThrottleInput ti; ti.init();
        int16_t speed = ti.tick(100);  // below dead zone (~200)
        CHECK(speed == 0, "below dead zone → 0");
    }

    printf("-- Throttle: linear mapping --\n");
    {
        mtr::ThrottleInput ti; ti.init();
        int16_t speed = ti.tick(4095); // max ADC → max speed
        CHECK(speed == kThrottleMaxSpeedMmps, "max → max");
        CHECK(speed == 3000, "max speed is 3000 mm/s");
    }

    printf("-- Throttle: mid-range --\n");
    {
        mtr::ThrottleInput ti; ti.init();
        int16_t speed = ti.tick(2048);
        // 2048 * 3000 / 4095 ≈ 1500
        CHECK(speed >= 1400 && speed <= 1550, "mid-range ~1500 mm/s");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    return fail ? 1 : 0;
}
