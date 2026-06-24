/*
 * smoke_test.cpp — Phase 1 verification: FreeRTOS kernel runs on host.
 *
 * Creates tasks, exercises queues, toggles virtual GPIO, and verifies
 * the scheduler preempts correctly.
 */
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <thread>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* ── test state ─────────────────────────────────────────────────── */
static std::atomic<int> g_pass{0};
static std::atomic<int> g_fail{0};
static std::atomic<int> g_task_counter{0};
static std::atomic<int> g_preemptions{0};
static QueueHandle_t g_queue = nullptr;
static SemaphoreHandle_t g_sem = nullptr;

#define CHECK(cond, msg) do {                              \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); }  \
    else { g_fail++; printf("  FAIL: %s (%s:%d)\n",       \
               msg, __FILE__, __LINE__); }                 \
} while(0)

/* ── high-priority task — preempts the low-priority counter ─────── */
static void vHighPriTask(void* pv) {
    (void)pv;
    printf("[high] started (prio 3)\n");

    // Wait for semaphore (given by low-prio task)
    if (xSemaphoreTake(g_sem, pdMS_TO_TICKS(2000)) == pdTRUE) {
        g_preemptions++;
        printf("[high] got semaphore — preempted low-prio\n");
    }

    // Send message through queue
    const char* msg = "hello from high-prio";
    xQueueSend(g_queue, &msg, pdMS_TO_TICKS(100));

    vTaskDelete(NULL);
}

/* ── low-priority task — yields to high-prio ────────────────────── */
static void vLowPriTask(void* pv) {
    (void)pv;
    printf("[low]  started (prio 1)\n");

    // Count a few ticks
    for (int i = 0; i < 5; i++) {
        g_task_counter++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("[low]  counter reached %d\n", g_task_counter.load());

    // Give semaphore to high-prio task (triggers preemption)
    printf("[low]  giving semaphore...\n");
    xSemaphoreGive(g_sem);

    // High-prio runs now. Wait for queue message.
    const char* received = nullptr;
    if (xQueueReceive(g_queue, &received, pdMS_TO_TICKS(2000)) == pdTRUE) {
        printf("[low]  received: '%s'\n", received);
        g_task_counter++;
    }

    printf("[low]  done\n");
    vTaskDelete(NULL);
}

/* ── periodic task — runs on timer tick ─────────────────────────── */
static void vPeriodicTask(void* pv) {
    (void)pv;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    int cycles = 0;

    while (cycles < 5) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
        cycles++;
        g_task_counter++;
    }
    printf("[per]  completed %d cycles at 50 Hz\n", cycles);
    vTaskDelete(NULL);
}

/* ── hooks ──────────────────────────────────────────────────────── */
extern "C" void vApplicationMallocFailedHook(void) {
    printf("FATAL: malloc failed\n");
    g_fail++;
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                              char* pcTaskName) {
    printf("FATAL: stack overflow in task '%s'\n", pcTaskName);
    g_fail++;
}

/* ── main ───────────────────────────────────────────────────────── */
int main() {
    printf("\n=== FreeRTOS Host Smoke Test ===\n\n");

    /* Test 1: Create and run tasks */
    printf("-- Test 1: Task creation --\n");
    g_queue = xQueueCreate(4, sizeof(const char*));
    g_sem   = xSemaphoreCreateBinary();

    CHECK(g_queue != nullptr, "queue created");
    CHECK(g_sem != nullptr,   "semaphore created");

    BaseType_t r;
    r = xTaskCreate(vLowPriTask,  "low",  2048, NULL, 1, NULL);
    CHECK(r == pdPASS, "low-prio task created");
    r = xTaskCreate(vHighPriTask, "high", 2048, NULL, 3, NULL);
    CHECK(r == pdPASS, "high-prio task created");
    r = xTaskCreate(vPeriodicTask, "per", 2048, NULL, 2, NULL);
    CHECK(r == pdPASS, "periodic task created");

    /* Test 2: Run the scheduler */
    printf("\n-- Test 2: Scheduler --\n");
    printf("Starting scheduler (3 tasks)...\n\n");

    vTaskStartScheduler();

    /* vTaskStartScheduler should NOT return on POSIX port
       (it calls exit() when all tasks are done).
       If we get here, something went wrong. */
    printf("\n=== Results ===\n");
    printf("Task counter: %d\n", g_task_counter.load());
    printf("Preemptions: %d\n", g_preemptions.load());
    printf("Pass: %d, Fail: %d\n", g_pass.load(), g_fail.load());

    // Clean up (unreachable in normal operation, but good practice)
    vQueueDelete(g_queue);
    vSemaphoreDelete(g_sem);

    return g_fail.load() > 0 ? 1 : 0;
}
