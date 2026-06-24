/*
 * FreeRTOSConfig.h — Host simulation configuration.
 * Matches ESP-IDF defaults: 1000 Hz tick, preemptive, generous heap.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configTICK_RATE_HZ                      1000   // match ESP-IDF CONFIG_FREERTOS_HZ=1000
#define configMAX_PRIORITIES                    10
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 256 )
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 16 * 1024 * 1024 ) )
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_TRACE_FACILITY                1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )
#define configQUEUE_REGISTRY_SIZE               0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         1

/* POSIX port specifics */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_POSIX_ERRNO                   0

/* Hook function names */
#define configMALLOC_FAILED_HOOK_NAME           vApplicationMallocFailedHook
#define configCHECK_FOR_STACK_OVERFLOW_NAME     vApplicationStackOverflowHook

/* Required for pdMS_TO_TICKS */
#define pdMS_TO_TICKS(ms)  ( ( TickType_t ) ( ( ( uint32_t ) ( ms ) * configTICK_RATE_HZ ) / 1000 ) )

#include <stdint.h>

#endif /* FREERTOS_CONFIG_H */
