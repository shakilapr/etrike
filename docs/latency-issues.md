# Latency Issues

## Summary

End-to-end latency from Jetson 0x300 (HOST_DRIVE_CMD) to SYS MCP4725 DAC output,
analysed by tracing every line of code in `rt-esp32/src/main.cpp` and
`sys-esp32/src/main.cpp`.

| Scenario | Latency | Dominant factor |
|----------|---------|-----------------|
| Best case (all phases aligned) | ~200 µs | CAN wire + interrupt wake |
| Typical (normal traffic, avg alignment) | ~23 ms | Three ~5 ms avg scheduling delays + 7.5 ms TX polling |
| Worst case, normal | ~56 ms | All 10 ms periods aligned against us + TX jitter |
| Worst case, degraded (boot / CAN fault) | ~540 ms | Dispatch blocks 500 ms waiting for low-bus heartbeat |
| Stop command (zero-speed drop bug) | +500 ms | Watchdog timeout before zero written to cmd_q |

CAN bus utilisation is ~15 % (low bus) and ~6 % (high bus) — bus contention is not
a factor.

---

## 1. Physical layer (all sub-millisecond)

| Layer | Mechanism | Time |
|-------|-----------|------|
| CAN frame on wire, 5-byte DLC @ 500 kbit/s | Bit time 2 µs, ~64 bit times + stuffing | ~130 µs |
| MCP2515 SPI register read/write @ 10 MHz | 3-byte transaction | ~5 µs |
| MCP2515 full-frame receive | ~10 SPI transactions (SIDH, SIDL, DLC, data, clear IF) | ~70 µs |
| MCP2515 full-frame send | ~12 SPI transactions (TXB0CTRL, SIDH, SIDL, DLC, data, RTS) | ~70 µs |
| TWAI transmit | Hardware TX buffer, typically free | ~50 µs |
| MCP4725 I2C DAC write | 100–400 kHz, 3 bytes (not yet implemented — stub only) | ~100–300 µs |

---

## 2. Queue / task pipeline (8 hops)

```
Jetson TX 0x300
  │  High CAN bus ~130 µs
  ▼
[A] t_can_rx_high (prio 5) — MCP2515 polling
  │  xQueueSend(g_can_rx_high_q, timeout=0, depth 16)
  ▼
[B] t_dispatch (prio 4) — dual-queue router
  │  xQueueOverwrite(g_cmd_q, depth 4)
  ▼
[C] t_control (prio 4, 100 Hz) — kinematics
  │  xQueueOverwrite(g_setpoint_q, depth 4)
  ▼
[D] t_can_tx_low (prio 3) — periodic TX
  │  twai_transmit (10 ms timeout)
  │  Low CAN bus ~130 µs
  ▼
[E] task_can_rx (SYS, prio 5) — TWAI interrupt-driven
  │  xQueueSend(g_can_rx_queue, timeout=0, depth 16)
  ▼
[F] task_dispatch (SYS, prio 4)
  │  atomic store → g_setpoint_speed_mmps
  ▼
[G] task_motor (SYS, prio 4, 100 Hz)
  │  MCP4725 DAC
  ▼
Motor voltage change
```

---

## 3. Issue A — MCP2515 polling, not interrupt-driven

**File:** `rt-esp32/src/can_driver_mcp2515.cpp:230-277`

GPIO40 (MCP_INT) is configured as a pull-up input but **never used as an interrupt
source**. The receive loop polls `read_status()` via SPI, sleeping 1 ms between
checks:

```cpp
while (true) {
    uint8_t status = read_status();        // ~5 µs SPI
    if (rx0 || rx1) { /* read frame */ }
    else {
        if (deadline exceeded) return false;
        vTaskDelay(pdMS_TO_TICKS(1));     // ← 1 ms sleep
    }
}
```

| Scenario | Latency |
|----------|---------|
| Frame arrives just before `read_status()` | ~70 µs |
| Frame arrives just after `read_status()` returned empty | ~1 ms |
| Average | ~500 µs |

**Fix:** Configure GPIO40 as edge-triggered ISR → FreeRTOS task notification.
Eliminates polling, reduces RX latency to interrupt + scheduling time (~100 µs).

---

