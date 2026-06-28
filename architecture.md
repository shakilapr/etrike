# E-Trike System Architecture

Five ECUs on three CAN buses: Jetson Orin (ROS 2 perception), RT ESP32-S3 (realtime physics + steering + CAN gateway), SYS ESP32-S3 (safety + body), MTR STM32 (motor actuation), PWT ESP32-S3 (powertrain gateway).

> Full CAN catalog, processing summaries, pseudocode, and ASCII topology preserved in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 1. Topology

Three physical CAN buses, two gateways:

- **High-level CAN (500 kbit/s):** Jetson ↔ RT. Planning commands (0x300), telemetry (0x210, 0x310, 0x311), heartbeat (0x7FC/0x7FD).
- **Low-level CAN (500 kbit/s):** RT, SYS, PWT, EPS-C (steering), SEB (brake). Actuator commands, safety status, mode control, motor feedback.
- **Powertrain CAN (250 kbit/s):** PWT, DC-DC converter, motor controller. Power control, motor telemetry.

**RT bridges** selected messages between high and low buses. **PWT bridges** selected messages between low and powertrain. Both follow a three-category model: transparent forward, consumed→regenerated, bus-local.

- **Actuators:** Steering (EPS-C via 0x169), brake (SEB via 0x7B9). Mode-gated dual control: RT commands both in AUTO; SYS commands SEB in MANUAL/ESTOP.
- **Motor:** MTR STM32 drives analog throttle (MCP4725 DAC 0–5V) and gear relays (72V). CAN telemetry-only to motor controller.
- **DC-DC converter:** SYS commands enable (0x012), PWT bridges to powertrain bus.

> See [`docs/wiring-harness.md`](docs/wiring-harness.md) for pin-level wiring.

---

## 2. CAN Message Catalog

See [`can-dictionary.md`](can-dictionary.md) for the full bit-level catalog.

Key architectural IDs:

| Bus | ID | Name | Purpose |
|-----|-----|------|---------|
| High | `0x300` | HOST_DRIVE_CMD | Jetson → RT: speed, yaw, gear |
| High | `0x301` | HOST_BRAKE_REQ | Jetson → RT: brake kPa |
| High | `0x302` | HOST_LIGHT_CMD | Jetson → RT (→ SYS): lights |
| High | `0x400` | HOST_OBSTACLE_DIST | Jetson → RT: min obstacle mm |
| High | `0x210` | RT_STATE_RPT | RT → Jetson: mode, steer valid, reversing |
| High | `0x310` | STEER_DIAG | RT → Jetson: steering telemetry |
| High | `0x311` | BRAKE_DIAG | RT → Jetson: brake telemetry |
| Low | `0x001` | SAFETY_ESTOP | Any → All: emergency stop (bridged) |
| Low | `0x011` | SYS_SAFETY_STS | SYS → RT (→ Jetson): estop, hb, lights |
| Low | `0x110` | SYS_MODE_CMD | SYS → RT: mode (Manual/Auto/Estop) |
| Low | `0x204` | RT_DRIVE_CMD | RT → MTR, SYS: speed + gear |
| Low | `0x205` | RT_BRAKE_CMD | RT → SYS: brake kPa |
| Low | `0x169` | VCU_SES_REQ | RT → EPS-C: steering angle |
| Low | `0x201` | SES_STATUS | EPS-C → RT: angle feedback |
| Low | `0x7B9` | VCU_SEB_REQ | RT (AUTO) / SYS (MANUAL/ESTOP) → SEB: brake |
| Low | `0x721` | SEB_STATUS | SEB → SYS: stroke feedback |
| Low | `0x206` | MTR_MOTOR_FBK | MTR → SYS, RT: speed, gear, faults |
| Low | `0x7FD` | RT_HEARTBEAT | RT → SYS, Jetson: alive counter |
| Low | `0x7FE` | SYS_HEARTBEAT | SYS → RT: alive counter |
| Low | `0x7FB` | PWT_HEARTBEAT | PWT → RT, SYS: alive counter |

> Full forwarding rules, per-ECU receive/send tables, and ID-to-bus mapping in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 3. Mode State Machine

