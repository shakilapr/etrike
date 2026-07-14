# Big-ESP32 Unified Architecture

This document defines the complete architecture for the unified `big-esp32` controller, which replaces the legacy dual-board (`sys-esp32` and `rt-esp32`) topology on the E-Trike. By consolidating real-time kinematics and system body/safety controls onto a single ESP32-S3 ECU, we eliminate inter-MCU communication delays, simplify state management, and enforce a localized CAN protocol model.

---

## 1. Top-Level Responsibilities

The `big-esp32` is the central vehicle control unit, acting as the sole interface between the High CAN bus (Jetson ORIN) and the Low CAN bus (Actuators). Its primary domains are:

1. **CAN Gateway & Protocol Enforcement:** Routes telemetry and commands between Jetson and Actuators, translating and enforcing protocol integrity.
2. **Vehicle Dynamics & Kinematics:** Executes the real-time physics model, calculates speed PID targets, and interpolates steering angles.
3. **Safety & Mode Supervision:** Maintains the global system state (ESTOP / MANUAL / AUTO), monitoring hardware limits, heartbeat timeouts, and EGAS fault conditions.
4. **Body Control:** Drives relays and bulbs for turn signals, headlights, brake lights, and reads physical switches (mode button, ESTOP mushroom button, ignition).

---

## 2. Protocol Ownership and YAML Rules

`big-esp32` departs from the legacy "monolithic shared folder" design. Instead, the controller strictly owns its local protocol definition to guarantee deterministic generation and complete test decoupling.

### Divide Contracts Without Duplicating Messages
Instead of monolithic `can_high.yaml` and `can_low.yaml` files, the CAN protocol definitions inside `big-esp32/protocol/contracts/` are divided logically by originating ECU and protocol family:

- `network.yaml`: Contains buses, nodes, bitrate, and forwarding references.
- `host.yaml`: Jetson-originated message definitions.
- `big-esp32.yaml`: Messages originated by this unified controller (merging the legacy RT and SYS definitions).
- `mtr.yaml`: Motor controller message definitions.
- `ses.yaml`: Externally owned steering protocol.
- `seb.yaml`: Externally owned braking protocol.
- `pwt.yaml`: PWT/DC-DC manufacturer protocol.

- **Single Source of Truth:** Each message layout is defined exactly once in its respective file. A receiver never duplicates the YAML layout. Forwarded routes are represented by reference.
- **Bus Instances:** The network topology is defined by explicit bus instances (`bus` + `CAN ID`). Runtime identity is always resolved by the physical bus instance.

### CAN Codec Strategies (Payload Integrity)
To decouple stateless payload parsing from stateful wire supervision, every CAN message in the YAML strictly selects exactly **one** codec strategy:

1. **`generated`**: An ordinary, stateless C++ payload codec is generated deterministically from the layout.
2. **`profile`**: A small named and versioned integrity implementation is applied (e.g., a repeated XOR checksum, or an AUTOSAR E2E profile). The `profile` method encapsulates sequence counters, freshness checks, and checksum validation, evaluating integrity *before* the payload is parsed or utilized.
3. **`custom`**: An explicit handwritten codec owns the algorithm. This is reserved solely for vendor protocols (like legacy EPS-C or SEB) where manufacturer-specific overlapping bits or undocumented state machines cannot be mapped via generic generation or standard profiles.

---

## 3. Hardware Architecture & Complete GPIO Unification

Merging SYS and RT into a single ESP32-S3 requires resolving overlapping pin assignments from the legacy `config.h` files. Below is the complete, unified, conflict-free pinout for `big-esp32`:

### CAN Interfaces (No conflicts)
- **Low CAN (Native TWAI):** `kCanLowTxGpio` = 5, `kCanLowRxGpio` = 4
- **High CAN (SPI MCP2515):** `kSpiSckGpio` = 15, `kSpiMosiGpio` = 16, `kSpiMisoGpio` = 17, `kSpiCsGpio` = 18, `kMcpIntGpio` = 7

### Safety & System Inputs (Kept on legacy SYS pins)
- `kEstopGpio` = 1 (Active-low, physical mushroom)
- `kBrakeLeverGpio` = 2 (Active-low)
- `kIgnitionGpio` = 8
- `kModeBtnGpio` = 11
- `kStartBtnGpio` = 41

### Encoders (Quadrature PCNT) (Moved to resolve conflicts)
*Legacy RT pins (1, 2, 6, 9, 10, 12, 13, 14) conflicted with SYS. Moved to higher unused GPIOs.*
- `kEncRearMotorA` = 35, `kEncRearMotorB` = 36
- `kEncFrontWheelA` = 37, `kEncFrontWheelB` = 38
- `kEncRearLeftA` = 42, `kEncRearLeftB` = 43
- `kEncRearRightA` = 44, `kEncRearRightB` = 47

### Body Control & Lighting (Resolved minor conflicts)
- `kSwitchRightTurn` = 6
- `kSwitchLeftTurn` = 9
- `kLightHead` = 10
- `kSwitchHeadlight` = 12 *(Moved from 7 to avoid MCP INT)*
- `kLightLeftTurn` = 14 *(Moved from 18 to avoid SPI CS)*
- `kLightRightTurn` = 19
- `kLightBrake` = 21

### Mode Indicator Bulbs & Relays (Resolved minor conflicts)
- `kBulbReady` = 13 *(Moved from 17 to avoid SPI MISO)*
- `kBulbEstop` = 20
- `kBulbManual` = 39
- `kPower12vRelay` = 40
- `kBulbAuto` = 48

---

## 4. State Machine & Mode Control

The `big-esp32` operates an internal, synchronous state machine determining the vehicle's capability to actuate motors and steering.