## 4. Issue B — Dispatch priority inversion (CRITICAL)

**File:** `rt-esp32/src/main.cpp:70-72`

```cpp
if (xQueueReceive(g_can_rx_low_q, &fr, 0) != pdTRUE &&
    xQueueReceive(g_can_rx_high_q, &fr, 0) != pdTRUE) {
    xQueueReceive(g_can_rx_low_q, &fr, portMAX_DELAY);  // ← blocks on low ONLY
}
```

The dispatch task blocks on the **low** queue with infinite timeout. A 0x300 frame
sitting in `g_can_rx_high_q` **cannot be processed** until a low-bus frame wakes
dispatch. The high bus is gated by low-bus traffic.

Low-bus periodic sources that keep dispatch alive:

| Source | Rate | Max gap |
|--------|------|---------|
| 0x201 (EPS-C steering status) | 100 Hz | 10 ms |
| 0x721 (SEB brake status) | 100 Hz | 10 ms |
| 0x120 (SYS throttle status) | 100 Hz | 10 ms |

In degraded states:
- During boot (before SYNTREE modules online): **500 ms** (2 Hz heartbeats only)
- If SYNTREE modules fault: **500 ms**
- Complete low-bus silence: **∞ (deadlock)**

**Fix:** Use `xQueueCreateSet` to block on both queues simultaneously, or use
task notifications from `t_can_rx_low` and `t_can_rx_high` to unblock dispatch
when either queue has data.

---

## 5. Issue C — Zero-speed commands silently dropped

**File:** `rt-esp32/src/main.cpp:96`

```cpp
if (cmd_buf.speed_mmps) {
    xQueueOverwrite(g_cmd_q, &cmd_buf);
    g_watchdog.feed(esp_timer_get_time());
}
```

A Jetson command with `speed_mmps == 0` (commanded stop) is silently discarded.
The previous non-zero command persists in `g_cmd_q`. The watchdog at 10 Hz
eventually overwrites with zero after 500 ms of staleness, but a commanded stop
can take **up to 500 ms** to reach the motor.

This also interacts with the watchdog: if Jetson sends a zero-speed command
(meaning "I'm alive but stopped"), the watchdog is **not fed**, so it will
incorrectly flag staleness and the system will enter fault behaviour even though
the host is healthy.

**Fix:** Remove the `if (cmd_buf.speed_mmps)` guard. Always write the command and
feed the watchdog. If zero-speed must be handled specially, use a separate flag.

---

## 6. Issue D — CAN TX polling jitter

**File:** `rt-esp32/src/main.cpp:128-149`

```cpp
if (xTaskGetTickCount() - t100 >= pdMS_TO_TICKS(10)) {
    t100 = xTaskGetTickCount();
    // ... send 0x204 ...
}
vTaskDelay(pdMS_TO_TICKS(5));  // free-running 5 ms sleep
```

The TX loop uses `vTaskDelay(5ms)` — NOT `vTaskDelayUntil`. The 100 Hz window
is checked at 5 ms granularity, producing 10–15 ms periods instead of steady 10 ms:

```
Timeline (ms):  0    5   10   15   20   25   30
Poll wake:      X    X    X    X    X    X    X
100Hz check:   [=]       [===]      [===]
TX 0x204:       Y            Y            Y
                ^10ms^      ^15ms^      ^10ms^
```

±5 ms jitter on 0x204 output. This cascades into the SYS 200 ms staleness check
and motor control smoothness. Also applies to the 50 Hz steering path (0x169)
where a 15 ms + 25 ms = 40 ms gap could trigger the SYNTREE EPS-C 20 ms comm-fault
timeout.

Additionally, `t_can_tx_low` is priority 3 while `t_control` is priority 4.
CAN transmission always yields to computation — backwards for a realtime gateway.

**Fix:** Use `vTaskDelayUntil(&last_tx_100, pdMS_TO_TICKS(10))` for the 100 Hz
loop body. Separate the 100 Hz and 50 Hz transmissions into their own
`vTaskDelayUntil`-scheduled blocks. Consider raising TX task priority to 4.

---

## 7. Issue E — SYNTREE 20 ms timeout with 50 Hz TX (future)

**Files:** `rt-esp32/src/config.h:18`, `rt-esp32/src/main.cpp:142-146`

