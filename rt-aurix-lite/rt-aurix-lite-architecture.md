# E-Trike System Architecture — AURIX Lite Variant

**Three-node consolidated control:** **Jetson Orin** (ROS 2 perception/planning), **AURIX TC3xx** (realtime physics, steering, brake, safety, body control, CAN gateway — combined RT+SYS), **MTR STM32** (motor actuation EGAS Level 1).

Two physical CAN buses at 500 kbit/s. AURIX bridges selected messages between buses (same role as RT in the distributed variant). Actuators are **SYNTREE** CAN modules: EPS-C (steer-by-wire) and SEB (electro-hydraulic brake). The AURIX directly commands both EPS-C and SEB in all modes — no mode-gated dual control needed (single controller owns both actuators). Motor control is on a dedicated STM32 board (MTR) for safety isolation per ISO 26262 EGAS 3-level concept.

> **Relationship to distributed architecture:** This is a consolidated variant of [`architecture.md`](../architecture.md). The distributed variant (RT ESP32-S3 + SYS ESP32-S3 on two CAN buses) remains the primary design. The AURIX Lite variant merges RT and SYS into one controller, keeping the same two-bus CAN topology. All CAN IDs, signal layouts, and protocol definitions are identical between variants. AURIX inherits both CAN interfaces that RT and SYS previously owned separately.

---

## 1. Topology

```
  ┌────────────────── High-Level CAN (500 kbit/s) ──────────────────┐
  │                                                                  │
  │  ┌──────────┐            ┌──────────────────┐                   │
  │  │  Jetson  │            │   AURIX TC3xx    │                   │
  │  │  Orin    │            │                  │                   │
  │  │          │            │ Physics          │                   │
  │  │ ROS 2    │            │ Steering         │                   │
  │  │ Planning │            │ Brake            │                   │
  │  └────┬─────┘            │ Safety           │                   │
  │       │                  │ Body Control     │                   │
  │  TX:  0x300,0x301,       │ Gateway          │                   │
  │       0x302,0x001        │                  │                   │
  │                           └──────┬───────────┘                   │
  │  RX:  0x011,0x210,      TX: 0x011,0x210,0x220,                  │
  │       0x220,0x400,             0x400,0x001,0x7FD                 │
  │       0x600,0x7FD         RX: 0x300,0x301,0x302,                 │
  │                                 0x001,0x7FC                      │
  └──────────────────────────────────────────────────────────────────┘
                                           │
                 ┌─────────────────────────┘
                 │
  ┌──────────────▼─────── Low-Level CAN (500 kbit/s) ───────────────┐
  │                                                                  │
  │  ┌──────────────────┐                                           │
  │  │   AURIX TC3xx    │                                           │
  │  │   (gateway)      │                                           │
  │  └────────┬─────────┘                                           │
  │           │                                                      │
  │      TX:  0x169,0x7B9,0x012,0x110,0x204,0x001                  │
  │      RX:  0x001,0x201,0x202,0x203,0x6FA,0x721,                 │
  │           0x731,0x741,0x6FB,0x206,0x120                          │
  │                                                                  │
  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
  │  │ SYNTREE  │  │ SYNTREE  │  │  DC-DC   │  │   MTR    │        │
  │  │   SEB    │  │  EPS-C   │  │ Converter│  │  STM32   │        │
  │  │ (Brake)  │  │(Steering)│  │ 72V→12V  │  │          │        │
  │  │0x7B9 cmd │  │0x169 cmd │  │ (0x012)  │  │RX:0x110, │        │
  │  │0x721 stat│  │0x201 stat│  └──────────┘  │   0x204  │        │
  │  └──────────┘  │0x202 err │                │TX:0x120, │        │
  │                │0x203 ver │                │   0x206  │        │
  │                │0x6FA test│                │RX:0x001  │        │
  │                └──────────┘                └──────────┘        │
  └──────────────────────────────────────────────────────────────────┘
```

