# RTOS Task Design Fundamentals

A **Real-Time Operating System (RTOS)** lets you split firmware into independent *tasks* that the scheduler interleaves on one or more CPU cores. Without an RTOS, embedded code runs in a single `while(1)` loop — add one blocking call and everything stalls. With an RTOS, if one task blocks waiting for a CAN frame, the scheduler runs another task instead.

The E-Trike uses **FreeRTOS** on both ESP32-S3 nodes (9 tasks on RT, 15 on SYS). Every design decision — task count, priority assignments, queue usage — follows from concepts below.

---

## 1. Preemptive Scheduling

A **preemptive scheduler** interrupts a running task when a higher-priority task becomes ready. The higher-priority task runs immediately — no cooperative "yield" needed.

```
Time ──────────────────────────────────────────────►

Task A (pri 2)  ████████████░░░░░░░░░░░░░░░░░░████████
Task B (pri 5)  ░░░░░░░░████████████░░░░░░░░░░░░░░░░░░
                 ▲                    ▲
                 │ B unblocks         │ B blocks again
                 │ (CAN frame arrives)│ A resumes where it left off
```

The key property: **the highest-priority ready task always runs.** If two tasks share a priority, they time-slice (round-robin).

---

## 2. Task Priorities — Why They Matter

FreeRTOS uses numeric priorities (0 = lowest, configMAX_PRIORITIES−1 = highest). Choosing them is a design decision, not an afterthought.

