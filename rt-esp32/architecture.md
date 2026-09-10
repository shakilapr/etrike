# RT ESP32-S3 Firmware Architecture

> **Firmware Version:** `v0.8.0-alpha-vehicle` / `v0.8.0-alpha-bench`  
> **Target Hardware:** ESP32-S3 DevKitC-1-N16R8 (16 MB Quad Flash, 8 MB Octal PSRAM)  
> **Framework:** ESP-IDF 5.x via PlatformIO | FreeRTOS (1000 Hz tick)  
> **CAN Interfaces:**  
>   - **Low CAN (500 kbit/s):** On-chip TWAI controller (CTX: GPIO5, CRX: GPIO4)  
>   - **High CAN (500 kbit/s):** External MCP2515 via SPI (SCK: GPIO15, MOSI: GPIO16, MISO: GPIO17, CS: GPIO18, INT: GPIO47)

---

## 1. System Role & Authority Boundaries

`rt-esp32` is the **Dual-Bus CAN Gateway and Autonomous Motion Master** of the vehicle:

```
                    ┌──────────────────────────────────────────────┐
                    │            Jetson Orin (Host)                │
                    │        Perception, Path Planner, HMI         │
                    └──────────────────────┬───────────────────────┘
                                           │ High CAN (500 kbit/s)
                                           ▼
                    ┌──────────────────────────────────────────────┐
                    │                   rt-esp32                   │
                    │  Kinematics Resolver, Gateway, Safety Master │
                    └──────────────────────┬───────────────────────┘
                                           │ Low CAN (500 kbit/s)
         ┌──────────────────┬──────────────┴──────────────┬──────────────────┐
         ▼                  ▼                             ▼                  ▼
  ┌──────────────┐   ┌──────────────┐              ┌──────────────┐   ┌──────────────┐
  │  mtr-stm32   │   │  sys-esp32   │              │  SES-EPS-C   │   │  SEB Brake   │
  │ Motor Driver │   │ Safety/Body  │              │ Steering Act │   │ Smart Actuat │
  └──────────────┘   └──────────────┘              └──────────────┘   └──────────────┘
```

### Core Invariants & Operating Topology
1. **Silent in MANUAL Mode:** RT never emits actuator commands (`0x204` motor speed, `0x169` steering angle, `0x205` brake kPa) while vehicle mode is `MANUAL`. In `MANUAL`, rider controls (throttle grip, brake lever, handlebars) are handled directly by `sys-esp32` and manual EPS.
2. **Autonomous Master in AUTO Mode:** In `AUTO`, RT converts Host high-level trajectory commands (`0x300 HOST_DRIVE_CMD` speed + yaw rate, or `0x303 HOST_STEER_CMD` direct angle) into actuator commands on Low CAN:
   - `0x204 RT_DRIVE_CMD` (100 Hz) to `mtr-stm32` (motor speed in mm/s and gear).
   - `0x169 VCU_SES_REQ` (50 Hz) to SES EPS-C (steer angle in 0.1° units with dynamic slew rate).
   - `0x205 RT_BRAKE_CMD` (50 Hz) to `sys-esp32` (arbitrated brake pressure in kPa).
3. **Emergency Fallback SEB Writer:** SYS is the sole regular writer of the final `0x7B9 VCU_SEB_REQ` brake command. RT transmits `0x7B9` **only** if SYS's `0x7B9` stream completely disappears from the Low bus (`EMERGENCY_FALLBACK` state in [`brake_fallback.h`](src/brake_fallback.h)).
4. **Transparent High ↔ Low Gateway:** Forwards select commands between Host and SYS (e.g. `0x111 HMI_MODE_REQ`, `0x112 HMI_PWR_REQ`, `0x114 HOST_ESTOP_RESET_REQ`, `0x302 HOST_LIGHT_CMD`).
5. **No Drive Authority at Boot:** RT powers on with `g_no_sys_authority = true`. Motion is prohibited until SYS has established a fresh, valid stream of `0x011 SYS_SAFETY_STS` and `0x110 SYS_MODE_CMD`.

---

## 2. Hardware Interfaces & Pinout