> **Dual CAN hardware on AURIX:** The AURIX TC3xx has multiple MCMCAN modules. MCMCAN0 drives the low-level CAN bus (SYNTREE, MTR, DC-DC). MCMCAN1 drives the high-level CAN bus (Jetson). The `task_can_rx` and `task_can_tx` run per-bus, same as RT's `can_rx_low`/`can_rx_high` in the distributed variant.

---

## 2. CAN Message Catalog

### 2.1 Low-Level CAN

| ID | Name | Sender | Receiver(s) | DLC | Rate | Prio |
|----|------|--------|-------------|-----|------|------|
| `0x001` | SAFETY_ESTOP | Any | All (bridged to high) | 0 | Event | Highest |
| `0x012` | SYS_DCDC_CMD | AURIX | DC-DC | 1 | Change | V.High |
| `0x110` | SYS_MODE_CMD | AURIX | MTR | 1 | Change | High |
| `0x120` | SYS_THROTTLE_STS | MTR | AURIX | 2 | 100 Hz | Medium |
| `0x169` | VCU_SES_REQ | AURIX | EPS-C | 8 | 50 Hz | Medium |
| `0x201` | SES_STATUS | EPS-C | AURIX | 8 | 100 Hz | Medium |
| `0x202` | SES_ErrInfo | EPS-C | AURIX | 8 | 10 Hz | Medium |
| `0x203` | SES_Version | EPS-C | AURIX | 8 | 1 Hz | Lowest |
| `0x204` | RT_DRIVE_CMD | AURIX | MTR | 5 | 100 Hz | Medium |
| `0x206` | MTR_MOTOR_FBK | MTR | AURIX | 4 | 50 Hz | Low |
| `0x6FA` | SES_Test | EPS-C | AURIX | 8 | 100 Hz | Lowest |
| `0x6FB` | SEB_Test | SEB | AURIX | 8 | 100 Hz | Lowest |
| `0x721` | SEB_STATUS | SEB | AURIX | 8 | 100 Hz | Medium |
| `0x731` | SEB_ErrInfo | SEB | AURIX | 8 | 10 Hz | Medium |
| `0x741` | SEB_Version | SEB | AURIX | 8 | 1 Hz | Lowest |
| `0x7B9` | VCU_SEB_REQ | AURIX | SEB | 8 | 50 Hz | Medium |

### 2.2 High-Level CAN

| ID | Name | Sender | Receiver(s) | DLC | Rate | Prio |
|----|------|--------|-------------|-----|------|------|
| `0x001` | SAFETY_ESTOP | AURIX (fwd), Jetson | Jetson, AURIX | 0 | Event | Highest |
| `0x011` | SYS_SAFETY_STS | AURIX (fwd from low) | Jetson | 2 | 5 Hz | V.High |
| `0x120` | SYS_THROTTLE_STS | AURIX (fwd from low) | Jetson | 2 | 100 Hz | Medium |
| `0x210` | RT_STATE_RPT | AURIX | Jetson | 3 | 10 Hz | Low |
| `0x220` | RT_PID_RPT | AURIX | Jetson | 6 | (reserved) | Low |
| `0x300` | HOST_DRIVE_CMD | Jetson | AURIX | 8 | ≤100 Hz | Medium |
| `0x301` | HOST_BRAKE_REQ | Jetson | AURIX | 4 | Demand | Medium |
| `0x302` | HOST_LIGHT_CMD | Jetson | AURIX | 8 | Change | Medium |
| `0x400` | RT_OBSTACLE_RPT | AURIX | Jetson | 4 | 10 Hz | Low |
| `0x600` | SYS_DIAG_RPT | AURIX (fwd from low) | Jetson | 8 | 1 Hz | Lowest |
| `0x7FC` | HOST_HEARTBEAT | Jetson | AURIX | 1 | 2 Hz | Lowest |
| `0x7FD` | AURIX_HEARTBEAT | AURIX | Jetson | 1 | 2 Hz | Lowest |

### 2.3 AURIX CAN Gateway — Forwarding Rules

AURIX inherits RT's gateway role. Same three categories as distributed §2.3:

**Category 1 — Transparent forward (same ID, same payload):**