| Priority level | What belongs here | Why |
|---------------|-------------------|-----|
| **Highest (5)** | CAN receive, safety GPIO poll | Must never miss a frame or button press. If the CAN RX task is delayed, frames are lost. |
| **High (4)** | Dispatch, control loop, mode FSM | Business logic. Runs frequently, must not be starved by telemetry. |
| **Medium (3)** | CAN transmit, actuator FSM, lights | Periodic output. Jitter is acceptable (a few ms late won't break anything). |
| **Low (2)** | Obstacle sensor, indicators, power relay | Nice to have. Dropped reads are tolerably stale. |
| **Lowest (1)** | Diagnostics, heartbeat | Background housekeeping. Can be delayed indefinitely without safety impact. |

**Rule of thumb from the E-Trike design:**
- Input tasks (CAN RX, GPIO) go at the top — data must be captured.
- Processing tasks (dispatch, control) go one level down — transform captured data.
- Output tasks (CAN TX, actuator write) go at medium — push results out.
- Telemetry and housekeeping go at the bottom — never block safety.

---

## 3. Queues — Why "Queues Over Shared State"

A FreeRTOS queue is a thread-safe FIFO pipe. One task sends, another receives. No mutexes, no semaphores.

```
Task A (CAN RX)          Queue (16 slots)          Task B (Dispatch)
     │                                                 │
     ├── xQueueSend(q, &frame, timeout) ──►            │
     │                                                 ├── xQueueReceive(q, &frame, timeout)
     │                                                 │
```

**Why queues over shared state (global variables + mutexes)?**

| Approach | Problem |
|----------|---------|
| Global variable + mutex | Priority inversion: low-prio task holds mutex, high-prio task blocked waiting — effectively runs at low prio until mutex is released. |
| Global variable + no mutex | Data races: ISR writes byte 3 while foreground reads byte 1 → corrupted frame. |
| Queue | No locks, no races. ISR-safe `xQueueSendFromISR()`. Sender and receiver have clear ownership. |

The E-Trike's design principle #1 captures this: *Queues over shared state. No mutexes, no semaphores.*

### Queue overflow

Every queue has a fixed depth. If the sender produces faster than the receiver consumes:

```
xQueueSend(q, &frame, 0)  →  returns errQUEUE_FULL
```

The sender must decide: drop the oldest (overwrite), drop the newest (skip), or block until space is available. The E-Trike uses **overwrite** for setpoint queues (only the latest command matters) and **drop** for telemetry queues (a missed periodic update is replaced by the next one).

---

## 4. Tick Rate

FreeRTOS's **tick interrupt** fires at a fixed frequency (1000 Hz on the E-Trike). Every tick, the scheduler checks if a higher-priority task is ready.

| Tick rate | Implication |
|-----------|-------------|
| 100 Hz (10 ms) | Coarse. Tasks get 10 ms granularity. A 100 Hz control loop needs to run every other tick. |
| **1000 Hz (1 ms)** | Standard for motor control. 100 Hz loop runs every 10 ticks. Jitter <1 ms. |
| 10000 Hz (100 µs) | Aggressive. CPU spends significant time in tick ISR. Only for sub-ms control. |

The E-Trike uses 1000 Hz because the steering CAN command at 50 Hz (20 ms period) needs tight periodicity — a missed 20 ms window causes EPS-C comm fault.

---

## 5. Task Periodicity Patterns

### Periodic task (fixed rate)

```cpp
void periodic_task() {
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        // Do work
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));  // 100 Hz = 10 ms
    }
}
```

`vTaskDelayUntil` corrects for drift — if the work takes 3 ms, it sleeps 7 ms (not 10 ms). Over time, the period stays exact.

### Event-driven task (blocking)

```cpp
void event_task() {
    while (true) {
        CanFrame frame;
        if (xQueueReceive(rx_queue, &frame, portMAX_DELAY) == pdTRUE) {
            process(frame);
        }
    }
}
```

The task blocks (consumes zero CPU) until a frame arrives. This is the pattern for CAN RX and dispatch tasks.

---

## 6. Stack Sizing

Every FreeRTOS task has its own stack. Too small → stack overflow → corrupted memory (hardest bug to debug). Too large → wasted RAM.

| Task type | Typical stack | Why |
|-----------|--------------|-----|
| CAN RX (event) | 4096 B | Needs buffer for incoming frame + function call depth |
| Control (periodic) | 4096 B | Floating-point math (kinematics, PID) uses stack heavily |
| CAN TX (event) | 3072 B | Simpler call chain, smaller structs |
| GPIO poll | 2048 B | Minimal logic, no deep calls |
| Heartbeat | 2048 B | Counter increment + CAN send |

**How to verify:** Fill the stack with a known pattern (0xA5) at task creation, then inspect the high-water mark after runtime. ESP-IDF provides `uxTaskGetStackHighWaterMark()`.

---

## 7. Common Pitfalls

| Pitfall | What happens | Fix |
|---------|-------------|-----|
| **Priority inversion** | Low-prio task holds a resource, medium-prio task starves it, high-prio task blocks on the resource | Use queues (lock-free) or priority inheritance mutexes |
| **Task starvation** | A high-prio task never blocks → lower-prio tasks never run | Every high-prio task must block (queue, delay, semaphore) |
| **Too many tasks** | Context-switch overhead dominates CPU time | One task per independent rate or blocking boundary. Don't split logic into unnecessary micro-tasks. |
| **Blocking in a high-prio task** | The blocking call waits forever → all lower-prio tasks starve | Always use timeouts on blocking calls in high-prio tasks |
| **printf in an ISR** | ISR blocks on UART → system hangs | ISRs must only do queue sends and GPIO toggles. No I/O. |

---

## 8. Why FreeRTOS for Safety-Critical Embedded?

- **Preemptive** — a safety task at priority 5 runs within 1 tick (1 ms) of its condition becoming true.
- **Memory isolation via tasks** — a stack overflow in the diagnostics task doesn't corrupt the CAN RX task's stack.
- **Mature and audited** — deployed in millions of devices, MISRA-compliant variant available.
- **Lightweight** — runs on a $2 ESP32-S3 with plenty of headroom.

The alternative — bare-metal superloop — works for simple devices. But when you have 9+ concurrent concerns at different rates (100 Hz control, 50 Hz CAN, 2 Hz heartbeat, event-driven dispatch), an RTOS is the right tool.

---

*See also: [[pid-speed-control]] for the 100 Hz control loop, [[can-protocol]] for CAN RX/TX task design, `architecture.md` §7.7 for RT task layout, §8.7 for SYS task layout.*