| Function | Pin / Resource | Configuration / Level | Details |
|---|---:|---|---|
| **Low CAN TX** | GPIO 5 | Output / TWAI CTX | On-chip TWAI transceivers @ 500 kbit/s |
| **Low CAN RX** | GPIO 4 | Input / TWAI CRX | (Swap option: `ETRIKE_RT_TWAI_SWAP_TX_RX`) |
| **MCP2515 SPI SCK** | GPIO 15 | Output | 8 MHz SPI clock (`SPI2_HOST`) |
| **MCP2515 SPI MOSI** | GPIO 16 | Output | SPI Master Out Slave In |
| **MCP2515 SPI MISO** | GPIO 17 | Input | SPI Master In Slave Out |
| **MCP2515 SPI CS** | GPIO 18 | Output | Active-LOW Chip Select |
| **MCP2515 INT** | GPIO 47 | Input / Pull-Up | Falling edge interrupt for CAN RX (`mcp_int_isr`) |
| **Developer Override** | GPIO 42 | Input / Pull-Up | Jumper to GND activates bench solo bypass mode |
| **Rear Motor Encoders** | GPIO 1, 2 | Input / PCNT | Quadrature PCNT channel 0 (compiled out by default) |
| **Front Wheel Encoders**| GPIO 10, 11| Input / PCNT | Quadrature PCNT channel 1 (compiled out by default) |

---

## 3. FreeRTOS Task Architecture

RT allocates **8 concurrent FreeRTOS tasks** initialized in [`main.cpp`](src/main.cpp):

```
                   [Low TWAI]                      [High MCP2515]
                       │                                 │
                       ▼                                 ▼
                 ┌───────────┐                     ┌───────────┐
                 │  rx_low   │ (Prio 5)            │  rx_high  │ (Prio 5)
                 └─────┬─────┘                     └─────┬─────┘
                       │ g_can_rx_low_q                  │ g_can_rx_high_q
                       ▼                                 ▼
                 ┌─────────────────────────────────────────────┐
                 │                  dispatch                   │ (Prio 4)
                 └──────┬──────────────┬───────────────┬───────┘
                        │              │               │
        g_safety_evt_q  │              │ g_cmd_q       │ g_gw_tx_*_q
                        ▼              ▼               ▼
                 ┌──────────────┐  ┌───────┐     ┌───────────┐
                 │   control    │  │       │     │  tx_low   │ (Prio 3, 5ms/100Hz)
                 │  (100 Hz)    │  │       │     │  tx_high  │ (Prio 3, 10ms/100Hz)
                 └──────┬───────┘  │       │     └───────────┘
                        │          │       │
          g_setpoint_q  ▼          │       │
                 ┌──────────────┐  │       │     ┌───────────┐
                 │  t_watchdog  │◄─┘       │     │    hb     │ (Prio 1, 2Hz)
                 │   (10 Hz)    │          └────►│  (Dual)   │
                 └──────────────┘                └───────────┘
```

### Task Specifications

| Task Name | Priority | Stack | Period / Execution | Core Responsibilities |
|---|---:|---:|---|---|
| **`rx_low`** | 5 | 4096 B | Event (TWAI queue) | Receives frames from on-chip TWAI, posts to `g_can_rx_low_q`. ESTOP `0x001` posts to front. |
| **`rx_high`** | 5 | 4096 B | Event (MCP2515 INT) | Reads frames from MCP2515 via SPI, posts to `g_can_rx_high_q`. Tracks RX overflow telemetry. |
| **`dispatch`** | 4 | 4096 B | Event (Queue receive) | Drains RX queues, runs [`route_frame()`](src/can_rx_router.h), updates atomics, posts to safety and gateway queues. |
| **`control`** | 4 | 4096 B | 10 ms (100 Hz periodic) | Drains safety events, resolves kinematics, runs obstacle limit & safety checks, updates setpoints. |
| **`tx_low`** | 3 | 3072 B | 5 ms (sub-loops: 100/50 Hz) | Transmits `0x204` (100 Hz), `0x205` (50 Hz), `0x169` (50 Hz), `0x501` (50 Hz), pumps gateway queue. |
| **`tx_high`** | 3 | 3072 B | 10 ms (outer: 100 ms / 10 Hz) | Transmits `0x121` (100 Hz), `0x210` (10 Hz), `0x501` (10 Hz), `0x310`/`0x311` (10 Hz), `0x620` (1 Hz). |
| **`watchdog`**| 1 | 4096 B | 100 ms (10 Hz periodic) | Checks per-task alive counters (`check_task_watchdog`), evaluates Host command staleness. |
| **`hb`** | 1 | 3072 B | 500 ms (2 Hz periodic) | Emits independent `0x7FD RT_HEARTBEAT` frames on both Low and High buses. |

---

## 4. CAN Interface & Routing Matrix

### 4.1. Frames Consumed or Emitted by RT

