# E-Trike System Architecture — AURIX Lite Variant

**Three-node consolidated control:** **Jetson Orin** (ROS 2 perception/planning), **AURIX TC3xx** (realtime physics, steering, brake, safety, body control, motor actuation — combined RT+SYS), **MTR STM32** (motor actuation EGAS Level 1).

Single CAN bus at 500 kbit/s. No gateway needed. Actuators are **SYNTREE** CAN modules: EPS-C (steer-by-wire) and SEB (electro-hydraulic brake). The AURIX directly commands both EPS-C and SEB in all modes (no mode-gated dual control needed — single controller owns both actuators). Motor control is on a dedicated STM32 board (MTR) for safety isolation per ISO 26262 EGAS 3-level concept.

> **Relationship to distributed architecture:** This is a consolidated variant of [`architecture.md`](../architecture.md). The distributed variant (RT ESP32-S3 + SYS ESP32-S3 on two CAN buses) remains the primary design. The AURIX Lite variant consolidates both into a single controller on one CAN bus for cost-reduced or space-constrained deployments. All CAN IDs, signal layouts, and protocol definitions are identical between variants.

---

## 1. Topology

```
                    ┌──────────────────────────────────────────────┐
                    │          Single CAN Bus (500 kbit/s)         │
                    │                                              │
   ┌──────────┐     │  ┌──────────────────┐    ┌──────────────┐    │
   │  Jetson  │     │  │   AURIX TC3xx    │    │  MTR STM32   │    │
   │  Orin    │     │  │                  │    │              │    │
   │          │     │  │ Physics          │    │ EGAS Level 1 │    │
   │ ROS 2    │     │  │ Steering         │    │ Throttle DAC │    │
   │ Planning │     │  │ Brake            │    │ Gear Relays  │    │
   └────┬─────┘     │  │ Safety           │    └──────┬───────┘    │
        │           │  │ Body Control     │           │            │
   TX:  0x300,0x301,│  │ Motor Actuation  │      RX:  0x110,      │
        0x302,0x001 │  └────────┬─────────┘           0x204       │
                    │           │                         │        │
   RX:  0x011,0x120,│      TX:  0x169,0x7B9,        TX: 0x120,   │
        0x210,0x220,│           0x012,0x011,             0x206    │
        0x400,0x600,│           0x110,0x120,                      │
        0x7FD       │           0x204,0x600,          RX: 0x001   │
                    │           0x001                            │
                    │      RX:  0x001,0x201,                      │
                    │           0x202,0x203,                      │
                    │           0x300,0x301,                      │
                    │           0x302,0x721,                      │
                    │           0x731,0x741,                      │
                    │           0x6FA,0x6FB,                      │
                    │           0x7FC,0x206                       │
                    │                                              │
                    │  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
                    │  │ SYNTREE  │  │ SYNTREE  │  │  DC-DC   │  │
                    │  │  SEB     │  │  EPS-C   │  │ Converter│  │
                    │  │ (Brake)  │  │(Steering)│  │ 72V→12V  │  │
                    │  │ 0x7B9 cmd│  │ 0x169 cmd│  │ (0x012)  │  │
                    │  │ 0x721 stat│ │ 0x201 stat│ │          │  │
                    │  └──────────┘  └──────────┘  └──────────┘  │
                    └──────────────────────────────────────────────┘
```

---

## 2. CAN Message Catalog (Single Bus)

All messages from both the original high-level and low-level buses coexist on one bus. CAN IDs are unique across the original buses, so no collisions.

