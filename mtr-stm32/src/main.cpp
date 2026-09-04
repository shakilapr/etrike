// MTR STM32G431 — Main Application Entry Point (C++17)
// Dual-workflow support: STM32CubeIDE (GUI) & PlatformIO (CLI)

#include "main.h"
#include "config.h"
#include "can_driver.h"
#include "relay_controller.h"
#include "dac_controller.h"
#include "motor_manager.h"

// Subsystem singletons
static mtr::CanDriver       g_can;
static mtr::RelayController g_relays;
static mtr::DacController   g_dac;
static mtr::MotorManager    g_motor(g_relays, g_dac);

// Peripheral handles required by HAL interrupt vectors
extern "C" {
    FDCAN_HandleTypeDef hfdcan1;
}

// Forward declaration of system clock setup
extern "C" void SystemClock_Config(void);

// FDCAN1 RX FIFO 0 Interrupt Handler Callback
extern "C" void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs) {
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0U) {
        g_can.handle_rx_fifo0_isr();
    }
}

// Error Handler definition
extern "C" void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}

// 16 MHz HSI System Clock configuration
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
    // 1. Reset peripherals, initialize Flash interface and Systick
    HAL_Init();

    // 2. Enable DWT cycle counter
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    *(volatile uint32_t *)0xE0001FB0 = 0xC5ACCE55;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // 3. Configure 16 MHz system clock
    SystemClock_Config();

    // 4. Initialize Motor Manager (configures GPIO Relays and DAC)
    g_motor.init();

    // 5. Initialize CAN Driver
    if (!g_can.init()) {
        Error_Handler();
    }
    // Link HAL handle
    hfdcan1 = *g_can.handle();

    uint32_t last_loop_ms = HAL_GetTick();
    uint32_t last_fbk_ms = last_loop_ms;
    uint32_t last_throttle_ms = last_loop_ms;

    // 6. Main Execution Loop
    while (1) {
        uint32_t now_ms = HAL_GetTick();

        // Drain incoming CAN messages
        can::Frame rx_frame;
        while (g_can.poll_rx(rx_frame)) {
            g_motor.handle_frame(rx_frame, now_ms);
        }

        // Periodic motor & watchdog evaluation (5 ms rate)
        if (now_ms - last_loop_ms >= mtr::kMainLoopPeriodMs) {
            last_loop_ms = now_ms;
            g_motor.tick(now_ms);
        }

        // Periodic 0x120 SYS_THROTTLE_STS broadcast (100 Hz / 10 ms rate)
        if (now_ms - last_throttle_ms >= mtr::kThrottlePeriodMs) {
            last_throttle_ms = now_ms;
            can::Frame fr = g_motor.build_throttle_status_frame();
            g_can.send(fr);
        }

        // Periodic 0x206 MTR_MOTOR_FBK broadcast (50 Hz / 20 ms rate)
        if (now_ms - last_fbk_ms >= mtr::kFeedbackPeriodMs) {
            last_fbk_ms = now_ms;
            can::Frame fr = g_motor.build_motor_feedback_frame();
            g_can.send(fr);
        }

        HAL_Delay(1);
    }
}
