/**
 * Minimal FreeRTOS type stubs for sim-engine-native.
 * The ECU logic modules only use FreeRTOS TYPES (QueueHandle_t, BaseType_t, etc.)
 * in their headers — they never call queue/semaphore/task functions from
 * the logic layer. Those calls live in main.cpp (the I/O layer).
 *
 * We provide just enough type definitions for the headers to compile.
 */
#pragma once

#include <cstdint>

// ── Basic types ──
using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using QueueHandle_t = void*;
using SemaphoreHandle_t = void*;
using TaskHandle_t = void*;

// ── Constants ──
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr uint32_t portMAX_DELAY = 0xFFFFFFFF;

// ── Macros ──
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
