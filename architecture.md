# E-Trike System Architecture

Five ECUs are planned on three CAN buses: Jetson Orin (ROS 2 perception), RT ESP32-S3 (realtime physics + steering + CAN gateway), SYS ESP32-S3 (safety + body), MTR STM32 (planned motor actuation), and PWT ESP32-S3 (standalone powertrain node).

> **Deployment status:** RT high-to-low CAN is implemented. MTR hardware initialization and ESTOP are incomplete, so no vehicle motor-actuation path is approved. PWT has one 250 kbit/s CAN interface; it is not a low-to-powertrain gateway. See [`docs/gpio-esp32-audit.md`](docs/gpio-esp32-audit.md).

> **Current versus target:** Statements marked as implemented describe the checked-in code. Planned or target behavior remains architectural intent and must not be treated as bench evidence until its referenced implementation and tests exist.

## CAN contract ownership and change impact

The CAN architecture uses controlled ownership rather than assuming every behavior can be generated.

| Concern | Authority |
|---|---|
| Wire facts | `shared/can/can_high.yaml`, `can_low.yaml`, and the standalone PWT manufacturer YAML |
| Ordinary payload codecs and metadata | Deterministic generated artifacts under `shared/can/generated/` |
| Vendor algorithms that cannot yet be generated safely | `shared/can/manual-mappings.yaml` plus adapters under `shared/can/manual/` |
| Runtime policy | Named component configuration: allowed misses, retries, escalation and logging policy |
| Hardware facts | Component board/driver configuration: GPIO, I2C/SPI, oscillator and calibration |

Every wire value must be generated, centrally named, or registered as a tested manual mapping. Generated files are never edited manually. A manual mapping records a stable ID, source message, reviewed per-message wire hash, adapter, consumers, tests and affected build targets. This preserves traceability without pretending that vendor checksums, overlapping layouts or stateful interpretation are automatically generated.

The normal change workflow is:

```text
python tools/can_change.py inspect MESSAGE_OR_ID
edit the authoritative YAML or explicitly owned policy/hardware configuration
python shared/can/generate_code.py
review every reported manual mapping and independent vector
python tools/can_change.py verify MESSAGE_OR_ID
build the targets reported by the inspection result
```

`codec_manifest.json` contains bus-instance and signal metadata. `change_impact.json` links each message to its source, wire hash, generated type, manual exception, consumers, tests and builds. Both have structured JSON suitable for developer tools, the Control UI backend and LLM clients. This metadata is implemented; automatic backend exposure remains Control UI target work.

> Full CAN catalog, processing summaries, pseudocode, and ASCII topology preserved in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 1. Topology

Three physical CAN buses, with one implemented gateway:

- **High-level CAN (500 kbit/s):** Jetson ↔ RT. Planning commands (0x300), telemetry (0x210, 0x310, 0x311), heartbeat (0x7FC/0x7FD).
- **Low-level CAN (500 kbit/s):** RT, SYS, EPS-C (steering), SEB (brake). Actuator commands, safety status, and mode control.
- **Powertrain CAN (250 kbit/s):** PWT, DC-DC converter, motor controller. Power control, motor telemetry.

**RT bridges** selected messages between high and low buses. A future low-to-powertrain bridge requires a second CAN controller on PWT or a different MCU.

- **Actuators:** Steering (EPS-C via 0x169), brake (SEB via 0x7B9). Mode-gated dual control: RT commands both in AUTO; SYS commands SEB in MANUAL/ESTOP.
- **Motor:** The planned MTR STM32 path is not hardware-complete. SYS direct motor I/O is compile-time disabled because its ADC map conflicts with body I/O.
- **DC-DC converter:** PWT currently sends its direct powertrain command only; SYS-to-PWT command forwarding is not implemented.

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
| High | `0x210` | RT_STATE_RPT | RT → Jetson: mode, safety_state+estop_reason (packed byte 1), reversing, rx_overflow, task_health, steer_state (DLC=6) |
| High | `0x220` | RT_PID_RPT | RT → Jetson: shadow PID telemetry (DLC=6) |
| High | `0x310` | STEER_DIAG | RT → Jetson: steering telemetry (DLC=8) |
| High | `0x311` | BRAKE_DIAG | RT → Jetson: brake telemetry (DLC=8) |
| High | `0x111` | HMI_MODE_REQ | HMI → SYS, Host: mode request (bridged to low, 1 Hz) |
| High | `0x112` | HMI_PWR_REQ | HMI → SYS: power request (bridged to low, 1 Hz) |
| Low | `0x001` | SAFETY_ESTOP | Any → All: emergency stop (bridged) |
| Low | `0x011` | SYS_SAFETY_STS | SYS → RT (→ Jetson): estop, hb, lights |
| Low | `0x110` | SYS_MODE_CMD | SYS → RT: mode (Manual/Auto only) |
| Low | `0x204` | RT_DRIVE_CMD | RT → MTR, SYS: speed + gear |
| Low | `0x205` | RT_BRAKE_CMD | RT → SYS: brake kPa |
| Low | `0x169` | VCU_SES_REQ | RT → EPS-C: steering angle |
| Low | `0x201` | SES_STATUS | EPS-C → RT: angle feedback (DLC=8, XOR checksum) |
| Low | `0x202` | SES_ERR_INFO | EPS-C → RT: L3 fault bits → ESTOP (DLC=8) |
| Low | `0x203` | SES_VERSION | EPS-C → RT: SW/HW version, logged once (DLC=8) |
| Low | `0x6FA` | SES_TEST | EPS-C → RT: motor current, ECU temp, voltage (DLC=8) |
| Low | `0x6FB` | SEB_TEST | SEB → SYS: motor current, ECU temp (DLC=8) |
| Low | `0x741` | SEB_VERSION | SEB → SYS: SW/HW version, logged once (DLC=8) |
| Low | `0x7B9` | VCU_SEB_REQ | RT (AUTO) / SYS (MANUAL/ESTOP) → SEB: brake (DLC=8, XOR checksum) |
| Low | `0x721` | SEB_STATUS | SEB → SYS: stroke feedback |
| Low | `0x120` | SYS_THROTTLE_STS | MTR → RT (→ Host): actual throttle speed (DLC=2) |
| Low | `0x206` | MTR_MOTOR_FBK | MTR → SYS, RT: speed, gear, faults (DLC=4) |
| Low | `0x7FD` | RT_HEARTBEAT | RT → SYS, Jetson: alive counter + health flags (DLC=2) |
| Low | `0x7FE` | SYS_HEARTBEAT | SYS → RT: alive counter + health flags (DLC=2) |