| ID | Name | Sender | Receiver(s) | DLC | Rate | Prio |
|----|------|--------|-------------|-----|------|------|
| `0x001` | SAFETY_ESTOP | Any | All | 0 | Event | Highest |
| `0x011` | SYS_SAFETY_STS | AURIX | Jetson | 2 | 5 Hz | V.High |
| `0x012` | SYS_DCDC_CMD | AURIX | DC-DC | 1 | Change | V.High |
| `0x110` | SYS_MODE_CMD | AURIX | MTR | 1 | Change | High |
| `0x120` | SYS_THROTTLE_STS | MTR | AURIX, Jetson | 2 | 100 Hz | Medium |
| `0x169` | VCU_SES_REQ | AURIX | EPS-C | 8 | 50 Hz | Medium |
| `0x201` | SES_STATUS | EPS-C | AURIX | 8 | 100 Hz | Medium |
| `0x202` | SES_ErrInfo | EPS-C | AURIX | 8 | 10 Hz | Medium |
| `0x203` | SES_Version | EPS-C | AURIX | 8 | 1 Hz | Lowest |
| `0x204` | RT_DRIVE_CMD | AURIX | MTR | 5 | 100 Hz | Medium |
| `0x206` | MTR_MOTOR_FBK | MTR | AURIX | 4 | 50 Hz | Low |
| `0x210` | RT_STATE_RPT | AURIX | Jetson | 3 | 10 Hz | Low |
| `0x220` | RT_PID_RPT | AURIX | Jetson | 6 | (reserved) | Low |
| `0x300` | HOST_DRIVE_CMD | Jetson | AURIX | 8 | ≤100 Hz | Medium |
| `0x301` | HOST_BRAKE_REQ | Jetson | AURIX | 4 | Demand | Medium |
| `0x302` | HOST_LIGHT_CMD | Jetson | AURIX | 1 | Change | Medium |
| `0x400` | RT_OBSTACLE_RPT | AURIX | Jetson | 4 | 10 Hz | Low |
| `0x600` | SYS_DIAG_RPT | AURIX | Jetson | 8 | 1 Hz | Lowest |
| `0x6FA` | SES_Test | EPS-C | AURIX | 8 | 100 Hz | Lowest |
| `0x6FB` | SEB_Test | SEB | AURIX | 8 | 100 Hz | Lowest |
| `0x721` | SEB_STATUS | SEB | AURIX | 8 | 100 Hz | Medium |
| `0x731` | SEB_ErrInfo | SEB | AURIX | 8 | 10 Hz | Medium |
| `0x741` | SEB_Version | SEB | AURIX | 8 | 1 Hz | Lowest |
| `0x7B9` | VCU_SEB_REQ | AURIX | SEB | 8 | 50 Hz | Medium |
| `0x7FC` | JETSON_HEARTBEAT | Jetson | AURIX | 1 | 2 Hz | Lowest |
| `0x7FD` | AURIX_HEARTBEAT | AURIX | Jetson, MTR | 1 | 2 Hz | Lowest |

> **No gateway needed.** All nodes are on the same bus. Messages that were forwarded between buses in the distributed architecture (0x011, 0x120, 0x600, 0x302) are now direct. RT-originated telemetry (0x210, 0x220, 0x400) goes directly to Jetson. Jetson commands (0x300, 0x301, 0x302) are received directly by AURIX.

---

## 3. Responsibility Split

| Concern | Jetson | AURIX | MTR |
|---------|:------:|:-----:|:---:|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| Tricycle kinematics | | ✓ | |
| Steering angle compute + CAN TX (0x169) | | ✓ | |
| Steering boot sync (Listen-Before-Speaking) | | ✓ | |
| Steering safety: dynamic angle clamp, hard-stops, following error | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| E-stop handling | | ✓ | ✓ |
| Brake control — SEB via CAN (0x7B9) | | ✓ | |
| Brake boot sync (Listen-Before-Speaking) | | ✓ | |
| Brake rolling counter + checksum | | ✓ | |
| DC-DC converter CAN control (0x012) | | ✓ | |
| Heartbeat monitoring | | ✓ | |
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

Same as distributed architecture §3:

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
    │  ESTOP button / CAN 0x001 / HB timeout
    │          │          │
    │    ┌─────▼────┐    │
    │    │  ESTOP   │────┘
    │    └─────┬────┘
    │         │ START button / MODE long-press (3s)
    │         ▼
    └──────── MANUAL