```
MANUAL ←→ AUTO ←→ ESTOP
   ↑                 │
   └──START button────┘
```

| Mode | Behavior |
|------|----------|
| **MANUAL** | Rider steers, throttle grip → MTR pass-through → motor. Brake lever → SYS → SEB. EPS-C standalone. DC-DC on. |
| **AUTO** | Jetson 0x300 → RT kinematics → 0x204 (speed) + 0x169 (steering). Lights from Jetson via 0x302. Brake via 0x7B9. |
| **ESTOP** | DAC=0V, gear OFF, steering ramps to 0° at 20°/s, brake=max. DC-DC stays ON. Exit: START button or mode long-press (3s). |

---

## 4. Signal Flow

### Manual Mode

```
Throttle grip → MTR ADC → MTR DAC → Motor controller
Gear selector → TLP281 opto → MTR GPIO → relays → ECU
Brake lever → SYS GPIO → 0x7B9 → SEB
Steering wheel → EPS-C standalone (RT monitors 0x201)
```

### Auto Mode

```
Jetson 0x300 → RT kinematics → 0x204 {speed,gear} → MTR → Motor
                              → 0x169 {angle} → EPS-C
Jetson 0x301 → RT brake arbitration → 0x205 → SYS → 0x7B9 → SEB
Jetson 0x302 → RT forward → 0x302 → SYS → lights
```

---

## 5. Responsibility Split

| Concern | Jetson | RT | SYS | MTR | PWT |
|---------|--------|-----|-----|-----|-----|
| Perception / planning | ✓ | | | | |
| ROS 2 → CAN bridge | ✓ | | | | |
| CAN gateway (high ↔ low) | | ✓ | | | |
| CAN gateway (low ↔ powertrain) | | | | | ✓ |
| Tricycle kinematics | | ✓ | | | |
| Steering compute + CAN TX | | ✓ | | | |
| Steering safety (clamp, following error) | | ✓ | | | |
| Obstacle speed limit | | ✓ | | | |
| Command staleness watchdog | | ✓ | | | |
| ESTOP GPIO + button | | | ✓ | ✓ | |
| Brake lever → CAN | | | ✓ | | |
| DC-DC converter control | | | ✓ | | ✓ |
| Heartbeat monitoring | | ✓ | ✓ | | ✓ |
| Mode switch | | | ✓ | | |
| Throttle ADC / DAC / gear I/O * | | | | ✓ | |
| Motor feedback CAN TX | | | | ✓ | |
| Lights / indicators / 12V relay | | | ✓ | | |
| System diagnostics | | | ✓ | | |

> * Motor I/O currently on SYS; target is MTR STM32 (migration pending hardware).

---

## 6. Design Principles

1. **Queues over shared state.** No mutexes. Thread-safe FreeRTOS queue pipes.
2. **ESTOP bypasses queues.** Safety task preempts and writes directly to actuators.
3. **One CAN ID = one sender per bus.** Heartbeats use per-node IDs (0x7FD RT, 0x7FE SYS, 0x7FC Jetson).
4. **Lower CAN ID = higher bus priority.** Safety IDs (0x00X) win arbitration.
5. **All multi-byte CAN fields big-endian** unless SYNTREE protocol specifies otherwise.
6. **Manual mode is pass-through, not dead.** SYS mirrors physical inputs to outputs.
7. **Actuators are standalone CAN modules.** Commanded via CAN, no direct MCU GPIO.
8. **RT is the only dual-bus node.** No direct Jetson ↔ SYS CAN path.
9. **Listen Before Speaking.** Actuators require status feedback before commands begin.
10. **EGAS 3-level motor safety.** MTR (STM32) = L1 function controller, SYS = L2 monitor, hardwired ESTOP = L3.
11. **Mode-gated dual control (Option D).** RT commands both steering and brake in AUTO (1-hop from kinematics). SYS commands brake in MANUAL/ESTOP. No single MCU failure takes both actuators.

> Full rationale, Options A-D comparison, and EGAS architecture detail in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 7. RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