> Full forwarding rules, per-ECU receive/send tables, and ID-to-bus mapping in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 3. Mode and ESTOP State

The mode button toggles between two operating modes: MANUAL and AUTO.
ESTOP is a **safety state** overlaid on the current mode, triggered exclusively
by the hardware ESTOP button (GPIO1), CAN 0x001, or safety faults — never by
the mode button or CAN 0x110.

```
MANUAL ←→ AUTO       (mode button)
   ↓       ↓
  ESTOP ←────────── (hardware button, CAN 0x001, safety faults)
   |
   └──START button──→ MANUAL
   └──MODE long-press─→ MANUAL
```

| State | Behavior |
|------|----------|
| **MANUAL** | Rider steers. Brake lever → SYS → SEB. EPS-C standalone. Vehicle motor actuation remains blocked pending MTR hardware completion. |
| **AUTO** | Jetson 0x300 → RT kinematics → 0x204 (planned motor command) + 0x169 (steering). Lights from Jetson via 0x302. Brake via 0x7B9. |
| **ESTOP** | Steering ramps to 0° at 20°/s and brake=max. Motor hardware kill behavior is a release blocker until MTR ESTOP hardware exists. Exit: START button or mode long-press (3s). |

---

## 4. Signal Flow

### Manual Mode

```
Throttle/gear motor path → **blocked pending MTR hardware implementation**
Brake lever → SYS GPIO → 0x7B9 → SEB
Steering wheel → EPS-C standalone (RT monitors 0x201)
```

### Auto Mode