| Direction | IDs |
|-----------|-----|
| Low → High | `0x001`, `0x011`, `0x120`, `0x600` |
| High → Low | `0x001`, `0x302` |

**Category 2 — Consumed by AURIX → different message generated:**

| Inbound | Bus | Outbound | Bus |
|---------|-----|----------|-----|
| `0x300` HOST_DRIVE_CMD | High | `0x204` RT_DRIVE_CMD + `0x169` VCU_SES_REQ | Low |
| `0x301` HOST_BRAKE_REQ | High | → brake arbitration → `0x7B9` VCU_SEB_REQ | Low |

**Category 3 — Bus-local (never forwarded):**

| Bus | IDs |
|-----|-----|
| Low only | `0x012`, `0x110`, `0x169`, `0x202`, `0x203`, `0x204`, `0x206`, `0x6FA`, `0x6FB`, `0x721`, `0x731`, `0x741`, `0x7B9` |
| High only | `0x210`, `0x220`, `0x400` (AURIX telemetry) |
| Both independent | `0x7FC` (Jetson, high only), `0x7FD` (AURIX, both buses — per-bus, NOT bridged) |

---

## 3. Responsibility Split

| Concern | Jetson | AURIX | MTR |
|---------|:------:|:-----:|:---:|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| CAN gateway (low ↔ high) | | ✓ | |
| Tricycle kinematics | | ✓ | |
| Steering angle compute + CAN TX (0x169) | | ✓ | |
| Steering boot sync (Listen-Before-Speaking) | | ✓ | |
| Steering safety: dynamic angle clamp, hard-stops, following error | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| E-stop GPIO + handling | | ✓ | ✓ |
| Brake lever → CAN (0x7B9, 50 Hz continuous) | | ✓ | |
| Brake boot sync (Listen-Before-Speaking) | | ✓ | |
| Brake rolling counter + checksum | | ✓ | |
| DC-DC converter CAN control (0x012) | | ✓ | |
| Heartbeat monitoring (Jetson high + MTR low) | | ✓ | |
| Mode switch reading | | ✓ | |
| Throttle MCP4725 DAC output (0–5V) | | | ✓ |
| Gear 72V output (relay module) | | | ✓ |
| Motor feedback CAN TX (0x206) | | | ✓ |
| 12V accessory power relay | | ✓ | |
| Mode indicator lights | | ✓ | |
| Signal lights (turn, brake, head) | | ✓ | |
| System diagnostics | | ✓ | |

---

## 4. Mode State Machine

Same as distributed architecture §3. No mode-gated dual control — AURIX owns both actuators in all modes.

```
         ┌──────────┐
    ┌───▶│  MANUAL  │◀───┐
    │    └─────┬────┘    │
    │     push btn=AUTO  push btn=MANUAL
    │          │          │
    │    ┌─────▼────┐    │
    │    │   AUTO   │    │
    │    └─────┬────┘    │
    │          │          │
    │  ESTOP button / CAN 0x001 / MTR heartbeat timeout
    │          │          │
    │    ┌─────▼────┐    │
    │    │  ESTOP   │────┘
    │    └─────┬────┘
    │         │ START button / MODE long-press (3s)
    │         ▼
    └──────── MANUAL
```

| Mode | Behavior |
|------|----------|
| **MANUAL** | Rider steers / rides throttle. MTR reads throttle ADC + gear sense → pass-through via MCP4725 + relays. Brake lever → AURIX GPIO → CAN 0x7B9 → SEB (Stroke Mode, 15mm/0mm). EPS-C standalone (AURIX monitors 0x201). DC-DC on. Mode via CAN 0x110 (AURIX → MTR). |
| **AUTO** | Jetson `/cmd_vel` → high CAN 0x300 → AURIX kinematics → low CAN 0x204 (MTR) + 0x169 (EPS-C). Brake via 0x7B9 (Pressure Mode from arbitration, Stroke override for lever). Lights from Jetson via 0x302. |
| **ESTOP** | MTR kills motor locally (MCP4725=0, all gear OFF). 0x7B9 stroke=max (full brake). Steering ramps to 0° at 20°/s via active 0x169. DC-DC ON. 12V accessory relay OFF. Brake light ON. Exit: START button or MODE long-press → MANUAL. |

