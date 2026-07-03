// Native test entry point for MTR throttle math.
// Build: pio run -e native
// Tests the pure functions that don't need STM32 HAL hardware.

#include <cstdio>
#include <cstdint>

// ── Minimal STM32 HAL stubs so throttle_input.h compiles ──
typedef int ADC_HandleTypeDef;
#define HAL_MAX_DELAY 0xFFFFFFFF
static ADC_HandleTypeDef hadc1;
static void HAL_ADC_Start(ADC_HandleTypeDef*) {}
static int  HAL_ADC_PollForConversion(ADC_HandleTypeDef*, unsigned) { return 0; }
static unsigned HAL_ADC_GetValue(ADC_HandleTypeDef*) { return 0; }
static void HAL_ADC_Stop(ADC_HandleTypeDef*) {}

#include "config.h"
#include "throttle_input.h"

static int pass=0, fail=0;
#define CHECK(cond,msg) do{if(cond)pass++;else{fail++;fprintf(stderr,"FAIL %s\n",msg);}}while(0)

int main() {
    printf("\n=== MTR Throttle Math — Native ===\n\n");

    mtr::ThrottleInput ti; ti.init();

    CHECK(ti.tick(0) == 0, "0 ADC -> 0 mm/s");
    CHECK(ti.tick(199) == 0, "199 ADC (<dead zone) -> 0");
    CHECK(ti.tick(200) > 0, "200 ADC (>dead zone) -> positive");

    int16_t max_speed = ti.tick(4095);
    CHECK(max_speed == mtr::kThrottleMaxSpeedMmps, "4095 ADC -> max speed");

    int16_t mid = ti.tick(2048);
    CHECK(mid > 1400 && mid < 1600, "2048 ADC ~1500 mm/s");

    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);
    return fail ? 1 : 0;
}

