# PWT ESP32-S3 — Powertrain CAN Gateway

Fifth node in the five-node distributed e-trike architecture. Manages the 250 kbit/s powertrain CAN bus and bridges selected messages to the 500 kbit/s low-level CAN bus.

> **Relationship to main architecture:** This document describes the PWT node in detail. The parent [`architecture.md`](../architecture.md) shows the five-node topology and shared CAN protocol. All CAN IDs and signal layouts are defined in [`shared/can/can_signals.yaml`](../shared/can/can_signals.yaml) and [`shared/can/can_protocol.h`](../shared/can/can_protocol.h).

---

## 1. Role

PWT bridges two CAN buses at different speeds:

| Bus | Speed | Role |
|-----|-------|------|
| Low-Level CAN | 500 kbit/s | Connects to RT, SYS, MTR, EPS-C, SEB |
| Powertrain CAN | 250 kbit/s | Connects to DC-DC converter, motor controller |

**Primary functions:**
- Forward `0x012 SYS_DCDC_CMD` from 500k low bus → 250k powertrain bus (transparent forward, same ID/payload)
- Receive motor controller CAN telemetry on 250k bus, forward speed/status to 500k low bus
- Monitor powertrain bus health; ESTOP passthrough
- Heartbeat on 500k low bus

**Design principle:** PWT does NOT bridge to the high-level CAN bus. RT remains the sole gateway to Jetson. PWT is a low↔powertrain gateway only.

---

## 2. Topology

```
                            ┌─── Low-Level CAN (500 kbit/s) ───┐
                            │          │                        │
                            │    ┌─────▼──────┐                │
                            │    │ PWT ESP32-S3│                │
                            │    │             │                │
                            │    │ TWAI0       │                │
                            │    │ (500k low)  │                │
                            │    └─────┬──────┘                │
                            │          │                        │
                            └──────────┼────────────────────────┘
                                       │
                            ┌──────────▼────────────────────────┐
                            │  Powertrain CAN (250 kbit/s)      │
                            │          │                         │
                            │    ┌─────▼──────┐                │
                            │    │ PWT ESP32-S3│                │
                            │    │             │                │
                            │    │ TWAI1       │                │
                            │    │ (250k pwt)  │                │
                            │    └─────┬──────┘                │
                            │          │                         │
                            │  ┌───────▼──────┐  ┌───────────┐ │
                            │  │    DC-DC     │  │   Motor    │ │
                            │  │  Converter   │  │ Controller │ │
                            │  │  72V→12V     │  │            │ │
                            │  │ RX: 0x012    │  │ CAN: TBD   │ │
                            │  └──────────────┘  │ Analog:    │ │
                            │                     │ 0-5V thr   │ │
                            │                     │ 72V gear   │ │
                            │                     └───────────┘ │
                            └───────────────────────────────────┘
```

> **Motor controller note:** The motor controller still receives analog throttle (0–5V via MCP4725) and gear (72V via relays) from the MTR STM32. Its CAN interface on the 250k bus is **telemetry-only** (outputs speed, current, temperature, fault flags). Specific CAN IDs are TBD — to be documented when the motor controller CAN protocol is available.

---

## 3. CAN hardware

| Bus | Controller | Interface | GPIO | Transceiver | Bitrate |
|-----|-----------|-----------|------|-------------|---------|
| Low-level | Built-in TWAI0 | Direct | TX=5, RX=4 | SN65HVD230 | 500 kbit/s |
| Powertrain | Built-in TWAI1 | Direct | TX=7, RX=6 | SN65HVD230 | 250 kbit/s |

ESP32-S3 has two built-in TWAI controllers — no external MCP2515 needed. Both use SN65HVD230 3.3V CAN transceivers.

---

## 4. CAN messages

### 4.1 Messages received (500k low bus)

| ID | Name | Source | Action |
|----|------|--------|--------|
| `0x001` | SAFETY_ESTOP | RT, SYS, or any | Forward to 250k powertrain bus. PWT does NOT generate ESTOP — transparent forward only. |
| `0x012` | SYS_DCDC_CMD | SYS | Forward to 250k powertrain bus (transparent — same ID, same payload, same DLC) |
| `0x7FD` | RT_HEARTBEAT | RT | Feed RT alive counter. If frozen >1000ms → log; PWT continues bridging (failsafe pass-through). |
| `0x7FE` | SYS_HEARTBEAT | SYS | Feed SYS alive counter. If frozen >200ms → forward ESTOP to 250k bus? (TBD: depends on whether DC-DC/motor controller need emergency shutdown independent of main bus). |

### 4.2 Messages sent (500k low bus)

| ID | Name | Payload | Rate | Notes |
|----|------|---------|------|-------|
| `0x001` | SAFETY_ESTOP (fwd) | — | Event | Forwarded from 250k bus (if any powertrain device triggers ESTOP) |
| `0x7Fx` | PWT_HEARTBEAT (TBD) | `u8 alive_ctr` | 2 Hz | PWT liveness to RT/SYS on low bus. Exact ID TBD — must not collide with 0x7FD (RT), 0x7FE (SYS), 0x7FC (Jetson). Suggest `0x7FB`. |
| TBD | PWT_MOTOR_TELEMETRY | Motor speed, current, temp, faults | TBD | Forwarded from motor controller on 250k bus. CAN ID(s) TBD. |

