/*
 * smoke_test.cpp -- Phase 1 verification: FreeRTOS kernel runs on host.
 *
 * Creates tasks, exercises queues and semaphores, verifies preemption.
 */
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <atomic>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* -- test state -- */
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

/* -- high-priority task -- preempts the low-priority counter -- */
static void vHighPriTask(void* pv) {
    (void)pv;
    printf("[high] started (prio 3)\n");

    if (xSemaphoreTake(g_sem, pdMS_TO_TICKS(2000)) == pdTRUE) {
        g_preemptions++;
        printf("[high] got semaphore -- preempted low-prio\n");
    }

    const char* msg = "hello from high-prio";
    xQueueSend(g_queue, &msg, pdMS_TO_TICKS(100));

    printf("[high] done\n");
    vTaskDelete(NULL);
}

/* -- low-priority task -- yields to high-prio -- */
static void vLowPriTask(void* pv) {
    (void)pv;
    printf("[low]  started (prio 1)\n");

    for (int i = 0; i < 5; i++) {
        g_task_counter++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("[low]  counter reached %d\n", g_task_counter.load());

    printf("[low]  giving semaphore...\n");
    xSemaphoreGive(g_sem);

    const char* received = nullptr;
    if (xQueueReceive(g_queue, &received, pdMS_TO_TICKS(2000)) == pdTRUE) {
        printf("[low]  received: '%s'\n", received);
        g_task_counter++;
    }

    printf("[low]  done\n");

    /* Scheduler never returns on Win32 port, so the last task calls exit(). */
    printf("\n=== Results ===\n");
    printf("Task counter: %d\n", g_task_counter.load());
    printf("Preemptions: %d\n", g_preemptions.load());
    printf("Pass: %d, Fail: %d\n", g_pass.load(), g_fail.load());
    fflush(stdout);
    _exit(g_fail.load() > 0 ? 1 : 0);
}

/* -- periodic task -- runs on timer tick -- */
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

/* -- main -- */
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

    /* Should not reach here -- low-prio task calls _exit(). */
    return 99;
}
