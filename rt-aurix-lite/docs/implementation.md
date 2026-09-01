# RT-AURIX-Lite — Implementation Guide

How the firmware core (`src/`) is structured and how each layer maps to
[`architecture.md`](../architecture.md). The code is **platform-agnostic**
(no ESP-IDF, no iLLD, no RTOS) and validated on the host. The AURIX board
target lives in [`board/`](../board/).

## Guiding principle

> *Do not port the ESP32 firmware's execution architecture. Port its required
> behavior. The execution architecture belongs to the TC375 and remains
> unresolved until target bring-up.* (see [`work-plan.md`](../work-plan.md))

## Layer diagram (dependency direction)

```
CAN bytes
   → generated codec            (protocol/generated/cpp + codecs/{ses,seb}.hpp)
   → protocol adapter           (src/protocol/adapters.h)
   → typed input                (src/core/types.h)
   → app orchestration          (src/app/controllers.*)
   → domain logic               (src/domain/*)
   → typed output               (src/core/types.h)
   → protocol adapter           (src/protocol/adapters.h)
   → generated codec
   → CAN bytes
```

## Directory map

| Path | Contents | Architecture ref |
|------|----------|------------------|
| `src/config/` | `control_config.h`, `safety_config.h`, `timing_config.h` (split by concern; reuse `shared_config.h`) | §12 |
| `src/core/` | `types.h` (typed I/O), `time.h` (`TimeUs`), `result.h` (fault bitmask) | — |
| `src/domain/` | `kinematics`, `steering`, `brake`, `mode`, `safety`, `liveness` — **pure logic** (no CAN/IPC/logging/clock) | §6, §10 |
| `src/app/` | `controllers.*` (Motion/Body/Gateway), `watchdog_supervisor.h` (health decision) | §5, §8 |
| `src/protocol/` | `adapters.h` (CAN↔typed), `route_table.h` (forwarding Category 1/2/3) | §3.3 |
| `src/ipc/` | `snapshot.h`, `spsc_channel.h`, `messages.h` (host `std::atomic`) | §6.4 |
| `src/hal/` | interfaces: `can.h` (`TxClass::Urgent`), `gpio.h`, `clock.h`, `watchdog.h` (`service()`) | — |
| `src/platform/host/` | `simulator.*` (deterministic 3-domain sim), `virtual_can.h`, `virtual_io.h` | §6.3 |
| `tests/` | 12 native test targets | — |

## Key design rules (enforced)

- **Domain purity** — domain code never logs, never reads a clock, never
  knows CAN IDs, never touches IPC. Time is passed in as `TimeUs`.
- **Wire encoding only in `protocol/`** — uses the generated subset codecs
  and the shared SES/SEB codecs (read-only deps).
- **HAL semantics** — `Can::transmit(..., TxClass::Urgent)` (no mailbox
  details), `Watchdog::service()` performs the hardware action (not a toggle).
- **Runtime-agnostic** — no 15-FreeRTOS-task shells; the deterministic
  simulator drives the same app/domain functions the future runtime will call.

## Functional units (15) — architecture §6.3

`can_rx_low`, `can_rx_high`, `dispatch`, `can_tx_low`, `can_tx_high`,
`heartbeat` (CPU0) · `safety`, `control`, `brake`, `watchdog` (CPU1) ·
`lights`, `mode`, `indicator`, `power`, `diag` (CPU2).

The simulator (`src/platform/host/simulator.*`) runs these as executors at
the required periods (control 10 ms, brake 20 ms, safety 50 ms, health
100 ms, lights 50 ms, mode 100 ms).

## Board HAL (`board/hal_aurix/aurix_hal.cpp`)

Implements `src/hal/*` over iLLD for the Lite Kit V2:

| Interface | iLLD binding |
|-----------|--------------|
| `AurixCan` | CAN0 Node 0 (low: P20.8/P20.7) + Node 2 (high: P15.0/P15.1), 500 kbit/s, `IfxCan_Can_*` |
| `AurixGpio` | rider inputs (pull-up) + relay outputs + CAN_STB (P20.6 low) + WDI (P33.1) |
| `AurixClock` | STM0 300 MHz → µs |
| `AurixWatchdog` | WDI pulse on P33.1 (TPS3850-Q1) |

Pin map: `board/board_pins.h` (frozen from iLLD `IfxCan_PinMap_TC37x_LQFP176`).

## Tests

| Target | Covers |
|--------|--------|
| `rta_kinematics_diff` | differential vs original `rt-esp32` physics_model |
| `rta_steering` | 6-state FSM, clamp, follow-error, ESTOP ramp/hold |
| `rta_brake` | 4-state FSM, priority chain, stroke raw values |
| `rta_mode` | toggle/ESTOP exit/HMI |
| `rta_safety` | EGAS L2, liveness, fault escalation |
| `rta_protocol_adapters` | codec round-trips (generated + SES/SEB) |
| `rta_route_table` | forwarding Category 1/2/3 |
| `rta_watchdog` | health decision |
| `rta_ipc` | snapshot/SPSC |
| `rta_controllers` | end-to-end typed flow |
| `rta_hal_compile` | interfaces implementable |
| `rta_simulator` | deterministic sim + fault injection |

See [`build-and-test.md`](build-and-test.md).