```
Jetson 0x300 → RT kinematics → 0x204 {speed,gear} → planned MTR → Motor
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
| CAN gateway (low ↔ powertrain) | | | | | planned |
| Tricycle kinematics | | ✓ | | | |
| Steering compute + CAN TX | | ✓ | | | |
| Steering safety (clamp, following error) | | ✓ | | | |
| Obstacle speed limit | | ✓ | | | |
| Command staleness watchdog | | ✓ | | | |
| ESTOP GPIO + button | | | ✓ | planned | |
| Brake lever → CAN | | | ✓ | | |
| DC-DC converter control | | | planned | | ✓ |
| Heartbeat monitoring | | ✓ | ✓ | | planned |
| Mode switch | | | ✓ | | |
| Throttle ADC / DAC / gear I/O | | | retired | planned | |
| Motor feedback CAN TX | | | | planned | |
| Lights / indicators / 12V relay | | | ✓ | | |
| System diagnostics | | | ✓ | | |

> Motor I/O is not approved on SYS or MTR until the MTR HAL, CAN, ADC, DAC, and direct ESTOP hardware have been implemented and tested.

---

## 6. Design Principles

1. **Queues over shared state.** No mutexes between FreeRTOS tasks — thread-safe FreeRTOS queue pipes for all inter-task communication. **Exception:** MCP2515 SPI bus uses a FreeRTOS mutex (`g_spi_mutex`) because ESP-IDF's SPI master driver was observed to assertion-fail (`ret_trans == trans_desc`) under concurrent access from rx_high(prio5), tx_high(prio3), control(prio4), and heartbeat(prio1). All `spi_device_transmit()` calls are wrapped in `spi_lock()`/`spi_unlock()`.
2. **ESTOP bypasses queues.** Safety task preempts and writes directly to actuators.
3. **One CAN ID = one sender per bus.** Heartbeats use per-node IDs (0x7FD RT, 0x7FE SYS, 0x7FC Jetson).
4. **Lower CAN ID = higher bus priority.** Safety IDs (0x00X) win arbitration.
5. **All multi-byte CAN fields big-endian** unless actuator protocol specifies otherwise.
6. **Manual mode is pass-through, not dead.** SYS mirrors physical inputs to outputs.
7. **Actuators are standalone CAN modules.** Commanded via CAN, no direct MCU GPIO.
8. **RT is the only dual-bus node.** No direct Jetson ↔ SYS CAN path.
9. **Listen Before Speaking.** Actuators require status feedback before commands begin.
10. **EGAS 3-level motor safety.** MTR (STM32) = L1 function controller, SYS = L2 monitor, hardwired ESTOP = L3.
11. **Mode-gated dual control (Option D).** RT commands both steering and brake in AUTO (1-hop from kinematics). SYS commands brake in MANUAL/ESTOP. No single MCU failure takes both actuators.
12. **Global state in a single translation unit.** `main.cpp` is the owner of all global state. `rt_state.h` declares externs for every global object, atomic, and queue. Cross-file access goes through the header. No hidden global state in .cpp files.
13. **Atomic sensor pipeline.** CAN RX task writes decoded values to atomics. Control task reads atomics, computes physics, writes setpoint atomics. TX task reads setpoint atomics, sends CAN frames. No locks — atomic load/store with relaxed memory order (single-writer, single-reader per atomic).
14. **Safety events use a queue.** Safety conditions (ESTOP, mode change) are enqueued as `SafetyEvent` structs on `g_safety_evt_q` (depth 16). The control task drains the queue at the start of each 100 Hz cycle. The queue guarantees no missed events (unlike flag atomics which can be overwritten). ESTOP events use `xQueueSendToFront` for priority.
15. **Dual heartbeat — independent per bus.** RT sends 0x7FD on both high and low buses with independent alive counters. Receivers track per-bus liveness independently using frozen-counter detection (timestamp only updated when counter changes). Heartbeats are never forwarded between buses.
16. **Layered watchdog.** Per-task alive counters checked at 10 Hz (RT) / 1 Hz (SYS) → log ERROR on stall. External TPS3850 hardware WDT toggled from control task. ESP-IDF task WDT monitors idle tasks. Three independent watchdog layers.

> Full rationale, Options A-D comparison, and EGAS architecture detail in [`docs/architecture-reference.md`](docs/architecture-reference.md).

### Task Priority Architecture

FreeRTOS priorities reflect the safety criticality and data-flow order:

| Priority | RT Tasks | SYS Tasks | Rationale |
|----------|----------|-----------|-----------|
| 5 | rx_low, rx_high | can_rx, safety | CAN RX must never be delayed. Safety is highest-app priority. |
| 4 | dispatch, control | dispatch, mode, motor | Data processing and control. Mode authority. |
| 3 | tx_low, tx_high | throttle, gear, brake, lights | CAN TX and actuation. |
| 2 | — | indicator, power, can_tx | Body control and TX. |
| 1 | watchdog, heartbeat | diag, heartbeat | Lowest — background monitoring. |

### CAN Health Monitoring Architecture

Two ECUs monitor their own CAN bus health independently:

| Path | Trigger | Check Rate | Action |
|------|---------|------------|--------|
| TWAI TEC poll | `g_can_low.get_error_counters()` | 10 Hz (RT), 1 Hz (SYS) | TEC>128 → warning. TEC≥255 → bus-off → `init()` recovery. 5 consecutive bus-off → ESTOP. |
| MCP2515 interrupt | ERRIF/MERRE in `receive()` ISR | ~100µs latency | Bus-off detected immediately via EFLG TXBO bit. Sets `m_bus_off` flag. |
| MCP2515 polled fallback | `get_error_counters()` | 10 Hz | Catches bus-off if ISR was missed. Debounced 500ms between reinit attempts. |

Recovery: `twai_initiate_recovery()` for TWAI (waits 128×11 recessive bits). Full `init()` reinstall for MCP2515 (stop→uninstall→install→start).

### Gateway Forwarding Architecture

```
Low Bus RX Queue (depth 16) ──→ dispatch ──→ gateway TX queues (depth 8)
High Bus RX Queue (depth 16) ──→ (prio 4) ──→ tx_low (prio 3) / tx_high (prio 3)
```

ESTOP (0x001) uses `xQueueSendToFront` to skip the queue. All other forwarded frames use normal `xQueueSend` with 0 timeout (non-blocking — dropped if queue full, counter incremented).

### CAN TX Recovery Logging

Each CAN TX path tracks failures and recovery independently:
- First failure → `ESP_LOGW` (warning) + set `had_failure` flag
- Subsequent failures → silent (counter incremented)
- First success after failure → `ESP_LOGI` (recovery) with fail/ok counts + clear `had_failure`
- This avoids log spam while still detecting intermittent CAN issues

### NVS Crash Persistence (SYS Only)

SYS persists reset reason and boot count to NVS flash:
- `esp_reset_reason()` read at boot → stored to NVS
- Boot counter incremented, logged at INFO
- NVS initialized with migration handling (`ESP_ERR_NVS_NO_FREE_PAGES`)
- Provides post-mortem crash analysis without external debugger

---

## 7. RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

**Role:** Converts Host 0x300 (speed+yaw+gear) into 0x204 (motor) + 0x169 (steering). Bridges CAN between high/low buses. Monitors SYS/Host liveness. Only operates in AUTO mode; silent in MANUAL.

**6–8 FreeRTOS tasks (varies by hardware):** `rx_low`, `rx_high`*, `dispatch`, `control`, `tx_low`, `tx_high`*, `watchdog`, `heartbeat`. Tasks marked * are only created if MCP2515 init succeeds — system degrades gracefully to 6 tasks when high CAN is absent. All tasks pinned to CPU0. Dual CAN (TWAI GPIO5/4 + MCP2515 SPI GPIO36-40).

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
| 0x169 | TX (low) | 50 Hz current wire schedule; state-machine timing still assumes 100 Hz | Steering angle → EPS-C. Checksum XOR^0xFF. Gated: only in AUTO/ESTOP. Open timing decision described in §15. |
| 0x210 | TX (high+low) | 10 Hz | Mode(byte0), safety_state(byte1:0-1), reversing(byte2), rx_overflow(byte3). SYS reads safety_state for takeover. |
| 0x310 | TX (high) | 10 Hz | Steering diag: angle(u16 BE, factor 0.1, offset -3000), fault, current, temp |
| 0x311 | TX (high) | 10 Hz | Brake diag: pressure, fault, current, temp |
| 0x220 | TX (high) | 10 Hz | Shadow PID telemetry (setpoint, measured, output). 6 bytes. |
| 0x6FA | RX (low) | 100 Hz | EPS-C telemetry: motor current, ECU temp, voltage. Logs warnings on thresholds. |
| 0x6FB | RX (low) | 100 Hz | SEB telemetry: motor current, ECU temp. Used for BRAKE_DIAG rescaling. |
| 0x203 | RX (low) | 1 Hz | EPS-C version. Logged once on first receipt. |
| 0x7FD | TX (both) | 2 Hz | Independent counters per bus. DLC=2 (counter + health flags). Not bridged. |

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
| TX buffer priority | TXB2 (highest) → ESTOP (0x001). TXB1 (medium) → telemetry (0x310, 0x311, 0x220). TXB0 (normal) → everything else. Prevents gateway bursts from delaying diagnostic frames. |
| Error interrupts | ERRIF + MERRE enabled. Bus-off detected in ~100µs via ISR on GPIO40 (NEGEDGE, IRAM_ATTR). ISR uses `vTaskNotifyGiveFromISR` to wake rx task. Null-guarded for cold-boot window. |
| RX rollover | BUKT=1. Overflow → RXB1 instead of drop. Two-buffer pending frame cache eliminates second SPI read when both buffers fill. |
| SPI mutex | FreeRTOS `SemaphoreHandle_t` serializes all SPI transactions across 4 tasks. Present because ESP-IDF driver assertion-fails under concurrent access. |
| Cold-boot retry | Up to 4 attempts (200/400/600ms backoff). MCP2515 oscillator needs up to 128ms to stabilize. Polls CANSTAT for config mode (0x80). |
| Listen-Only mode | Bench only (`CONFIG_BENCH_SOLO`). Prevents TX error accumulation from un-ACKed frames when using CANalyst-II. |
| Crystal | 8 MHz default. `-D MCP2515_16MHZ` for 16 MHz modules. |
| Bus-off recovery | Interrupt-driven fast path + 10 Hz polled fallback. `fast_path_handled` guard prevents polled path from racing ISR reinit. Debounced 500ms. |

### Kinematics (Inverse Bicycle Model)

`PhysicsModel::resolve()` converts Host drive command (speed, yaw) to motor speed + steer angle. Standard inverse bicycle model with three speed regimes:

| Regime | Condition | Behavior |
|--------|-----------|----------|
| Normal | \|v\| ≥ 50mm/s | `steer = atan2(L * yaw, v)`. Signed v for reverse handling. |
| Low-speed decay | \|v\| < 50mm/s, w ≈ 0 | `steer *= 0.8` per cycle → straight |
| Zero-speed yaw | \|v\| ≈ 0, \|w\| > 0.001 | Full steering lock in yaw direction, speed=0 (no forward lurch) |

Obstacle response: `obstacle_limit()` scales speed 0→full (300mm→3000mm). `obstacle_to_kpa()` scales brake 5000kPa→0 (300mm→3000mm). Max-selected with Host brake kPa.

### PID Controller (Shadow, Future Active)

Shadow PID (`SpeedController::update_shadow_pid()`) computes correction for telemetry only:

| Parameter | Value | Notes |
|-----------|-------|-------|
| Kp, Ki, Kd | 1.0, 0.1, 0.05 | Gains |
| Algorithm | D-on-measurement | Avoids derivative kick |
| Anti-windup | Conditional integration | Stops when output saturated |
| I-reset | On setpoint change >500mm/s | Prevents integral windup |
| Encoder guard | `measured==0 → output=0, PID reset` | Prevents spurious correction |

Active PID (`CONFIG_ENABLE_ACTIVE_PID`): correction injected into motor setpoint. Disabled by default — requires physical encoder installed, quadrature verified, PCNT enabled, speed validated on 0x220 telemetry, and no-load bench test passed.

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

**12 FreeRTOS tasks. TWAI GPIO5/4 (low bus only). MTR owns all motor I/O in vehicle.**

### CAN I/O

| Frame | Dir | Rate | Purpose |
|-------|-----|------|---------|
| 0x001 | RX+TX | event | ESTOP. Rate-limited: max 2 per 500ms window. |
| 0x204 | RX | 100 Hz | RT drive setpoint → EGAS L2 monitor |
| 0x205 | RX | 50 Hz | RT brake kPa → SYS converts to 0x7B9 SEB pressure mode |
| 0x206 | RX | 50 Hz | MTR actual speed. EGAS L2: compare vs 0x204 setpoint. Fault flags (ESTOP_ACTIVE, StartupReady). |
| 0x210 | RX | 10 Hz | RT safety_state (byte1:0-1). Used for takeover detection. |
| 0x302 | RX | change | Host lights (RT-forwarded) → light control |
| 0x721 | RX | 100 Hz | SEB status. Checksum-validated (XOR^0xFF). Stroke feedback. |
| 0x731 | RX | 10 Hz | SEB L3 fault bits → ESTOP |
| 0x7FD | RX | 2 Hz | RT heartbeat. Frozen-counter detection. Timeout 1000ms → ESTOP. |
| 0x011 | TX | 5 Hz | Safety status: estop(byte0), hb_ok(byte1), light_state(byte2:0-3) |
| 0x110 | TX | change + 1s | Mode command. Periodic refresh prevents split-brain on frame loss. |
| 0x111 | RX | 1 Hz | HMI Mode Request. Evaluated by mode manager (ignored in ESTOP). |
| 0x112 | RX | 1 Hz | HMI Power Request. |
| 0x600 | TX | 1 Hz | Diag: mode, brake, hb, estop, heap, TEC/REC |
| 0x7B9 | TX | 50 Hz | SEB brake command. Suppressed in AUTO when RT is healthy and RT safety_state==Normal. |
| 0x6FB | RX | 100 Hz | SEB telemetry: motor current, ECU temp. Logs warning >80°C. |
| 0x741 | RX | 1 Hz | SEB version. Logged once on first receipt. |
| 0x7FE | TX | 10 Hz | SYS heartbeat. DLC=2 (counter + health flags: hb_ok, estop, mode, can_ok). |

### 0x7B9 Suppression Logic

SYS suppresses its own 0x7B9 in AUTO mode to avoid dual-sender collision with RT:
```
suppress = (mode == AUTO) && rt_heartbeat_ok && rt_safety_state == Normal && !lever && !estop
```
When RT safety_state != Normal (InternalEstop or Fault), SYS does NOT suppress — it continues sending brake commands. This resolves the triple-sender issue (S2).



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

The system uses a unified 3-mode configuration via `SYSTEM_RUN_MODE` in `shared/system_mode.h`:

| Mode | Name | Effect |
|------|------|--------|
| 0 | PRODUCTION | Strict safety, requires real hardware. No bypasses allowed. |
| 1 | PROTOTYPE | Checks physical developer override pin (GPIO 35) to dynamically enable bypasses. |
| 2 | PURE SIM | Mocks everything. Disables cross-ECU heartbeat timeouts and actuator syncs. |

The physical override pin allows safe lab testing on real hardware without flashing a compromised binary.

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
- Set `SYSTEM_RUN_MODE = 2`
- CANalyst-II injects Host frames (0x300, 0x7FC) on high bus
- No peer ECUs, no actuators

**Full bench (2 ECUs + CANalyst-II):**
- RT + SYS on low bus, CANalyst-II as Host on high bus
- Set `SYSTEM_RUN_MODE = 1` and jump GPIO 35 to GND.
- CANalyst-II injects synthetic peer frames as needed

---

## 10. CAN Bus Device Maps

**Low-level (500 kbit/s):** RT, SYS, MTR, EPS-C, SEB
**High-level (500 kbit/s):** Jetson, RT
**Powertrain (250 kbit/s):** PWT, DC-DC converter, motor controller (telemetry-only)

## 11. Hardware Summary

- **Motor controller:** Analog throttle 0–5V (MCP4725), gear 72V relays, CAN telemetry-only
- **EPS-C:** Steer-by-wire, 0x169 command @ 50 Hz, 0x201 feedback @ 100 Hz
- **SEB:** Electro-hydraulic brake, 0x7B9 command @ 50 Hz, 0x721 feedback @ 100 Hz
- **DC-DC converter:** 72V→12V, direct PWT extended command `0x10262B27` on the 250 kbit/s powertrain bus
- **Power:** 72V traction battery, 12V rail from DC-DC for MCUs + transceivers
- **Watchdog:** TPS3850 external on each MCU, toggled at 20–100 Hz

---

## 12. Build Profiles

All firmware builds with PlatformIO. Three environments per ECU:

| Environment | Purpose | Flags |
|---|---|---|
| `[env:vehicle]` | Production. | `FW_VERSION` only |
| `[env:bench]` | Bench testing convenience. | `FW_VERSION` only |
| `[env:native]` | Host-side validation. PlatformIO `native`. | `HOST_BUILD`, `TESTING`, shadow HAL headers |

**Native test environment:** `[env:native]` compiles selected source files for the host OS using shadow HAL headers from `native-test/hal/shadow/`. Produces a console executable that validates physics, kinematics, mode manager, and safety monitor. No ESP32 hardware required. Run via `pio test -e native`.

| ECU | Board | Framework | Vehicle Flags | Bench Flags |
|-----|-------|-----------|--------------|-------------|
| RT | esp32-s3-devkitc-1 | espidf | `-D CONFIG_FREERTOS_HZ=1000 -D CONFIG_TWAI_ISR_IN_IRAM=1` | Same as Vehicle |
| SYS | esp32-s3-devkitc-1 | espidf | `-D CONFIG_FREERTOS_HZ=1000 -D CONFIG_TWAI_ISR_IN_IRAM=1` | `TESTING` |
| MTR | genericSTM32F103C8 | stm32cube | HAL calls | Same as Vehicle |
| PWT | esp32-s3-devkitc-1 | espidf | 250k CAN | Same as Vehicle |

**ESP-IDF requirement:** 5.0 or later (`#error` guard in both RT and SYS `main.cpp`).