```

**Key difference from distributed:** No mode-gated dual control. The AURIX owns both EPS-C (0x169) and SEB (0x7B9) in all modes. In MANUAL: EPS-C standalone, SEB follows brake lever. In AUTO: EPS-C follows kinematics angle, SEB follows brake arbitration. In ESTOP: EPS-C ramps to 0°, SEB goes to max stroke.

---

## 5. Signal Flow

### 5.1 Manual Mode

```
Throttle grip (0–5V) ──► MTR ADC ──► MTR MCP4725 (0–5V) ──► Motor controller
Gear selector (72V)  ──► TLP281 opto → MTR GPIO ──► relay module → 72V → ECU
Brake lever           ──► AURIX GPIO ──► CAN 0x7B9 → SEB (stroke=15mm if pressed)
Steering wheel        ──► EPS-C standalone (AURIX monitors 0x201)
Signal lights         ──► Turn: handlebar switches (AURIX GPIO). Head: toggle (AURIX GPIO). Brake: OR logic → AURIX GPIO
DC-DC converter       ──► AURIX CAN 0x012 enable=1 → 12V rail on
```

### 5.2 Auto Mode

```
Jetson /cmd_vel ──► CAN 0x300 ──► AURIX kinematics
                                      │
           ┌──────────────────────────┤
           ▼                          ▼
   0x204 {speed, gear} → MTR    0x169 {angle} → EPS-C
           │                          │
           ├──► MCP4725 → Motor       │  AURIX listens 0x201 for feedback
           ├──► Relays → ECU gear     │  Dynamic angle clamp + following error
           └──► CAN 0x206 → AURIX     │
                EGAS L2 comparison    │
                                      │
Jetson ──► CAN 0x301 ──► AURIX brake arbitration → CAN 0x7B9 → SEB
Jetson ──► CAN 0x302 ──► AURIX → light relays
```

---

## 6. EGAS 3-Level Motor Safety

Same architecture as distributed §6.1, but Level 2 monitoring runs on the same AURIX that does Level 1-like body control. The MTR STM32 remains the dedicated Level 1 Function Controller with hardware isolation:

```
Level 3: Hardware — ESTOP button wired direct to both AURIX and MTR
         TPS3850 external watchdog on each MCU. No software, no CAN.

Level 2: Function Monitor — AURIX TC3xx (safety_task)
         Monitors MTR via CAN: compares 0x204 setpoint vs 0x206 feedback.
         Mismatch > threshold → CAN 0x001 ESTOP.
         Also handles QM body functions.

Level 1: Function Controller — MTR STM32
         Normal actuation: reads sensors, drives MCP4725 DAC + gear relays.
         No wireless, no OS, minimal attack surface.