```cpp
constexpr int kSteerCmdRateHz = 50;  // SYNTREE 20 ms period
```

The SYNTREE EPS-C triggers an internal comm fault if 0x169 frames stop for
>20 ms. Transmission at exactly 50 Hz (20 ms period) has **zero margin**. Any
jitter — from the 5 ms TX polling granularity, control task preemption, or
CAN arbitration — pushes the inter-frame gap past 20 ms.

When combined with Issue D (±5 ms jitter), the effective period is 15–25 ms,
guaranteeing periodic comm faults on the EPS-C.

**Note:** The steering path is not yet wired (see §9). This issue will manifest
when steering is connected.

**Fix:** Set `kSteerCmdRateHz = 100` (10 ms period, 10 ms margin). At minimum,
use 67 Hz (15 ms period, 5 ms margin).

---

## 8. Issue F — No WCET analysis or timing instrumentation

No task execution time is measured anywhere in the codebase. There are no
`esp_timer_get_time()` calls for profiling, no WCET comments, and no period
overrun detection.

Specifically unknown:
- Kinematics compute time (atan2, clamp, multiply — likely <10 µs but unmeasured)
- MCP2515 full-frame send time under contention
- Any blocking in I2C DAC writes (when implemented)
- Whether any task overruns its period under worst-case load

**Fix:** Add `esp_timer_get_time()` instrumentation at key pipeline points
(MCP2515 RX → dispatch → control → TX → SYS RX → motor DAC). Log max/min
latency once per second on the diag task. Add period-overrun detection in
every `vTaskDelayUntil` loop.

---

## 9. Known unwired paths

Two latency-critical paths exist in the architecture but are not yet implemented
in code:

| Path | Status |
|------|--------|
| 0x300 → steering (0x169 via SYNTREE EPS-C) | `g_steering.set_target()` never called. `steer_angle_mdeg` from physics model computed but discarded. Steering state machine stuck in LISTEN_SYNC (always receives `INT16_MIN`). 0x201 feedback angle parsed but not forwarded to steering control. |
| 0x301 → brake (0x205 via SYS → SYNTREE SEB) | `brake_arbitrate()` result cast to `(void)`. No 0x205 frame constructed or sent by RT. |

Only the drive command path (0x300 → 0x204 → MCP4725 DAC) is functional
end-to-end. Both steering and brake paths require wiring before latency testing
can begin.

---

## 10. Jitter model

The output timing (SYS motor DAC update) is jittered by three independent 100 Hz
oscillators that are not phase-locked:

```
RT Control:     |⏸ 10ms ⏸|⏸ 10ms ⏸|⏸ 10ms ⏸|
RT TX (0x204):  |⏸ 10-15ms ⏸|⏸ 10-15ms ⏸|
SYS Motor:      |⏸ 10ms ⏸|⏸ 10ms ⏸|⏸ 10ms ⏸|
```

Variance sources:
1. RT control: `vTaskDelayUntil` — ±0 (fixed phase, no drift) — **0 jitter**
2. RT TX: free-running poll — **±5 ms jitter**
3. RT dispatch: gated by low-bus traffic — **0–10 ms (normal) to 500 ms (degraded)**
4. SYS motor: `vTaskDelayUntil` — ±0 (fixed phase) — **0 jitter**

Total peak-to-peak end-to-end jitter under normal conditions: **~30 ms**.

---

## 11. Recommendation priority

| # | Issue | Severity | Effort |
|---|-------|----------|--------|
| 1 | Dispatch priority inversion (§4) | Critical — up to 500 ms, potential deadlock | Small |
| 2 | Zero-speed command drop (§5) | Critical — safety impact on stop latency | Trivial |
| 3 | Wire steering and brake paths (§9) | High — blocks latency testing on those paths | Medium |
| 4 | MCP2515 interrupt (§3) | Medium — unnecessary 1 ms poll delay | Small |
| 5 | TX polling jitter (§4) | Medium — ±5 ms cascading jitter | Small |
| 6 | SYNTREE 20 ms margin (§7) | Medium — will cause comm faults when steering wired | Trivial (config) |
| 7 | Timing instrumentation (§8) | Medium — blocks validation of all other fixes | Medium |