| CAN ID | Name | Bus | Dir | Nominal Rate | Content / Action in RT |
|---|---|---|---|---|---|
| `0x001` | `SAFETY_ESTOP` | Both | RX+TX | Event | Emergency stop trip/latch. Cross-forwarded bidirectionally. |
| `0x011` | `SYS_SAFETY_STS` | Low | RX | 5 Hz | Authoritative safety status from SYS. CRC + counter validated. |
| `0x110` | `SYS_MODE_CMD` | Low | RX | 10 Hz | Authoritative operating mode (`MANUAL`=0, `AUTO`=1). |
| `0x121` | `RT_MOTION_RPT` | High | TX | 100 Hz | Coherent motion report: speed, gear, yaw rate, validity flags. |
| `0x169` | `VCU_SES_REQ` | Low | TX | 50 Hz | Steer-by-wire target angle (0.1°), dynamic slew rate, counter. |
| `0x201` | `SES_STATUS` | Low | RX | 100 Hz | Measured steering angle, center alignment status, error status. |
| `0x202` | `SES_ERR_INFO` | Low | RX | 10 Hz | EPS-C Level 3 fault bits. Active L3 trips immediate ESTOP. |
| `0x204` | `RT_DRIVE_CMD` | Low | TX | 100 Hz | Commanded speed (mm/s) and gear to `mtr-stm32`. Forced {0, N} in MANUAL. |
| `0x205` | `RT_BRAKE_CMD` | Low | TX | 50 Hz | Arbitrated brake pressure (kPa) to `sys-esp32`. Suppressed in MANUAL. |
| `0x206` | `MTR_MOTOR_FBK` | Low | RX | 50 Hz | Echoed motor command speed and gear state. Supervised in AUTO. |
| `0x210` | `RT_STATE_RPT` | Both | TX | 10 Hz | Mode, safety state, reversing flag, task health bitmask. |
| `0x220` | `RT_PID_RPT` | High | TX | 10 Hz | Shadow PID telemetry (setpoint, measured, correction). |
| `0x300` | `HOST_DRIVE_CMD` | High | RX | 100 Hz | Target speed (mm/s) and yaw rate (mrad/s) from Jetson. |
| `0x303` | `HOST_STEER_CMD` | High | RX | 100 Hz | Direct steering angle override (0.1°). Bypasses bicycle model. |
| `0x304` | `HOST_OBSTACLE_DIST` | High | RX | 10 Hz | Closest obstacle distance (mm) from perception. |
| `0x310` | `STEER_DIAG` | High | TX | 10 Hz | Rescaled EPS-C telemetry (angle, current, temperature, fault). |
| `0x311` | `BRAKE_DIAG` | High | TX | 10 Hz | Rescaled SEB telemetry (pressure, current, temperature, fault). |
| `0x501` | `RT_NODE_STATUS` | Both | TX | 50Hz (Low) / 10Hz (Hi) | Node readiness, blocker bitmask, rolling counter, CRC-8. |
| `0x620` | `RT_DIAG_RPT` | High | TX | 1 Hz | MCP2515 SPI transaction health, error flags, supervision state. |
| `0x621` | `RT_DIAG_EVENT_RPT` | High | TX | Event | Phase B diagnostic event reporter (latched fault replay). |
| `0x721` | `SEB_STATUS` | Low | RX | 100 Hz | Hydraulic brake pressure, stroke, error status. |
| `0x7B9` | `VCU_SEB_REQ` | Low | TX/RX | 50 Hz | SEB brake command. Normally observed from SYS; emitted ONLY in emergency fallback. |
| `0x7FC` | `HOST_HEARTBEAT` | High | RX | 2 Hz | Jetson alive counter. Timeout (1500 ms) triggers assisted stop. |
| `0x7FD` | `RT_HEARTBEAT` | Both | TX | 2 Hz | Independent alive counters and health bitmask on both buses. |
| `0x7FE` | `SYS_HEARTBEAT` | Low | RX | 10 Hz | SYS alive counter. Timeout (200 ms) inhibits motion. |

### 4.2. Gateway Forwarding Matrix