```

> **Freedom from interference note:** Unlike the distributed variant where SYS is a physically separate MCU, Level 2 monitoring runs on the same AURIX die as the steering and brake control tasks. ISO 26262 permits this when the Level 2 function has freedom from interference via memory protection (AURIX MEMPROT) and temporal isolation (strict RTOS scheduling with timing protection). The AURIX TC3xx's Safety Management Unit (SMU) provides hardware-enforced freedom from interference between ASIL and QM software partitions.

---

## 7. RTOS Task Layout

**16 FreeRTOS tasks** on AURIX TC3xx @ 300 MHz, 1000 Hz tick.

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx` | 5 | 4096 B | Event | CAN receive → queue |
| `safety` | 5 | 2048 B | 20 Hz | ESTOP GPIO, MTR heartbeat monitor, EGAS L2 comparison |
| `dispatch` | 4 | 4096 B | Event | Route CAN frames, update atomics, heartbeat tracking, fault escalation |
| `control` | 4 | 4096 B | 100 Hz | Kinematics, dynamic angle clamp, obstacle limit, brake arbitration, safety checks |
| `can_tx` | 3 | 4096 B | Event | 0x204@100Hz, 0x169@50Hz, 0x7B9@50Hz, 0x210@10Hz, 0x400@10Hz, gateway N/A |
| `brake` | 3 | 2048 B | 50 Hz | SEB boot sequence + 0x7B9 continuous transmission |
| `steering` | 3 | 2048 B | — | Steering state machine (inline in can_tx or separate) |
| `lights` | 3 | 1536 B | 20 Hz | Turn/brake/head lamp GPIOs + blink timing |
| `dcdc` | 3 | 1024 B | 5 Hz | DCDC FSM, CAN 0x012 |
| `mode` | 4 | 2048 B | 10 Hz | MODE/START buttons, ESTOP exit, CAN 0x110 |
| `indicator` | 2 | 1024 B | 5 Hz | Mode bulbs (AUTO/MANUAL) |
| `power` | 2 | 1024 B | 5 Hz | 12V accessory relay |
| `diag` | 1 | 2048 B | 1 Hz | System health → CAN 0x600 |
| `heartbeat` | 1 | 2048 B | 2 Hz | CAN 0x7FD (single bus — no per-bus split needed) |
| `watchdog` | 1 | 2048 B | 10 Hz | Command staleness (500ms) → zero setpoints + stop steer |
| `throttle_adc` | 3 | 1536 B | 100 Hz | ADC read (MANUAL passthrough) → CAN 0x120 (delegated to MTR in this variant) |

> **Merged from distributed:** `can_rx_low` + `can_rx_high` → single `can_rx`. `can_tx_low` + `can_tx_high` → single `can_tx`. Gateway queues eliminated (single bus). `brake` and `steering` state machines run locally — no cross-MCU coordination. MTR tasks (throttle, gear) remain on MTR STM32.

---

## 8. Design Principles

1. **Queues over shared state.** No mutexes, no semaphores. Thread-safe queue pipes. (Same)
2. **ESTOP bypasses queues.** Safety task preempts and writes directly to actuators. (Same)
3. **One CAN ID = one sender.** Every CAN ID has exactly one originator on the bus. (Simplified — single bus, no bus-local exceptions)
4. **Lower CAN ID = higher bus priority.** (Same)
5. **All multi-byte CAN fields are big-endian (MSB first)** — unless SYNTREE protocol specifies otherwise. (Same)
6. **Manual mode is pass-through, not dead.** (Same)
7. **Actuators are standalone CAN modules.** (Same)
8. **Single controller — no gateway.** All nodes on one bus. No message forwarding.
9. **Listen Before Speaking.** SYNTREE units require status feedback before command. (Same)
10. **EGAS 3-level safety separation for motor actuation.** MTR STM32 isolation preserved. (Same)
11. **Single owner of both SYNTREE actuators.** AURIX commands EPS-C and SEB directly in all modes. No mode-gated dual control needed. No single MCU failure can take both actuators because... there's only one MCU. The MTR STM32 provides motor kill independence. EPS-C and SEB have internal timeout-fault behavior as hardware backup.

---

## 9. Hardware Pin Assignments — AURIX TC3xx

> Pins are assigned from available AURIX TC3xx Lite Kit (KIT_A2G_TC387_LITE) peripherals. CAN uses onboard MCMCAN. I2C and GPIO use standard port pins.

| Signal | AURIX Pin | Port | Direction | Notes |
|--------|-----------|------|-----------|-------|
| CAN TX | P20.8 | MCMCAN0 | Out | Onboard transceiver |
| CAN RX | P20.7 | MCMCAN0 | In | |
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
| Encoder A (rear motor) | P02.0 | GPIO | In | Speed feedback (PCNT) |
| Encoder B (rear motor) | P02.1 | GPIO | In | |

---

## 10. Configuration Constants

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

// ── PID (deferred until encoder fitted) ───────────────────────
constexpr float kPidKp = 1.0f, kPidKi = 0.1f, kPidKd = 0.05f;