```
[MANUAL] <────────(Mode Button)────────> [AUTO]
   |                                        |
   v                                        v
[ ESTOP ] <──────(Faults, Button, CAN)──────┘
```

- **MANUAL Mode:** Operator steers manually. Brake lever directly commands the SEB (brake-by-wire). Jetson drive commands (`HOST_DRIVE_CMD`) are ignored.
- **AUTO Mode:** The Jetson commands steering (`0x300`), speed, and braking. Actuation is managed by the `control` task.
- **ESTOP State:** A hardware-enforced overlay. Triggers include the physical ESTOP button, Jetson heartbeat timeout, EGAS tracking faults, or Low CAN timeouts. When active, `big-esp32` immediately sets speed target to 0, ramps steering to center, and commands maximum braking. Exit requires holding the `kStartBtnGpio`.

---

## 5. RTOS Task Schedule

Tasks are explicitly pinned to CPU cores and prioritized to ensure real-time determinism.

| Task Name        | Core | Priority | Freq | Description |
|------------------|------|----------|------|-------------|
| `can_rx_high`    | 1    | 5 (Highest)| ISR  | Polls MCP2515 INT pin, drops High CAN frames into `rx_queue`. |
| `can_rx_low`     | 1    | 5        | ISR  | TWAI driver event loop, drops Low CAN frames into `rx_queue`. |
| `can_dispatch`   | 1    | 4        | Asyc | Pops `rx_queue`, applies `profile` integrity checks, updates memory structs. |
| `control_loop`   | 1    | 4        | 100Hz| Reads unified state, executes PID & Steering interpolation, pushes to `tx_queue`. |
| `can_tx_high`    | 1    | 3        | Asyc | Flushes outgoing High CAN frames to MCP2515. |
| `can_tx_low`     | 1    | 3        | Asyc | Flushes outgoing Low CAN frames to TWAI. |
| `body_lights`    | 0    | 2        | 50Hz | Reads switches, drives relays and indicator bulbs. |
| `body_mode`      | 0    | 2        | 50Hz | Debounces mode button, manages mode transitions. |
| `safety_monitor` | 0    | 1        | 20Hz | Cross-checks Jetson heartbeat, EGAS faults, controls ESTOP event flag. |

---

## 6. Diagnostic and Telemetry Strategy

`big-esp32` natively multiplexes telemetry without requiring external bus polling:
- `RT_STATE_RPT` (High CAN): Broadcasts the unified `MANUAL/AUTO/ESTOP` state, task health bits, and steering angle feedback.
- `RT_PID_RPT` (High CAN): Shadow telemetry of the active PID controller parameters.
- `STEER_DIAG` / `BRAKE_DIAG` (High CAN): Translated status bytes originally emitted by the EPS-C and SEB units on the Low CAN bus. 

---

## 7. Remediation of Legacy Architecture Gaps

This unified architecture explicitly resolves the following gaps identified in `architecture-yaml-code-gaps.md`:

### Frame & Payload Gaps (FRM)
- **FRM-001 (`0x210` ambiguous routing):** Resolved. `big-esp32` generates `0x210 RT_STATE_RPT` solely for the Jetson over High CAN. Since `SYS` functionality is now internal, the low bus transmission of `0x210` is retired.
- **FRM-002, FRM-003, FRM-004 (Heartbeat collisions and bit packing):** Resolved. We eliminate `SYS_HEARTBEAT` and `RT_HEARTBEAT` passing between ESP32s entirely. A single `VEHICLE_HEARTBEAT` (DLC 2) is sent to the Jetson encompassing the unified safety state.

### RT Implementation Gaps (RT)
- **RT-001 (Task CPU Affinity):** Resolved. All tasks are explicitly pinned using `xTaskCreatePinnedToCore` as shown in Section 5, guaranteeing that body/I/O tasks on Core 0 cannot preempt critical control tasks on Core 1.
- **RT-002 (Safety Queue Overflows using `xQueueOverwrite`):** Resolved. We replace the depth-16 safety queue with FreeRTOS **Event Groups**. Event flags natively coalesce redundant state transitions without overflowing, safely mitigating burst failures.
- **RT-003 (Gateway drop counters missing):** Resolved by elimination. Because the High-to-Low gateway logic is internalized into a shared memory dispatch loop, inter-bus CAN forwarding drops no longer exist as a failure mode.
- **RT-004 (Log Flooding):** Resolved. `profile` integrity faults (like E2E checksum failures) increment an internal counter rather than `printf`ing every frame. Telemetry exports the counters instead of flooding the UART.

### SYS Implementation Gaps (SYS)
- **SYS-001 (`SYSTEM_RUN_MODE` hardcoded):** Resolved. Run modes (`vehicle`, `bench`, `hardware_bench`) are injected exclusively via PlatformIO environments (`-D ETRIKE_SYSTEM_RUN_MODE=X`) and are no longer hardcoded in `system_mode.h`.
- **SYS-002 (`g_brake_fault_active` never cleared):** Resolved. The unified `safety_monitor` task owns all fault states and applies explicit hysteresis timers for recovery, eliminating permanently latched, un-clearable phantom faults.
- **SYS-003 (TEC `< 255` treated as CAN OK):** Resolved. The `can_dispatch` layer reads the native TWAI status flags (Error Active, Error Passive, Bus-Off) and accurately degrades the unified system state to ESTOP if Error Passive is reached, rather than waiting for full Bus-Off.
- **SYS-004 (Bench vs Vehicle wiring proof):** Resolved via PlatformIO environments isolating simulated logic from production wiring.
- **SYS-005 (Checksum failure logs):** Addressed alongside RT-004 using `profile` payload codec strategies to quietly aggregate checksum streak violations.