---

## 5. EGAS 3-Level Motor Safety

```
Level 3: Hardware — ESTOP button wired direct to both AURIX and MTR
         TPS3850 external watchdog on each MCU. No software, no CAN.
         ESTOP press → MTR cuts throttle + gear instantly (local).

Level 2: Function Monitor — AURIX TC3xx (safety_task, prio 5)
         Monitors MTR via CAN: compares 0x204 setpoint vs 0x206 feedback.
         Mismatch > 500mm/s for >500ms → CAN 0x001 ESTOP.
         AURIX MEMPROT isolates safety_task memory from QM tasks.
         AURIX SMU provides hardware-enforced freedom from interference.

Level 1: Function Controller — MTR STM32
         Normal actuation: reads sensors, drives MCP4725 DAC + gear relays.
         MANUAL: pass-through from grip/gear. AUTO: follows CAN 0x204.
         No wireless, no OS, minimal attack surface.
```

---

## 6. Dual CAN Hardware

| Bus | MCMCAN Module | GPIO | Transceiver |
|-----|--------------|------|-------------|
| Low-level | MCMCAN0 | TX=P20.8, RX=P20.7 | Onboard (KIT_A2G_TC387_LITE) |
| High-level | MCMCAN1 | TX=P14.0, RX=P14.1 | External SN65HVD230 via GPIO |

---

## 7. RTOS Task Layout

