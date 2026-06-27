#pragma once
// Build-only FreeRTOS compatibility shim for the current MTR STM32 stub target.
// Replace with a real CubeMX/FreeRTOS integration when hardware drivers land.

#include <stdint.h>
#include <stddef.h>

typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
typedef void*    TaskHandle_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
