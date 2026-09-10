# SYS ESP32-S3 Firmware Issues & Optimal Solutions

This document identifies latent software defects, architectural bottlenecks, and resource optimizations in `sys-esp32`, along with complete code solutions and cross-firmware impact analyses.

---

## Index of Issues

| ID | Severity | Category | Description |
|---|---|---|---|
| [SYS-BUG-01](#sys-bug-01-mtr-estop-ack-exhaustion-silently-cleared-by-legacy-seb-timer) | **P1 (High)** | Safety / Latch Loss | MTR ESTOP ACK exhaustion silently clears after 3 seconds via legacy SEB recovery loop |
| [SYS-BUG-02](#sys-bug-02-can-rx-priority-inversion-and-intermediate-queue-overflow) | **P2 (Med-High)** | Latency / Comms | CAN RX double-queue buffering causes frame drops and artificial priority inversion |
| [SYS-BUG-03](#sys-bug-03-fragmented-and-inconsistent-estop-dispatch) | **P3 (Medium)** | Architecture | 11 copy-pasted ESTOP trigger blocks with inconsistent MTR ACK supervision |
| [SYS-BUG-04](#sys-bug-04-flash-wear-on-every-boot-cycle-via-nvs-commit) | **P4 (Low-Med)** | Hardware Endurance | Unnecessary NVS flash commit on every boot for local-only counter |
| [SYS-OPT-05](#sys-opt-05-micro-task-proliferation-power-indicator-gear) | **P5 (Low)** | Optimization | 3 micro-tasks waste 7 KB stack RAM and scheduler context switches |

---

## SYS-BUG-01: MTR ESTOP ACK Exhaustion Silently Cleared by Legacy SEB Timer

### 1. Problem Statement
When an emergency stop occurs, SYS supervises `mtr-stm32` to verify that it acknowledges ESTOP by setting `shared::kMtrFaultEstopActive` in `0x206 MTR_MOTOR_FBK`.
If `mtr-stm32` fails to acknowledge after 3 timeout retries, `MtrEstopAckWatchdog` signals `ExhaustedFault`.
SYS flags this as `g_brake_fault_active = true`. However, a legacy auto-recovery loop intended for temporary SEB excursions inspects only SEB health (`seb_healthy < 3`) and resets `g_brake_fault_active` back to `false` after 3 seconds!

As a result, a **critical motor controller fault is silently cleared** after 3 seconds, allowing status frames (`0x500.degraded` and `0x600.brake_fault`) to report normal health even though the motor controller never verified de-energization.

### 2. Problematic Code & Logic Flaw

In [`sys-esp32/src/main.cpp:724-727`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L724-L727):
```cpp
} else if (action == sys::MtrEstopAckWatchdog::Action::ExhaustedFault) {
    ESP_LOGE(TAG, "MTR ESTOP ACK failed after retries — latched brake fault");
    g_brake_fault_active.store(true, std::memory_order_relaxed);
}
```

Directly below in [`sys-esp32/src/main.cpp:769-787`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L769-L787):
```cpp
// Legacy brake-fault auto-recovery (SEB L3 / following-error paths...)
if (g_brake_fault_active.load(std::memory_order_relaxed)) {
    bool seb_healthy = g_seb_error_status.load(std::memory_order_relaxed) < 3;
    bool not_in_estop = (g_mode_mgr.mode() != can::Mode::Estop);

    static int brake_recovery_count = 0;
    if (seb_healthy && not_in_estop) {
        if (++brake_recovery_count >= 30) {  // 30 * 100ms = 3s
            g_brake_fault_active.store(false, std::memory_order_relaxed); // BUG: Silently cleared!
            brake_recovery_count = 0;
            ESP_LOGI(TAG, "Brake fault cleared — all conditions recovered");
        }
    } else {
        brake_recovery_count = 0;
    }
}
```

### 3. Optimal Solution

Integrate MTR ACK failure into the canonical `LatchedFaultReason` mask in [`inhibit_state.h`](file:///e:/work/etrike/sys-esp32/src/inhibit_state.h):

1. Add `kLatchedMtrEstopAckFailed = 1u << 2` to `LatchedFaultReason`.
2. In `main.cpp`, set `sys::set_latched_fault(sys::kLatchedMtrEstopAckFailed)` when retries exhaust.
3. In `latched_causes_currently_clearable()`, require that MTR has acknowledged `ESTOP_ACTIVE` before the latch can be reset.
4. Delete the legacy `g_brake_fault_active` auto-recovery loop.

```cpp
// sys-esp32/src/inhibit_state.h
enum LatchedFaultReason : uint32_t {
    kLatchedBrakeFollowing    = 1u << 0,  // confirmed persistent following error
    kLatchedSebL3             = 1u << 1,  // SEB error_status == 3
    kLatchedMtrEstopAckFailed = 1u << 2,  // MTR failed to ACK ESTOP after retries
};

inline bool latched_causes_currently_clearable() {
    uint32_t latched = g_latched_fault_reasons.load(std::memory_order_relaxed);
    if ((latched & kLatchedSebL3) &&
        g_seb_error_status.load(std::memory_order_relaxed) >= 3) {
        return false;
    }
    if ((latched & kLatchedBrakeFollowing) &&
        g_seb_status_byte0.load(std::memory_order_relaxed) == 0xFF) {
        return false;
    }
    if ((latched & kLatchedMtrEstopAckFailed) &&
        !(g_motor_fault_flags.load(std::memory_order_relaxed) & shared::kMtrFaultEstopActive)) {
        return false;  // MTR has still not acknowledged ESTOP
    }
    return true;
}
```

### 4. Cross-Firmware Impact Analysis
* **`mtr-stm32`**: **Safer**. Prevents operator or Host reset from re-energizing the vehicle while `mtr-stm32` is unresponsive.
* **`rt-esp32`**: **Positive**. RT monitors `0x500 SYS_NODE_STATUS.block_mask`. RT correctly sees `kResetBlockLatchedFault` continuously asserted instead of disappearing after 3 seconds.
* **`jetson`**: Host `0x114` reset attempts will return `0x115 SYS_ESTOP_RESET_RSP` with `result = REJECTED (1)` and `blocker_mask` indicating the active MTR fault, providing deterministic diagnosability.
* **Wire Protocol**: **Zero change**. Compatible with current DBC/YAML specifications.

---

## SYS-BUG-02: CAN RX Priority Inversion and Intermediate Queue Overflow

### 1. Problem Statement
CAN frames are received through two serial FreeRTOS queues:
$$\text{TWAI Driver ISR} \xrightarrow{} \text{rx\_queue\_ (capacity 32)} \xrightarrow[\text{Prio 5}]{\text{task\_can\_rx}} \text{g\_can\_rx\_queue (capacity 16)} \xrightarrow[\text{Prio 4}]{\text{task\_dispatch}} \text{Decoders}$$

Under heavy CAN bursts (SEB 100 Hz + SES 100 Hz + MTR 50 Hz + RT 100 Hz), `task_can_rx` (Priority 5) starves `task_dispatch` (Priority 4), causing `g_can_rx_queue` to overflow. The author inserted a 5 ms yield delay (`pdMS_TO_TICKS(5)`) to mitigate this, which introduces up to 5 ms of artificial latency to incoming `0x7FD` heartbeats and `0x205` brake commands.

### 2. Problematic Code & Logic Flaw

In [`sys-esp32/src/main.cpp:231-248`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L231-L248):
```cpp
[[noreturn]] static void task_can_rx(void*) {
    can::Frame fr;
    while (1) {
        if (g_can.receive(fr, 100)) {
            // Priority inversion workaround: 5ms delay to let dispatch drain queue
            if (xQueueSend(g_can_rx_queue, &fr, pdMS_TO_TICKS(5)) != pdTRUE) {
                g_can_rx_overflow.fetch_add(1, std::memory_order_relaxed);
                ...
            }
        }
    }
}
```

### 3. Optimal Solution

Eliminate `task_can_rx` and `g_can_rx_queue` entirely. `CanDriver` already maintains an ISR-safe FreeRTOS queue (`rx_queue_`, capacity 32). Have `task_dispatch` read directly from `g_can.receive()`:

```cpp
// Delete task_can_rx and g_can_rx_queue.
// task_dispatch runs at Priority 5 and directly receives from driver:
[[noreturn]] static void task_dispatch(void*) {
    can::Frame fr;
    while (1) {
        g_alive_dispatch.store(xTaskGetTickCount(), std::memory_order_relaxed);
        if (!g_can.receive(fr, 100)) continue;

        switch (fr.id) {
            case can::kIdRtDriveCmd: ...
            // Decode directly
        }
    }
}
```

### 4. Cross-Firmware Impact Analysis
* **`rt-esp32`**: **Direct Reliability Gain**. Eliminates dropped `0x7FD` (RT heartbeat) frames during bus bursts, preventing false `kHeartbeatTimeoutMsRt` ESTOP trips on SYS.
* **`mtr-stm32` & `jetson`**: Jitter on incoming high-level requests is reduced by up to 5 ms.
* **Wire Protocol**: **Zero change**.

---

## SYS-BUG-03: Fragmented and Inconsistent ESTOP Dispatch

### 1. Problem Statement
The logic to trigger an ESTOP is copy-pasted across 11 different places in 4 tasks (`task_dispatch`, `task_safety`, `task_mode`, `task_diag`).
Because the code was duplicated by hand, several trigger sites omit vital safety steps:
- `task_diag:1235` (task deadline failure) and `task_diag:1275` (CAN bus-off) **omit calling `trigger_estop_ack_watchdog()`**, meaning MTR is never verified to have transitioned to ESTOP.
- `task_dispatch:431` (CAN `0x001` ingest) omits checking if the mode is already ESTOP before re-triggering.

### 2. Problematic Code & Logic Flaw

Sample from [`sys-esp32/src/main.cpp:1232-1240`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L1232-L1240):
```cpp
if (g_mode_mgr.mode() != can::Mode::Estop) {
    ESP_LOGE(TAG, "SYS critical task(s) dead ... — forcing ESTOP");
    g_mode_mgr.force_estop();
    // BUG: trigger_estop_ack_watchdog() is MISSING here!
    if (can_send_estop()) {
        send_estop_frame("ESTOP");
    }
}
```

### 3. Optimal Solution

Consolidate ESTOP triggering into a single authoritative function:

```cpp
static void trigger_system_estop(const char* reason, bool broadcast_can = true) {
    const bool was_estop = (g_mode_mgr.mode() == can::Mode::Estop);
    if (!was_estop) {
        g_mode_mgr.force_estop();
        ESP_LOGE(TAG, "ESTOP triggered: %s", reason);
    }
    // Always arm watchdog to verify MTR acknowledgment
    trigger_estop_ack_watchdog(xTaskGetTickCount());
    if (broadcast_can && can_send_estop()) {
        send_estop_frame(reason);
    }
}
```
Replace all 11 ad-hoc blocks with `trigger_system_estop("Reason")`.

### 4. Cross-Firmware Impact Analysis
* **`mtr-stm32`**: **High Safety Value**. Ensures that *any* internal SYS failure (including internal task stalls and CAN bus-off) systematically supervises MTR to guarantee propulsion is cut.
* **`rt-esp32` & `jetson`**: Consistent CAN `0x001` broadcast behavior across all fault classes.
* **Wire Protocol**: **Zero change**.

---

## SYS-BUG-04: Flash Wear on Every Boot Cycle via NVS Commit

### 1. Problem Statement
In [`main.cpp:1421-1432`](file:///e:/work/etrike/sys-esp32/src/main.cpp#L1421-L1432), SYS opens NVS, reads `reset_count`, increments it, writes it back, and calls `nvs_commit()`.
This counter is printed **only to local UART console** on boot. It is never broadcast on CAN (`0x600 SYS_DIAG_RPT` sends `free_heap_kb`, `tec`, `rec`, and `rx_overflow`, but no `reset_count`).
On test benches where the ECU is power-cycled hundreds of times during automated testing, this causes unnecessary wear on the SPI NOR flash.

### 2. Problematic Code & Logic Flaw

```cpp
if (nvs_open("sys_diag", NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_get_u32(nvs, "reset_count", &reset_count);
    reset_count++;
    nvs_set_u32(nvs, "reset_count", reset_count);
    nvs_set_u32(nvs, "reset_reason", static_cast<uint32_t>(reason));
    nvs_commit(nvs);  // Executes flash write on EVERY boot
    nvs_close(nvs);
}
```

### 3. Optimal Solution

Use ESP32 RTC Slow Memory instead of NVS for volatile reset tracking:
```cpp
// Survives soft reset, deep sleep, and watchdog resets without flash wear:
static RTC_NOINIT_ATTR uint32_t s_boot_count;
static RTC_NOINIT_ATTR uint32_t s_last_reset_reason;

void app_main() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON) {
        s_boot_count = 1;
    } else {
        s_boot_count++;
    }
    s_last_reset_reason = static_cast<uint32_t>(reason);
    ESP_LOGI(TAG, "Reset reason: %d, reset count: %lu", (int)reason, (unsigned long)s_boot_count);
    ...
```

### 4. Cross-Firmware Impact Analysis
* **All Firmwares**: **Completely Isolated**. Zero external effect; preserves MCU flash endurance.

---

## SYS-OPT-05: Micro-Task Proliferation (`power`, `indicator`, `gear`)

### 1. Problem Statement
SYS allocates 3 separate FreeRTOS tasks for minimal workloads:
- `task_power` (2560 B stack): 1 line of GPIO toggle at 5 Hz.
- `task_indicator` (2560 B stack): 4 GPIO writes at 5 Hz.
- `task_gear` (2048 B stack): Reads two atomics at 50 Hz to log a warning if they mismatch.

This consumes **7168 bytes of RAM** and causes constant context-switching overhead for simple operations that share identical periodic cadences.

### 2. Optimal Solution

1. **Merge `task_power` and `task_indicator` into `task_can_tx`**: `task_can_tx` already wakes up at the exact same 5 Hz cadence (200 ms). It can drive the bulbs and relay immediately after building the CAN frames.
2. **Move gear mismatch check into `task_safety`**: `task_safety` already runs at 20 Hz and performs command-path speed consistency checks; gear consistency belongs there.
3. Delete `task_power`, `task_indicator`, and `task_gear`.

### 3. Cross-Firmware Impact Analysis
* **`rt-esp32`**: **Zero Impact**. `0x7FE SYS_HEARTBEAT` reports `task_safety_ok`, `task_brake_ok`, `task_dispatch_ok`, `task_can_tx_ok`. None of the eliminated tasks are monitored in the wire contract.
* **All Firmwares**: Zero wire or behavioral changes. Reclaims 7 KB RAM.