---

## 13. Hardware Pin Assignments

### RT ESP32-S3

| GPIO | Function | Notes |
|------|----------|-------|
| 4 | TWAI RX | Low CAN bus |
| 5 | TWAI TX | Low CAN bus |
| 21 | WDT toggle | TPS3850 external watchdog, toggled at 100 Hz by t_control |
| 15 | MCP2515 SCK | SPI clock, 8 MHz |
| 16 | MCP2515 MOSI | SPI data out |
| 17 | MCP2515 MISO | SPI data in |
| 18 | MCP2515 CS | SPI chip select |
| 47 | MCP2515 INT | Interrupt (active low, pull-up, NEGEDGE) |
| 35 | OVERRIDE | Developer override pin for Mode 1 |
| 1-2 | Encoder rear motor | Quadrature PCNT (rear motor speed feedback) |
| 10,6 | Encoder front wheel | Quadrature PCNT (front wheel) |
| 9,12 | Encoder rear left | Quadrature PCNT (differential) |
| 13,14 | Encoder rear right | Quadrature PCNT (differential) |

### SYS ESP32-S3

| GPIO | Function | Notes |
|------|----------|-------|
| 4 | TWAI RX | Low CAN bus |
| 5 | TWAI TX | Low CAN bus |
| 1 | ESTOP button | NC contact from 3.3 V with external pull-down; LOW/open circuit = ESTOP. |
| 2 | Brake lever | Active-low, pull-up. Rider brake input for MANUAL mode. |
| 8 | Ignition relay | Reserved; production firmware does not drive this pin. |
| 11 | Mode button | Momentary, toggles MANUAL↔AUTO |
| 12-14 | Gear sense | Retired SYS motor-I/O reservation; do not wire to vehicle. |
| 15-16 | Throttle I2C | Retired SYS motor-I/O reservation; do not wire to vehicle. |
| 17 | Bulb READY | Green indicator — system ready (AUTO/MANUAL, RT alive, no faults) |
| 18 | Light left turn | Relay output |
| 19 | Light right turn | Relay output |
| 20 | Bulb ESTOP | Red indicator — dedicated ESTOP indicator |
| 21 | Light brake | Relay output |
| 10 | Light head | Relay output |
| 47 | WDT toggle | TPS3850 external watchdog, toggled at 20 Hz by task_safety |
| 48 | Bulb AUTO | Mode indicator |
| 39 | Bulb MANUAL | Mode indicator |
| 40 | 12V relay | Accessory power relay |
| 41 | START button | Green momentary — press=ignition ON, hold 3s=OFF |
| 33-35 | Gear outputs | Retired SYS motor-I/O reservation; do not wire to vehicle. |

