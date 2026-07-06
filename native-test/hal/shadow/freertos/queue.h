#pragma once
#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using QueueHandle_t = void*;
using SemaphoreHandle_t = void*;
using TaskHandle_t = void*;

constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr uint32_t portMAX_DELAY = 0xFFFFFFFF;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