**Role:** Converts Host 0x300 (speed+yaw+gear) into 0x204 (motor) + 0x169 (steering). Bridges CAN between high/low buses. Monitors SYS/Host liveness. Only operates in AUTO mode; silent in MANUAL.

**8 FreeRTOS tasks, dual CAN (TWAI GPIO5/4 + MCP2515 SPI GPIO36-40).**

### CAN I/O

| Frame | Dir | Rate | Purpose |
|-------|-----|------|---------|
| 0x300 | RX (high) | ≤100 Hz | Host drive → kinematics → 0x204+0x169 |
| 0x301 | RX (high) | demand | Host brake → max-select → 0x205 |
| 0x400 | RX (high) | 10 Hz | Obstacle distance → speed limit |
| 0x7FC | RX (high) | 2 Hz | Host heartbeat. Timeout 1500ms → assisted stop (2000 kPa). |
| 0x001 | RX+TX (both) | event | ESTOP. Forwarded bidirectionally. TXB2 priority on MCP2515. |
| 0x302 | RX+FW (high→low) | change | Host lights → transparent forward to SYS |
| 0x7FE | RX (low) | 10 Hz | SYS heartbeat. Timeout 200ms → RT brake takeover. |
| 0x201 | RX (low) | 100 Hz | EPS-C steering angle. Checksum-validated. |
| 0x202 | RX (low) | 10 Hz | EPS-C L3 faults → ESTOP |
| 0x721 | RX (low) | 100 Hz | SEB status. Pressure stored only in Pressure mode. |
| 0x204 | TX (low) | 100 Hz | Motor speed+gear. Gated: only in AUTO/ESTOP. |
| 0x205 | TX (low) | 50 Hz | Brake kPa → SYS. Gated: only in AUTO/ESTOP. |
| 0x169 | TX (low) | 100 Hz | Steering angle → EPS-C. Checksum XOR^0xFF. Gated: only in AUTO/ESTOP. |
| 0x210 | TX (high+low) | 10 Hz | Mode(byte0), safety_state(byte1:0-1), reversing(byte2), rx_overflow(byte3). SYS reads safety_state for takeover. |
| 0x310 | TX (high) | 10 Hz | Steering diag: angle(u16 BE, factor 0.1, offset -3000), fault, current, temp |
| 0x311 | TX (high) | 10 Hz | Brake diag: pressure, fault, current, temp |
| 0x7FD | TX (both) | 2 Hz | Independent counters per bus. Not bridged. |

### Safety State (0x210 byte 1)

| Value | Meaning | Trigger |
|-------|---------|---------|
| 0 | Normal | Steering ACTIVE |
| 1 | Internal ESTOP | Steering in RAMP_TO_ZERO or HOLD_THEN_SILENT |
| 2 | Fault | Steering FAULT (sync timeout, angle implausible) |

SYS reads this at 10 Hz. If `safety_state != 0`, SYS does NOT suppress its own 0x7B9 — it assumes RT is degraded and continues sending brake commands.

### MCP2515 (High Bus)

| Feature | Implementation |
|---------|---------------|
| Error interrupts | ERRIF + MERRE enabled. Bus-off detected in ~100µs via ISR. |
| RX rollover | BUKT=1. Overflow → RXB1 instead of drop. |
| ESTOP priority | ESTOP (0x001) → TXB2. All others → TXB0. TXB2 transmits first. |
| Listen-Only mode | `set_mode(ListenOnly)` API. Bus-safe monitoring. |
| Crystal | 8 MHz default. `-D MCP2515_16MHZ` for 16 MHz modules. |
| Bus-off recovery | Interrupt-driven fast path + 10 Hz polled fallback. Debounced 500ms. |

### Gateway Forwarding

| Direction | IDs | ESTOP prioritization |
|-----------|-----|---------------------|
| Low → High | 0x001, 0x011, 0x120, 0x206, 0x600 | 0x001 uses `xQueueSendToFront` |
| High → Low | 0x001, 0x302 | Same |

Gateway TX queues: depth 8. Overflow counter logged. ESTOP skips queue via send-to-front.

### Error Responses

