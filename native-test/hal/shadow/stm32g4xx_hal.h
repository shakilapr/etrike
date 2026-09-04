#pragma once
// Lightweight STM32G4xx HAL stub for native unit testing
#include <cstdint>

#define GPIOA ((void*)0x48000000)
#define GPIOB ((void*)0x48000400)
#define GPIOC ((void*)0x48000800)

#define GPIO_PIN_0   ((uint16_t)0x0001)
#define GPIO_PIN_1   ((uint16_t)0x0002)
#define GPIO_PIN_2   ((uint16_t)0x0004)
#define GPIO_PIN_3   ((uint16_t)0x0008)
#define GPIO_PIN_4   ((uint16_t)0x0010)
#define GPIO_PIN_5   ((uint16_t)0x0020)
#define GPIO_PIN_6   ((uint16_t)0x0040)
#define GPIO_PIN_7   ((uint16_t)0x0080)

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

#define GPIO_MODE_OUTPUT_PP 0x00000001U
#define GPIO_MODE_OUTPUT_OD 0x00000011U
#define GPIO_NOPULL         0x00000000U
#define GPIO_PULLUP         0x00000001U
#define GPIO_SPEED_FREQ_LOW 0x00000000U
#define GPIO_SPEED_FREQ_HIGH 0x00000002U

#define __NOP() do {} while(0)

#define __HAL_RCC_GPIOA_CLK_ENABLE() do {} while(0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() do {} while(0)

inline uint32_t g_mock_gpioa_pins = 0xFFFFFFFF; // all pins high initially (active-low OFF)
inline uint32_t g_mock_gpioc_pins = 0xFFFFFFFF;

inline void HAL_GPIO_WritePin(void* port, uint16_t pin, GPIO_PinState state) {
    if (port == GPIOA) {
        if (state == GPIO_PIN_SET) g_mock_gpioa_pins |= pin;
        else g_mock_gpioa_pins &= ~pin;
    } else if (port == GPIOC) {
        if (state == GPIO_PIN_SET) g_mock_gpioc_pins |= pin;
        else g_mock_gpioc_pins &= ~pin;
    }
}

inline void HAL_GPIO_TogglePin(void* port, uint16_t pin) {
    if (port == GPIOA) g_mock_gpioa_pins ^= pin;
    else if (port == GPIOC) g_mock_gpioc_pins ^= pin;
}

inline GPIO_PinState HAL_GPIO_ReadPin(void* port, uint16_t pin) {
    if (port == GPIOA) return (g_mock_gpioa_pins & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    if (port == GPIOC) return (g_mock_gpioc_pins & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    return GPIO_PIN_RESET;
}

inline void HAL_GPIO_Init(void* port, GPIO_InitTypeDef* init) {
    (void)port;
    (void)init;
}