### MTR STM32 (planned - not approved for vehicle wiring)

| Pin | Function | Notes |
|-----|----------|-------|
| TBD | CAN / ESTOP / ADC / DAC / gear I/O | No CubeMX hardware configuration or validated pin assignment exists. |

---

## 14. MTR STM32 — Planned Motor Actuation

**Status:** Source-level control logic exists, but the CubeMX GPIO, ADC, I2C, CAN, and ESTOP hardware layers are not implemented. This section is a target design, not a vehicle-ready implementation.

The following task and safety details are design targets only. They do not make the MTR ECU deployable until its peripheral initialization and direct ESTOP path exist.

### Task Architecture

| Task | Priority | Rate | Function |
|------|----------|------|----------|
| can_rx | 5 | event-driven (2ms poll) | Process 0x001/0x110/0x204 into atomics |
| safety | 5 | 20 Hz | ESTOP GPIO, 0x204 staleness, startup grace |
| control | 4 | 100 Hz | Mode-gated motor control (see below) |
| can_tx | 3 | 100 Hz | TX 0x120 every cycle, 0x206 every 2nd cycle |

### Atomic Sensor Pipeline (Same Pattern as RT)

Nine lock-free atomics: `g_mode`, `g_estop_active`, `g_cmd_speed_mmps`, `g_cmd_gear`, `g_last_cmd_tick`, `g_actual_speed_mmps`, `g_current_gear`, `g_fault_flags`, `g_startup_grace`. CAN RX writes to atomics. Control reads atomics + ADC. CAN TX reads atomics. No locks.