// ── Timing ─────────────────────────────────────────────────────
constexpr int kControlLoopHz = 100;
constexpr int kHeartbeatIntervalMs = 500;
constexpr int kHeartbeatTimeoutMsMtr = 200;
constexpr int kHeartbeatTimeoutMsJetson = 1500;
constexpr int kHeartbeatId = 0x7FD;
constexpr int kWdtToggleGpio = 107;  // P10.7

// ── CAN ────────────────────────────────────────────────────────
constexpr int kCanBitrateHz = 500000;

// ── GPIO pin assignments ───────────────────────────────────────
constexpr int kEstopGpio      = 0;   // P00.0
constexpr int kBrakeLeverGpio = 1;   // P00.1
constexpr int kStartBtnGpio   = 2;   // P00.2
constexpr int kModeBtnGpio    = 3;   // P00.3
constexpr int kSwLeftTurnGpio = 4;   // P00.4
constexpr int kSwRightTurnGpio = 5;  // P00.5
constexpr int kSwHeadlightGpio = 6;  // P00.6
constexpr int kLightLeftGpio  = 80;  // P10.0
constexpr int kLightRightGpio = 81;  // P10.1
constexpr int kBrakeLightGpio = 82;  // P10.2
constexpr int kHeadlightGpio  = 83;  // P10.3
constexpr int kBulbAutoGpio   = 84;  // P10.4
constexpr int kBulbManualGpio = 85;  // P10.5
constexpr int kPower12vRelayGpio = 86; // P10.6
constexpr int kCanTxGpio = 0;   // P20.8 (MCMCAN)
constexpr int kCanRxGpio = 0;   // P20.7 (MCMCAN)
constexpr int kI2cSdaGpio = 0;  // P15.4
constexpr int kI2cSclGpio = 0;  // P15.5
constexpr int kEncRearMotorA = 16; // P02.0
constexpr int kEncRearMotorB = 17; // P02.1

} // namespace aurix
```

---

## 11. Differences from Distributed Architecture

| Aspect | Distributed (RT+SYS ESP32) | AURIX Lite |
|--------|---------------------------|------------|
| MCUs | 2× ESP32-S3 | 1× AURIX TC3xx |
| CAN buses | 2 (high + low) | 1 (combined) |
| Gateway | RT bridges messages | None needed |
| Mode-gated dual control | RT in AUTO, SYS in MANUAL/ESTOP | AURIX in all modes |
| EPS-C owner | RT (0x169) | AURIX (0x169) |
| SEB owner | SYS in MANUAL/ESTOP, RT in AUTO | AURIX (0x7B9) in all modes |
| Motor actuation | SYS → MCP4725 + relays | MTR STM32 (same) |
| EGAS Level 2 | SYS ESP32 (separate MCU) | AURIX safety_task (MEMPROT isolated) |
| Heartbeats | 3 (RT, SYS, Jetson) on 2 buses | 2 (AURIX, Jetson) on 1 bus |
| BOM cost | Higher (2 MCUs, 2 CAN transceivers) | Lower (1 MCU, 1 CAN transceiver) |
| Safety isolation | Physical (separate MCU) | Logical (MEMPROT + SMU) |
| Development complexity | Higher (cross-MCU coordination) | Lower (single codebase) |

---

## 12. When to Use Each Variant

**Distributed (RT+SYS ESP32-S3):**
- When physical separation of safety functions is required (ISO 26262 ASIL-C without MEMPROT)
- When 2 CAN buses are needed for bandwidth or logical separation
- When independent MCU failure modes are a hard requirement
- Reference platform — all development and testing targets this first

**AURIX Lite:**
- Cost-reduced or space-constrained builds
- When AURIX TC3xx safety hardware (SMU, MEMPROT, lockstep cores) satisfies freedom-from-interference requirements
- Single-CAN-bus topologies where all nodes fit on one bus
- Faster development cycle (single codebase, no cross-MCU debugging)

---

*See also: [`architecture.md`](../architecture.md) for the distributed reference architecture, [`can-dictionary.md`](../can-dictionary.md) for CAN signal layouts (identical between variants).*
