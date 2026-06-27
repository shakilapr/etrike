#pragma once

#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void*);

static inline TickType_t xTaskGetTickCount(void) {
    return 0;
}

static inline void vTaskDelay(TickType_t ticks) {
    (void)ticks;
}

static inline void vTaskDelayUntil(TickType_t* previous_wake_time,
                                   TickType_t time_increment) {
    if (previous_wake_time != NULL) {
        *previous_wake_time += time_increment;
    }
}

static inline BaseType_t xTaskCreate(TaskFunction_t task_code,
                                     const char* name,
                                     uint16_t stack_depth,
                                     void* parameters,
                                     UBaseType_t priority,
                                     TaskHandle_t* created_task) {
    (void)task_code;
    (void)name;
    (void)stack_depth;
    (void)parameters;
    (void)priority;
    if (created_task != NULL) {
        *created_task = NULL;
    }
    return 1;
}

static inline void vTaskStartScheduler(void) {}
