# Comprehensive CAN Architecture & System Specification

## Table of Contents
1. [System Architecture & Network Topology](#1-system-architecture--network-topology)
2. [Low-Level Driver Mechanics & Frame Integrity](#2-low-level-driver-mechanics--frame-integrity)
3. [Gateway Bridging & Routing Rules](#3-gateway-bridging--routing-rules)
4. [Comprehensive Timing Deadlines & Staleness Matrix](#4-comprehensive-timing-deadlines--staleness-matrix)
5. [Handshakes, ACKs & Verification Loops](#5-handshakes-acks--verification-loops)
6. [ECU State Machines & Operational Lifecycles](#6-ecu-state-machines--operational-lifecycles)
7. [Actuator Command & Arbitration Stacks](#7-actuator-command--arbitration-stacks)
8. [Heartbeats, Watchdogs & Liveness Verification](#8-heartbeats-watchdogs--liveness-verification)
9. [CAN Lighting Control & Powertrain Integration](#9-can-lighting-control--powertrain-integration)
10. [Exhaustive CAN Bus-Off Triggers & Software Mechanisms](#10-exhaustive-can-bus-off-triggers--software-mechanisms)
11. [Hidden & Counter-Intuitive Bus-Off / Bus-Down Root Causes](#11-hidden--counter-intuitive-bus-off--bus-down-root-causes)
12. [Standardized Decision Flowcharts & Structured Text Explanations](#12-standardized-decision-flowcharts--structured-text-explanations)
13. [FreeRTOS Task Priority Architecture](#13-freertos-task-priority-architecture)
14. [Stacked & Layered Safety Control Hierarchy Overview](#14-stacked--layered-safety-control-hierarchy-overview)

---

## 1. System Architecture & Network Topology

The eTrike platform utilizes a multi-bus, fault-tolerant CAN architecture designed to segregate real-time kinematics, high-level autonomous navigation, system-level safety authority, and high-voltage power management.

```
                          +-------------------------------+
                          |      Host (Jetson Orin)       |
                          |   Autonomous Navigation & HMI |
                          +-------------------------------+
                                          |
                                          | High CAN (500 kbit/s)
                                          v
+-----------------------+     +-------------------------------+
|  Ultrasonic / Sensors | --> |          RT ESP32-S3          |  <--- Dual-Bus Gateway
+-----------------------+     | Real-Time Kinematics & Gateway|
                              +-------------------------------+
                                              |
                                              | Low CAN (500 kbit/s)
         +--------------------+---------------+---------------+--------------------+
         |                    |                               |                    |
         v                    v                               v                    v
 +---------------+    +---------------+               +---------------+    +---------------+
 | SYS ESP32-S3  |    | EPS-C Steering|               |   SEB Brake   |    |  MTR (STM32)  |
 | Safety & Mode |    | (Steer-by-Wire|               | (Brake-by-Wire|    | (Motor Drive) |
 +---------------+    +---------------+               +---------------+    +---------------+

                      +-------------------------------+
                      |           PWT ESP32           |
                      |     Powertrain Controller     |
                      +-------------------------------+
                                      |
                                      | Powertrain CAN (250 kbit/s)
                                      v
                      +-------------------------------+
                      |    DC-DC 48V-to-12V Converter |
                      +-------------------------------+
```

### Physical Bus Segregation & Rationale

#### 1. High CAN (500 kbit/s)
- **Primary Function**: Dedicated point-to-point physical link connecting Host (Jetson Orin) and **RT** (Real-Time Controller).
- **Transmitted Messages**: High-level ROS drive setpoints `0x300` (`HOST_DRIVE_CMD`), brake setpoints `0x301` (`HOST_BRAKE_REQ`), light commands `0x302` (`HOST_LIGHT_CMD`), and Host heartbeat `0x7FC` (`HOST_HEARTBEAT`).
- **Architectural Rationale**: Prevents high-bandwidth sensor telemetry, point-cloud ROS bridges, or host chatter from corrupting real-time vehicle actuator channels on Low CAN.

#### 2. Low CAN (500 kbit/s)
- **Primary Function**: Shared vehicle control bus connecting **RT**, **SYS** (Safety & Mode Authority), **EPS-C** (Steering), **SEB** (Braking), **MTR** (Motor Controller), and **HMI**.
- **Transmitted Messages**: System Mode Command `0x110` (`SYS_MODE_CMD`), Steering Command `0x169` (`VCU_SES_REQ`), Steering Feedback `0x201` (`SES_STATUS`), RT Drive Command `0x204` (`RT_DRIVE_CMD`), RT Brake Command `0x205` (`RT_BRAKE_CMD`), SEB Brake Command `0x7B9` (`VCU_SEB_REQ`), SEB Status `0x721` (`SEB_STATUS`), Motor Feedback `0x206` (`MTR_MOTOR_FBK`), and Emergency Stop `0x001` (`SAFETY_ESTOP`).
- **Architectural Rationale**: Operates under strict real-time deterministic timing contracts (50 Hz / 100 Hz loops) to guarantee collision-free actuator execution.

#### 3. Powertrain CAN (250 kbit/s)
- **Primary Function**: Completely isolated bus managed by **PWT** (Powertrain ECU) controlling the 48V-to-12V DC-DC power converter.
- **Transmitted Messages**: Extended ID `0x10262B27` (`PWT_DCDC_CMD`).
- **Architectural Rationale**: Eliminates 48V/12V DC-DC switching noise and prevents baud rate mismatches (250 kbit/s vs 500 kbit/s) from disrupting primary vehicle safety nodes.

---

## 2. Low-Level Driver Mechanics & Frame Integrity

### 1. Handle-Based TWAI Driver (ESP-IDF 5.5 Migration)

#### Zero-Length DLC 0 Wire Contract Preservation
- **Target Frame**: `0x001` (`SAFETY_ESTOP`)
- **Mechanism**: Migrated from legacy `twai_transmit()` to handle-based `twai_new_node_onchip()` API.
- **Reason**: Legacy ESP-IDF driver automatically expanded zero-length frames (DLC 0) to 8 bytes. Modern handle-based API guarantees strict **0-byte wire contract** required by SEB and EPS-C actuators.
- **Code Reference**: [can_driver.h:L70-L90](file:///e:/work/etrike/sys-esp32/src/can_driver.h#L70-L90)

#### Hardware Pin Mapping & Swap Support
- **SYS Pin Map**: CTX = GPIO 5, CRX = GPIO 4 ([sys/config.h:17-18](file:///e:/work/etrike/sys-esp32/src/config.h#L17-L18)).
- **RT Pin Map**: Default CTX = GPIO 5, CRX = GPIO 4 ([rt/config.h:65-66](file:///e:/work/etrike/rt-esp32/src/config.h#L65-L66)), with compile-time pin-swap support via `ETRIKE_RT_TWAI_SWAP_TX_RX`.

#### Deterministic Slot Reclamation on Bus-Off
- **Mechanism**: Configures `fail_retry_cnt = 0` and `tx_queue_depth = 1`.
- **Reason**: ESP-IDF 5.5 abandons in-flight frames during Bus-Off events without triggering `on_tx_done` callbacks. Single-depth queue configuration ensures driver slots are reclaimed deterministically upon recovery without leaking FreeRTOS queue handles.

---

### 2. Hardware Slot Protection & Software Retry Loop (`gw_pump`)

#### Arbitration Re-enqueueing Logic
- **Queue**: `GwTxFrame` software gateway queue
- **Retry Parameters**: Max attempts `kGwMaxAttempts = 40` (40 attempts $\times 10\text{ ms} = 400\text{ ms}$ max lifetime). Note: `kGwFreshnessTicks = 80ms` (8 ticks at 100 Hz) constant exists in source but freshness checking logic is unused in `gw_pump()`.
- **Execution Path**: If hardware TX buffer is busy or arbitration is lost, `gw_pump()` re-enqueues the frame to the **front of the queue** (`xQueueSendToFront`).
- **Benefit**: Preserves exact frame transmission order without dropping packets under heavy bus loads.
- **Code Reference**: [main.cpp:L219-L242](file:///e:/work/etrike/rt-esp32/src/main.cpp#L219-L242)

---

### 3. Peer Presence & Admission Control (`g_last_low_peer_us`)

#### ACK-Starvation Protection Gate
- **Condition**: Evaluates `drv->tx_admitted()`.
- **Requirement**: Requires at least one valid frame to have arrived from a physical peer on Low CAN within `kLowCanPeerTimeoutMs = 1500ms` liveness window.
- **Action**: Operational TX tasks (`0x210` `RT_STATE_RPT`, `0x7FD` `RT_HEARTBEAT`, `0x169` `VCU_SES_REQ`, `0x7B9` `VCU_SEB_REQ`) are suspended when no physical peer is present.
- **Benefit**: Prevents FreeRTOS TX mailboxes from filling and deadlocking application loops when operating standalone on a bench.
- **Code Reference**: [can_dispatch.h:L111-L118](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L111-L118), [config.h:L51](file:///e:/work/etrike/rt-esp32/src/config.h#L51)

#### ESTOP Priority Gate Bypass
- **Target Frame**: `0x001` (`SAFETY_ESTOP`)
- **Action**: Bypasses peer presence admission checks completely (`bypasses tx_admitted()`).
- **Benefit**: Guarantees emergency stop broadcasts transmit immediately even if normal operational TX is suspended.
- **Code Reference**: [main.cpp:L165](file:///e:/work/etrike/rt-esp32/src/main.cpp#L165)

---

## 3. Gateway Bridging & Routing Rules

**RT** is the single dual-bus ECU in the architecture, operating a rate-managed transparent gateway between High CAN and Low CAN.

```
High CAN (500k)  <=================>  RT ESP32 Gateway  <=================>  Low CAN (500k)
```

### Forwarding Routing Table

| CAN ID | Message Name | Origin $\rightarrow$ Destination | Payload Handling | Special Gateway Logic |
| :--- | :--- | :--- | :--- | :--- |
| **`0x001`** | `SAFETY_ESTOP` | High $\leftrightarrow$ Low (Bi-directional) | DLC 0 (Zero-length) | Enqueued to queue head (`xQueueSendToFront`). Cross-bus anti-loop filter prevents echoing back to source bus. |
| **`0x011`** | `SYS_SAFETY_STS` | Low $\rightarrow$ High | Same Frame | SYS safety status, ESTOP state, and light status forwarded to Host. |
| **`0x111`** | `HMI_MODE_REQ` | High $\rightarrow$ Low | Same Frame | HMI mode change requests forwarded from Host to SYS. |
| **`0x112`** | `HMI_PWR_REQ` | High $\rightarrow$ Low | Same Frame | Power state requests forwarded from Host to SYS. |
| **`0x120`** | `SYS_THROTTLE_STS` | Low $\rightarrow$ High | Same Frame | Measured physical throttle speed forwarded to Host. |
| **`0x206`** | `MTR_MOTOR_FBK` | Low $\rightarrow$ High | Same Frame | Motor RPM, gear status, and fault flags forwarded to Host. |
| **`0x302`** | `HOST_LIGHT_CMD` | High $\rightarrow$ Low | Same Frame | Rider/Host lighting commands forwarded to SYS. |
| **`0x600`** | `SYS_DIAG_RPT` | Low $\rightarrow$ High | Same Frame | System heap memory, reset reasons, and CAN error counters forwarded to Host. |

---

## 4. Comprehensive Timing Deadlines & Staleness Matrix

The vehicle operates under strict deterministic timing boundaries. Violations trigger localized fallback or global safety ESTOP:

| Constant Name | Value | Scope | Associated CAN Message | Description & Safety Violation Action |
| :--- | :--- | :--- | :--- | :--- |
| `kStartupGracePeriodMs` | **3000 ms** | RT & SYS Boot | All Heartbeats | Grace window post power-on. Suppresses missing heartbeat ESTOPs during peripheral boot. |
| `kHeartbeatTimeoutMsRt` | **200 ms** | SYS Monitor | `0x7FD` (`RT_HEARTBEAT`) | 2 missed 100ms RT heartbeats. SYS forces ESTOP and broadcasts `0x001` (`SAFETY_ESTOP`). |
| `kHeartbeatTimeoutMsSys` | **200 ms** | RT Monitor | `0x7FE` (`SYS_HEARTBEAT`) | 2 missed 100ms SYS heartbeats. RT triggers SEB brake takeover (`0x7B9` `VCU_SEB_REQ` 2000 kPa). |
| `kHeartbeatTimeoutMsHost`| **1500 ms** | RT Monitor | `0x7FC` (`HOST_HEARTBEAT`)| Host liveness loss. RT initiates assisted stop with 2000 kPa (`kAssistStopKpa`) brake setpoint. |
| `kHostCmdStaleTimeoutMs`| **500 ms** | RT Watchdog | `0x300` (`HOST_DRIVE_CMD`)| RT watchdog for Host drive setpoint. Zeroes `0x204` (`RT_DRIVE_CMD`) and triggers steering ESTOP. |
| `kSetpointStaleMs` | **50 ms** | SYS Brake Task | `0x204` (`RT_DRIVE_CMD`) | 5 missed 10ms RT drive setpoint frames. Fast-path deadman revokes RT brake control and takes over `0x7B9`. |
| `kBrakeSetpointStaleMs` | **100 ms** | SYS Brake Task | `0x205` (`RT_BRAKE_CMD`) | 5 missed 20ms RT brake setpoint frames. Overwrites stale RT input with `kMaxBrakeKpa` (5000 kPa). |
| `kSteerFollowingErrMs` | **300 ms** | RT Control Task| `0x201` (`SES_STATUS`) | Steering angle following-error persistence limit before aborting AUTO mode and latching ESTOP. |
| `kBrakeFollowingErrMs` | **100 ms** | SYS Brake Task | `0x721` (`SEB_STATUS`) | SEB stroke following-error persistence limit before logging actuator fault warning. |
| `kEgasFaultDurationMs` | **500 ms** | SYS Safety Task| `0x204` vs `0x206` | EGAS Level 2 speed mismatch persistence (`0x204` vs `0x206`) before forcing global ESTOP. |
| `kMtrFbkStaleMs` | **200 ms** | SYS Safety Task| `0x206` (`MTR_MOTOR_FBK`) | Motor feedback staleness limit. |
| `kSebStatusTimeoutMs` | **100 ms** | SYS Brake Task | `0x721` (`SEB_STATUS`) | SEB status frame loss warning limit. |
| `kSebRollingTimeoutMs` | **100 ms** | SYS Brake Task | `0x721` (`SEB_STATUS`) | Maximum window for SEB `0x721` rolling counter advancement. |
| `kSebHandoffGraceMs` | **500 ms** | SYS Brake Task | `0x7B9` (`VCU_SEB_REQ`) | Handoff window during MANUAL $\rightarrow$ AUTO transition for RT to claim `0x7B9` sole ownership. |
| `kMtrEstopAckTimeoutMs` | **100 ms** | SYS Safety Task| `0x206` (`MTR_MOTOR_FBK`) | MTR ESTOP acknowledgment deadline in `0x206` post `0x001` broadcast. |
| `kDebounceMs` | **500 ms** | SYS Mode Manager| Physical Switch | Physical button debounce lock-out window. |
| `kEstopLongPressMs` | **3000 ms** | SYS Mode Manager| MODE Button | Required hold duration on MODE button in ESTOP state to recover to MANUAL mode. |
| `kEstopRateLimitWindowMs`| **500 ms**| RT & SYS Safety| `0x001` (`SAFETY_ESTOP`) | Rolling window for rate-limiting ESTOP broadcasts (max 2 frames per 500ms). |
| `kEstopBroadcastMinIntervalUs`| **250 ms**| Shared Safety | `0x001` (`SAFETY_ESTOP`) | Minimum 250,000 µs interval between `0x001` broadcasts per ECU node. |

---

## 5. Handshakes, ACKs & Verification Loops

```
  +--------------+               0x001 SAFETY_ESTOP              +--------------+
  | SYS ESP32-S3 | ============================================> | MTR Controller|
  +--------------+                                               +--------------+
         ^                                                              |
         |         0x206 MTR_MOTOR_FBK (ESTOP_ACTIVE bit = 1)           |
         +--------------------------------------------------------------+
                           (Must arrive within 100ms)
```

### 1. Motor ESTOP Acknowledgment Handshake

#### Overview
- **Trigger Frame**: `0x001` (`SAFETY_ESTOP`)
- **ACK Return Frame**: `0x206` (`MTR_MOTOR_FBK`)
- **ACK Field**: Byte 3 Bit 0 (`kMtrFaultEstopActive = 0x01`)
- **Timeout Limit**: `kMtrEstopAckTimeoutMs = 100ms`

#### Verification Logic
- When SYS triggers an ESTOP and broadcasts `0x001` (`SAFETY_ESTOP`), SYS starts a 100 ms ACK timer.
- MTR STM32 motor controller must set `kMtrFaultEstopActive = 0x01` in Byte 3 of `0x206` (`MTR_MOTOR_FBK`) within 100 ms.
- If `0x206` fails to reflect `kMtrFaultEstopActive` within 100 ms, SYS logs an un-acknowledged ESTOP warning.

---

### 2. SEB Rolling Counter ACK & Fallback Monitoring

#### Overview
- **Command Frame**: `0x7B9` (`VCU_SEB_REQ`)
- **Feedback Frame**: `0x721` (`SEB_STATUS`)
- **ACK Field**: Byte 1 Bits 0–3 (`rolling_counter`)
- **Timeout Limit**: `kSebRollingTimeoutMs = 100ms`

#### Verification Logic
- In AUTO mode, RT sends `0x7B9` (`VCU_SEB_REQ`) directly to SEB.
- SYS monitors `rolling_counter` in `0x721` (`SEB_STATUS`) to confirm SEB is actively receiving and executing commands.
- **Fallback Action**: If RT's `0x7B9` transmission fails or experiences CAN bus errors, SEB stops incrementing its rolling counter. SYS detects the stalled counter and **resumes broadcasting `0x7B9` from SYS** to maintain physical brake authority.

---

### 3. EPS-C Steering Security ACK & Rolling Counter Guard

#### Overview
- **Command Frame**: `0x169` (`VCU_SES_REQ`)
- **Feedback Frame**: `0x201` (`SES_STATUS`)
- **Security Fields**: Byte 5 (`RollCnt_Enable`, `CheckSum_Enable`, `RollCnt`), Byte 7 (`XOR8 Checksum`)

#### Verification Logic
- RT computes XOR8-complement checksum across Bytes 0–6 of `0x169` (`VCU_SES_REQ`) and increments `RollCnt` monotonically.
- On incoming feedback `0x201` (`SES_STATUS`), RT tracks `last_eps_roll`.
- **Frozen Counter Protection**: If `rolling_counter` in `0x201` remains identical across consecutive frames, RT skips feedback angle parsing, preventing stuck CAN DMA buffers from masking actuator faults.

---

### 4. HMI Request Parsing & Mode Validation

#### Overview
- **Request Frames**: `0x111` (`HMI_MODE_REQ`) and `0x112` (`HMI_PWR_REQ`)
- **Code Reference**: [mode_manager.cpp:L79-L94](file:///e:/work/etrike/sys-esp32/src/mode_manager.cpp#L79-L94)

#### Verification Logic
- `ModeManager::parse_hmi_mode(uint8_t requested_mode)` evaluates incoming HMI mode change requests from Jetson Orin.
- **Validation Gates**:
  - Rejects HMI requests if vehicle is currently in `ESTOP` mode (hardware ESTOP overrides software requests).
  - Validates `requested_mode <= 1` (allowing only `0 = MANUAL` or `1 = AUTO`; rejects out-of-bounds or simulation modes).
  - Updates mode state atomically if request is valid.

---

## 6. ECU State Machines & Operational Lifecycles

### Mode Authority State Machine (SYS ESP32-S3)

#### State Diagram

```
                  +-----------------------------------+
                  |              BOOT                 |
                  +-----------------------------------+
                                    |
                                    v
                  +-----------------------------------+
                  |             MANUAL                | <------------------+
                  +-----------------------------------+                    |
                    |                               |                      |
      MODE Press /  |                               | ESTOP Trigger        | START Press /
      0x111 Request |                               | (Button/Fault/0x001) | MODE Long-Press (3s)
                    v                               v                      |
                  +-----------------------------------+                    |
                  |              AUTO                 | -------------------+
                  +-----------------------------------+
```

#### Detailed State Descriptions
- **BOOT State**: Initial power-on state. Peripheral initialization, NVS flash read, and hardware self-test occur. Suppresses missing heartbeat ESTOPs for 3000 ms (`kStartupGracePeriodMs`).
- **MANUAL State**: Default operating mode. Rider handlebar switches control steering, throttle, and braking directly. High-CAN autonomous drive commands are ignored.
- **AUTO State**: Autonomous drive mode. Activated via physical MODE button toggle or `0x111` (`HMI_MODE_REQ`). SYS delegates `0x7B9` (`VCU_SEB_REQ`) brake authority to RT while supervising safety.
- **ESTOP State**: Emergency stop mode. Triggered by physical ESTOP button, software fault, or incoming `0x001` (`SAFETY_ESTOP`). Zeroes motor throttle, commands maximum SEB brake stroke ($27\text{ mm}$), and forces brake lamps ON. Recovery to MANUAL requires a physical START button press or 3-second long-press on the MODE button (`kEstopLongPressMs`).

---

### Steering Boot State Machine (`SteerState` in RT ESP32-S3)

#### Code Reference: [steering_control.h:L20-L88](file:///e:/work/etrike/rt-esp32/src/steering_control.h#L20-L88)

#### Detailed State Transitions
- **`STEER_BOOT_WAIT`**:
  - *Duration*: 500 ms (`kSteerBootWaitMs`).
  - *Behavior*: Command outputs held silent; no `0x169` (`VCU_SES_REQ`) frames transmitted.
- **`STEER_LISTEN_SYNC`**:
  - *Trigger*: Boot wait expires.
  - *Behavior*: Listens for `0x201` (`SES_STATUS`). Checks 5-second sync timeout (`kSteerSyncTimeoutMs = 5000ms`).
  - *Plausibility Check*: Verifies `angle_status == 1` (alignment complete) and checks $|\theta_{\text{boot}}| \le 30^\circ$. If power-on wheel angle exceeds $30^\circ$, flags offset error $\rightarrow$ `STEER_FAULT`.
- **`STEER_ACTIVE`**:
  - *Trigger*: Alignment confirmed and angle plausible.
  - *Behavior*: Normal 50 Hz steering command publication on `0x169` (`VCU_SES_REQ`).
- **`ESTOP_RAMP_TO_ZERO`**:
  - *Trigger*: Non-obstacle ESTOP event.
  - *Behavior*: Ramps steering target angle smoothly back to $0^\circ$ (straight) at $20^\circ/\text{s}$, then holds position.
- **`ESTOP_HOLD_THEN_SILENT`**:
  - *Trigger*: Obstacle-triggered ESTOP event.
  - *Behavior*: Holds current steering angle for 500 ms so vehicle stays straight while braking, then transitions to silent-stop.
- **`STEER_FAULT`**:
  - *Trigger*: Comms loss, sync timeout, or L3 actuator fault (`0x202` `SES_ERR_INFO`).
  - *Behavior*: Completely ceases transmitting `0x169` (`VCU_SES_REQ`), allowing EPS-C to enter silent fallback.

---

### SEB Brake Boot State Machine (`BrakeState` in SYS ESP32-S3)

#### Code Reference: [brake_control.h:L7-L78](file:///e:/work/etrike/sys-esp32/src/brake_control.h#L7-L78)

#### Detailed State Transitions
- **`BOOT_WAIT`**:
  - *Duration*: 500 ms (`kBrakeBootWaitMs`).
  - *Behavior*: Commands held silent.
- **`LISTEN_SYNC`**:
  - *Trigger*: Boot wait expires.
  - *Behavior*: Listens for `0x721` (`SEB_STATUS`). Captures baseline stroke (`m_sync_stroke_raw`) for hold-on-sync upon alignment (`status_byte0 & 1`). If 2-second timeout (`kBrakeSyncTimeoutMs = 2000ms`) expires without alignment $\rightarrow$ `BRAKE_DEGRADED`.
- **`DEGRADED`**:
  - *Trigger*: Alignment timeout expiration.
  - *Behavior*: Transmits 50 Hz commands with physical brake lever functionality ONLY, ignoring incoming CAN `0x205` (`RT_BRAKE_CMD`) pressure setpoints until `0x721` alignment recovers.
- **`ACTIVE`**:
  - *Trigger*: SEB alignment confirmed.
  - *Behavior*: Normal 50 Hz operation supporting full Pressure Mode and Stroke Mode arbitration.

---

## 7. Actuator Command & Arbitration Stacks

### 1. SEB Brake Command Arbitration (Option D)

#### Architecture Overview
To eliminate single points of failure (SPOF) while achieving 1-hop minimal latency:
- **In AUTO Mode**: When RT is healthy (`rt_alive && rt_normal && rt_setpoint_fresh`), RT transmits `0x7B9` (`VCU_SEB_REQ`) directly to SEB (1-hop latency). SYS suppresses its own `0x7B9` output ([sys-esp32/src/main.cpp:L626-L656](file:///e:/work/etrike/sys-esp32/src/main.cpp#L626-L656)).
- **In MANUAL / ESTOP Mode**: SYS assumes direct ownership of `0x7B9` (`VCU_SEB_REQ`), translating physical lever inputs or ESTOP brake curves into SEB commands.
- **Rider Lever Override**: If the rider pulls the physical brake lever in AUTO mode, SYS immediately overrides RT, takes over `0x7B9` (`VCU_SEB_REQ`), and commands maximum rider braking pressure.

#### Strict 4-Tier SEB Priority Hierarchy

##### Priority 1: ESTOP (Highest Priority)
- **Trigger**: Physical ESTOP button, software fault, or incoming `0x001` (`SAFETY_ESTOP`).
- **Command Output**: Maximum stroke ($27\text{ mm}$, raw 1140) in **Stroke Mode** (`control_mode = 0`).

##### Priority 2: Physical Brake Lever
- **Trigger**: Rider pulls handlebar brake lever (`lever == true`).
- **Command Output**: Driver override ALWAYS WINS (commands $15\text{ mm}$ stroke) in **Stroke Mode** (`control_mode = 0`), overriding automated CAN pressure setpoints.

##### Priority 3: Automated CAN Pressure
- **Trigger**: Automated brake setpoint `brake_kpa > 0` (from RT `0x205` or Host `0x301`).
- **Command Output**: Switches SEB to **Pressure Mode** (`control_mode = 1`). Converts kPa to raw pressure units:
  $$\text{RawPressure} = \text{uint8\_t}(\text{brake\_kpa} \cdot 0.02\text{ (50 kPa/bit)})$$

##### Priority 4: Default Released (Lowest Priority)
- **Trigger**: No ESTOP, no lever pull, `brake_kpa == 0`.
- **Command Output**: Commands $0\text{ mm}$ stroke in **Stroke Mode** (`control_mode = 0`).

---

### 2. Obstacle Auto-Braking & Max-Select Math

#### Code Location
- RT Kinematics: [safety_monitor.h:L157-L162](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L157-L162), [brake_arbitration.h:L7](file:///e:/work/etrike/rt-esp32/src/brake_arbitration.h#L7)
- Target CAN Frame: `0x205` (`RT_BRAKE_CMD`) / `0x7B9` (`VCU_SEB_REQ`)

#### Mathematical Formulation & Parameters
When obstacle distance $d_{\text{obstacle}} \le 300\text{ mm}$ (`kObstacleStopMM`) and vehicle speed $> 50\text{ mm/s}$ (`kLowSpeedThreshMmps`):
- Obstacle stop distance threshold: $d_{\text{stop}} = 300\text{ mm}$ (`kObstacleStopMM`).
- Obstacle clear distance threshold: $d_{\text{clear}} = 3000\text{ mm}$ (`kObstacleClearMM`).
- Maximum brake pressure cap: $P_{\text{max}} = 5000\text{ kPa}$ (`kMaxBrakeKpa` / `kObstacleMaxKpa`).
- Assisted stop pressure: $P_{\text{assist}} = 2000\text{ kPa}$ (`kAssistStopKpa`).

Brake pressure arbitration math:
$$P_{\text{final\_brake}} = \text{clamp}\left(\max(P_{\text{obstacle\_calc}}, P_{\text{host\_req}}), 0, 5000\right) \quad [\text{kPa}]$$

---

### 3. Steering Dynamic Slew-Rate & Following-Error Math

#### Speed Units & Dynamic Equations
- **Vehicle Speed Unit**: Measured in $\text{km/h}$ ($v_{\text{kmh}}$).
- **Dynamic Angle Clamp**: Steering limit decreases with speed to enforce stability at higher speeds:
  $$\theta_{\text{clamp\_limit}} = \text{clamp}\left(40.0 - (v_{\text{kmh}} - 2.0) \cdot \frac{35.0}{23.0}, 5.0, 40.0\right) \quad [^\circ]$$
- **Dynamic Slew Rate Equation**: Slew rate increases with speed ($v_{\text{kmh}}$):
  $$\text{SlewRate} = \text{clamp}\left(125.0 + (v_{\text{kmh}} - 2.0) \cdot \frac{400.0}{23.0}, 125.0, 525.0\right) \quad [^\circ/\text{s}]$$

#### Dynamic Following Error Threshold Equation
The following error threshold is defined as a fraction of the dynamic angle clamp:
$$\text{Threshold}_{\text{deg}} = \max\left(2.0, 0.25 \cdot \theta_{\text{clamp\_limit}}(v_{\text{kmh}})\right) \quad [^\circ]$$

Because $\theta_{\text{clamp\_limit}}$ **decreases as vehicle speed increases** (from $40^\circ$ at $2\text{ km/h}$ down to $5^\circ$ at $\ge 25\text{ km/h}$), the following-error threshold **DECREASES with speed** (stricter error monitoring at higher speeds).

If absolute steering error $|\theta_{\text{cmd}} - \theta_{\text{actual}}| > \text{Threshold}_{\text{deg}}$ for $>300\text{ ms}$ (`kSteerFollowingErrMs = 300ms`, 30 consecutive ticks at 100 Hz), RT aborts AUTO mode and latches ESTOP (`kEstopReasonFollowingError`).

---

## 8. Heartbeats, Watchdogs & Liveness Verification

### 1. Dual 10 Hz Heartbeats & Frozen Counter Guard

#### Target CAN Messages
- RT Heartbeat: `0x7FD` (`RT_HEARTBEAT`)
- SYS Heartbeat: `0x7FE` (`SYS_HEARTBEAT`)

#### Stuck/Frozen DMA Counter Guard Logic
- **Field Monitored**: Byte 0 (`alive_ctr`)
- **Code Reference**: [can_dispatch.h:L73-L86](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L73-L86)
- **Math Formula**:
  $$\text{delta} = \text{uint8\_t}(\text{new\_alive\_ctr} - \text{last\_alive\_ctr})$$
- **Evaluation**: If `delta == 0` across consecutive frames, software flags the frame as **frozen** (indicating a deadlocked control task with hardware CAN DMA active), skipping timestamp updates and forcing a 200 ms timeout.

---

### 2. Independent Sequence Counters (`DualHeartbeat`)

#### Code Reference: [heartbeat.h:L13-L32](file:///e:/work/etrike/rt-esp32/src/heartbeat.h#L13-L32)
- RT maintains completely **separate sequence counters** (`m_ctr_low` for Low CAN `0x7FD`, `m_ctr_high` for High CAN `0x7FD`).
- **Benefit**: A transmit failure or queue block on High CAN does not corrupt packet sequence continuity on Low CAN.

---

## 9. CAN Lighting Control & Powertrain Integration

### 1. Isolated Powertrain CAN Bus (PWT ESP32-S3)

#### Physical Isolation Rationale
- PWT operates an **isolated 250 kbit/s powertrain CAN bus** on TWAI0 (`GPIO7 TX`, `GPIO6 RX`).
- Transmits Extended ID `0x10262B27` (`PWT_DCDC_CMD`), DLC 8, every 100 ms to control the 48V-to-12V converter.
- **Isolation Constraint**: The 250 kbit/s powertrain bus must NEVER be joined to the 500 kbit/s Low-level bus to prevent frame corruption and bus-off crashes.
- **Rate-Limited Failure Logging**: PWT logs the 1st TX failure and every 50th aggregate failure to prevent log flooding during powertrain disconnects.

---

### 2. CAN Lighting Control & Quad-Input OR Logic

#### Hardware Pin Definitions ([sys/config.h:30-38](file:///e:/work/etrike/sys-esp32/src/config.h#L30-L38))
- **Handlebar Switch Inputs**: Left Turn = GPIO 9, Right Turn = GPIO 6, Headlight = GPIO 7.
- **Relay Lamp Outputs**: Left Turn Lamp = GPIO 18, Right Turn Lamp = GPIO 19, Brake Lamp = GPIO 21, Headlamp = GPIO 10.

#### Quad-Input Brake Lamp Boolean Formula
The SYS brake lamp hardware output illuminates if ANY of 4 conditions evaluate to true:
$$\text{BrakeLamp} = \text{BrakeLever} \lor (\text{Mode} == \text{ESTOP}) \lor \text{CAN\_BrakeBit}(0\text{x}302) \lor (\text{SEB\_Stroke} > 0.5\text{ mm})$$

Where `SEB_Stroke > 0.5mm` corresponds to raw stroke reading `g_seb_actual_stroke_raw > 610`.

---

## 10. Exhaustive CAN Bus-Off Triggers & Software Mechanisms

> [!IMPORTANT]
> **Hardware Bus-Off Isolation Principle**: When a node's CAN hardware enters Bus-Off ($\text{TEC} \ge 255$), its physical transceiver is disconnected. The node **CANNOT transmit software ESTOP frames (`0x001` `SAFETY_ESTOP`)** onto the bus. 
> 
> Safety is guaranteed by **Remote Peer Loss Watchdogs**: when a node enters Bus-Off, its 10 Hz heartbeat (`0x7FD` `RT_HEARTBEAT` / `0x7FE` `SYS_HEARTBEAT`) drops. Within **200 ms**, remote peers detect the missing heartbeat and locally trigger ESTOP and fallback safety controls.

### 1. Real-World Bus-Off Root Causes ("If X Happens $\rightarrow$ Bus Goes Off")

| Event Category | Specific Trigger Condition | Immediate Hardware Effect | Vehicle Safety Reaction |
| :--- | :--- | :--- | :--- |
| **Physical Wiring Fault** | `CAN_H` shorted to `CAN_L`, GND, or 12V/48V power | Differential bus voltage collapses. Bit errors occur on every transmit bit $\rightarrow$ `TEC` hits 255 in 32 bit times. | Node enters Bus-Off. Remote peers detect missing heartbeat in **200 ms** $\rightarrow$ SYS assumes direct SEB brake control (`0x7B9` `VCU_SEB_REQ`). |
| **Connector Disconnect** | Physical CAN cable unplugged or loose contact pin | Transmit ACK bit is not driven dominant. Node experiences ACK errors (`+8` `TEC` per frame). | Node hits $\text{TEC} \ge 255$ within 32 frames $\rightarrow$ Bus-Off. Software liveness gate (`g_last_low_peer_us`) blocks operational TX. |
| **Termination Failure** | $120\,\Omega$ end-of-line resistors missing or severed | Signal reflection and high-frequency ringing corrupt frame bit sampling points $\rightarrow$ Bit Error saturation. | Repeated `TEC` increments $\rightarrow$ Bus-Off. |
| **Baud Rate Mismatch** | 250 kbit/s node plugged into 500 kbit/s Low-CAN | Sampling point mismatch generates continuous **Stuff Errors** and **Form Errors** on every bit. | All nodes on bus saturate `TEC` within milliseconds $\rightarrow$ simultaneous Multi-Node Bus-Off. |
| **Transceiver Power Loss** | 5V rail brownout on CAN transceiver (SN65HVD230 / VP230) | Transceiver TXD/RXD fails to translate dominant bits $\rightarrow$ Bit Monitoring Errors (`+8` `TEC`). | Hardware enters Bus-Off. |
| **Severe EMI Noise Burst** | High-voltage motor inverter PWM noise spike $>1.5\,\text{V}$ | Differential noise flips bit states during arbitration or frame CRC $\rightarrow$ Bit/CRC Error saturation. | Transient TEC accumulation. If persistent $\rightarrow$ Bus-Off. |
| **ACK-Starvation (Single Node Power-On)** | Node powers on without any physical peer alive on bus | No physical node drives ACK dominant $\rightarrow$ ACK Error saturation (`+8` `TEC` per TX frame). | Node enters Bus-Off. Solved in software via `g_last_low_peer_us` admission checks. |

---

### 2. Deep Software-Level CAN Mechanisms & Code Logic

#### 1. TWAI Peer Liveness Gate (`g_last_low_peer_us` / `!drv->tx_admitted()`)
- **Location**: [can_dispatch.h:L111-L118](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L111-L118)
- **Logic**: Tracks incoming packet timestamps. If no valid frame arrives from any physical peer within the liveness window (`kLowCanPeerTimeoutMs = 1500ms`), software sets `m_tx_admitted = false`.
- **Effect**: Blocks operational CAN publication (`0x210` `RT_STATE_RPT`, `0x7FD` `RT_HEARTBEAT`, `0x169` `VCU_SES_REQ`, `0x7B9` `VCU_SEB_REQ`) to prevent queue memory leaks.

#### 2. 5-Consecutive Bus-Off Software Latch (`bus_off_count >= 5`)
- **Location**: [can_health.h:L35-L42](file:///e:/work/etrike/rt-esp32/src/can_health.h#L35-L42)
- **Logic**: 10 Hz `monitor_can_bus_off()` supervisor increments counter on each Bus-Off event.
- **Effect**: At 5 consecutive Bus-Off events, software permanently halts operational publication and latches ESTOP mode until hard MCU reboot or START button recovery.

#### 3. Software Queue Pump & Head Re-enqueueing (`gw_pump`)
- **Location**: [main.cpp:L219-L242](file:///e:/work/etrike/rt-esp32/src/main.cpp#L219-L242)
- **Logic**: Buffers cross-bus frames for up to 40 retries (`kGwMaxAttempts = 40`, max 400ms TTL).
- **Effect**: If HW slot is busy, uses `xQueueSendToFront` to push failed frames back to queue head, preserving packet ordering.

#### 4. ESTOP Software Rate-Limiting Gate (`can_send_estop()`)
- **Location**: [safety_monitor.h:L53-L55](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L53-L55)
- **Logic**: Limits `0x001` (`SAFETY_ESTOP`) broadcasts to max 2 frames per 500 ms window (`kEstopRateLimitWindowMs`, min 250ms interval `kEstopBroadcastMinIntervalUs`).
- **Effect**: Drops subsequent ESTOP requests to prevent queue starvation of heartbeats and state reports.

#### 5. Cross-Bus Anti-Loop Echo Filtering
- **Location**: [can_dispatch.h:L124-L130](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L124-L130)
- **Logic**: Inspects incoming `0x001` (`SAFETY_ESTOP`) frame origin.
- **Effect**: Forwards `0x001` *only* across buses (High $\rightarrow$ Low or Low $\rightarrow$ High); never echoes back to origin bus.

#### 6. Stuck/Frozen DMA Counter Rejection (`alive_ctr - last_sys_ctr`)
- **Location**: [can_dispatch.h:L73-L86](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L73-L86)
- **Logic**: Computes 8-bit unsigned rollover delta (`delta = new_ctr - last_ctr`).
- **Effect**: If `delta == 0`, rejects timestamp update, forcing a 200 ms liveness timeout action.

#### 7. Option D Dual-Sender Suppression & Fast-Path Deadman (`g_seb_suppressed`)
- **Location**: [sys-esp32/src/main.cpp:L626-L656](file:///e:/work/etrike/sys-esp32/src/main.cpp#L626-L656)
- **Logic**: Evaluates `rt_setpoint_fresh = (now_ticks - g_last_setpoint_tick) < 50ms` (`kSetpointStaleMs = 50ms`).
- **Effect**: If RT drive commands stall $>50\text{ ms}$, SYS cancels `suppress_seb` and resumes direct 50 Hz SYS brake control.

#### 8. Actuator Boot State Machine Silence (`SteerState` & `BrakeState`)
- **Location**: [steering_control.h:L50-L75](file:///e:/work/etrike/rt-esp32/src/steering_control.h#L50-L75) and [brake_control.h:L34-L63](file:///e:/work/etrike/sys-esp32/src/brake_control.h#L34-L63)
- **Logic**: Evaluates boot delays and alignment status (`0x201` / `0x721` `status_byte0 & 1`).
- **Effect**: Silences `0x169` (`VCU_SES_REQ`) steering commands during sync and transitions SEB to `DEGRADED` stroke-only mode if alignment fails.

#### 9. Dual-Queue Fair Interleaving Dispatch (`t_dispatch`)
- **Location**: [can_dispatch.h:L248-L265](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L248-L265)
- **Logic**: Alternates draining `g_can_rx_low_q` and `g_can_rx_high_q` using zero-timeout polls (`pdMS_TO_TICKS(0)`).
- **Effect**: Prevents high-volume ROS telemetry on High CAN from starving Low-CAN safety frames.

#### 10. MCP2515 High-CAN Mutex Verification (`!spi_lock()`)
- **Location**: `can_driver_mcp2515.cpp`
- **Logic**: Checks `spi_lock()` mutex before issuing SPI commands.
- **Effect**: Aborts TX queueing cleanly when SPI locks fail, preventing task deadlocks.

#### 11. Safety Event Queue Fallback Atomics (`g_pending_estop_event`)
- **Location**: [can_dispatch.h:L33-L43](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L33-L43)
- **Logic**: Sets atomic fallback flags (`g_pending_estop_event.store(true)`) if `g_safety_evt_q` queue is full.
- **Effect**: Guarantees safety transitions are caught on the next control loop tick without packet loss.

#### 12. NVS Flash Diagnostic Persistence (`sys_diag`)
- **Location**: `sys-esp32/src/main.cpp:L1047-L1053`
- **Logic**: Opens NVS namespace `sys_diag` during boot.
- **Effect**: Reads and increments `reset_count` and records `reset_reason` into NVS flash across unexpected reboots for post-mortem diagnostics.

---

## 11. Hidden & Counter-Intuitive Bus-Off / Bus-Down Root Causes

This section details 8 subtle, sneaky, or counter-intuitive reasons where CAN buses silently enter **Bus-Off**, stop publishing, drop frames, or lock up queues:

### 1. Peer Admission Revocation (`g_last_low_peer_us == 0`)
- **CAN Frame Affected**: All Low CAN operational frames (`0x210` `RT_STATE_RPT`, `0x7FD` `RT_HEARTBEAT`, `0x169` `VCU_SES_REQ`, `0x7B9` `VCU_SEB_REQ`)
- **Code Location**: [can_dispatch.h:L111-L118](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L111-L118)
- **Sneaky Trigger**: If Low CAN receives zero valid frames from any peer (e.g. SEB, SYS, EPS-C are unpowered or disconnected during bench test).
- **Silent Effect**: `drv->tx_admitted()` evaluates to `false`, silently **blocking all operational TX queues**. No physical CAN signals leave RT, even though RT MCU itself is perfectly healthy.

---

### 2. Fast Recovery Supervisor Race Condition
- **CAN Frame Affected**: All TWAI Low-CAN Frames
- **Code Location**: [can_health.h:L29-L38](file:///e:/work/etrike/rt-esp32/src/can_health.h#L29-L38)
- **Sneaky Trigger**: TWAI hardware enters Bus-Off, and driver auto-recovery succeeds in $<100\text{ ms}$ (between 10 Hz polling ticks). When the 10 Hz `monitor_can_bus_off()` supervisor polls, `health.state` has already returned to `Active` (`TEC == 0`).
- **Silent Effect**: Polled state check (`state == BusOff`) misses the event completely. Solved by tracking monotonic `health.recovery_attempts` delta; without this, silent Bus-Off glitches would bypass safety logging and ESTOP queue resets.

---

### 3. ESP-IDF 5.5 In-Flight Slot Leakage on Bus-Off
- **CAN Frame Affected**: Hardware TWAI Mailboxes
- **Code Location**: [can_driver.h:L79-L80](file:///e:/work/etrike/sys-esp32/src/can_driver.h#L79-L80)
- **Sneaky Trigger**: On TWAI hardware Bus-Off entry, ESP-IDF 5.5 abandons in-flight frames without calling the `on_tx_done` callback.
- **Silent Effect**: If `tx_queue_depth > 1` or `fail_retry_cnt > 0`, free TX slots leak permanently. When the physical bus recovers, application tasks freeze on `xQueueSend()` because the driver queue still thinks hardware slots are occupied. Solved by setting `fail_retry_cnt = 0`, `tx_queue_depth = 1`, and invoking `xQueueReset()`.

---

### 4. High-CAN MCP2515 SPI Mutex Contention Abort
- **CAN Frame Affected**: High-CAN Gateway Frames (`0x011` `SYS_SAFETY_STS`, `0x120` `SYS_THROTTLE_STS`, `0x206` `MTR_MOTOR_FBK`, `0x600` `SYS_DIAG_RPT`)
- **Code Location**: `can_driver_mcp2515.cpp` (`!spi_lock()`)
- **Sneaky Trigger**: Multiple tasks (`t_can_rx`, `t_dispatch`, `t_heartbeat`) concurrently invoke SPI methods. If one task holds the SPI mutex slightly longer during a register read, `spi_lock()` times out for other tasks.
- **Silent Effect**: Outgoing High-CAN messages are **silently aborted and dropped** without triggering a hard crash, causing missing telemetry packets or silent gateway drops.

---

### 5. Frozen DMA Main-Loop Deadlock ("Ghost Publishing")
- **CAN Frame Affected**: `0x7FD` (`RT_HEARTBEAT`) / `0x7FE` (`SYS_HEARTBEAT`)
- **Code Location**: [can_dispatch.h:L73-L86](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L73-L86)
- **Sneaky Trigger**: Software deadlock in RT or SYS freezes the main FreeRTOS control task, but ESP32 hardware CAN DMA peripheral continues autonomously transmitting the last DMA buffer payload.
- **Silent Effect**: Remote nodes receive valid physical CAN packets with identical `alive_ctr` bytes. Solved by 8-bit unsigned delta checking (`uint8_t delta = alive_ctr - last_ctr`) to prevent ghost nodes from tricking peers into thinking they are healthy.

---

### 6. Intermittent Termination Reflection Ringing
- **CAN Frame Affected**: All Low-CAN 500 kbit/s frames
- **Code Location**: [can_health.h:L23](file:///e:/work/etrike/rt-esp32/src/can_health.h#L23)
- **Sneaky Trigger**: A loose terminal pin or missing $120\,\Omega$ resistor causes high-frequency signal reflections at 500 kbit/s. While basic lower-rate signals pass, fast bit transitions flip single bits, accumulating `+8` `TEC` per error.
- **Silent Effect**: Bus transitions `Active` $\rightarrow$ `Error Passive` (`TEC > 127`) $\rightarrow$ `Bus-Off` (`TEC \ge 255`) repeatedly without any obvious broken wire.

---

### 7. MCP2515 3.0-Second Re-Initialization Delay
- **CAN Frame Affected**: All High-CAN Frames
- **Code Location**: [can_health.h:L83-L87](file:///e:/work/etrike/rt-esp32/src/can_health.h#L83-L87)
- **Sneaky Trigger**: MCP2515 enters Bus-Off. To prevent SPI register spamming, `monitor_can_bus_off()` throttles controller re-initialization with a **3.0-second delay** (`3'000'000 µs`).
- **Silent Effect**: High CAN remains completely offline for 3 full seconds after a transient glitch, making High CAN publication appear "dead" for an unexpectedly long time.

---

### 8. Option D Dual-Sender Suppression Circular Dependency Block
- **CAN Frame Affected**: `0x7B9` (`VCU_SEB_REQ`)
- **Code Location**: [sys-esp32/src/main.cpp:L658-L674](file:///e:/work/etrike/sys-esp32/src/main.cpp#L658-L674)
- **Sneaky Trigger**: On MANUAL $\rightarrow$ AUTO transition, SYS suppressed `0x7B9` (`VCU_SEB_REQ`) only after verifying RT was publishing `RT_NORMAL`. But RT required SEB to be receiving `0x7B9` (`VCU_SEB_REQ`) commands to verify brake alignment.
- **Silent Effect**: A circular deadlock where both nodes transmitted `0x7B9` (`VCU_SEB_REQ`) simultaneously, colliding on Low-CAN and blocking RT's single TX slot. Solved by introducing `kSebHandoffGraceMs` (500 ms unconditional suppression window).

---

## 12. Standardized Decision Flowcharts & Structured Text Explanations

This section combines visual ASCII flowcharts with step-by-step, structured text explanations for each core firmware decision path.

### 1. SEB Option D Brake Control & Arbitration

#### Structured Text Explanation
1. **Loop Execution**: Executed by `task_brake` on SYS ESP32-S3 at 50 Hz ([sys-esp32/src/main.cpp:L633-L721](file:///e:/work/etrike/sys-esp32/src/main.cpp#L633-L721)).
2. **Priority 1 & 2 Check (Override Inputs)**:
   - Evaluates whether the rider physical brake lever is pulled (`lever == true`) or system mode is ESTOP (`mode == Estop`).
   - **If YES**: Overrides automated control immediately. SYS takes direct ownership of `0x7B9` (`VCU_SEB_REQ`) and commands maximum brake stroke ($27\text{ mm}$ for ESTOP, $15\text{ mm}$ for lever).
3. **Priority 3 Check (AUTO Mode Authority Verification)**:
   - **If NO (Normal AUTO mode)**: Evaluates RT authority parameters:
     - `rt_alive`: SYS heartbeat watchdog verified (`<200ms`).
     - `rt_normal`: RT state is Normal (no internal ESTOP).
     - `rt_setpoint_fresh`: Fast-path deadman check verifies RT's drive setpoint tick is fresh (`<50ms`, `kSetpointStaleMs`).
     - `auto_handoff_grace`: Active during the first 500 ms after entering AUTO mode (`kSebHandoffGraceMs`).
4. **Decision Output**:
   - **If RT Authority Valid**: SYS sets `suppress_seb = true`. SYS silences its own `0x7B9` (`VCU_SEB_REQ`) output, giving RT 1-hop un-arbitrated access to transmit `0x7B9` (`VCU_SEB_REQ`) directly to SEB.
   - **If RT Authority Fails**: SYS sets `suppress_seb = false`. Fast-path deadman revokes RT control, overwrites stale input with `kMaxBrakeKpa` (5000 kPa), and SYS transmits `0x7B9` (`VCU_SEB_REQ`) directly to SEB.

```
                                    [ 50 Hz task_brake Loop ]
                                                |
                                                v
                            +---------------------------------------+
                            | Is Rider Brake Lever Pulled? (lever)  |
                            |         OR Is Mode == ESTOP?          |
                            +---------------------------------------+
                                        /               \
                                      YES                NO
                                      /                   \
                                     v                     v
                 +-----------------------+     +-----------------------------------+
                 | Priority 1 & 2 Active |     | Is Vehicle in AUTO Mode? (mode)   |
                 |  - Override RT        |     +-----------------------------------+
                 |  - Transmit SYS 0x7B9 |                 /               \
                 +-----------------------+               YES                NO
                                                         /                   \
                                                        v                     v
                                  +---------------------------+     +-----------------------+
                                  | Check RT Authority:       |     | Priority 4 Active     |
                                  | 1. RT Heartbeat OK?       |     |  - SYS Direct 0x7B9   |
                                  | 2. RT Normal (no ESTOP)?  |     |    Stroke Mode        |
                                  | 3. Setpoint Fresh (<50ms) |     +-----------------------+
                                  | 4. Auto Grace (<500ms)?   |
                                  +---------------------------+
                                              /       \
                                            YES        NO
                                            /           \
                                           v             v
                           +-------------------+   +-------------------------+
                           | Suppress SYS 0x7B9|   | RT Authority Lost!      |
                           | RT Transmits 0x7B9|   | SYS Takes Over 0x7B9    |
                           |  (1-Hop Low-CAN)  |   | Overwrites stale input  |
                           +-------------------+   | with Max Brake (5000kPa)|
                                                   +-------------------------+
```

---

### 2. Dual-Bus Gateway Router Decision Logic

#### Structured Text Explanation
1. **Loop Execution**: Executed by `t_dispatch` on RT ESP32-S3 at Priority 4 ([can_dispatch.h:L63-L137](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L63-L137)).
2. **Special Frame Inspection (`0x001 SAFETY_ESTOP`)**:
   - Inspects frame ID. If `fr.id == 0x001` (`SAFETY_ESTOP`):
     - Normalizes payload to strict **DLC 0 zero-length wire contract**.
     - Enqueues to queue head (`xQueueSendToFront`) for immediate dispatch.
     - Enforces **Cross-Bus Anti-Loop Guard**: Forwards `0x001` (`SAFETY_ESTOP`) *only* across buses (High $\rightarrow$ Low or Low $\rightarrow$ High); never echoes back to origin bus.
3. **Standard Gateway Routing Table**:
   - **If ID != 0x001**: Matches frame against routing policies:
     - High $\rightarrow$ Low: `0x111` (`HMI_MODE_REQ`), `0x112` (`HMI_PWR_REQ`), `0x302` (`HOST_LIGHT_CMD`).
     - Low $\rightarrow$ High: `0x011` (`SYS_SAFETY_STS`), `0x120` (`SYS_THROTTLE_STS`), `0x206` (`MTR_MOTOR_FBK`), `0x600` (`SYS_DIAG_RPT`).
4. **Queue Pump Hand-off (`gw_pump`)**:
   - Enqueues matched frames to software gateway queues (`g_gw_tx_low_q` / `g_gw_tx_high_q`) for single-slot hardware TX hand-off with up to 40 retries (400ms TTL).

```
                                     [ Frame Received ]
                                             |
                                             v
                           +-----------------------------------+
                           | Is Frame ID == 0x001 (ESTOP)?     |
                           +-----------------------------------+
                                       /               \
                                     YES                NO
                                     /                   \
                                    v                     v
                +-----------------------+     +-----------------------------------+
                | 1. DLC 0 Wire Normal- |     | Match Forwarding Rules Table:     |
                |    ization            |     | High -> Low: 0x111, 0x112, 0x302  |
                | 2. Enqueue Queue Head |     | Low -> High: 0x011, 0x120, 0x206, |
                |    (xQueueSendToFront)|     |              0x600                |
                | 3. Anti-Loop Gate:    |     +-----------------------------------+
                |    Forward ONLY across|
                |    buses (No Echo!)   |
                +-----------------------+
```

---

### 3. Steering Safety & Following-Error Decision Logic

#### Structured Text Explanation
1. **Loop Execution**: Executed by `t_control` on RT ESP32-S3 at 100 Hz ([safety_monitor.h:L123-L151](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L123-L151)).
2. **Threshold Calculation**:
   - Computes speed-dependent steering following-error threshold:
     $$\text{Threshold}_{\text{deg}} = \max\left(2.0, 0.25 \cdot \theta_{\text{clamp\_limit}}(v_{\text{kmh}})\right)$$
   - Converts to tenths of a degree (`0_1deg`): $\text{Threshold}_{0.1^\circ} = \text{Threshold}_{\text{deg}} \cdot 10$.
3. **Error Evaluation**:
   - Computes absolute difference between target angle in `0x169` (`VCU_SES_REQ`) and actual feedback angle in `0x201` (`SES_STATUS`): $\text{Error}_{0.1^\circ} = |\theta_{\text{cmd}} - \theta_{\text{actual}}|$.
   - **If Error > Threshold**: Increments persistence tick counter (`steer_follow_err_ticks++`).
   - **If Error <= Threshold**: Resets tick counter (`steer_follow_err_ticks = 0`).
4. **Safety Latch Trigger**:
   - When `steer_follow_err_ticks >= 30` (300 ms persistence threshold, `kSteerFollowingErrMs = 300`):
     - Aborts AUTO mode immediately.
     - Zeroes steering effort setpoints in `0x169` (`VCU_SES_REQ`) and disables autonomous steering output (`disable_steering = true`).
     - Forces ESTOP mode and broadcasts `0x001` (`SAFETY_ESTOP`) onto CAN.

```
                           +-----------------------------------+
                           |  Steer Following Error > Threshold |
                           +-----------------------------------+
                                       /               \
                                     YES                NO
                                     /                   \
                                    v                     v
                +-----------------------+     +-----------------------------------+
                | Increment Tick Counter|     | Reset Tick Counter (ticks = 0)    |
                | (ticks++)             |     +-----------------------------------+
                +-----------------------+
                            |
                            v
                +-----------------------+
                | Have 30 Ticks Elapsed |
                | (300 ms at 100 Hz)?   |
                +-----------------------+
                            /       \
                          YES        NO
                          /           \
                         v             v
             +--------------------+  +-------------------+
             | Abort AUTO Mode!   |  | Continue Monitoring|
             | Latch ESTOP & Zero |  +-------------------+
             | Steering Setpoints |
             +--------------------+
```

---

### 4. Stuck DMA Frozen Counter Verification Logic

#### Structured Text Explanation
1. **Loop Execution**: Executed upon receiving `0x7FD` (`RT_HEARTBEAT`) / `0x7FE` (`SYS_HEARTBEAT`) heartbeat frames in `t_dispatch` ([can_dispatch.h:L73-L98](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L73-L98)).
2. **Delta Rollover Math**:
   - Calculates 8-bit unsigned rollover delta:
     $$\text{delta} = \text{uint8\_t}(\text{new\_alive\_ctr} - \text{last\_alive\_ctr})$$
3. **Evaluation**:
   - **If delta != 0 (or first frame)**:
     - Heartbeat sequence counter is active. Updates `last_sys_hb_us` timestamp and feeds watchdog.
   - **If delta == 0**:
     - **Frozen Counter Detected**: Software main loop of the sending ECU is deadlocked while CAN DMA hardware continues emitting stale buffers.
     - Software **skips updating the timestamp**, forcing a 200 ms heartbeat timeout action.

```
                        [ Heartbeat Frame Arrives (0x7FD / 0x7FE) ]
                                            |
                                            v
                        +---------------------------------------+
                        | Calculate Unsigned 8-Bit Delta:       |
                        |   delta = new_alive_ctr - last_ctr    |
                        +---------------------------------------+
                                        /               \
                                   delta != 0        delta == 0
                                      /                   \
                                     v                     v
                 +-----------------------+     +-----------------------------------+
                 | Counter Active:       |     | Frozen Counter Detected!          |
                 |  - Update timestamp   |     |  - Skip timestamp update          |
                 |  - Feed watchdog      |     |  - Force 200 ms timeout           |
                 +-----------------------+     +-----------------------------------+
```

---

## 13. FreeRTOS Task Priority Architecture

To guarantee deterministic execution order and eliminate priority inversion across CAN operations:

### RT ESP32-S3 Task Architecture
- **Priority 5 (Highest)**: `rx_low` (Low-CAN TWAI RX), `rx_high` (High-CAN MCP2515 RX)
- **Priority 4**: `t_dispatch` (Gateway Router), `t_control` (Real-Time Kinematics & 100 Hz Safety Checks)
- **Priority 3**: `tx_low` (Low-CAN TWAI TX), `tx_high` (High-CAN MCP2515 TX)
- **Priority 1 (Lowest)**: `t_watchdog` (External Watchdog), `t_heartbeat` (10 Hz Dual Heartbeats)

### SYS ESP32-S3 Task Architecture (12 Tasks)
- **Priority 5 (Highest)**: `task_can_rx` (CAN RX Handler), `task_safety` (20 Hz Safety Supervisor)
- **Priority 4**: `task_dispatch` (Internal Dispatcher), `task_mode` (Mode State Machine)
- **Priority 3**: `task_gear` (50 Hz Gear Control), `task_brake` (50 Hz SEB Brake Arbitration), `task_lights` (Lighting Controller)
- **Priority 2**: `task_indicator` (Turn Signal Blink Loop), `task_power` (12V Relay Control), `task_can_tx` (CAN TX Handler), `task_can_control` (CAN Bus Manager)
- **Priority 1 (Lowest)**: `task_diag` (System Diagnostics), `task_hb` (10 Hz SYS Heartbeat)

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
  | - EGAS L2 setpoint vs speed feedback mismatch check (>500ms -> ESTOP)   |
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