### 4.3 Messages received (250k powertrain bus)

| ID | Name | Source | Action |
|----|------|--------|--------|
| TBD | Motor controller telemetry | Motor Controller | Forward speed/status to 500k low bus. Specific IDs and signal layout TBD — depends on motor controller CAN protocol. |
| `0x001` | SAFETY_ESTOP | Any | Forward to 500k low bus |

### 4.4 Messages sent (250k powertrain bus)

| ID | Name | Payload | Rate | Notes |
|----|------|---------|------|-------|
| `0x012` | SYS_DCDC_CMD (fwd) | `u8 enable` | Change | Transparent forward from SYS on 500k low bus |
| `0x001` | SAFETY_ESTOP (fwd) | — | Event | Forwarded from 500k low bus |

### 4.5 Gateway categories

PWT follows the same three-category model as RT (§2.3 of main architecture):

| Category | Direction | IDs | Notes |
|----------|-----------|-----|-------|
| Transparent forward | 500k → 250k | `0x012` | DC-DC command — same ID, same payload |
| Transparent forward | Bidirectional | `0x001` | ESTOP — must reach all buses |
| Bus-local (never forwarded) | 250k only | Motor controller telemetry (TBD) | Consumed by PWT, forwarded as different IDs on 500k bus |

---

## 5. Control mechanisms

### 5.1 DC-DC command bridging (0x012)

```
SYS → 0x012 on 500k low bus → PWT receives → PWT transmits 0x012 on 250k powertrain bus → DC-DC converter
```

- **Transparent forward** — same CAN ID (`0x012`), same DLC (1), same payload (`u8 enable`)
- **On change only** — SYS sends 0x012 on state change; PWT forwards immediately
- **No transformation** — the DC-DC converter sees the frame exactly as SYS sent it
- **No acknowledgment** — PWT does not confirm delivery; SYS DC-DC FSM already handles TX errors

### 5.2 Motor controller telemetry relay

```
Motor Controller → CAN frames on 250k powertrain bus → PWT receives → PWT republishes on 500k low bus (TBD IDs)
```

- Motor controller CAN protocol is **TBD** — specific IDs, signal layouts, and update rates depend on the motor controller model
- PWT will parse motor controller frames and republish relevant data (speed, current, temperature, faults) as new CAN frames on the 500k low bus
- RT can then forward motor telemetry to Jetson via high CAN (e.g., populating `0x310 STEER_DIAG` or new IDs)
- This provides **independent speed verification** — motor controller speed on CAN vs MTR estimated speed on `0x120` — useful for gap #5 (PID speed control)

### 5.3 ESTOP passthrough

```
Any bus → 0x001 → PWT receives → PWT transmits 0x001 on the other bus
```