### Mode-Gated Control

| Mode | DAC Output | Gear MOSFETs | CAN TX |
|------|-----------|-------------|--------|
| MANUAL | ADC passthrough (0-5V → 0-4095 DAC) | Follow gear selector | 0x120 (speed=0), 0x206 |
| AUTO | 0x204 speed → DAC value | 0x204 gear → relays* | 0x120 (actual speed), 0x206 |
| ESTOP | DAC=0V, all MOSFETs OFF | Forced N | 0x120, 0x206 (fault flags set) |

*Gear switching in AUTO is speed-supervised: MOSFETs only change when `abs(speed) < 50mm/s` to prevent switching 72V under load.

### Safety Features

| Feature | Implementation |
|---------|---------------|
| ADC stuck-at-rail | Raw 0 or 4095 → `kMtrFaultAdcFault` (short to GND/VCC detection) |
| Gear conflict | Multiple gear sense lines HIGH → `kMtrFaultGearConflict` → forces N |
| Speed clamping | AUTO speed clamped to `[kMaxSpeedRevMmps, kMaxSpeedFwdMmps]` — guards against corrupt CAN 0x204 |
| 0x204 staleness | 200ms timeout → `kMtrFaultCmdTimeout` |
| Startup grace | 3s grace period → auto-sets `kMtrFaultStartupReady` (bit 4 in 0x206) |
| DAC timeout | 100ms finite I2C timeout (never `HAL_MAX_DELAY`). Retries once. Tracks consecutive failures. |
| CAN peripheral | STM32 bxCAN (memory-mapped, no SPI mutex needed). Hardware RX FIFO. Non-blocking TX via mailbox. |

### CAN I/O

| Frame | Dir | Rate | Purpose |
|-------|-----|------|---------|
| 0x001 | RX | event | ESTOP → DAC=0, MOSFETs OFF |
| 0x110 | RX | change | Mode from SYS |
| 0x204 | RX | 100 Hz | Drive setpoint (speed+gear) from RT |
| 0x120 | TX | 100 Hz | Throttle position feedback (actual speed) |
| 0x206 | TX | 50 Hz | Motor feedback (speed, gear, fault flags) |

---

## 15. Steering State Machine & 0x7B9 Suppression

### Steering (6 States)

RT steering (`steering_control.h`):

```
BOOT_WAIT(500ms) → LISTEN_SYNC → ACTIVE
                      ↓(timeout)     ↓(obstacle/ESTOP)
                     FAULT       ESTOP_RAMP(20°/s)
                      ↑              ↓(ramp done)
                 (follow err)    ESTOP_HOLD(500ms)→SILENT
```

| State | 0x169 TX | 0x204 Gate |
|-------|----------|------------|
| BOOT_WAIT | No | Suppressed |
| LISTEN_SYNC | No | Suppressed |
| ACTIVE | **Open timing decision:** current bus scheduler/YAML use 50 Hz; steering state-machine configuration still assumes 100 Hz | Allowed |
| ESTOP_RAMP | Ramping to 0° | Allowed |
| ESTOP_HOLD/SILENT | Hold/stop | Allowed |
| FAULT | No | Suppressed |

The timing mismatch is intentionally not resolved by documentation alone. `can_low.yaml` and the current RT transmit schedule specify 20 ms/50 Hz, while `kSteerCmdRateHz` is 100 and is used for state-machine tick calculations. Firmware, YAML, golden timing tests and this table must be changed together after the required EPS-C rate is confirmed.

### 0x7B9 Suppression (6 Conditions)

SYS suppresses its own 0x7B9 in AUTO (RT sends directly via Option D). All 6 conditions required:
```
suppress = AUTO && rt_hb_ok && rt_safety==Normal
        && seb_roll_ok && !lever && !estop && rt_sp_fresh
```
Any condition failing → SYS resumes sending 0x7B9 immediately. The `rt_sp_fresh` (200ms) provides fast deadman before the 1000ms heartbeat timeout.

---

---

## 16. RT Control Architecture

### Safety Event Pipeline

The control task (`t_control`, 100 Hz) drains the safety event queue each cycle:

```
CAN RX → process_frame() → SafetyEvent enqueued on g_safety_evt_q (depth 16)
                                  ↓
t_control drains queue (xQueueReceive with 0 timeout, then xQueueOverwrite fallback)
                                  ↓
    ESTOP event      → m_estop_pending = true
    MODE_CHANGE event → m_current_mode updated; clears m_estop_pending ONLY if
                        no ESTOP arrived in same drain cycle (race guard)
    HB timeout       → m_seb_takeover = true (auto-cleared on recovery)
                                  ↓
    run_safety_checks() → evaluates m_estop_pending, m_current_mode, m_seb_takeover
                                  ↓
    produces SafetyResult { zero_setpoints, brake_kpa, disable_steering, obstacle_triggered }
```

Startup grace: All heartbeat and following-error checks are suppressed for 3 seconds after boot to prevent false ESTOPs during ECU synchronization.

### Asymmetric Bus-Off Response

| Bus | 5× Bus-Off Response | Rationale |
|-----|---------------------|-----------|
| Low | Full ESTOP: 0x001 on both buses, g_estop_reason set to kEstopReasonBusOff | Loss of actuator bus is non-survivable |
| High | Graceful degradation: zero setpoints, steering ramp-to-zero | Loss of Jetson link is survivable |

Bus-off recovery uses a fast-path (MCP2515 ISR at ~100µs) plus polled fallback (10 Hz). A `fast_path_handled` guard prevents the polled path from clearing the bus-off counter after the ISR path already reinitialized.

### Frozen Counter Heartbeat Detection

RT first validates the exact ID, frame type, DLC and payload using the generated heartbeat DTO. Frozen-counter detection then updates the heartbeat timestamp only when the decoded alive counter advances. Unsigned delta comparison handles 8-bit rollover. This is applied independently to SYS heartbeat (0x7FE, timeout 200ms) and Host heartbeat (0x7FC, timeout 1500ms); invalid frames do not refresh liveness.

### Heartbeat Health Flags

RT 0x7FD byte 1 carries four health bits whose wire locations are defined in YAML and emitted through generated metadata/codecs:
- bit 0: `heartbeat_ok` — both SYS and Host heartbeats alive
- bit 1: `estop_active` — steering in ramp/hold OR mode is ESTOP
- bit 2: `mode_auto` — mode is AUTO
- bit 3: `can_ok` — MCP2515 reports not bus-off

### Command Watchdog

Starts stale at boot (last feed = negative). Requires first Host 0x300 command to feed. On staleness (500ms): overwrites command queue with zero + triggers steering ramp-to-zero. Clears on next valid Host command.

### Checksum-Before-L3 Pattern

For steering (0x201) and brake (0x721) status frames, the L3 error check happens AFTER checksum validation. A corrupt frame with noise flipping error bits to 3 is rejected by checksum before L3 evaluation. DLC < 8 causes immediate reject (cannot validate checksum).

---

## 17. SYS Control Architecture

### Startup Sequence

```
app_main:
  0. NVS init: read esp_reset_reason(), store reset_count + reset_reason in "sys_diag" namespace
  1. CAN init (g_can.init)
  2. Sequential module init: safety → mode_mgr → throttle → dac → gear → brake → lights → indicator → wdt
  3. GPIO init: status bulbs (AUTO/MANUAL/READY/ESTOP)
  4. Create CAN RX queue (depth 16)
  5. Create 15 tasks in priority order
```

### CAN TX Retry Strategy

Critical frames (0x7B9 brake, 0x001 ESTOP, 0x011 safety status) get one retry with 20ms timeout. Non-critical frames are dropped after first failure. This balances safety delivery against bus congestion.

### CAN RX Queue Timeout (Priority Inversion Fix)

`xQueueSend` to the CAN RX queue uses 5ms timeout instead of 0. When the queue is full, the prio 5 can_rx task yields briefly so the prio 4 dispatch task can drain the queue. Without this, continuous CAN bursts cause continuous frame drops.

### SEB Rolling Counter Monitor

SYS monitors the SEB 0x721 rolling counter (4-bit). If frozen, `g_seb_rolling` is set false. This feeds into the 0x7B9 suppression logic — if SEB stops acknowledging RT's commands, SYS resumes sending its own 0x7B9 immediately. This is a third deadman layer (beyond heartbeat timeout and setpoint staleness).

### MTR ESTOP_ACTIVE Propagation

When SYS sees ESTOP_ACTIVE (bit 0) in 0x206 fault_flags and SYS is not already in ESTOP, SYS calls `force_estop()` and broadcasts 0x001. This provides a redundant ESTOP path — if SYS missed the original 0x001 frame, MTR's ACK triggers it anyway.

### Brake Following-Error Monitor

In 0x721 dispatch: compares commanded stroke vs actual stroke. If `|cmd - actual| > 60` raw (~3mm) for >100ms → sets `g_brake_fault_active`. Only active in Stroke mode (Pressure mode would false-trigger because cmd_stroke stays at 600 while SEB builds pressure).

### Gear Mismatch Monitor

Compares commanded gear (0x204) vs reported gear (0x206). Mismatch persisting >500ms → ERROR log. For the future MTR-owned-motor configuration where SYS is EGAS L2 monitor only.

### 0x204 Startup Grace

`task_motor` has a 3-second startup grace period before enforcing the 200ms 0x204 staleness timeout. This masks the gap between SYS boot and RT's first 0x204 transmission.

### Brake Light OR-Logic

Brake lamp illuminates on any of: lever pressed, CAN 0x302 brake bit set, or SEB stroke >0.5mm (raw 610). Three-input OR for redundancy.

---

## 18. Steering Control Architecture (Full)

### 6-State Machine with Sub-Behaviors