| Failure | Detection | Response |
|---------|-----------|----------|
| SYS HB timeout (200ms) | `g_last_sys_hb_us` frozen-counter check | RT brake takeover: 0x7B9 max stroke. Zero setpoints. |
| Host HB timeout (1500ms) | `g_last_host_hb_us` | Zero drive + assist stop brake (2000 kPa). Mode stays AUTO. |
| Steering follow-error | |cmd−actual| > threshold for 300ms | ESTOP (0x001 both buses). |
| CAN bus-off (low/high) | 10 Hz TEC poll + interrupt (high) | Auto-recover init(). 5 consecutive → ESTOP or zero setpoints. |
| Command stale (500ms) | `g_watchdog.is_stale()` | Zero 0x204 + steering ESTOP. |
| EPS-C angle implausible | >30° at boot sync | Refuse ACTIVE → FAULT. |
| Task stalled >500ms | Per-task alive counters (control, dispatch, tx_low, tx_high) | Log ERROR. HW WDT (TPS3850) as ultimate backstop. |

### Task Watchdog

Four per-task alive counters (`g_alive_control`, `g_alive_dispatch`, `g_alive_tx_low`, `g_alive_tx_high`). Updated every task iteration. `t_watchdog` checks all counters at 10 Hz. Logs ERROR if any task >500ms stale. Hardware WDT (TPS3850) toggled from t_control at 100 Hz.

---

## 8. SYS ESP32-S3 — Vehicle Safety & Mode Authority

**Role:** SYS is the safety and mode authority. Physically wired to rider controls (ESTOP, Mode, Start buttons, brake lever). Owns the mode state machine — all nodes follow SYS's mode via 0x110. Two concern groups share the MCU:

| Group | Priority | Functions |
|-------|----------|-----------|
| A — Safety (ASIL) | 5–4 | ESTOP, mode, RT heartbeat, EGAS L2, brake control, CAN TX |
| B — Body (QM) | 3–1 | Lights, DCDC, indicators, 12V relay, diagnostics, heartbeat |

**15 FreeRTOS tasks. TWAI GPIO5/4 (low bus only). I2C DAC (MCP4725, migrating to MTR).**

### CAN I/O

| Frame | Dir | Rate | Purpose |
|-------|-----|------|---------|
| 0x001 | RX+TX | event | ESTOP. Rate-limited: max 2 per 500ms window. |
| 0x204 | RX | 100 Hz | RT drive setpoint → motor DAC (SYS_OWNS_MOTOR) + EGAS L2 monitor |
| 0x205 | RX | 50 Hz | RT brake kPa → SYS converts to 0x7B9 SEB pressure mode |
| 0x206 | RX | 50 Hz | MTR actual speed. EGAS L2: compare vs 0x204 setpoint. Fault flags (ESTOP_ACTIVE, StartupReady). |
| 0x210 | RX | 10 Hz | RT safety_state (byte1:0-1). Used for takeover detection. |
| 0x302 | RX | change | Host lights (RT-forwarded) → light control |
| 0x721 | RX | 100 Hz | SEB status. Checksum-validated (XOR^0xFF). Stroke feedback. |
| 0x731 | RX | 10 Hz | SEB L3 fault bits → ESTOP |
| 0x7FD | RX | 2 Hz | RT heartbeat. Frozen-counter detection. Timeout 1000ms → ESTOP. |
| 0x011 | TX | 5 Hz | Safety status: estop(byte0), hb_ok(byte1), light_state(byte2:0-3) |
| 0x012 | TX | 5 Hz + change | DC-DC enable. Periodic refresh every 5s. Always ON during ESTOP. |
| 0x110 | TX | change + 1s | Mode command. Periodic refresh prevents split-brain on frame loss. |
| 0x600 | TX | 1 Hz | Diag: mode, brake, hb, estop, heap, TEC/REC |
| 0x7B9 | TX | 50 Hz | SEB brake command. Suppressed in AUTO when RT is healthy and RT safety_state==Normal. |
| 0x7FE | TX | 10 Hz | SYS heartbeat. |

### 0x7B9 Suppression Logic