- Bidirectional transparent forward (same as RT's 0x001 bridging)
- PWT does NOT generate ESTOP autonomously — it only forwards
- Rate-limited: max 2 forwards per 500ms (same as gap #14 mitigation)

### 5.4 Heartbeat

PWT sends a heartbeat on the 500k low bus only. It does NOT appear on the high bus (RT is the sole gateway to Jetson).

| Parameter | Value |
|-----------|-------|
| CAN ID | TBD (suggest `0x7FB`) |
| DLC | 1 |
| Payload | `u8 alive_ctr` (0–255, wraps) |
| Period | 500 ms (2 Hz) |

**Who monitors PWT:**
- **SYS**: PWT heartbeat timeout → log warning (PWT failure doesn't block DC-DC or motor — SYS still sends 0x012, it just won't reach DC-DC)
- **RT**: PWT heartbeat timeout → forward to Jetson for telemetry

**Startup grace period:** 3 seconds (same as other nodes).

---

## 6. RTOS task layout

**5 FreeRTOS tasks** on ESP32-S3 @ 240 MHz, 1000 Hz tick.

```
Pri 5  can_rx_low    ── TWAI0 (500k) → can_rx_low_queue (8)
       can_rx_pwt    ── TWAI1 (250k) → can_rx_pwt_queue (8)

Pri 4  dispatch      ◀── both RX queues
             Routes: low 0x012→pwt_tx_queue (DC-DC forward)
                     low 0x001→pwt_tx_queue (ESTOP forward)
                     pwt 0x001→low_tx_queue (ESTOP forward)
                     pwt motor telemetry→parse→motor_telemetry_queue

Pri 3  can_tx_low    ◀── low_tx_queue + motor_telemetry_queue
             → 0x001 (event), motor telemetry (TBD period), PWT heartbeat (2 Hz)

       can_tx_pwt    ◀── pwt_tx_queue
             → 0x012 (change), 0x001 (event)

Pri 1  heartbeat     ── 2 Hz PWT heartbeat on 500k low bus
```

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx_low` | 5 | 2048 B | Event | `twai_receive()` on TWAI0 (500k) → queue |
| `can_rx_pwt` | 5 | 2048 B | Event | `twai_receive()` on TWAI1 (250k) → queue |
| `dispatch` | 4 | 3072 B | Event | Route both RX queues: 0x012→pwt_tx, 0x001→both, motor→parse |
| `can_tx_low` | 3 | 2048 B | Event | Low bus TX: heartbeat (2 Hz), motor telemetry, ESTOP forward |
| `can_tx_pwt` | 3 | 2048 B | Event | Powertrain TX: DC-DC (change), ESTOP forward |
| `heartbeat` | 1 | 1536 B | 2 Hz | PWT alive counter on 500k low bus |

---

## 7. Hardware pin assignments

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX (low, 500k) | 5 | Out | SN65HVD230, TWAI0 |
| CAN RX (low, 500k) | 4 | In | SN65HVD230, TWAI0 |
| CAN TX (pwt, 250k) | 7 | Out | SN65HVD230, TWAI1 |
| CAN RX (pwt, 250k) | 6 | In | SN65HVD230, TWAI1 |
| WDT toggle | 21 | Out | External watchdog IC (TPS3850). Toggled by `can_tx_low` at 10 Hz. |

> GPIO 4/5 are the default TWAI0 pins on ESP32-S3. GPIO 6/7 are available for TWAI1. All ESP32-S3 GPIOs can be remapped via the GPIO matrix — final pin assignment subject to board layout.

---

## 8. Configuration constants

```cpp
namespace pwt {
// CAN
constexpr int kCanLowBitrateHz    = 500000;
constexpr int kCanPwtBitrateHz    = 250000;
constexpr int kCanLowTxGpio       = 5,  kCanLowRxGpio  = 4;
constexpr int kCanPwtTxGpio       = 7,  kCanPwtRxGpio  = 6;
// Timing
constexpr int kHeartbeatIntervalMs = 500;   // 2 Hz
constexpr int kDispatchLoopHz      = 100;
// Watchdog
constexpr int kWdtToggleGpio       = 21;
constexpr int kWdtToggleRateHz     = 10;
// CAN IDs (from shared/can/can_protocol.h)
//   kIdSysDcdcCmd (0x012) — forwarded 500k→250k
//   kIdSafetyEstop (0x001) — forwarded bidirectionally
//   kIdPwtHeartbeat (0x7FB) — TBD, PWT→SYS/RT on 500k low bus
// Queues
constexpr int kRxQueueLen = 8, kTxQueueLen = 8;
} // namespace pwt
```

---

## 9. Error handling

| Failure | Detection | Response |
|---------|-----------|----------|
| Low CAN bus-off (500k) | TWAI0 TEC > 255 | Log, auto-recover. DC-DC forwarding stops — DC-DC holds last state. |
| Powertrain CAN bus-off (250k) | TWAI1 TEC > 255 | Log, auto-recover. Motor telemetry gaps. |
| DC-DC TX fail (250k) | TWAI1 TX errors | Log. SYS DC-DC FSM retries on next state change. |
| Motor controller CAN stale | No frames for >TBD ms | Log warning; zero motor telemetry on 500k bus. Does NOT trigger ESTOP (motor controller is telemetry-only). |
| ESTOP flood on either bus | >2× 0x001 in 500ms | Rate-limit forwarding (same as gap #14). |
| External WDT timeout | TPS3850 MR pin | MCU hardware reset → all outputs safe state (no CAN transmission) |
| Queue full | `xQueueSend` fail | Frame dropped (0x001 bypasses queue — direct TX) |

---

## 10. Startup

```
 1. can_low_init()   → TWAI0, 500 kbit/s, GPIO4/5
 2. can_pwt_init()   → TWAI1, 250 kbit/s, GPIO6/7
 3. watchdog_init()  → GPIO21, armed by can_tx_low task
 4. Create queues    → can_rx_low(8), can_rx_pwt(8), low_tx(8), pwt_tx(8)
 5. Create 5 tasks
 6. ESP_LOGI("PWT Ready")
```

Startup time <500ms. DC-DC converter receives first `0x012` when SYS sends it (after SYS startup completes). Motor controller telemetry begins flowing as soon as the motor controller boots and transmits.

---

## 11. Build

```bash
cd pwt-esp32 && pio run && pio run -t upload && pio device monitor
```

> PlatformIO project not yet created — this is the architecture document. The `pwt-esp32/` directory will contain `platformio.ini`, `src/`, and `include/` when implementation begins.

---

## 12. Open items

| # | Item | Notes |
|---|------|-------|
| 1 | Motor controller CAN protocol | Specific CAN IDs, signal layouts, update rates TBD — depends on motor controller model selection |
| 2 | PWT heartbeat CAN ID | Suggest `0x7FB` — must not collide with `0x7FC`/`0x7FD`/`0x7FE` |
| 3 | Motor telemetry CAN IDs on 500k bus | Define when motor controller protocol is known |
| 4 | SYS heartbeat loss → ESTOP forward to 250k? | Does DC-DC or motor controller need emergency shutdown independent of main bus? |
| 5 | PlatformIO project | Create `pwt-esp32/` firmware skeleton when implementation begins |
