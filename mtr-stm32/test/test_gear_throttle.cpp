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

static uint16_t g_gpio_state = 0;  // bits represent pin levels

static int HAL_GPIO_ReadPin(GPIO_TypeDef*, uint16_t mask) {
    return (g_gpio_state & mask) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
static void HAL_GPIO_WritePin(GPIO_TypeDef*, uint16_t mask, int state) {
    if (state == GPIO_PIN_SET) g_gpio_state |= mask;
    else g_gpio_state &= ~mask;
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

    printf("-- GearControl: single gear sense --\n");
    {
        mtr::GearControl gc; gc.init();
        // Simulate D gear active (GPIO LOW on D sense pin = GPIO_PIN_RESET)
        // Using pin from config.h: kGearDSense
        g_gpio_state = 1u << (kGearDSense & 0x0F);  // set D pin HIGH (not active)
        // Wait, TLP281 active-low: GPIO LOW = active. HAL_GPIO_ReadPin returns RESET=LOW.
        // Our stub returns RESET when mask bit is CLEAR, SET when mask bit is SET.
        // So to simulate "D active", clear the D bit.
        g_gpio_state = 0;  // all LOW = all "active" — but that's a conflict!
        // Let's just test the pure logic by directly unit-testing the functions.
        // Actually, let's use the stub properly:
        // To make D active (LOW on D pin): clear D bit
        g_gpio_state = 0;
        g_gpio_state |= 1u << (kGearSSense & 0x0F);  // S not active
        g_gpio_state |= 1u << (kGearRSense & 0x0F);  // R not active
        // D bit = 0 → RESET → active
        Gear g = gc.read_sense();
        CHECK(g == Gear::D, "D active → D gear");
        CHECK(!gc.gear_conflict_detected(), "no conflict");
    }

    printf("-- GearControl: conflict detection --\n");
    {
        mtr::GearControl gc; gc.init();
        // Multiple gears active → conflict → N
        g_gpio_state = 0;  // all pins LOW → D, S, R all "active"
        Gear g = gc.read_sense();
        CHECK(g == Gear::N, "conflict → N (fail-safe)");
        CHECK(gc.gear_conflict_detected(), "conflict detected");
    }

    printf("-- GearControl: set_mosfets and all_off --\n");
    {
        mtr::GearControl gc; gc.init();
        gc.set_mosfets(Gear::D);
        CHECK(gc.current_gear() == Gear::D, "set to D");
        CHECK(g_gpio_state != 0, "output pins set");
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
