# RT ESP32-S3 Firmware Architectural Issues & Technical Debt

This document catalogs the architectural flaws, driver workarounds, scheduling bottlenecks, and dead subsystems in `rt-esp32`, complete with **logic and code analysis**, **historical context**, and **optimal solutions**.

---

## Issue 1: TWAI Single-Shot Mode & The Software Gateway Retry Pump

### 1. Problem Statement
On the Low CAN bus, incoming gateway traffic from the Host (such as `0x111 HMI_MODE_REQ`) can be dropped or delayed by hundreds of milliseconds. The driver suffers from internal TX slot leaks and relies on an ad-hoc 5 ms timeout timer and an external FreeRTOS software retry loop (`gw_pump`) retrying frames up to 40 times.

### 2. Logic & Code Analysis
In [`can_driver_twai.cpp:L185-L194`](src/can_driver_twai.cpp#L185-L194):
```cpp
// Single-shot TX (fail_retry_cnt = 0).
config.fail_retry_cnt = 0;
config.tx_queue_depth = 1;
```
1. **The Slot Leak:** With `kTxSlots = 1` and `fail_retry_cnt = 0`, the ESP32-S3 TWAI controller attempts transmission exactly once. If it loses bus arbitration to higher-priority frames (e.g. `0x110 SYS_MODE_CMD`), the hardware drops the frame **without invoking the `on_tx_done` callback**.
2. **The Timer Hack:** Because `on_tx_done` never fired, the single driver slot leaked. To recover from this, an in-flight timeout reclamation was added in [`can_driver_twai.cpp:L430-L450`](src/can_driver_twai.cpp#L430-L450):
   ```cpp
   // If the in-flight slot has exceeded kTxReclaimUs without completion, force-reclaim
   if (now - self->m_inflight_us.load() > kTxReclaimUs) {
       self->reset_tx_slots();
   }
   ```
3. **The Software Retry Pump:** Because frames were abandoned upon arbitration loss, frames like `0x111 HMI_MODE_REQ` failed to reach SYS. To fix dropped frames, a software pump was built in [`main.cpp:L255-L275`](src/main.cpp#L255-L275):
   ```cpp
   static bool gw_pump(QueueHandle_t q, bool (*send_fn)(can::Frame&), const char* tag) {
       GwTxFrame gw;
       if (xQueueReceive(q, &gw, 0) != pdTRUE) return false;
       const bool accepted = send_fn(gw.frame);
       if (accepted) return true;
       gw.attempts++;
       if (gw.attempts <= kGwMaxAttempts) xQueueSendToFront(q, &gw, 0); // Re-queue in software!
       return true;
   }
   ```

### 3. Historical Root Cause
Developers tested RT on the bench without active CAN peers (or with CANalyst-II in Listen-Only mode). When a frame was transmitted with standard auto-retransmit, missing ACKs incremented `TEC` until hitting 255, triggering `TWAI_ERROR_BUS_OFF` and halting bench tests. Setting `fail_retry_cnt = 0` stopped the Bus-Off on the bench, but broke standard CAN arbitration on the vehicle.

### 4. Optimal Solution
1. In [`can_driver_twai.cpp`](src/can_driver_twai.cpp), conditionally configure `fail_retry_cnt`:
   ```cpp
   #if ETRIKE_RT_TWAI_SELF_TEST
       // Solo bench mode: single-shot self-test to prevent Bus-Off when no peer ACKs
       config.fail_retry_cnt = 0;
       config.flags.enable_self_test = 1;
   #else
       // Vehicle mode: standard CAN hardware auto-retransmission
       config.fail_retry_cnt = -1;  // Re-attempt until delivered or arbitration won
   #endif
   ```
2. Delete `gw_pump()`, the `GwTxFrame` struct, and the 5 ms in-flight slot reclamation timer. Standard CAN hardware handles arbitration in silicon in microseconds.

---

## Issue 2: The 8-Task FreeRTOS Pipeline & High End-to-End Latency

### 1. Problem Statement
The firmware splits CAN I/O, routing, control, and telemetry across **8 separate FreeRTOS tasks**, consuming ~29 KB of RAM in task stacks and causing **23 ms to 56 ms** of end-to-end command latency.

### 2. Logic & Code Analysis
Trace a drive command (`0x300 HOST_DRIVE_CMD`) from the wire to the motor:
```
Jetson 0x300 Wire
  │
  ▼
[Hop 1] task_can_rx (High) (Prio 5)
  │  xQueueSend(g_can_rx_high_q)
  ▼ Context Switch
[Hop 2] t_dispatch (Prio 4)
  │  xQueueOverwrite(g_cmd_q)
  ▼ Context Switch
[Hop 3] t_control (Prio 4, 100 Hz timer)
  │  xQueueOverwrite(g_setpoint_q)
  ▼ Context Switch
[Hop 4] t_can_tx_low (Prio 3, 5 ms timer)
  │  twai_transmit()
  ▼
Low CAN 0x204 Wire
```
* Three separate FreeRTOS queue hops.
* Two queues (`g_cmd_q` and `g_setpoint_q`) have depth 1 and are used purely as atomic global variables via `xQueueOverwrite` and `xQueuePeek`.
* To monitor whether these 8 tasks stall each other, [`check_task_watchdog()`](src/main.cpp#L1041-L1058) was added with atomic alive flags (`g_alive_control`, `g_alive_dispatch`, `g_alive_tx_low`, `g_alive_tx_high`) and CAN diagnostic bitmasks.

### 3. Historical Root Cause
The 8-task architecture was designed on paper in early specifications (`architecture.md §7`) under the theoretical goal of giving every bus and loop frequency its own isolated thread.

### 4. Optimal Solution
Collapse the 8 tasks into **2 deterministic tasks** mapped to the ESP32-S3's dual cores:
1. **Core 0 — CAN I/O Worker (`task_can_io`):**
   - Handles Low TWAI RX and High MCP2515 SPI RX/TX.
   - Transparent gateway frames forward directly to the opposite bus buffer.
   - Drive/sensor frames decode directly into a single coherent atomic input struct:
     ```cpp
     struct VehicleInputs {
         std::atomic<int32_t> host_speed_mmps;
         std::atomic<int32_t> host_yaw_mrad_s;
         std::atomic<int32_t> direct_steer_0_1deg;
         std::atomic<int64_t> last_host_cmd_us;
         // ... sensor feedback timestamps
     };
     ```
2. **Core 1 — Deterministic Control Loop (`task_control_100hz`):**
   - Wakes every 10.0 ms via `vTaskDelayUntil`.
   - Evaluates safety timeouts directly (`now - last_rx_us > timeout`).
   - Resolves kinematics and limits.
   - Emits outgoing frames directly using tick dividers:
     - `0x204 RT_DRIVE_CMD` every tick (100 Hz).
     - `0x169 VCU_SES_REQ` and `0x205 RT_BRAKE_CMD` every 2 ticks (50 Hz).
     - `0x7FD RT_HEARTBEAT` every 50 ticks (2 Hz).
3. **Delete:** `g_cmd_q`, `g_setpoint_q`, separate watchdog task, separate heartbeat task, and per-task alive counters.

---

## Issue 3: MCP2515 SPI Bus Contention Across 4 Tasks

### 1. Problem Statement
Outgoing telemetry and heartbeats intermittently block high-priority CAN frame reception on the High CAN bus, causing SPI transaction timeouts (`m_spi_fail_count`) and dropped frames from Jetson.

### 2. Logic & Code Analysis
In [`can_driver_mcp2515.cpp:L26-L34`](src/can_driver_mcp2515.cpp#L26-L34):
```cpp
// rx_high (prio 5), tx_high (prio 3), control (prio 4), heartbeat (prio 1)
// all access the MCP2515 via SPI.
static SemaphoreHandle_t g_spi_mutex = nullptr;
```
* `rx_high` (Prio 5) runs whenever the INT pin fires.
* `t_control` (Prio 4) calls `pump_diagnostics()` which sends over SPI.
* `tx_high` (Prio 3) transmits `0x121`, `0x210`, `0x310`, `0x311` over SPI.
* `hb` (Prio 1) transmits `0x7FD` over SPI every 500 ms.
When the priority-1 heartbeat task takes `g_spi_mutex`, the priority-5 RX task is blocked. If an SPI transaction takes longer than expected or the mutex deadline expires, `m_spi_fail_count` increments and the frame is dropped.

### 3. Historical Root Cause
The external MCP2515 was added as a driver object whose member functions (`send()`, `receive()`, `read_bus_diag()`) were called directly from whichever task needed them, necessitating a global mutex.

### 4. Optimal Solution
Make a single task on Core 0 the **exclusive owner** of the MCP2515 SPI bus:
1. `rx_high` reads frames from MCP2515 upon GPIO 47 interrupt.
2. Other tasks do not call SPI functions directly; they enqueue outgoing High CAN frames to a lockless ring buffer.
3. The SPI worker task flushes the outgoing ring buffer to the MCP2515 between RX events.
4. **Delete `g_spi_mutex` entirely.**

---

## Issue 4: Queue/Atomic Dual-Path Synchronization

### 1. Problem Statement
Safety events use both a FreeRTOS queue AND atomic fallback flags simultaneously, creating redundant synchronization mechanisms and code complexity.

### 2. Logic & Code Analysis
In [`can_dispatch.h:L34-L46`](src/can_dispatch.h#L34-L46):
```cpp
inline bool enqueue_safety_event(const rt::SafetyEvent& evt, TickType_t timeout) {
    if (xQueueSend(g_safety_evt_q, &evt, timeout) == pdTRUE) return true;

    // Queue failed / full: fall back to atomics
    g_safety_event_drops.fetch_add(1, std::memory_order_relaxed);
    if (evt.type == rt::SafetyEvent::ESTOP) {
        g_pending_estop_event.store(true, std::memory_order_release);
    } else if (evt.type == rt::SafetyEvent::SAFETY_CLEAR) {
        g_pending_safety_clear.store(true, std::memory_order_release);
    } else {
        g_pending_mode_event.store(evt.payload, std::memory_order_release);
    }
    return false;
}
```
And in [`main.cpp:L358-L402`](src/main.cpp#L358-L402), `t_control` drains:
1. `g_pending_estop_event.exchange(false)`
2. `g_pending_mode_event.exchange(-1)`
3. `g_pending_safety_clear.exchange(false)`
4. `xQueueReceive(g_safety_evt_q, &evt, 0)` in a loop

### 3. Historical Root Cause
Developers adopted the rule "queues over shared state" to guarantee discrete event delivery. When stress tests proved the queue could fill up or drop under load, rather than diagnosing queue depth or architecture, they wrapped atomic fallback variables around it.

### 4. Optimal Solution
State in automotive functional safety is state-based, not event-based:
- Maintain current authoritative mode in a single `std::atomic<uint8_t> g_active_mode`.
- Maintain ESTOP latch in a single `std::atomic<bool> g_estop_latched`.
- Delete `g_safety_evt_q`, `g_pending_estop_event`, `g_pending_mode_event`, `g_pending_safety_clear`, and `enqueue_safety_event()`.

---

## Issue 5: Unused Shadow Calculations & Dead Firmware Code

### 1. Problem Statement
Production vehicle firmware runs floating-point PID math and `std::exp()` plant model calculations every 10 ms even though PID and encoders are disabled on the vehicle. Furthermore, duplicate simulator files exist in the firmware source tree.

### 2. Logic & Code Analysis
1. **Unused Plant Simulation in Production Loop ([`main.cpp:L482`](src/main.cpp#L482)):**
   ```cpp
   // Update calculated speed estimator with the commanded setpoint
   // (runs always; is only consumed when SpeedFeedbackSource::Calculated).
   g_calc_speed.update(sp.motor_speed_mmps, 0.01f);
   ```
   In [`calculated_speed.h:L34-L39`](src/calculated_speed.h#L34-L39), this computes:
   `const float alpha = 1.0f - std::exp(-dt_s / kPlantTimeconstantS);`
   Every 10 ms at 100 Hz, an `exp()` calculation runs on CPU0 despite open-loop vehicle configuration (`ETRIKE_RT_SPEED_FEEDBACK_SOURCE=0`).
2. **Shadow PID Execution ([`main.cpp:L610`](src/main.cpp#L610)):**
   Runs PID calculations and broadcasts `0x220 RT_PID_RPT` over High CAN even when `ETRIKE_RT_PID_MODE=0`.
3. **Dead Hardware Driver:** [`encoder_pcnt.cpp`](src/encoder_pcnt.cpp) (150+ lines of hardware pulse counter setup) is linked despite no wheel encoders existing on the vehicle.
4. **Duplicate Simulator Core in Firmware Directory:** [`src/core/rt_core.cpp`](src/core/rt_core.cpp) and [`rt_core.h`](src/core/rt_core.h) are excluded by `platformio.ini` `build_src_filter` and used only by external host tests.

### 3. Historical Root Cause
Created for Software-in-the-Loop (SIL) bench testing before hardware encoders were delivered, and left running in production firmware.

### 4. Optimal Solution
1. Wrap plant calculations:
   ```cpp
   #if ETRIKE_RT_SPEED_FEEDBACK_SOURCE == 3
       g_calc_speed.update(sp.motor_speed_mmps, 0.01f);
   #endif
   ```
2. Wrap PID calculations and `0x220` telemetry transmission:
   ```cpp
   #if ETRIKE_RT_PID_MODE > 0
       g_speed_ctrl.update_shadow_pid(...);
       // emit 0x220 RT_PID_RPT
   #endif
   ```
3. Move [`src/core/rt_core.*`](src/core/) out of `rt-esp32/src/core/` and into `simulation/full_system/vehicle/`.