```
BOOT_WAIT(500ms) ──→ LISTEN_SYNC ──→ ACTIVE
                        │                │
                   ┌────┼────┐      ┌────┼────┐
                   │timeout│plaus│    │obst│es│top│
                   ▼       ▼     ▼    ▼    ▼  ▼
                  FAULT  FAULT FAULT RAMP_TO_ZERO(20°/s)
                   ▲                     │
                   │(follow err >300ms)   │(ramp done)
                   └─────────────────────┘
                                        HOLD_THEN_SILENT(500ms)
                                           │(hold expires)
                                          SILENT_STOP
```

**LISTEN_SYNC failures:**
- Timeout: no 0x201 for 5s → FAULT
- Alignment: EPS-C reports `angle_status != 1` → FAULT
- Plausibility: angle >30° off center at boot → FAULT

**RAM monitoring (Gap C3):** During ESTOP_RAMP_TO_ZERO, monitors `|active_angle - ses_angle|`. If >5° for >1s → FAULT (jammed linkage detection).

**Deferred exit (Gap #6):** START button during ramp sets `m_estop_exit_pending`. Ramp completes to 0° first, THEN transitions to ACTIVE. A new `set_target()` clears pending exit. A new `start_estop()` overrides it.

**Obstacle ESTOP dynamic clamp (Gap #9):** Hold angle is clamped to `compute_dynamic_limit(speed)`. At high speed the limit may be only 5°, preventing rollover during hard braking.

**Dynamic slew rate:** `125 + (speed_kmh - 2) * (400/23)` deg/s, clamped [125, 525]. Faster steering at higher speeds.

### Dynamic Angle Clamp Formula

```
limit_deg = 40.0 - (speed_kmh - 2.0) * (35.0 / 23.0)
clamped to [5.0, 40.0]
```

### Following Error Threshold Formula

```
threshold_deg = max(2.0, 0.25 * dynamic_limit)
must persist >300ms to trigger FAULT
```

---

## 19. Brake Control Architecture (Full)

### 4-State Machine

```
BOOT_WAIT(500ms) ──→ LISTEN_SYNC ──→ ACTIVE ←── DEGRADED
                         │(2s timeout)              │(0x721 aligned)
                         ▼                          │
                      DEGRADED ─────────────────────┘
```

**LISTEN_SYNC:** Waits for 0x721 with alignment bit. First ACTIVE frame re-sends the stroke captured during sync (prevents sudden release). Timeout 2s → DEGRADED.

**DEGRADED:** Sends lever-only commands (no CAN pressure). Recovers to ACTIVE when valid aligned 0x721 arrives. Not a terminal state.

### build_command() Priority Chain

1. **ESTOP** → max stroke 27mm (raw 1140), Stroke mode, `auto_brake=0`
2. **Lever pressed** → 15mm stroke (raw 900), Stroke mode
3. **CAN brake_kpa > 0** → Pressure mode (control_mode=1), `auto_brake=1`
4. **Released** → 0mm stroke (raw 600)

### Stroke vs Pressure Encoding

- **Stroke mode:** `raw = (mm + 30) / 0.05`. 0mm→600, 15mm→900, 27mm→1140.
- **Pressure mode:** `raw = kPa / 50`. 5000kPa→100, 20000kPa→400.
- All multi-byte fields are LE per vendor protocol. XOR checksum over bytes 0-6 ^ 0xFF.

---

## 20. ESTOP Rate Limiting Architecture

Two-layer rate limiting prevents ESTOP bus flooding from a faulty node:

| Layer | Scope | Limit | Implementation |
|-------|-------|-------|---------------|
| Per-ECU broadcast | RT or SYS individually | 250ms interval between broadcasts | `shared_config.h`: `kEstopBroadcastMinIntervalUs`, `g_last_estop_sent_us` atomic |
| Per-ECU RX | SYS receiving 0x001 | 2 frames per 500ms window | `sys-esp32/config.h`: `kEstopRateLimitWindowMs`, `kEstopRateLimitMax` |
| Gateway forwarding | RT gateway per-bus | 1 frame per 100ms per bus | `can_dispatch.h`: independent per-bus rate limiters |

---

## 21. MTR Fault Flag Protocol

Defined in `shared_config.h`. MTR reports fault state in 0x206 byte 3:

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | ESTOP_ACTIVE | MTR confirms ESTOP received and active |
| 1 | CMD_TIMEOUT | 0x204 command stale >200ms |
| 2 | ADC_FAULT | Throttle ADC reading fault |
| 3 | GEAR_CONFLICT | Multiple gear select lines HIGH simultaneously |
| 4 | STARTUP_READY | MTR boot complete, accepting commands |

SYS monitors ESTOP_ACTIVE for ACK (100ms timeout) and STARTUP_READY for MTR liveness.

---

## 22. Reference Documents

- [`can-dictionary.md`](can-dictionary.md) — Full CAN signal catalog
- [`docs/architecture-reference.md`](docs/architecture-reference.md) — Detailed tables, pseudocode, processing summaries
- [`docs/wiring-harness.md`](docs/wiring-harness.md) — Pin-level wiring
- [`docs/hil-safety-test-plan.md`](docs/hil-safety-test-plan.md) — HIL test scenarios
- [`tem/testing-guide.md`](tem/testing-guide.md) — Complete test suite guide (2,470+ assertions)
- [`docs/can-bench-test.md`](docs/can-bench-test.md) — Bench test plan
- [`shared/can/generated/can_messages.h`](shared/can/generated/can_messages.h) — generated C++ payload DTOs, metadata and checked codecs
- [`shared/can/manual-mappings.yaml`](shared/can/manual-mappings.yaml) — registered handwritten vendor behavior and reviewed wire hashes
- [`shared/can/manual/vendor_protocol.h`](shared/can/manual/vendor_protocol.h) — current SES/SEB checksum and overlay adapter boundary
- [`shared/can/can_protocol.h`](shared/can/can_protocol.h) — legacy compatibility structs plus shared frame/enums; new application codecs must not be added here