SYS suppresses its own 0x7B9 in AUTO mode to avoid dual-sender collision with RT:
```
suppress = (mode == AUTO) && rt_heartbeat_ok && rt_safety_state == Normal && !lever && !estop
```
When RT safety_state != Normal (InternalEstop or Fault), SYS does NOT suppress — it continues sending brake commands. This resolves the triple-sender issue (S2).

### Motor Ownership Gate

Motor actuation is gated by `#ifdef SYS_OWNS_MOTOR`:
- **Defined (current bench):** SYS writes DAC + drives gear relays. MTR is monitoring-only.
- **Undefined (future):** SYS is EGAS L2 monitor only. MTR owns all motor I/O.

### Error Responses

| Failure | Detection | Response |
|---------|-----------|----------|
| ESTOP GPIO (hardware) | GPIO1 LOW (NC) | Immediate: mode→ESTOP. CAN 0x001 broadcast. |
| RT heartbeat timeout (1000ms) | Frozen-counter on 0x7FD | ESTOP via 0x001. Brake=max. |
| MTR ESTOP ACK timeout (100ms) | No ESTOP_ACTIVE bit in 0x206 after ESTOP | Retrigger ESTOP. Set persistent `brake_fault`. |
| MTR feedback stale (200ms) | No 0x206 arrival | Zero speed setpoint + force Neutral. Set `brake_fault`. |
| EGAS L2 speed mismatch | \|0x204 − 0x206\| > 500mm/s for 500ms | ESTOP. |
| 0x204 staleness (200ms) | `g_last_setpoint_tick` | Zero speed + Neutral. |
| 0x721 SEB checksum fail | XOR(bytes 0-6)^0xFF ≠ byte 7 | Drop frame. |
| SEB L3 fault (0x731) | 16 L3 fault bits | ESTOP via 0x001. |
| CAN bus-off | 1 Hz TEC poll | Auto-recover init(). 5 consecutive → ESTOP. |
| DAC write(0) fails in ESTOP | `g_dac.write(0)` returns false | ESP_LOGE. Rely on hardware ESTOP GPIO (Level 3). |
| CAN TX mailbox full | `send_can()` retry once for 0x7B9/0x001 (20ms timeout) | Log failure, increment counter. |
| CAN RX queue overflow | `xQueueSend` returns false | Log warning, increment counter. |

### Task Watchdog

Four per-task alive counters (safety, brake, dispatch, can_tx). Updated every task iteration. `task_diag` checks all at 1 Hz. Logs ERROR if any >200ms stale (500ms for can_tx). HW WDT (TPS3850) toggled from task_safety.

---

## 9. Bench Bypass & Debug Tool

### 9.1 Bench Testing Without Full Hardware

The system has hardware dependencies on peer ECUs and SYNTREE actuators. For
bench testing with a single ESP32-S3 and a CAN bus analyzer, compile-time
bypass flags disable these dependencies:

| Flag | Effect | ECU |
|------|--------|-----|
| `CONFIG_BENCH_SOLO` | Disable cross-ECU heartbeat timeouts. Single board won't ESTOP. | RT, SYS |
| `CONFIG_BYPASS_EPS_C_SYNC` | Skip EPS-C listen-sync. Steering assumes centered (0°). | RT |
| `CONFIG_BYPASS_SEB_SYNC` | Skip SEB listen-sync. Brake operates in DEGRADED (lever-only). | SYS |
| `CONFIG_BYPASS_MTR_ABSENT` | Skip EGAS L2 speed monitoring. No MTR feedback required. | SYS |

These flags are in `platformio.ini` `build_flags`. Remove all `CONFIG_BENCH_*`
and `CONFIG_BYPASS_*` before vehicle deployment.

### 9.2 Debug Tool — Synthetic Peer ECUs

When bypass flags aren't sufficient (e.g., testing EGAS L2 with realistic MTR
data), the debug tool can inject synthetic CAN frames to simulate absent peers:

| Peer | Frame | Rate | What it simulates |
|------|-------|------|-------------------|
| EPS-C | 0x201 SES_STATUS | 100 ms | Centered, aligned. RT steering syncs. |
| SEB | 0x721 SEB_STATUS | 100 ms | Aligned, 0mm stroke. SYS brake syncs. |
| MTR | 0x206 MTR_MOTOR_FBK | 50 ms | Speed feedback. SYS EGAS L2 has data. |
| SYS | 0x7FE SYS_HEARTBEAT | 100 ms | SYS alive. RT heartbeat monitor satisfied. |
| RT | 0x7FD RT_HEARTBEAT | 500 ms | RT alive. SYS heartbeat monitor satisfied. |
| Host | 0x300 HOST_DRIVE_CMD | 100 ms | Drive commands. RT generates 0x204/0x169. |
| Host | 0x7FC HOST_HEARTBEAT | 500 ms | Host alive. RT heartbeat monitor satisfied. |

The debug tool has 12 injection templates (was 6). Templates are convenience
presets — the tool can inject any CAN frame via the encode API.

### 9.3 Bench Test Configurations

**Minimal bench (1 ECU + CANalyst-II):**
- Enable `CONFIG_BENCH_SOLO` + all `CONFIG_BYPASS_*` flags
- CANalyst-II injects Host frames (0x300, 0x7FC) on high bus
- No peer ECUs, no actuators

**Full bench (2 ECUs + CANalyst-II):**
- RT + SYS on low bus, CANalyst-II as Host on high bus
- Enable `CONFIG_BENCH_SOLO`, disable actuator bypasses
- CANalyst-II injects synthetic peer frames as needed

---

## 10. CAN Bus Device Maps

**Low-level (500 kbit/s):** RT, SYS, PWT, EPS-C, SEB

**High-level (500 kbit/s):** Jetson, RT

**Powertrain (250 kbit/s):** PWT, DC-DC converter, motor controller (telemetry-only)

> Full per-bus CAN ID tables in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 10. Hardware Summary

- **Motor controller:** Analog throttle 0–5V (MCP4725), gear 72V relays, CAN telemetry-only
- **EPS-C:** Steer-by-wire, 0x169 command @ 50 Hz, 0x201 feedback @ 100 Hz
- **SEB:** Electro-hydraulic brake, 0x7B9 command @ 50 Hz, 0x721 feedback @ 100 Hz
- **DC-DC converter:** 72V→12V, CAN enable via 0x012
- **Power:** 72V traction battery, 12V rail from DC-DC for MCUs + transceivers
- **Watchdog:** TPS3850 external on each MCU, toggled at 20–100 Hz

---

## 11. Build

All firmware builds with PlatformIO (`pio run`). Bench bypass flags documented in §9.

| ECU | Board | Framework | Key Flags |
|-----|-------|-----------|-----------|
| RT | esp32-s3-devkitc-1 | espidf | `-D CONFIG_BENCH_SOLO -D CONFIG_BYPASS_EPS_C_SYNC` (bench only) |
| SYS | esp32-s3-devkitc-1 | espidf | `-D SYS_OWNS_MOTOR -D CONFIG_BENCH_SOLO -D CONFIG_BYPASS_SEB_SYNC -D CONFIG_BYPASS_MTR_ABSENT` (bench only) |
| MTR | genericSTM32F103C8 | stm32cube | HAL calls uncommented; CubeMX `.ioc` pending |
| PWT | esp32-s3-devkitc-1 | espidf | 250k CAN |

Remove all `CONFIG_BENCH_*` and `CONFIG_BYPASS_*` before vehicle deployment.

---

## 12. Reference Documents

- [`can-dictionary.md`](can-dictionary.md) — Full CAN signal catalog
- [`docs/architecture-reference.md`](docs/architecture-reference.md) — Detailed tables, pseudocode, processing summaries
- [`docs/wiring-harness.md`](docs/wiring-harness.md) — Pin-level wiring
- [`docs/hil-safety-test-plan.md`](docs/hil-safety-test-plan.md) — HIL test scenarios
- [`docs/can-bench-test.md`](docs/can-bench-test.md) — Bench test plan
- [`shared/can/can_protocol.h`](shared/can/can_protocol.h) — C++ CAN message structs