| CAN ID | Frame Name | Source | Destination | Forwarding Rule |
|---|---|---|---|---|
| `0x001` | `SAFETY_ESTOP` | Low ↔ High | Opposite Bus | Bidirectional. Standard frame, DLC 0. Priority enqueued. |
| `0x011` | `SYS_SAFETY_STS` | Low | High | Transparent copy to Host. |
| `0x111` | `HMI_MODE_REQ` | High | Low | Forwarded to SYS. Subject to software retry pump (`gw_pump`). |
| `0x112` | `HMI_PWR_REQ` | High | Low | Forwarded to SYS. |
| `0x114` | `HOST_ESTOP_RESET_REQ` | High | Low | Forwarded to SYS. |
| `0x115` | `SYS_ESTOP_RESET_RSP` | Low | High | Forwarded to Host. |
| `0x120` | `SYS_THROTTLE_STS` | Low | High | Forwarded to Host. |
| `0x206` | `MTR_MOTOR_FBK` | Low | High | Forwarded to Host. |
| `0x302` | `HOST_LIGHT_CMD` | High | Low | Forwarded to SYS. |
| `0x600` | `SYS_DIAG_RPT` | Low | High | Forwarded to Host. |

---

## 5. Safety Architecture & Supervisors

RT implements **layered, decoupled supervisors** evaluating vehicle state every 10 ms inside `t_control`:

```
               ┌────────────────────────────────────────────────┐
               │        100 Hz Safety Evaluation Cycle          │
               └──────────────────────┬─────────────────────────┘
                                      │
        ┌─────────────────────────────┼────────────────────────────┐
        ▼                             ▼                            ▼
┌──────────────────┐        ┌──────────────────┐         ┌──────────────────┐
│  Safety Stream   │        │   MTR Health     │         │  Brake Fallback  │
│    Authority     │        │   Supervisor     │         │   State Machine  │
│ (SYS 0x011/0x110)│        │   (0x206 fbk)    │         │  (SYS vs RT 7B9) │
└──────────────────┘        └──────────────────┘         └──────────────────┘
```

### 5.1. SYS 0x011 Safety Authority & Asymmetric Clear
* **Acquisition State Machine ([`safety_stream_loss.h`](src/safety_stream_loss.h)):**
  `UNACQUIRED` → `ACQUIRED` → `LOST`. Boot grants zero motion authority. RT must receive consecutive fresh `0x011` frames with valid CRC-8 before authority is confirmed. If stream is lost after acquisition, ESTOP latches.
