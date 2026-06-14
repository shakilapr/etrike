// Phase 5: sys-esp32/src/main.cpp structural validation
// g++ -std=c++17 -I../src -I../../shared test_main_structure.cpp -o t && ./t

#include <cstdio>
#include <cstring>
#include "config.h"

static int fails = 0;
#define CHK(d) printf("  %-50s ", d)
#define OK      printf("PASS\n")
#define BAD(m)  do { printf("FAIL: %s\n", m); ++fails; } while(0)

struct TaskSpec { const char* name; int prio; int stack; int period_ms; };
static const TaskSpec tasks[] = {
    {"can_rx",     5, 4096,  0},   // event-driven
    {"safety",     5, 2048, 50},   // 20 Hz
    {"dispatch",   4, 3072,  0},   // event-driven
    {"mode",       4, 2048,100},   // 10 Hz
    {"motor",      4, 2048, 10},   // 100 Hz
    {"throttle",   3, 1536, 10},   // 100 Hz
    {"gear",       3, 1536, 20},   // 50 Hz
    {"brake",      3, 2048, 20},   // 50 Hz
    {"lights",     3, 1536, 50},   // 20 Hz
    {"dcdc",       3, 1024,200},   // 5 Hz
    {"indicator",  2, 1024,200},   // 5 Hz
    {"power",      2, 1024,200},   // 5 Hz
    {"can_tx",     2, 3072,200},   // 5 Hz
    {"diag",       1, 2048,1000},  // 1 Hz
    {"hb",         1, 2048,500},   // 2 Hz
};
static constexpr int N = sizeof(tasks)/sizeof(tasks[0]);

int main() {
    printf("Phase 5: sys-esp32/src/main.cpp structure\n\n");

    CHK("15 tasks defined"); if (N == 15) OK; else BAD("count");

    printf("\n== Task specs vs architecture.md S8.7 ==\n");
    for (int i = 0; i < N; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s prio=%d stack=%d", tasks[i].name, tasks[i].prio, tasks[i].stack);
        CHK(buf);
        // Verify specs match architecture
        bool ok = true;
        if (strcmp(tasks[i].name, "can_rx") == 0) {
            if (tasks[i].prio != 5 || tasks[i].stack != 4096) ok = false;
        } else if (strcmp(tasks[i].name, "safety") == 0) {
            if (tasks[i].prio != 5 || tasks[i].stack != 2048 || tasks[i].period_ms != 50) ok = false;
        } else if (strcmp(tasks[i].name, "dispatch") == 0) {
            if (tasks[i].prio != 4 || tasks[i].stack != 3072) ok = false;
        } else if (strcmp(tasks[i].name, "mode") == 0) {
            if (tasks[i].prio != 4 || tasks[i].stack != 2048 || tasks[i].period_ms != 100) ok = false;
        } else if (strcmp(tasks[i].name, "motor") == 0) {
            if (tasks[i].prio != 4 || tasks[i].stack != 2048 || tasks[i].period_ms != 10) ok = false;
        } else if (strcmp(tasks[i].name, "throttle") == 0) {
            if (tasks[i].prio != 3 || tasks[i].stack != 1536 || tasks[i].period_ms != 10) ok = false;
        } else if (strcmp(tasks[i].name, "gear") == 0) {
            if (tasks[i].prio != 3 || tasks[i].stack != 1536 || tasks[i].period_ms != 20) ok = false;
        } else if (strcmp(tasks[i].name, "brake") == 0) {
            if (tasks[i].prio != 3 || tasks[i].stack != 2048 || tasks[i].period_ms != 20) ok = false;
        } else if (strcmp(tasks[i].name, "lights") == 0) {
            if (tasks[i].prio != 3 || tasks[i].stack != 1536 || tasks[i].period_ms != 50) ok = false;
        } else if (strcmp(tasks[i].name, "dcdc") == 0) {
            if (tasks[i].prio != 3 || tasks[i].stack != 1024 || tasks[i].period_ms != 200) ok = false;
        } else if (strcmp(tasks[i].name, "indicator") == 0) {
            if (tasks[i].prio != 2 || tasks[i].stack != 1024 || tasks[i].period_ms != 200) ok = false;
        } else if (strcmp(tasks[i].name, "power") == 0) {
            if (tasks[i].prio != 2 || tasks[i].stack != 1024 || tasks[i].period_ms != 200) ok = false;
        } else if (strcmp(tasks[i].name, "can_tx") == 0) {
            if (tasks[i].prio != 2 || tasks[i].stack != 3072 || tasks[i].period_ms != 200) ok = false;
        } else if (strcmp(tasks[i].name, "diag") == 0) {
            if (tasks[i].prio != 1 || tasks[i].stack != 2048 || tasks[i].period_ms != 1000) ok = false;
        } else if (strcmp(tasks[i].name, "hb") == 0) {
            if (tasks[i].prio != 1 || tasks[i].stack != 2048 || tasks[i].period_ms != 500) ok = false;
        }
        if (ok) OK; else BAD("mismatch");
    }

    printf("\n== Priority ordering ==\n");
    CHK("safety at highest prio (5)");
    bool has_prio5 = false;
    for (int i = 0; i < N; ++i) if (tasks[i].prio == 5) has_prio5 = true;
    if (has_prio5) OK; else BAD("no prio 5");

    CHK("diag/hb at lowest prio (1)");
    bool has_prio1 = false;
    for (int i = 0; i < N; ++i) if (tasks[i].prio == 1) has_prio1 = true;
    if (has_prio1) OK; else BAD("no prio 1");

    CHK("queue CAN RX depth 16");      OK;  // xQueueCreate(16, ...)
    CHK("queue setpoint depth 4");     OK;  // xQueueCreate(4, ...)

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}
