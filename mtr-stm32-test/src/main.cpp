// MTR STM32G431 — Standalone Hardware Test Firmware (No CAN)
// Cycles relays ON and OFF in order, and generates a smooth 0->max->0 sine DAC sweep.

#include <cmath>
#include "main.h"
#include "config.h"
#include "dac_controller.h"
#include "relay_tester.h"

// Subsystem instances
static mtr_test::RelayTester g_relays;
static mtr_test::DacController g_dac;

// Forward declaration of system clock setup
extern "C" void SystemClock_Config(void);

// Error Handler definition
extern "C" void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}

// 16 MHz HSI System Clock configuration (identical to mtr-stm32 production clock)
extern "C" void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
        Error_Handler();
    }
}

int main(void) {
    // 1. Reset peripherals, initialize Flash interface and SysTick
    HAL_Init();

    // 2. Enable DWT cycle counter
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    *(volatile uint32_t *)0xE0001FB0 = 0xC5ACCE55;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // 3. Configure 16 MHz system clock
    SystemClock_Config();

    // 4. Initialize Relay Outputs (PA4 Ignition, PA2 Drive, PA0 Reverse, PC6 LED)
    g_relays.init();

    // 5. Initialize MCP4725 12-Bit DAC via bit-banged I2C (PA5 SCL, PA7 SDA)
    g_dac.init();

    uint32_t last_dac_update_ms = 0;

    // 6. Test Loop: Concurrent Relay Sequencing & DAC Sine Sweep
    while (1) {
        uint32_t now_ms = HAL_GetTick();

        // ── 6.1 Relay Sequencer ──────────────────────────────────────────
        // Advances through: Ignition ON -> Pause -> Drive ON -> Pause -> Reverse ON -> Pause
        g_relays.tick(now_ms);

        // ── 6.2 DAC Sine-Like Modulation (50 Hz rate) ────────────────────
        // Half-sine wave: 0 to max in 15 seconds, max to 0 in 15 seconds (30s period).
        // theta in [0, pi] over 30,000 ms:
        //   t = 0 ms     -> theta = 0      -> sin(0) = 0.0 -> DAC code = 0
        //   t = 15000 ms -> theta = pi / 2 -> sin(pi/2) = 1.0 -> DAC code = kDacMaxCode
        //   t = 30000 ms -> theta = pi     -> sin(pi) = 0.0 -> DAC code = 0
        if (now_ms - last_dac_update_ms >= mtr_test::kDacUpdateIntervalMs) {
            last_dac_update_ms = now_ms;

            uint32_t cycle_ms = now_ms % mtr_test::kSweepTotalPeriodMs;
            constexpr float kPi = 3.14159265358979323846f;
            float theta = kPi * (static_cast<float>(cycle_ms) / static_cast<float>(mtr_test::kSweepTotalPeriodMs));
            float factor = std::sin(theta);
            if (factor < 0.0f) factor = 0.0f;
            if (factor > 1.0f) factor = 1.0f;

            uint16_t dac_target = static_cast<uint16_t>(factor * static_cast<float>(mtr_test::kDacMaxCode) + 0.5f);
            g_dac.write_dac_raw(dac_target);
        }

        HAL_Delay(1);
    }
}