* **Asymmetric Clear Sequence ([`can_dispatch.h:L202-L228`](src/can_dispatch.h#L202-L228)):**
  Entering ESTOP takes **1 frame**. Releasing an ESTOP latch requires **TWO consecutive fresh frames** with `estop_active == 0` whose rolling counters advance by exactly `+1` (`counter == last + 1`). Any gap or jump restarts the confirmation counter.

### 5.2. MTR Actuator Health Supervisor
* Implemented in [`safety_monitor.h:L50-L109`](src/safety_monitor.h#L50-L109).
* In `AUTO`, RT allows an acquisition grace (300 ms) after mode entry. Thereafter, if `0x206 MTR_MOTOR_FBK` is missing for `> 200 ms`, MTR is declared unavailable and propulsion is prohibited (`zero_setpoints = true`) **even at standstill**.
* Max brake (5000 kPa) is escalated only if a non-zero motion command was commanded within the last 500 ms (`g_last_nonzero_cmd_us`).
* Recovery requires **3 consecutive fresh 0x206 frames** at the control cadence.

### 5.3. SEB Brake Ownership & Emergency Fallback
* Implemented in [`brake_fallback.h`](src/brake_fallback.h).
* Three states:
  1. `NORMAL`: SYS heartbeat (`0x7FE`) and SYS brake command (`0x7B9`) are healthy. RT does not transmit `0x7B9`.
  2. `SYS_DEGRADED`: SYS heartbeat is lost (motion prohibited), but SYS `0x7B9` frames are still observed on Low CAN. RT does NOT transmit `0x7B9`.
  3. `EMERGENCY_FALLBACK`: SYS `0x7B9` command has physically disappeared from the bus for `> 250 ms`. RT asserts `0x001` ESTOP and assumes emergency ownership of `0x7B9` (commanding 5000 kPa).
* Handback is epoch-guarded (`handback_epoch_us_`): RT silences its own `0x7B9` and requires 5 consecutive fresh `0x7B9` frames from SYS before returning to `NORMAL`.

### 5.4. Steering State Machine (`SteeringControl`)
* Implemented in [`steering_control.h`](src/steering_control.h).
* States:
  - `STEER_BOOT_WAIT`: 500 ms silent wait after power-on.
  - `STEER_LISTEN_SYNC`: Awaits valid `0x201 SES_STATUS` with `angle_aligned == 1` and angle plausibility check (`< 30°` offset). Timeout (5000 ms) → `STEER_FAULT`.
  - `STEER_ACTIVE`: Normal operation. Emits `0x169` at 50 Hz. Slew rate scales dynamically between 125°/s (at 2 km/h) and 525°/s (at ≥25 km/h).
  - `ESTOP_RAMP_TO_ZERO`: Non-obstacle ESTOP. Ramps steering angle to center (0°) at 20°/s. Checks for linkage jam (following error `> 5°` for 1000 ms → `STEER_FAULT`).
  - `ESTOP_HOLD_THEN_SILENT`: Obstacle ESTOP. Clamps hold angle to dynamic rollover limit, holds for 500 ms, then enters `STEER_FAULT` (stops transmitting).
  - `STEER_FAULT`: Transmission ceases; actuator reverts to mechanical/standalone damping.

---

## 6. Motion Kinematics & Resolution Pipeline

1. **Resolution Selection ([`resolver_config.h`](src/resolver_config.h)):**
   - **PhysicsModel (`ETRIKE_RT_KINEMATICS_RESOLVER=0`):** Inverse bicycle model. Calculates `steer = atan((L * w) / v)` for `|v| ≥ 50 mm/s`. Below 50 mm/s, locks steering into yaw direction without forward speed, or decays toward zero (`factor = 0.8`).
   - **DirectResolver (`ETRIKE_RT_KINEMATICS_RESOLVER=1`):** Linear passthrough (`15 mdeg` per `mrad/s`).
2. **Direct Steering Override ([`phase2_motion.h`](src/phase2_motion.h)):**
   If `0x303 HOST_STEER_CMD` is received and valid within 100 ms, it overrides the resolver output directly.
3. **Dynamic Angle Clamping:**
   Calculates maximum safe steering angle based on speed:
   $$\text{limit\_deg} = 40.0 - (\text{speed\_kmh} - 2.0) \times \frac{35.0}{23.0} \quad \in [5.0^\circ, 40.0^\circ]$$
4. **Obstacle Speed Limiter & Brake Curve:**
   Linear interpolation between `shared::kObstacleStopMM` (300 mm) and `shared::kObstacleClearMM` (3000 mm). Obstacle brake request is max-selected against Host brake request.

---

## 7. Driver Implementations & Transport Mechanics

### 7.1. TWAI Driver (Low Bus)
* **API:** ESP-IDF handle-based TWAI driver (`twai_new_node_onchip`).
* **Configuration:** Single-shot mode (`fail_retry_cnt = 0`) with 1 TX slot (`kTxSlots = 1`).
* **Slot Leak Workaround:** An in-flight slot reclamation timer (`kTxReclaimUs = 5000 µs`) force-reclaims the slot if `on_tx_done` does not fire due to arbitration loss.
* **Gateway Retry Pump:** Frames that lose arbitration are held in FreeRTOS queue `g_gw_tx_low_q` and retried in software up to 40 times (`kGwMaxAttempts = 40`) by `gw_pump()`.

### 7.2. MCP2515 Driver (High Bus)
* **API:** Custom ESP-IDF SPI driver on `SPI2_HOST` (8 MHz, Mode 0).
* **Interrupt Infrastructure:** GPIO 47 falling-edge ISR notifies `rx_high` via `vTaskNotifyGiveFromISR`.
* **SPI Serialization:** All SPI transactions across 4 tasks (`rx_high`, `control`, `tx_high`, `hb`) are serialized via FreeRTOS mutex `g_spi_mutex`.
* **Hardware TX Buffer Allocation:**
  - TXB2: Priority allocation for `0x001 SAFETY_ESTOP`.
  - TXB0: General outgoing traffic.

---

## 8. Runtime Bypass Modes (Development & Bench)

Defined in [`system_mode.h`](src/system_mode.h) and evaluated at boot via `DEVELOPER_OVERRIDE_PIN` (GPIO 42):

| Run Mode | Jumper Pin 42 | Flag `g_bench_solo_mode` | Behavior |
|---|---|---|---|
| `0` (Production) | Ignored | `false` | Full safety checks enforced. SYS, Host, and Actuator timeouts active. |
| `1` (Prototype) | Open (HIGH) | `false` | Safety enforced. |
| `1` (Prototype) | Jumped to GND | `true` | Developer override active. Safety sync checks bypassed. |
| `2` (Bench) | Ignored | `true` | Solo bench mode. Bypasses SYS heartbeat timeout, EPS sync, and SEB sync. |