**16 FreeRTOS tasks** on AURIX TC3xx @ 300 MHz, 1000 Hz tick.

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx_low` | 5 | 4096 B | Event | MCMCAN0 → queue |
| `can_rx_high` | 5 | 4096 B | Event | MCMCAN1 → queue |
| `safety` | 5 | 2048 B | 20 Hz | ESTOP GPIO, MTR heartbeat monitor, EGAS L2 comparison, 0x7FD monitoring |
| `dispatch` | 4 | 4096 B | Event | Route both RX queues + gateway + steer feedback + fault escalation |
| `control` | 4 | 4096 B | 100 Hz | Kinematics, dynamic angle clamp, obstacle limit, brake arbitration, safety checks, ESTOP handling |
| `can_tx_low` | 3 | 3072 B | Event | 0x204@100Hz, 0x169@50Hz, 0x7B9@50Hz, gateway forwards |
| `can_tx_high` | 3 | 3072 B | Event | Telemetry (0x210,0x400@10Hz, 0x011,0x120,0x600 fwd) |
| `brake` | 3 | 2048 B | 50 Hz | SEB boot sequence + 0x7B9 continuous TX |
| `lights` | 3 | 1536 B | 20 Hz | Turn/brake/head lamp GPIOs + blink timing |
| `dcdc` | 3 | 1024 B | 5 Hz | DCDC FSM, CAN 0x012 |
| `mode` | 4 | 2048 B | 10 Hz | MODE/START buttons, ESTOP exit, CAN 0x110 |
| `indicator` | 2 | 1024 B | 5 Hz | Mode bulbs (AUTO/MANUAL) |
| `power` | 2 | 1024 B | 5 Hz | 12V accessory relay |
| `diag` | 1 | 2048 B | 1 Hz | System health → CAN 0x600 |
| `heartbeat` | 1 | 2048 B | 2 Hz | 0x7FD on both buses (per-bus, NOT bridged, separate counters) |
| `watchdog` | 1 | 2048 B | 10 Hz | Command staleness (500ms) → zero setpoints + stop steer |

---

## 8. Hardware Pin Assignments — AURIX TC3xx

| Signal | AURIX Pin | Peripheral | Direction | Notes |
|--------|-----------|------------|-----------|-------|
| CAN TX (low) | P20.8 | MCMCAN0 | Out | Onboard transceiver |
| CAN RX (low) | P20.7 | MCMCAN0 | In | |
| CAN TX (high) | P14.0 | MCMCAN1 | Out | External SN65HVD230 |
| CAN RX (high) | P14.1 | MCMCAN1 | In | |
| ESTOP_BTN | P00.0 | GPIO | In (pull-up) | NC, active-low. Shared with MTR |
| BRAKE_LEVER | P00.1 | GPIO | In (pull-up) | Active-low |
| START_BTN | P00.2 | GPIO | In (pull-up) | Green, exits ESTOP |
| MODE_BTN | P00.3 | GPIO | In (pull-up) | MANUAL↔AUTO toggle |
| SW_LEFT_TURN | P00.4 | GPIO | In (pull-up) | Handlebar switch |
| SW_RIGHT_TURN | P00.5 | GPIO | In (pull-up) | Handlebar switch |
| SW_HEADLIGHT | P00.6 | GPIO | In (pull-up) | Toggle |
| LIGHT_LEFT | P10.0 | GPIO | Out | Relay → 12V lamp |
| LIGHT_RIGHT | P10.1 | GPIO | Out | Relay → 12V lamp |
| BRAKE_LIGHT | P10.2 | GPIO | Out | Relay → 12V lamp |
| HEADLIGHT | P10.3 | GPIO | Out | Relay → 12V lamp |
| BULB_AUTO | P10.4 | GPIO | Out | Relay → 12V indicator |
| BULB_MANUAL | P10.5 | GPIO | Out | Relay → 12V indicator |
| RELAY_12V | P10.6 | GPIO | Out | Relay → accessory bus |
| WDT_TOGGLE | P10.7 | GPIO | Out | TPS3850 WDI, 100 Hz |
| I2C SDA | P15.4 | I2C0 | I/O | IMU (optional) |
| I2C SCL | P15.5 | I2C0 | Out | IMU (optional) |
| Encoder A (rear motor) | P02.0 | GPIO | In | Speed feedback |
| Encoder B (rear motor) | P02.1 | GPIO | In | |

---

## 9. Configuration Constants

```cpp
namespace aurix {

// ── Vehicle (shared) ──────────────────────────────────────────
// Use shared:: constants from shared/shared_config.h

// ── Steering (SYNTREE EPS-C) ──────────────────────────────────
constexpr float kSteerHardLimitDeg = 40.0f;
constexpr float kSteerFollowingErrDeg = 5.0f;
constexpr int   kSteerFollowingErrMs = 300;
constexpr int   kSteerCmdRateHz = 50;
constexpr int   kSteerBootWaitMs = 500;
constexpr float kSteerMaxAngleLowSpeed = 40.0f;
constexpr float kSteerMaxAngleHighSpeed = 5.0f;

// ── Brake (SYNTREE SEB) ───────────────────────────────────────
constexpr int   kBrakeCmdRateHz = 50;
constexpr int   kBrakeBootWaitMs = 500;
constexpr float kBrakeManualStroke = 15.0f;
constexpr float kBrakeMaxStroke = 27.0f;

// ── PID (deferred) ─────────────────────────────────────────────
constexpr float kPidKp = 1.0f, kPidKi = 0.1f, kPidKd = 0.05f;

// ── Timing ─────────────────────────────────────────────────────
constexpr int kControlLoopHz = 100;
constexpr int kHeartbeatId = 0x7FD;
constexpr int kHeartbeatIntervalMs = 500;
constexpr int kHeartbeatTimeoutMsMtr = 200;
constexpr int kHeartbeatTimeoutMsJetson = 1500;
constexpr int kWdtToggleGpio = 87;  // P10.7

// ── CAN ────────────────────────────────────────────────────────
constexpr int kCanLowBitrateHz = 500000;
constexpr int kCanHighBitrateHz = 500000;

// ── GPIO ───────────────────────────────────────────────────────
constexpr int kEstopGpio      =  0;  // P00.0
constexpr int kBrakeLeverGpio =  1;  // P00.1
constexpr int kStartBtnGpio   =  2;  // P00.2
constexpr int kModeBtnGpio    =  3;  // P00.3
constexpr int kSwLeftTurnGpio =  4;  // P00.4
constexpr int kSwRightTurnGpio = 5;  // P00.5
constexpr int kSwHeadlightGpio = 6;  // P00.6
constexpr int kLightLeftGpio  = 80;  // P10.0
constexpr int kLightRightGpio = 81;  // P10.1
constexpr int kBrakeLightGpio = 82;  // P10.2
constexpr int kHeadlightGpio  = 83;  // P10.3
constexpr int kBulbAutoGpio   = 84;  // P10.4
constexpr int kBulbManualGpio = 85;  // P10.5
constexpr int kPower12vRelayGpio = 86; // P10.6
constexpr int kCanLowTxGpio = 0;   // P20.8 (MCMCAN0, onboard)
constexpr int kCanLowRxGpio = 0;   // P20.7 (MCMCAN0, onboard)
constexpr int kCanHighTxGpio = 0;  // P14.0 (MCMCAN1, external)
constexpr int kCanHighRxGpio = 0;  // P14.1 (MCMCAN1, external)
constexpr int kI2cSdaGpio = 0;    // P15.4
constexpr int kI2cSclGpio = 0;    // P15.5
constexpr int kEncRearMotorA = 16; // P02.0
constexpr int kEncRearMotorB = 17; // P02.1

} // namespace aurix
```

---

## 10. Heartbeat — Liveness Supervision

AURIX sends `0x7FD` on **both buses independently** (same ID, separate alive counters per bus — NOT bridged). Same pattern as RT's dual-bus heartbeat in distributed §8.6.

| ID | Sender | Bus | Receiver | Period | Timeout | Action on loss |
|----|--------|-----|----------|--------|---------|----------------|
| `0x7FD` | AURIX | Low | MTR | 2 Hz | 200ms | MTR local fallback (maintain last safe speed, log) |
| `0x7FD` | AURIX | High | Jetson | 2 Hz | 1500ms | Jetson stops publishing `/cmd_vel` |
| `0x7FC` | Jetson | High | AURIX | 2 Hz | 1500ms | Assisted stop: zero 0x204, stop 0x169, 0x7B9 brake=2000 kPa |
| `0x206` | MTR | Low | AURIX | 50 Hz | 200ms | `0x206` staleness — log warning (motor feedback lost) |

> MTR heartbeat is implicit — `0x206` at 50 Hz serves as the liveness signal. No separate MTR heartbeat ID needed. AURIX monitors `time_since_last_0x206 > 200ms` as the MTR liveness check.

---

## 11. Differences from Distributed Architecture

| Aspect | Distributed (RT+SYS ESP32) | AURIX Lite |
|--------|---------------------------|------------|
| MCUs | 2× ESP32-S3 | 1× AURIX TC3xx |
| CAN buses | 2 (high + low) | 2 (same) |
| CAN gateway | RT ESP32 bridges | AURIX bridges (same role) |
| Mode-gated dual control | RT in AUTO, SYS in MANUAL/ESTOP | AURIX in all modes (no mode-gating) |
| EPS-C owner | RT (0x169) | AURIX (0x169) |
| SEB owner | SYS in MANUAL/ESTOP, RT in AUTO | AURIX (0x7B9) in all modes |
| Heartbeats | RT (0x7FD), SYS (0x7FE), Jetson (0x7FC) | AURIX (0x7FD, both buses), Jetson (0x7FC) |
| Motor actuation | SYS → MCP4725 + relays | MTR STM32 (same as after migration) |
| EGAS Level 2 | SYS ESP32 (separate MCU) | AURIX safety_task (MEMPROT + SMU isolated) |
| RTOS tasks | 8 (RT) + 15 (SYS) = 23 | 16 (merged, duplicates eliminated) |
| BOM cost | Higher (2 MCUs, 2 CAN transceivers) | Lower (1 MCU, 2 CAN transceivers) |
| Safety isolation | Physical (separate MCU) | Logical (MEMPROT + SMU + lockstep) |
| Development complexity | Higher (cross-MCU coordination) | Lower (single codebase) |

---

*See also: [`architecture.md`](../architecture.md) for the distributed reference architecture, [`can-dictionary.md`](../can-dictionary.md) for CAN signal layouts (identical between variants).*
