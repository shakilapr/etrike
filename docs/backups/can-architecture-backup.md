# CAN Architecture & Protocol Specification (BACKUP)
# Saved on 2026-07-26

This document provides a technical specification of the CAN bus architecture, gateway routing policies, driver design, arbitration rules, handshakes, ACKs, timing deadlines, actuator control policies, and fault recovery mechanisms across the **RT** (Real-Time Controller) and **SYS** (System Safety & Mode Authority) ECUs.

---

## Table of Contents
1. [Network Topology & Bus Structure](#1-network-topology--bus-structure)
2. [Low-Level Driver Architecture & Frame Integrity](#2-low-level-driver-architecture--frame-integrity)
3. [Gateway Bridging & Routing Rules](#3-gateway-bridging--routing-rules)
4. [Comprehensive Timing Deadlines & Staleness Thresholds](#4-comprehensive-timing-deadlines--staleness-thresholds)
5. [Handshakes, Acknowledgments (ACKs) & Verification Loops](#5-handshakes-acknowledgments-acks--verification-loops)
6. [Operational Handshakes & State Machine Synchronization](#6-operational-handshakes--state-machine-synchronization)
7. [Actuator Dual-Control & Arbitration (Option D)](#7-actuator-dual-control--arbitration-option-d)
8. [Actuator Boot State Machines & Priority Arbitration](#8-actuator-boot-state-machines--priority-arbitration)
9. [Heartbeats, Watchdogs, & Liveness Verification](#9-heartbeats-watchdogs--liveness-verification)
10. [CAN Lighting Control & Powertrain Integration](#10-can-lighting-control--powertrain-integration)
11. [Failure Modes, Fault Recovery, & Self-Healing](#11-failure-modes-fault-recovery--self-healing)
12. [Bus Corruption Root Causes, Failure Modes & Code Safeguards](#12-bus-corruption-root-causes-failure-modes--code-safeguards)
13. [Additional Advanced Firmware Logic & Edge Cases](#13-additional-advanced-firmware-logic--edge-cases)
14. [Stacked & Layered Safety Control Hierarchy Overview](#14-stacked--layered-safety-control-hierarchy-overview)

---

## 1. Network Topology & Bus Structure

The eTrike platform defines three physical CAN buses operating at fixed baud rates:

```
                      +-----------------------------+
                      |     Host (Jetson Orin)      |
                      +-----------------------------+
                                     |
                                     | High CAN (500 kbit/s)
                                     v
                      +-----------------------------+
                      |       RT ESP32-S3           |  <--- Dual-Bus Gateway
                      +-----------------------------+
                                     |
                                     | Low CAN (500 kbit/s)
         +-------------------+-------+-------+-------------------+
         |                   |               |                   |
         v                   v               v                   v
 +---------------+   +---------------+   +-------+       +---------------+
 |  SYS ESP32-S3 |   |  EPS-C Steer  |   |  SEB  |       |  MTR (STM32)  |
 +---------------+   +---------------+   +-------+       +---------------+
                                                             (Planned)

                      +-----------------------------+
                      |         PWT ESP32           |
                      +-----------------------------+
                                     |
                                     | Powertrain CAN (250 kbit/s)
                                     v
                      +-----------------------------+
                      |     DC-DC Converter         |
                      +-----------------------------+
```

### Bus Definitions
1. **High CAN (500 kbit/s)**: Dedicated link between Host (Jetson Orin) and RT. Transmits high-level drive commands, light commands, and telemetry reports.
2. **Low CAN (500 kbit/s)**: Shared vehicle control bus connecting RT, SYS, EPS-C (Steering-by-Wire), SEB (Smart Electronic Brake), MTR (Motor Controller), and HMI.
3. **Powertrain CAN (250 kbit/s)**: Isolated bus connecting PWT to the 48V-to-12V DC-DC converter (`0x10262B27`).

---

## 2. Low-Level Driver Architecture & Frame Integrity

### Handle-Based TWAI API (ESP-IDF 5.5)
- **Zero-Length Frame Preservation**: The legacy ESP-IDF `twai_transmit()` API automatically expanded DLC=0 frames to DLC=8. The SYS and RT drivers migrate to the handle-based `twai_new_node_onchip()` API ([can_driver.h](file:///e:/work/etrike/sys-esp32/src/can_driver.h#L70-L90)) to preserve the strict **DLC 0 wire contract** for `0x001` (`SAFETY_ESTOP`).
- **Deterministic TX Slot Reclamation on Bus-Off**: ESP-IDF 5.5 abandons in-flight frames without invoking the `on_tx_done` callback during Bus-Off events. Drivers configure `fail_retry_cnt = 0` and `tx_queue_depth = 1` so that driver slots are reclaimed deterministically during bus recovery without leaking FreeRTOS queue slots.

### Queue Pump & Software Retry Loop (`gw_pump`)
- **Single-Slot HW Slot Protection**: Software gateway queues (`GwTxFrame`) store un-transmitted frames for up to 40 attempts (`kGwMaxAttempts = 40`, `kGwFreshnessTicks = 80ms`).
- **Arbitration Retry**: If a frame loses arbitration or the hardware TX slot is busy, `gw_pump` ([main.cpp](file:///e:/work/etrike/rt-esp32/src/main.cpp#L222-L242)) re-enqueues the frame to the **front** of the queue (`xQueueSendToFront`) to re-attempt transmission on the next tick without losing frame ordering.
- **Hardware Auto-Retransmit**: Once accepted into the HW slot (`send_fn == true`), the controller handles arbitration retries automatically. Software does not re-enqueue accepted frames, preventing duplicate frame storms.

### Peer Presence & Admission Control
- **TWAI Peer Liveness (`g_last_low_peer_us`)**: Every valid frame received on Low CAN from any registered unit updates `g_last_low_peer_us` ([can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L111-L118)). This verifies that an ACK-capable physical peer is present before admitting normal operational TX.
- **ESTOP Priority Admission**: `0x001` (`SAFETY_ESTOP`) bypasses normal TX admission gates ([main.cpp](file:///e:/work/etrike/rt-esp32/src/main.cpp#L165)) and is sent immediately even if operational TX is suspended.

---

## 3. Gateway Bridging & Routing Rules

RT is the **only dual-bus ECU** and acts as a transparent, rate-managed gateway between High CAN and Low CAN.

### Cross-Bus Forwarding Routes
- **`0x001` (`SAFETY_ESTOP`)**: Forwarded High $\leftrightarrow$ Low immediately (`same_frame`, DLC 0). Enqueued at queue head (`xQueueSendToFront`) for zero-latency dispatch.
- **`0x011` (`SYS_SAFETY_STS`)**: Forwarded Low $\rightarrow$ High (`same_frame`). SYS safety, ESTOP, heartbeat, and light status to Host.
- **`0x111` (`HMI_MODE_REQ`)**: Forwarded High $\rightarrow$ Low (`same_frame`). Mode change requests from HMI to SYS.
- **`0x112` (`HMI_PWR_REQ`)**: Forwarded High $\rightarrow$ Low (`same_frame`). Power state requests from HMI to SYS.
- **`0x120` (`SYS_THROTTLE_STS`)**: Forwarded Low $\rightarrow$ High (`same_frame`). Measured throttle speed to Host.
- **`0x206` (`MTR_MOTOR_FBK`)**: Forwarded Low $\rightarrow$ High (`same_frame`). Motor speed, gear state, and fault flags to Host.
- **`0x302` (`HOST_LIGHT_CMD`)**: Forwarded High $\rightarrow$ Low (`same_frame`). Rider/Host lighting commands to SYS.
- **`0x600` (`SYS_DIAG_RPT`)**: Forwarded Low $\rightarrow$ High (`same_frame`). SYS system diagnostics, heap, and CAN error counters to Host.

---

## 4. Comprehensive Timing Deadlines & Staleness Thresholds

The system enforces deterministic timing limits across all CAN channels:

| Parameter Constant | Value | Scope / Location | Description & Safety Action |
| :--- | :--- | :--- | :--- |
| `kStartupGracePeriodMs` | **3000 ms** | RT & SYS Boot | Grace window after power-on. Missing heartbeats are ignored to allow full peripheral/NVS boot. |
| `kHeartbeatTimeoutMsRt` | **200 ms** | SYS Safety Monitor | 2 missed 100ms RT heartbeats (`0x7FD`). SYS forces ESTOP and broadcasts `0x001`. |
| `kHeartbeatTimeoutMsSys` | **200 ms** | RT Safety Monitor | 2 missed 100ms SYS heartbeats (`0x7FE`). RT triggers SEB brake takeover (`0x7B9` max brake). |
| `kHeartbeatTimeoutMsHost` | **1500 ms** | RT Safety Monitor | Host liveness loss (`0x7FC`). RT initiates assisted stop with 2000 kPa brake setpoint. |
| `kSetpointStaleMs` | **200 ms** | SYS Brake Task | RT drive setpoint (`0x204`) staleness. Fast-path deadman revokes RT brake control immediately. |
| `kBrakeSetpointStaleMs` | **100 ms** | SYS Brake Task | RT brake setpoint (`0x205`) staleness. Overwrites stale RT input with `kMaxBrakeKpa`. |
| `kSteerFollowingErrMs` | **100 ms** | RT Control Task | Persistence of steering angle following error before aborting AUTO mode and latching ESTOP. |
| `kBrakeFollowingErrMs` | **100 ms** | SYS Brake Task | Persistence of SEB stroke following error before logging actuator warning. |
| `kEgasFaultDurationMs` | **500 ms** | SYS Safety Task | EGAS Level 2 speed mismatch (`0x204` vs `0x206`) persistence threshold before forcing ESTOP. |
| `kMtrFbkStaleMs` | **200 ms** | SYS Safety Task | Motor feedback (`0x206`) loss deadline. |
| `kSebStatusTimeoutMs` | **100 ms** | SYS Brake Task | SEB status frame (`0x721`) loss warning limit. |
| `kSebRollingTimeoutMs` | **100 ms** | SYS Brake Task | Maximum allowed window for SEB `0x721` rolling counter to advance. |
| `kSebHandoffGraceMs` | **100 ms / 500 ms** | SYS Brake Task | Handoff window during MANUAL $\rightarrow$ AUTO transition for RT to claim `0x7B9` sole ownership. |
| `kMtrEstopAckTimeoutMs` | **100 ms** | SYS Safety Task | Deadline for MTR to acknowledge ESTOP in `0x206` after `0x001` broadcast. |
| `kDebounceMs` | **500 ms** | SYS Mode Manager | Physical button debounce lock-out window. |
| `kEstopLongPressMs` | **3000 ms** | SYS Mode Manager | Required hold duration on MODE button in ESTOP mode to exit to MANUAL. |
| `kEstopRateLimitWindowMs`| **500 ms** | RT & SYS Safety | Rolling window for rate-limiting ESTOP broadcasts (max 2 frames per 500ms). |

---

## 5. Handshakes, Acknowledgments (ACKs) & Verification Loops

### 1. Motor ESTOP Acknowledgment Handshake (`kMtrEstopAckTimeoutMs = 100ms`)
When SYS triggers an ESTOP (due to hardware button, software fault, or CAN `0x001`), SYS expects the MTR motor controller to acknowledge the ESTOP state by setting the `ESTOP_ACTIVE` bit in `0x206` (`MTR_MOTOR_FBK`) within **100 ms**. If MTR fails to return this ACK within 100 ms, SYS logs an un-acknowledged ESTOP warning.

### 2. SEB Rolling Counter ACK & Fallback Monitoring
SYS actively monitors the `rolling_counter` in incoming `0x721` (`SEB_STATUS`) frames:
- When RT is in AUTO mode, RT sends `0x7B9` directly to SEB.
- SYS verifies that SEB's rolling counter is actively incrementing (`g_seb_rolling == true`).
- **ACK Loss Fallback**: If RT's `0x7B9` transmissions fail (or are corrupted by bus noise), SEB stops receiving valid commands and stops incrementing its rolling counter. SYS detects that SEB is no longer acknowledging RT's commands and **immediately resumes broadcasting `0x7B9`** from SYS to restore physical brake control.

### 3. EPS-C Steering Security ACK & Rolling Counter Guard
The EPS-C steering unit requires rolling counter validation and checksum verification on `0x169` (`VCU_SES_REQ`):
- Byte 5 contains security bitfields: `RollCnt_Enable`, `CheckSum_Enable`, and `RollCnt`.
- Byte 7 contains an XOR8-complement checksum computed across Bytes 0–6.
- **Frozen Rolling Counter Guard**: On incoming feedback `0x201` (`SES_STATUS`), RT tracks `last_eps_roll`. If the rolling counter freezes across frames, RT skips feedback updates to prevent a stuck CAN controller from masking actuator faults.

### 4. HMI Request ACKs & Rolling Counter Verification
Incoming High-CAN HMI requests (`0x111` `HMI_MODE_REQ` and `0x112` `HMI_PWR_REQ`) include an 8-bit `rolling_counter`. SYS verifies counter progression to prevent replay attacks or stale frame processing.

---

## 6. Operational Handshakes & State Machine Synchronization

### Mode State Machine & Authority
1. **Mode Authority**: SYS is the single source of truth for system mode ([ModeManager](file:///e:/work/etrike/sys-esp32/src/mode_manager.cpp#L15-L68)). Mode transitions occur on SYS via:
   - **Rider Controls**: Physical MODE button toggle (`MANUAL` $\leftrightarrow$ `AUTO`).
   - **ESTOP Recovery**: START button press or 3-second long-press of the MODE button in `ESTOP` mode transitions the vehicle to `MANUAL`.
   - **HMI Network Commands**: `HMI_MODE_REQ` (`0x111`) from High-CAN bridged to SYS.
2. **Mode Broadcast**: SYS broadcasts its state via `SYS_MODE_CMD` (`0x110`).
3. **RT Mode Adoption**: When RT receives `0x110`, [can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L240) enqueues a `SafetyEvent::MODE_CHANGE` into RT's 100 Hz control loop queue ([safety_monitor.h](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L28-L35)). RT updates its internal mode to match SYS.

```
 +------------------+            0x110 SYS_MODE_CMD             +--------------------+
 |   SYS ESP32-S3   | ========================================> |    RT ESP32-S3     |
 | (Mode Authority) | <======================================== | (Motion Controller)|
 +------------------+           0x210 RT_STATE_RPT              +--------------------+
```

### Collision-Free AUTO Handoff Grace Period
When transitioning from `MANUAL` to `AUTO`:
- SYS grants a **100 ms handoff grace period** (`kSebHandoffGraceMs`, defined in [sys-esp32/src/main.cpp](file:///e:/work/etrike/sys-esp32/src/main.cpp#L643-L646)).
- During this window, SYS suppresses its own `0x7B9` brake command to give RT a clear, collision-free low-bus window to transmit its first `0x210` state report and assume `0x7B9` brake control.

---

## 7. Actuator Dual-Control & Arbitration (Option D)

To eliminate single point of failure (SPOF) while achieving low-latency control:

1. **Brake Actuator Control (`0x7B9`)**:
   - **In AUTO Mode**: When RT is healthy (`rt_alive == true`, `rt_safety_state == Normal`, and setpoints are fresh $<200\text{ ms}$), RT transmits `0x7B9` directly to the SEB brake unit (1-hop from kinematics). SYS suppresses its own `0x7B9` frame ([sys-esp32/src/main.cpp](file:///e:/work/etrike/sys-esp32/src/main.cpp#L626-L656)).
   - **SEB Control Mode Tracking**: When target pressure `0x205 > 0`, SYS commands SEB in **Pressure Mode** (`control_mode = 1`). When `0x205 == 0`, SYS falls back to **Stroke Mode** (`control_mode = 0`) for lever/ESTOP triggers.
   - **In MANUAL / ESTOP Mode**: SYS takes direct control of `0x7B9`, converting physical rider lever inputs or ESTOP brake curves directly to SEB commands.
   - **Rider Lever Override**: If the rider pulls the physical brake lever while in AUTO mode, SYS immediately overrides RT, takes over `0x7B9`, and commands maximum requested rider braking pressure.

2. **Steering Control (`0x169`)**:
   - RT exclusively commands the EPS-C steering unit via `0x169` (`VCU_SES_REQ`). In `MANUAL` or `ESTOP` modes, RT zeroes steering effort setpoints and disables autonomous steering output.

---

## 8. Actuator Boot State Machines & Priority Arbitration

### 1. Steering Boot State Machine (`SteerState`)
In [steering_control.h](file:///e:/work/etrike/rt-esp32/src/steering_control.h#L20-L88):
- `STEER_BOOT_WAIT`: 500 ms power-on delay (`kSteerBootWaitMs = 500ms`). Inverted outputs held silent; no `0x169` frames transmitted.
- `STEER_LISTEN_SYNC`: Waits for `0x201` (`SES_STATUS`). Checks 5-second sync timeout (`kSteerSyncTimeoutMs = 5000ms`) $\rightarrow$ `STEER_FAULT`. Requires `angle_status == 1` (alignment complete). Performs $\pm 30^\circ$ power-on angle plausibility check (if wheels $>30^\circ$ off at sync, flags sensor/offset error and enters `STEER_FAULT`).
- `STEER_ACTIVE`: Normal 50 Hz operation.
- `ESTOP_RAMP_TO_ZERO`: Non-obstacle ESTOP: ramps steering target angle back to $0^\circ$ straight at $20^\circ/\text{s}$, then holds position.
- `ESTOP_HOLD_THEN_SILENT`: Obstacle ESTOP: holds current steering angle for 500 ms so vehicle stays straight while braking, then transitions to silent-stop.
- `STEER_FAULT`: Comms loss or silent-stop — stops transmitting `0x169`.

### 2. SEB Brake Boot State Machine (`BrakeState`) & Strict Arbitration Order
In [brake_control.h](file:///e:/work/etrike/sys-esp32/src/brake_control.h#L7-L78):
- `BOOT_WAIT`: 500 ms boot delay (`kBrakeBootWaitMs = 500ms`).
- `LISTEN_SYNC`: Waits for `0x721` (`SEB_STATUS`). Captures baseline stroke (`m_sync_stroke_raw`) for smooth hold-on-sync transition upon first alignment (`status_byte0 & 1`). If 2-second timeout (`kBrakeSyncTimeoutMs = 2000ms`) expires without alignment, enters `BRAKE_DEGRADED`.
- `DEGRADED`: Continues sending 50 Hz commands with physical brake lever functionality ONLY, ignoring incoming CAN `0x205` pressure setpoints until `0x721` alignment recovers.
- `ACTIVE`: Normal 50 Hz operation.

#### Strict SEB Priority Arbitration Order (Highest to Lowest):
1. **Priority 1: ESTOP**: Commands maximum stroke ($27\text{ mm}$, raw 1140).
2. **Priority 2: Physical Brake Lever**: Driver override ALWAYS WINS (commands $15\text{ mm}$ stroke), even over automated CAN pressure.
3. **Priority 3: Automated CAN Pressure (`brake_kpa > 0`)**: Switches SEB to **Pressure Mode** (`control_mode = 1`), converting kPa setpoint to raw pressure units (`(uint8_t)(kpa * 0.02f)`).
4. **Priority 4: Default Released**: Commands $0\text{ mm}$ stroke in **Stroke Mode** (`control_mode = 0`).

---

## 9. Heartbeats, Watchdogs, & Liveness Verification

Both RT and SYS run dual-layered heartbeat verification at 10 Hz:

### Stuck/Frozen Counter Guard
Both ECUs validate counter progression rather than just packet arrival.
- In [rt-esp32/src/can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L73-L86) and [sys-esp32/src/safety_monitor.cpp](file:///e:/work/etrike/sys-esp32/src/safety_monitor.cpp#L28-L36), if `alive_ctr == last_alive_ctr`, the frame is flagged as **frozen** (indicating a hung main loop with hardware CAN DMA active). The timestamp update is skipped, triggering a timeout.

### Independent Per-Bus Sequence Counters
- RT's `DualHeartbeat` ([heartbeat.h](file:///e:/work/etrike/rt-esp32/src/heartbeat.h#L13-L32)) maintains completely **separate sequence counters** (`m_ctr_low` and `m_ctr_high`) for Low-CAN and High-CAN transmissions. Transmit failures on one bus do not corrupt sequence continuity on the other.

---

## 10. CAN Lighting Control & Powertrain Integration

### 1. Isolated Powertrain CAN Bus (PWT ESP32-S3)
- PWT runs an **isolated 250 kbit/s powertrain CAN bus** on TWAI0 (`GPIO7 TX`, `GPIO6 RX`).
- Emits extended CAN ID `0x10262B27` (`PWT_DCDC_CMD`), DLC 8, every 100 ms to control the 48V-to-12V DC-DC converter.
- **Physical Bus Isolation**: The 250 kbit/s powertrain bus must NEVER be joined to the 500 kbit/s Low-level bus to prevent frame corruption, stuff errors, and bus-off crashes across nodes.
- **Rate-Limited Failure Logging**: PWT logs the 1st TX failure and every 50th aggregate failure to prevent log flooding during powertrain disconnects.

### 2. CAN Lighting Control & Quad-Input OR Logic (`0x302` `HOST_LIGHT_CMD`)
In [light_control.h](file:///e:/work/etrike/sys-esp32/src/light_control.h#L25-L64):
- In `MANUAL` mode: Handlebar switches toggle left/right indicators and headlights.
- In `AUTO` mode: High-CAN `0x302` bits (`HOST_LIGHT_CMD`) control turn signals ($1\text{ Hz}$ blink rate).
- **Quad-Input Brake Lamp OR Logic**: The SYS brake lamp output illuminates if ANY of 4 conditions are met:
  $$\text{BrakeLamp} = \text{BrakeLever} \lor (\text{Mode} == \text{ESTOP}) \lor \text{CAN\_BrakeBit}(0\text{x}302) \lor (\text{SEB\_Stroke} > 0\text{ mm})$$
- In `ESTOP` mode: Brake lamp is forced ON continuously as an emergency warning.

---

## 11. Failure Modes, Fault Recovery, & Self-Healing

```
  +-------------------------------------------------------------------------+
  |                              FAULT DETECTED                             |
  |  (Bus-Off, Heartbeat Timeout, Lever Pull, EGAS Mismatch, L3 Sensor Err) |
  +-------------------------------------------------------------------------+
                                       |
                                       v
               +-----------------------------------------------+
               | Rate-Limited ESTOP Broadcast (CAN 0x001)      |
               | [can_send_estop() - max 2 frames per 500ms]    |
               +-----------------------------------------------+
                                       |
                                       v
               +-----------------------------------------------+
               | Latch System State to ESTOP Mode              |
               | - RT zero setpoints & active brake            |
               | - SYS direct 0x7B9 brake stroke control       |
               +-----------------------------------------------+
                                       |
                                       v
               +-----------------------------------------------+
               | Human Recovery Requirement                    |
               | (Software commands blocked; START button press|
               |  or MODE long-press required to reach MANUAL) |
               +-----------------------------------------------+
```

1. **CAN Bus-Off & Exponential Backoff Recovery**:
   - **Low-CAN TWAI Driver**: Monitored at 10 Hz ([can_health.h](file:///e:/work/etrike/rt-esp32/src/can_health.h#L7-L73)). If Transmit Error Counter (TEC) $\ge 255$, TWAI enters Bus-Off. TX admission is revoked and queues reset. TX admission restoration follows **exponential backoff starting at 500 ms** (`500'000 µs`), doubling on repeated failures up to a cap of **5.0 seconds** (`5'000'000 µs`). **5 consecutive Bus-Off events latch a global ESTOP**.
   - **High-CAN MCP2515 SPI Driver**: Monitored via ISR + 10 Hz polling loop. Backoff progression: 200 ms $\rightarrow$ 400 ms $\rightarrow$ 600 ms. Polled controller re-initialization delay is **3.0 seconds** (`3'000'000 µs`).

2. **ESTOP Bus Flood Prevention (Rate Limiting)**:
   To prevent a corrupted node from collapsing the CAN bus by spamming `0x001` frames, [can_send_estop()](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L53-L55) enforces a maximum of **2 ESTOP frames per 500 ms window**.

3. **EGAS 3-Level Motor Safety & Level 2 Monitoring**:
   - **Level 1**: MTR STM32 functional motor controller.
   - **Level 2**: SYS safety monitor. In AUTO mode, [task_safety](file:///e:/work/etrike/sys-esp32/src/main.cpp#L483-L485) compares RT's commanded speed in `0x204` against actual motor feedback in `0x206`. Mismatch exceeding threshold for $>200\text{ ms}$ triggers ESTOP.
   - **Level 3**: Hardwired physical ESTOP circuit.

4. **Steering Following-Error & Dynamic Slew Rate**:
   - RT continuously monitors actual EPS-C steering feedback angle vs commanded target ([safety_monitor.h](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L123-L151)). Target steering angle slew rate is speed-dependent (`VCU_SES_Tgt_StrAngleSpd`, range 125–525 °/s). If following error exceeds `compute_following_error_threshold(speed)` for $>100\text{ ms}$, RT aborts autonomous mode and latches ESTOP.

5. **Actuator L3 Fault Escalation**:
   Severe actuator internal faults (L3 faults from SEB `0x721` or EPS-C `0x202`) bypass local retries and escalate directly to CAN `0x001` ESTOP broadcasts ([can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L165-L178)).

6. **Developer Bypass Modes**:
   - `g_bench_solo_mode`: Suppresses missing peer heartbeat timeouts during bench testing without disabling physical ESTOP safety.
   - `g_bypass_eps_sync`: Disables EPS-C steering angle following-error checks when testing without physical steering hardware connected.
   - `g_bypass_mtr_absent`: Bypasses EGAS Level 2 speed mismatch checks when MTR STM32 hardware is absent.

7. **Queue Overflow Tracking & Diagnostics**:
   - FreeRTOS RX queue drops are recorded in `rx_overflow` metrics and reported on CAN via `RT_STATE_RPT` (byte 3) and `SYS_DIAG_RPT` (byte 2 bits 1–6).
   - SYS records crash reset reasons and boot metrics into Non-Volatile Storage (NVS) flash across unexpected reboots for post-mortem diagnostics.

---

## 12. Bus Corruption Root Causes, Failure Modes & Code Safeguards

This section details the specific failure conditions that can cause total bus corruption, packet dropouts, or complete publication cessation, along with the firmware safeguards that prevent or mitigate them.

### 1. Unbounded ESTOP Queue Flooding (Cascading Bus Blackout)
- **Root Cause**: If a node experiences a hardware fault and continuously chatters `0x001` (`SAFETY_ESTOP`) frames, standard queue pushing (`xQueueSendToBack`) fills all TX queues on RT and SYS. This starves periodic heartbeats (`0x7FD`, `0x7FE`), state reports (`0x210`), and brake commands (`0x7B9`), escalating a single node fault into a vehicle-wide network blackout.
- **Code Safeguard**:
  - **Rate Limiting**: `can_send_estop()` ([safety_monitor.h](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L53-L55)) limits ESTOP broadcasts to a maximum of **2 frames per 500 ms window** (`kEstopRateLimitWindowMs`).
  - **Queue Head Priority**: Incoming `0x001` frames bypass normal queueing and use `xQueueSendToFront` ([main.cpp](file:///e:/work/etrike/rt-esp32/src/main.cpp#L136)) to transmit immediately without queueing delay.
  - **Cross-Bus Anti-Loop Guard**: RT's gateway ([can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L124-L130)) forwards `0x001` *only* across buses (High $\rightarrow$ Low or Low $\rightarrow$ High); it **never echoes `0x001` back onto the bus of origin**, preventing infinite reflection feedback loops that flood the network.

### 2. Unchecked SPI Mutex Bypass in High-CAN MCP2515 Driver
- **Root Cause**: Concurrent access by multiple high-priority tasks (`t_can_rx`, `t_dispatch`, `t_heartbeat`) to the shared SPI bus without verifying mutex acquisition locks up the SPI controller or corrupts driver registers, causing High CAN to silently stop publishing.
- **Code Safeguard**:
  - `spi_lock()` checking: All SPI transmission methods check the mutex return value before executing `spi_device_transmit()`. If the mutex is not acquired, the call returns cleanly without releasing an unheld semaphore, avoiding FreeRTOS kernel panics and SPI bus corruption.

### 3. High Bus-Off Recovery Counter Race Condition
- **Root Cause**: A race condition between interrupt-driven fast-path bus-off entry and slow-path 10 Hz polling. If the fast path detected a bus-off and reset the MCP2515 controller, the immediate 10 Hz TEC poll read `TEC == 0` (freshly reset), executing the `else` branch which erased `bus_off_count_high = 0`. The counter never reached 5, leaving the system in an infinite recovery loop while High CAN was dead.
- **Code Safeguard**:
  - Latched recovery monitoring ([can_health.h](file:///e:/work/etrike/rt-esp32/src/can_health.h#L75-L100)): MCP2515 bus-off state is latched asynchronously by the receive path and remains latched until a full controller-only recovery cycle completes. Polled counters preserve monotonic failure counts across resets.

### 4. ESP-IDF 5.5 TWAI DLC Expansion (Payload Integrity Corruption)
- **Root Cause**: The deprecated legacy `twai_transmit()` API in ESP-IDF 5.5 automatically expanded zero-length DLC 0 frames to DLC 8. Sending `0x001` as DLC 8 broke the strict 0-byte wire contract, causing SEB and EPS-C actuator nodes to reject or misinterpret ESTOP frames.
- **Code Safeguard**:
  - Handle-Based API Migration ([can_driver.h](file:///e:/work/etrike/sys-esp32/src/can_driver.h#L70-L90)): Uses `twai_new_node_onchip()`, guaranteeing exact DLC 0 wire transmission for `SAFETY_ESTOP`.

### 5. In-Flight Slot Leaks & Queue Deadlock on Bus-Off
- **Root Cause**: On TWAI hardware Bus-Off entry, ESP-IDF 5.5 abandons in-flight frames without invoking the `on_tx_done` callback. Application queues tracking free TX slots (`free_tx_slots_`) would leak slots and permanently lock up transmission even after bus recovery.
- **Code Safeguard**:
  - Driver configuration ([can_driver.h](file:///e:/work/etrike/sys-esp32/src/can_driver.h#L79-L80)): Sets `fail_retry_cnt = 0` and `tx_queue_depth = 1`. Upon Bus-Off entry, the supervisor explicitly calls `xQueueReset()` to flush stale slots and reclaim in-flight slots deterministically.

### 6. ACK-Starvation & Physical Bus Disconnect (Silent TX Blocking)
- **Root Cause**: If no ACK-capable peer is physically connected on Low CAN (or transceivers are unpowered), a CAN controller retries every frame infinitely. Transmit mailboxes fill, blocking all application tasks attempting CAN TX.
- **Code Safeguard**:
  - **Peer Liveness Gate (`g_last_low_peer_us`)**: Operational TX checks `drv->tx_admitted()`, which requires an ACK-capable peer to have emitted at least one valid frame within the peer window.
  - **ListenOnly Check**: MCP2515 driver verifies `can_transmit()` before queuing frames on High CAN, aborting immediately if transceivers are unpowered or in listen-only mode rather than blocking task queues.

### 7. Frozen Task / DMA Main-Loop Hung State (Ghost Publishing)
- **Root Cause**: If an ECU's main FreeRTOS control task deadlocks while hardware CAN DMA remains active, the CAN peripheral continues transmitting stale buffer data indefinitely. Remote nodes relying only on frame arrival timestamps would perceive the hung node as healthy.
- **Code Safeguard**:
  - **Wrapping Counter Guard (`alive_ctr`)**: Remote nodes ([can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L73-L86) and [safety_monitor.cpp](file:///e:/work/etrike/sys-esp32/src/safety_monitor.cpp#L28-L36)) verify `heartbeat.alive_ctr != last_alive_ctr`. If `alive_ctr` remains identical across frames, the packet is flagged as frozen, skipping the timestamp update and forcing a 200 ms safety timeout.

### 8. Dual-Node Brake Command Collision (`0x7B9` Bus Arbitration Contention)
- **Root Cause**: If both RT and SYS simultaneously transmit `0x7B9` (`VCU_SEB_REQ`) with different payload setpoints (e.g., RT commanding 0 kPa in AUTO while SYS commands 2000 kPa), continuous CAN arbitration loss and message collisions corrupt actuator commands.
- **Code Safeguard**:
  - **Mode-Gated Single-Sender Suppression (Option D)**: SYS suppresses its `0x7B9` transmission in AUTO mode when RT authority is established (`rt_alive && rt_normal && rt_setpoint_fresh`).
  - **Handoff Grace Window (`kSebHandoffGraceMs = 100ms`)**: Provides RT a collision-free window on MANUAL $\rightarrow$ AUTO transitions to publish its initial state report (`0x210`) and claim sole `0x7B9` ownership.

### 9. Corrupt Frame Checksum ESTOP Trigger (Bus Noise Vulnerability)
- **Root Cause**: EMI noise on Low CAN flipping bits in `0x721` (`SEB_STATUS`) could make `error_status` evaluate as `3` (L3 fault). If evaluated before verifying payload integrity, corrupt frames trigger false global ESTOPs.
- **Code Safeguard**:
  - **Checksum-First Evaluation**: In [can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L219-L232), frame decoding enforces XOR8 checksum validation *before* evaluating error status bits, discarding corrupted frames cleanly without triggering ESTOP.

### 10. Multi-Layered Watchdog System (Task & Hardware WDT Expiration)
- **Root Cause**: If any safety task stalls for $>1000\text{ ms}$, hardware or RTOS watchdogs trigger an MCU reset, causing sudden bus silent drops.
- **Code Safeguard**:
  - **Layered 3-Tier Watchdog Architecture**:
    1. **Task-Level Alive Counters**: Polled at 10 Hz (RT) / 1 Hz (SYS) to verify active task execution.
    2. **External Hardware WDT (TPS3850)**: Physical WDT pin toggled exclusively from the control task.
    3. **ESP-IDF Task WDT (TWDT)**: Monitors FreeRTOS idle tasks to catch kernel deadlocks.

---

## 13. Additional Advanced Firmware Logic & Edge Cases

### 1. Dual-Queue Fair Interleaving in Dispatch (`t_dispatch`)
In [can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L248-L265), the `t_dispatch` task alternates draining `g_can_rx_low_q` and `g_can_rx_high_q` using zero-timeout polls (`pdMS_TO_TICKS(0)`), before falling back to a 10 ms wait. This fair polling mechanism ensures that high-volume High-CAN telemetry never starves Low-CAN safety frames or vice versa.

### 2. Obstacle-Triggered Automatic Braking & Max-Select Arbitration
RT performs obstacle brake calculation based on distance inputs (`g_obstacle_mm`). When an obstacle is detected within stop distance (`obstacle_mm <= kObstacleStopMM` = 500mm) at vehicle speeds $>100\text{ mm/s}$:
- RT computes:
  $$P_{\text{brake}} = \max(P_{\text{obstacle\_calc}}, P_{\text{host\_req}})$$
- On SYS, if $P_{\text{brake}} > 0$, SYS switches SEB to **Pressure Mode** (`control_mode = 1`), converting kPa setpoint to raw `VCU_SEB_Pre_Value_Req` using scale factor $0.02\text{ (50 kPa/bit)}$. When $P_{\text{brake}} == 0$, SYS reverts to **Stroke Mode** (`control_mode = 0`) for physical lever/ESTOP triggers.

### 3. SEB Stroke Alignment & Boot Synchronization
During system power-on:
- SYS reads `SEB_Stroke_Value` from `0x721` to establish initial baseline target alignment.
- SYS verifies `SEB_Alignment_Status == 1` before allowing automated brake operation. If `SEB_Alignment_Status == 0`, automated brake movements are inhibited until homing/alignment completes.

### 4. Steering EPS-C Speed-Dependent Dynamic Slew Rate
The EPS-C steering unit requires target steering angle slew rate (`VCU_SES_Tgt_StrAngleSpd`) to scale with vehicle speed to prevent sudden jerks at high speeds while maintaining high responsiveness at low speeds:
$$\text{SlewRate} = \text{clamp}\left(125 + 0.1 \cdot |v_{\text{veh}}|, 125, 525\right) \quad [^\circ/\text{s}]$$
Commands below $125^\circ/\text{s}$ or above $525^\circ/\text{s}$ are clamped to avoid EPS-C command rejection.

### 5. Multi-Task FreeRTOS Priority Architecture
To guarantee deterministic execution order and prevent priority inversion across CAN operations:
- **Priority 5 (Highest)**: CAN RX Interrupt/Task & Safety Monitor (`task_safety`, `t_can_rx`)
- **Priority 4**: Dispatch Router (`t_dispatch`)
- **Priority 3**: Real-Time Control Loop & Actuator TX (`task_brake`, `t_control`, `t_can_low_tx`)
- **Priority 2**: High-CAN Gateway TX (`t_can_high_tx`)
- **Priority 1 (Lowest)**: Periodic Heartbeats & Diagnostic Telemetry (`t_heartbeat`, `task_diag`)

---

## 14. Stacked & Layered Safety Control Hierarchy Overview

The eTrike CAN architecture enforces multi-tiered safety layers built directly on top of each other:

```
  +-------------------------------------------------------------------------+
  | LEVEL 3: HARDWIRED PHYSICAL ESTOP INTERLOCK                             |
  | (Cuts motor power relay directly; un-bypassable by software)            |
  +-------------------------------------------------------------------------+
                                       ^
                                       |
  +-------------------------------------------------------------------------+
  | LEVEL 2: SOFTWARE SAFETY & MODE SUPERVISOR (SYS ESP32-S3)               |
  | - EGAS L2 setpoint vs speed feedback mismatch check (>200ms -> ESTOP)   |
  | - Physical brake lever override (ALWAYS WINS over CAN pressure)         |
  | - 200ms RT heartbeat watchdog & direct SEB 0x7B9 takeover               |
  +-------------------------------------------------------------------------+
                                       ^
                                       |
  +-------------------------------------------------------------------------+
  | LEVEL 1: REAL-TIME KINEMATICS & GATEWAY (RT ESP32-S3)                    |
  | - Speed-dependent steering slew rate & following-error checks           |
  | - Obstacle distance max-select brake arbitration (max(obstacle, host))  |
  | - Dual-bus rate-managed transparent gateway & 200ms SYS HB takeover     |
  +-------------------------------------------------------------------------+
                                       ^
                                       |
  +-------------------------------------------------------------------------+
  | LEVEL 0: LOW-LEVEL DRIVER & BUS INTEGRITY                               |
  | - Handle-based TWAI API preserving zero-length DLC 0 SAFETY_ESTOP       |
  | - Deterministic slot reclamation & single-supervisor Bus-Off recovery   |
  | - Rate-limited 0x001 ESTOP broadcast & anti-loop cross-bus filtering    |
  +-------------------------------------------------------------------------+
```

This 4-tier stack guarantees that even if a lower layer fails or hangs, higher safety tiers retain independent authority to safely stop the vehicle.
