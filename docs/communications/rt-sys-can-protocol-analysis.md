# RT & SYS CAN Protocol Analysis

This document provides a comprehensive technical analysis of the CAN communication protocol between the **RT** (Real-Time Controller) and **SYS** (System Safety & Mode Authority) ECUs in the eTrike platform.

---

## 1. Architecture & Node Responsibilities

In the eTrike vehicle architecture, the **RT** and **SYS** nodes communicate over the **Low-Level CAN Bus** operating at **500 kbit/s**.

- **RT ESP32-S3**: Real-time vehicle dynamics controller, steering-by-wire supervisor, and dual-bus CAN gateway (bridging Jetson Orin on High-CAN with Low-CAN).
- **SYS ESP32-S3**: Master vehicle safety authority, mode state machine owner, body controller, and direct reader of physical rider inputs (ESTOP button, Mode button, Start button, and Brake lever).

---

## 2. Protocol Message Exchange Matrix

The protocol follows the principle: **One CAN ID = One Sender per Bus** to eliminate arbitration conflicts.

| CAN ID | Message Name | Direction | Rate | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x001` | [`SAFETY_ESTOP`](file:///e:/work/etrike/can-dictionary.md#L41) | RT $\leftrightarrow$ SYS | Event | DLC 0 frame. Global Emergency Stop notification broadcast by either node upon hardware or software fault. Latching state. |
| `0x011` | [`SYS_SAFETY_STS`](file:///e:/work/etrike/can-dictionary.md#L52) | SYS $\rightarrow$ RT | 10 Hz | System safety state, hardware ESTOP flag, SYS heartbeat status, and light states. RT forwards this to High CAN for Jetson telemetry. |
| `0x110` | [`SYS_MODE_CMD`](file:///e:/work/etrike/can-dictionary.md#L89) | SYS $\rightarrow$ RT | 10 Hz / Event | Master system operational mode broadcast (`0` = MANUAL, `1` = AUTO). Mode authority resides exclusively in SYS. |
| `0x204` | [`RT_DRIVE_CMD`](file:///e:/work/etrike/can-dictionary.md#L119) | RT $\rightarrow$ SYS | 50 Hz | Computed motor speed setpoint (mm/s) and target gear (`N/D/S/R`). SYS monitors this for EGAS Level 2 safety checks against actual motor speed (`0x206`). |
| `0x205` | [`RT_BRAKE_CMD`](file:///e:/work/etrike/can-dictionary.md#L141) | RT $\rightarrow$ SYS | 50 Hz | RT target brake pressure setpoint (kPa), output from RT brake arbitration (selecting `max(obstacle_kpa, host_kpa)`). |
| `0x210` | [`RT_STATE_RPT`](file:///e:/work/etrike/can-dictionary.md#L659) | RT $\rightarrow$ SYS | 10 Hz | RT operational status, safety state (`0`=Normal, `1`=Degraded, `2`=Estop), active `estop_reason`, and CAN buffer overflow counts. |
| `0x7B9` | [`VCU_SEB_REQ`](file:///e:/work/etrike/can-dictionary.md#L154) | RT / SYS $\rightarrow$ SEB | 50 Hz | Direct brake actuator command frame. Gated by Option D dual-control authority (RT in AUTO mode when healthy; SYS in MANUAL/ESTOP or takeover). |
| `0x7FD` | [`RT_HEARTBEAT`](file:///e:/work/etrike/can-dictionary.md#L590) | RT $\rightarrow$ SYS | 10 Hz | Low-level RT liveness frame carrying an 8-bit incrementing `alive_ctr` and system health status flags. |
| `0x7FE` | [`SYS_HEARTBEAT`](file:///e:/work/etrike/can-dictionary.md#L609) | SYS $\rightarrow$ RT | 10 Hz | Low-level SYS liveness frame carrying an 8-bit incrementing `alive_ctr` and system health status flags. |

---

## 3. Operational Handshakes & State Machine Synchronization

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

## 4. Actuator Dual-Control & Arbitration (Option D)

To eliminate single point of failure (SPOF) while achieving low-latency control:

1. **Brake Actuator Control (`0x7B9`)**:
   - **In AUTO Mode**: When RT is healthy (`rt_alive == true`, `rt_safety_state == Normal`, and setpoints are fresh $<200\text{ ms}$), RT transmits `0x7B9` directly to the SEB brake unit (1-hop from kinematics). SYS suppresses its own `0x7B9` frame ([sys-esp32/src/main.cpp](file:///e:/work/etrike/sys-esp32/src/main.cpp#L626-L656)).
   - **In MANUAL / ESTOP Mode**: SYS takes direct control of `0x7B9`, converting physical rider lever inputs or ESTOP brake curves directly to SEB commands.
   - **Rider Lever Override**: If the rider pulls the physical brake lever while in AUTO mode, SYS immediately overrides RT, takes over `0x7B9`, and commands maximum requested rider braking pressure.

2. **Steering Control (`0x169`)**:
   - RT exclusively commands the EPS-C steering unit via `0x169` (`VCU_SES_REQ`). In `MANUAL` or `ESTOP` modes, RT zeroes steering effort setpoints and disables autonomous steering output.

---

## 5. Heartbeats, Watchdogs, & Liveness Verification

Both RT and SYS run dual-layered heartbeat verification at 10 Hz:

### Heartbeat Packet Structure
- **Byte 0**: `alive_ctr` — uint8 wrapping counter ($0 \rightarrow 255 \rightarrow 0$), incremented on every tick.
- **Byte 1**: `health_flags` — bitfield (`bit0` = `heartbeat_ok`, `bit1` = `estop_active`, `bit2` = `mode_auto`, `bit3` = `can_ok`).

### Stuck/Frozen Counter Guard
Both ECUs validate counter progression rather than just packet arrival.
- In [rt-esp32/src/can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L73-L86) and [sys-esp32/src/safety_monitor.cpp](file:///e:/work/etrike/sys-esp32/src/safety_monitor.cpp#L28-L36), if `alive_ctr == last_alive_ctr`, the frame is flagged as **frozen** (indicating a hung main loop with hardware CAN DMA active). The timestamp update is skipped, triggering a timeout.

### Timeout Deadlines & Fail-Safe Actions

| Failure Event | Detection Logic | Fail-Safe Triggered |
| :--- | :--- | :--- |
| **SYS Heartbeat Loss on RT** | $>200\text{ ms}$ without valid `0x7FE` increment ([safety_monitor.h](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L95-L111)) | RT initiates **SEB Brake Takeover** (`seb_takeover = true`). RT begins spammed transmission of `0x7B9` with maximum brake stroke/pressure to bring vehicle to a hard stop. |
| **RT Heartbeat Loss on SYS** | $>200\text{ ms}$ without valid `0x7FD` increment ([safety_monitor.cpp](file:///e:/work/etrike/sys-esp32/src/safety_monitor.cpp#L38-L47)) | SYS forces **ESTOP mode**, broadcasts CAN `0x001`, and takes direct control of `0x7B9` brake output with maximum safety pressure. |
| **Stale RT Setpoint Deadman** | $>200\text{ ms}$ since last `0x204` setpoint on SYS ([main.cpp](file:///e:/work/etrike/sys-esp32/src/main.cpp#L647-L650)) | Fast-path takeover: SYS immediately revokes RT `0x7B9` authority without waiting for the full heartbeat timeout window. |

---

## 6. Failure Modes, Fault Recovery, & Self-Healing

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

1. **CAN Bus-Off & Transport Recovery**:
   - **Low-CAN TWAI Driver (RT & SYS)**: Monitored at 10 Hz ([can_health.h](file:///e:/work/etrike/rt-esp32/src/can_health.h#L7-L73)). If Transmit Error Counter (TEC) $\ge 255$, TWAI enters Bus-Off. TX admission is revoked, queues reset, and hardware recovery is initiated. **5 consecutive Bus-Off events latch a global ESTOP**.
   - **High-CAN MCP2515 SPI Driver (RT)**: Monitored via ISR + 10 Hz polling loop. Triggers active controller reset via SPI.

2. **ESTOP Bus Flood Prevention (Rate Limiting)**:
   To prevent a corrupted node from collapsing the CAN bus by spamming `0x001` frames, [can_send_estop()](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L53-L55) enforces a maximum of **2 ESTOP frames per 500 ms window**.

3. **EGAS Level 2 Safety Cross-Checks**:
   On SYS, [task_safety](file:///e:/work/etrike/sys-esp32/src/main.cpp#L483-L485) compares RT's commanded speed in `0x204` against actual motor feedback in `0x206`. If a setpoint vs feedback mismatch exceeds safety thresholds for $>200\text{ ms}$, SYS triggers ESTOP.

4. **Steering Following-Error Check**:
   RT continuously monitors actual EPS-C steering feedback angle vs commanded target ([safety_monitor.h](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h#L123-L151)). If the error exceeds the dynamic speed-dependent threshold for $>100\text{ ms}$, RT aborts autonomous mode and latches ESTOP.

5. **Actuator L3 Fault Escalation**:
   Severe actuator internal faults (L3 faults from SEB `0x721` or EPS-C `0x202`) bypass local retries and escalate directly to CAN `0x001` ESTOP broadcasts ([can_dispatch.h](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h#L165-L178)).

6. **Crash Diagnostics & NVS Persistence**:
   SYS records crash reset reasons, crash loop counts, and panic metrics into Non-Volatile Storage (NVS) flash across unexpected reboots for post-mortem diagnostics.
