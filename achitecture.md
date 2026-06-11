# E-Trike System Architecture — Unified Single-ESP32 Design

Two-node distributed control: **Jetson Orin NX** (ROS 2 perception/planning) and a single **ESP32-S3** (realtime physics, vehicle I/O, safety, and actuation). Communication over dual CAN bus at 500 kbit/s.

> **Design revision (2026-06):** The original dual-ESP32 (RT + SYS) layout was merged into one ESP32-S3. The inter-MCU UART link and its protocol are eliminated; RT-to-SYS setpoints now flow through a direct FreeRTOS queue. Updated throttle to bidirectional 0–5V, added gear selector (72V I/O), lighting control, and 12V PSU actuation. See [[#safety-architecture]] for the safety rationale and [[notes/rtos-architecture]] for the task-level design.

## Node topology

```
┌─────────────────────────────────────────────────────────────────────────┐
│                       Public CAN Bus (500 kbit/s)                       │
│                                                                          │
│  ┌──────────────┐               ┌──────────────────────────────────┐    │
│  │   Jetson     │               │        SINGLE ESP32-S3           │    │
│  │   Orin NX    │               │                                  │    │
│  │              │               │  Core 0: Safety & CAN I/O        │    │
│  │  ROS 2       │◄═════════════►│  Core 1: Computation & I/O       │    │
│  │  Bridge      │    CAN        │                                  │    │
│  └──────────────┘               │  ┌─ safety_task                 │    │
│                                  │  ├─ can_rx / dispatch           │    │
│  ┌──────────────────────────────┐│  ├─ mode_task + mode lights     │    │
│  │  Vehicle I/O (direct-wired)  ││  ├─ control_task (physics+PID) │    │
│  │                              ││  ├─ throttle_task (ADC read +  │    │
│  │  Throttle: 0–5V in/out ──────┼│  │     0–5V PWM out)           │    │
│  │  Gear: D/S/R, 72V in/out ────┼│  ├─ gear_task (read + drive)   │    │
│  │  Signal lights: L/R ─────────┼│  ├─ brake_task (CAN + FSM)     │    │
│  │  Mode lights: Auto/Manual ───┼│  ├─ obstacle_task (HC-SR04)    │    │
│  │  12V PSU control ────────────┼│  ├─ syntree_tx_task            │    │
│  │  E-stop, brake lever ────────┼│  ├─ lighting_task              │    │
│  │  Encoder, IMU ───────────────┼│  ├─ watchdog_task              │    │
│  └──────────────────────────────┘│  ├─ diagnostics_task           │    │
│                                  │  └─ heartbeat_task             │    │
│                                  └────────────┬─────────────────────┘    │
│                                               │                          │
│                                Private CAN Bus (500 kbit/s)              │
│                                               │                          │
│                                  ┌────────────┴─────────────────────┐    │
│                                  │  Steering (Syntree EPS-C, CAN)   │    │
│                                  │  Brake    (Syntree SEB,   CAN)   │    │
│                                  └──────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Vehicle interface summary

| Interface | Type | Direction | Protocol | Notes |
|-----------|------|-----------|----------|-------|
| Throttle | 0–5V analog | **Read** (Manual) + **Generate** (Auto) | ADC in, LEDC PWM + RC filter out | Motor controller expects 0–5V; ESP32 reads via ADC divider, drives via filtered PWM |
| Gear selector | 72V discrete | **Read** + **Drive** | Voltage divider in, transistor/relay out | D=Drive, S=?, R=Reverse. One line hot at 72V. Both directions needed |
| Steering | Drive-by-wire | **Command** | CAN (Syntree EPS-C) | Private CAN bus `0x169` |
| Brake | Electro-hydraulic | **Command** | CAN (Syntree SEB) | Private CAN bus. Has CAN module |
| Signal lights | 12V lamps | **Drive** | GPIO → transistor/relay | Left + Right |
| Mode lights | 12V indicator | **Drive** | GPIO → transistor/relay | Auto + Manual indicators |
| 12V PSU | Power supply | **Drive** | GPIO → relay | Actuate 12V rail for lights/accessories |
| E-stop | NC button | **Read** | GPIO, active-low | Hardware relay layer independent of MCU |
| Brake lever | Switch | **Read** | GPIO, active-low | Manual brake lever |
| Encoder | Quadrature | **Read** | PCNT | Rear wheel speed |
| IMU | I2C | **Read** | I2C | Yaw rate (gyro Z) |
| Obstacle | HC-SR04 | **Read** | GPIO TRIG/ECHO | Ultrasonic distance |
| Mode switch | Toggle | **Read** | GPIO | Manual / Auto |

---

## CAN message catalog

### Public bus: Jetson ↔ ESP32-S3

| ID | Name | Sender | Receiver | DLC | Payload | Rate |
|----|------|--------|----------|-----|---------|------|
| `0x001` | SAFETY_ESTOP | Any | ESP32 | 0 | (none) | On event |
| `0x011` | SAFETY_STATUS | ESP32 | Jetson | 2 | u8 estop, u8 hb_ok | 5 Hz |
| `0x120` | THROTTLE_POS | ESP32 | Jetson | 2 | i16 speed_mmps | 100 Hz |
| `0x121` | GEAR_STATE | ESP32 | Jetson | 1 | u8 gear (0=N,1=D,2=S,3=R) | 10 Hz |
| `0x210` | RT_STATE_REPORT | ESP32 | Jetson | 3 | u8 mode, u8 steer_ok, u8 rev | 10 Hz |
| `0x220` | RT_PID_FEEDBACK | ESP32 | Jetson | 6 | i16 sp, i16 meas, i16 out | 10 Hz |
| `0x300` | HOST_DRIVE_CMD | Jetson | ESP32 | 8 | i32 speed, i32 yaw | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQUEST | Jetson | ESP32 | 4 | i32 brake_pressure_kpa | On demand |
| `0x302` | HOST_LIGHT_CMD | Jetson | ESP32 | 1 | u8 lights (bit0=L, bit1=R) | On change |
| `0x400` | OBSTACLE_DIST | ESP32 | Jetson | 4 | u32 distance_mm | 10 Hz |
| `0x600` | SYS_DIAG | ESP32 | Jetson | 8 | diag struct | 1 Hz |
| `0x7FF` | HEARTBEAT | Both | Both | 0 | (none) | 2 Hz |

### Private bus: ESP32-S3 → Syntree actuators

| ID | Name | Sender | Receiver | DLC | Payload | Rate |
|----|------|--------|----------|-----|---------|------|
| `0x169` | EPS_COMMAND | ESP32 | EPS-C | 8 | steer angle + checksum | 50 Hz |
| `0x201` | EPS_STATUS | EPS-C | ESP32 | 8 | angle feedback + faults | 10 Hz |
| `0x7B9` | SEB_COMMAND | ESP32 | SEB | 8 | brake pressure + checksum | 50 Hz |
| `0x721` | SEB_STATUS | SEB | ESP32 | 8 | pressure feedback + faults | 10 Hz |

### Messages eliminated by single-MCU merge

| Old ID | Name | Reason |
|--------|------|--------|
| ~~`0x010`~~ | SYS_BRAKE_CMD | Internal state — brake handled within firmware |
| ~~`0x110`~~ | SYS_MODE_CMD | Internal state — mode is shared memory |
| ~~`0x200`~~ | RT_DRIVE_SETPOINT | Replaced by FreeRTOS `setpoint_queue` (control→motor) |

---

## Mode state machine

```
         ┌──────────┐
    ┌───▶│  MANUAL  │◀───┐
    │    └─────┬────┘    │
    │   switch=Auto    switch=Manual
    │          │          │
    │    ┌─────▼────┐    │
    │    │   AUTO   │    │
    │    └─────┬────┘    │
    │          │          │
    │  E-stop button / CAN 0x001 / HB timeout
    │          │          │
    │    ┌─────▼────┐    │
    └────│  ESTOP   │────┘  (cannot leave ESTOP via switch)
         └──────────┘
```

| Mode | Throttle | Gear | Steering | Brake | Lights |
|------|----------|------|----------|-------|--------|
| **Manual** | Read ADC → 0–5V out to motor controller | Read gear selector, drive gear lines as-read | Disabled (mechanical) | Brake lever → SEB CAN | Signal lights: manual switch or CAN; Mode light: Manual ON |
| **Auto** | Generate 0–5V from PID output to motor controller | Drive gear lines: D forward, R reverse, S disabled | Syntree EPS-C via CAN | Jetson 0x301 or E-stop → SEB CAN | Signal lights: CAN 0x302; Mode light: Auto ON |
| **Estop** | 0V out (motor stop) | Force Neutral | Disabled (limp) | 100% brake via SEB CAN | Mode lights: both OFF; Signal lights: preserve last state |

**ESTOP is the only state that bypasses the queue pipeline.** The `safety_task` at priority 5 calls `motor_stop()` and `brake_engage()` directly — no queue, no delay.

---

## Interface details

### Throttle — bidirectional 0–5V

The motor controller expects a **0–5V analog signal**. The ESP32-S3 must:

- **Read** (Manual mode): ADC pin reads the 0–5V signal via a voltage divider (0–5V → 0–3.3V). Ratio ≈ 2:1 (e.g., 10kΩ + 6.8kΩ).
- **Generate** (Auto mode): MCP4725 12-bit I2C DAC, powered by 5V, outputs true 0–5V analog. Shares I2C0 bus with IMU (different address: IMU=0x68, DAC=0x60).

A mode-controlled analog switch (e.g., CD4053) or relay selects whether the motor controller sees the physical throttle or the DAC-generated signal.

```
Physical throttle ──► Voltage divider ──► ADC (GPIO10)       [always readable]
MCP4725 DAC ────────► 0–5V output ──────► Analog switch      [Auto mode only]
                                Physical throttle ───────────► [Manual mode]
                                                     │
                                          Motor controller 0–5V input
```

### Gear selector — 72V discrete I/O

The gear selector operates at **72V**. Three positions: **D** (Drive), **S**, **R** (Reverse). When a gear is engaged, that line carries 72V. We need to both read and drive:

- **Read**: Each gear line → current-limiting resistor → TLP281 optoisolator LED input → GPIO with pull-up to 3.3V. 72V active → LED on → phototransistor pulls GPIO LOW (active-low). 4-channel module provides 3 channels (D/S/R) + 1 spare.
- **Drive**: GPIO → NPN transistor / optocoupler → relay / high-side switch → 72V to gear line. One GPIO per gear position.

```
Gear D (72V) ──► Rled ──► TLP281 ch1 LED ──► GND (72V side)
                             ch1 photo ──► GPIO13 (3.3V, pull-up, active-low)

Gear S (72V) ──► Rled ──► TLP281 ch2 LED ──► GND (72V side)
                             ch2 photo ──► GPIO26 (3.3V, pull-up, active-low)

Gear R (72V) ──► Rled ──► TLP281 ch3 LED ──► GND (72V side)
                             ch3 photo ──► GPIO14 (3.3V, pull-up, active-low)

GPIO6  ──► transistor ──► 72V switch ──► Gear D (drive)
GPIO42 ──► transistor ──► 72V switch ──► Gear S (drive)
GPIO43 ──► transistor ──► 72V switch ──► Gear R (drive)
```

In Manual mode, gear output follows gear input (pass-through). In Auto mode, ESP32 drives D for forward, R for reverse, N (all off) for stop. In Estop, all gear outputs are forced off (Neutral).

### Steering — CAN drive-by-wire (Syntree EPS-C)

Steering rack is drive-by-wire via CAN. ESP32 sends `0x169` EPS_COMMAND on the private CAN bus (TWAI1) with angle targets, enable bit, and checksum. Feedback via `0x201` EPS_STATUS with actual angle and fault codes. Protocol details discovered in Phase 1.

### Brake — CAN electro-hydraulic (Syntree SEB)

Brake is electro-hydraulic with a CAN module. ESP32 sends pressure/stroke commands on the private CAN bus. Three trigger sources, max-select arbitration:
1. E-stop → 100% brake pressure
2. Brake lever (GPIO2) → 100% brake pressure
3. Jetson `0x301` → commanded brake pressure

Feedback via SEB_STATUS frames with actual pressure, stroke, and fault codes.

### 12V PSU control

ESP32 GPIO drives a relay or MOSFET to switch the 12V power rail. The 12V rail powers signal lights, mode lights, and accessories. Controlled at startup and can be shut off in Estop for power conservation.

### Lighting

| Light | Control | GPIO | Notes |
|-------|---------|------|-------|
| Signal Left | GPIO → transistor → 12V lamp | 40 | Also controllable via CAN 0x302 in Auto |
| Signal Right | GPIO → transistor → 12V lamp | 41 | Also controllable via CAN 0x302 in Auto |
| Mode: Auto | GPIO → transistor → 12V lamp | 38 | ON in Auto mode only |
| Mode: Manual | GPIO → transistor → 12V lamp | 39 | ON in Manual mode only |

Signal lights can be triggered by physical switches (Manual mode) or CAN command (Jetson `0x302` in Auto mode). The `lighting_task` polls switches and CAN, updates outputs at 20 Hz.

---

## Responsibility map

| Concern | Jetson | ESP32-S3 |
|---------|--------|----------|
| Perception / planning | ✓ | |
| ROS 2 → CAN bridge | ✓ | |
| Tricycle kinematics | | **Core 1** |
| Speed PID (100 Hz) | | **Core 1** |
| Steering command (CAN) | | **Core 1** |
| Obstacle speed limiting | | **Core 1** |
| Command staleness watchdog | | **Core 1** |
| Throttle: ADC read (0–5V) | | **Core 1** |
| Throttle: PWM generate (0–5V) | | **Core 1** |
| Gear: read (72V→3.3V) | | **Core 1** |
| Gear: drive (72V switching) | | **Core 1** |
| Brake: SEB CAN command | | **Core 1** |
| Signal lights + mode lights | | **Core 1** |
| 12V PSU control | | **Core 1** |
| E-stop GPIO + button | | **Core 0 (prio 5)** |
| Brake lever input | | **Core 0** |
| Heartbeat monitoring | | **Core 0 (prio 5)** |
| Mode switch reading | | **Core 0** |
| Syntree EPS-C/SEB CAN TX | | **Core 1** |
| Syntree EPS-C/SEB CAN RX | | **Core 0** |
| System diagnostics (1 Hz) | | **Core 1** |

---

## Core pinning & task layout

```
Core 0 — Safety & CAN I/O          Core 1 — Computation & Vehicle I/O
─────────────────────────────────  ─────────────────────────────────────
Priority                            Priority
   5  safety_task      20 Hz           4  control_task       100 Hz
   5  can_rx_task0     event           4  motor_task         100 Hz
   5  can_rx_task1     event           4  syntree_tx_task     50 Hz
   4  dispatch_task    event           3  throttle_task      100 Hz
   4  mode_task        10 Hz           3  gear_task           20 Hz
   4  can_tx_task      5 Hz            3  brake_task          20 Hz
   1  heartbeat_task   2 Hz            3  lighting_task       20 Hz
   1  watchdog_task    10 Hz           2  obstacle_task       10 Hz
                                       1  diagnostics_task     1 Hz
```

| Task | Core | Prio | Stack | Period | Behavior |
|------|------|------|-------|--------|----------|
| `safety` | **0** | **5** | 2048 B | 20 Hz | Polls E-stop + brake lever GPIOs. Monitors Jetson heartbeat. Calls `motor_stop()` + `brake_engage()` + `gear_neutral()` directly on fault. Toggles external watchdog GPIO21. |
| `can_rx0` | **0** | 5 | 4096 B | event | Public CAN bus — blocks on `twai_receive()`, enqueues frames to `can_rx_queue`. |
| `can_rx1` | **0** | 5 | 4096 B | event | Private CAN bus — blocks on `twai_receive()` for Syntree EPS-C/SEB status frames. |
| `dispatch` | **0** | 4 | 3072 B | event | Parses `can_rx_queue`. Routes `0x300`→`cmd_queue`, `0x301`→atomic, `0x302`→light atomic, ESTOP IDs→`mode_set()`. |
| `mode` | **0** | 4 | 2048 B | 10 Hz | Reads mode switch GPIO. Calls `mode_set()`. Ignored in ESTOP. Updates mode light outputs. |
| `can_tx` | **0** | 4 | 3072 B | 5 Hz | Periodic safety status `0x011`. |
| `control` | **1** | 4 | 4096 B | 100 Hz | Reads IMU yaw rate (I2C). `vTaskDelayUntil`. Physics resolve → obstacle limit → PID → motor/throttle setpoint via `setpoint_queue`. |
| `motor` | **1** | 4 | 2048 B | 100 Hz | Reads `setpoint_queue`. Writes 0–5V throttle output (LEDC PWM duty). In ESTOP → 0V. |
| `syntree_tx` | **1** | 4 | 3072 B | 50 Hz | Sends EPS-C and SEB command frames on private CAN bus (when enabled). |
| `throttle` | **1** | 3 | 1536 B | 100 Hz | Reads throttle ADC (0–5V via divider), stores to atomic for CAN `0x120`. In Auto mode, also responsible for setting 0–5V PWM output duty from `setpoint_queue`. |
| `gear` | **1** | 3 | 1536 B | 20 Hz | Reads gear position from 72V divider inputs. In Manual: pass-through (gear out = gear in). In Auto: drive D/R/N per control command. In Estop: force Neutral. Publishes gear state to CAN `0x121`. |
| `brake` | **1** | 3 | 1536 B | 20 Hz | Brake FSM: ESTOP→engage, lever→engage, CAN 0x301→commanded pressure. Max-select arbitration. Outputs SEB CAN command via `syntree_tx_task`. |
| `lighting` | **1** | 3 | 1536 B | 20 Hz | Signal lights: read physical switches or CAN 0x302, drive Left/Right GPIOs. Mode lights: drive Auto/Manual indicators per `g_mode`. |
| `obstacle` | **1** | 2 | 2048 B | 10 Hz | HC-SR04 ultrasonic poll → CAN `0x400`. |
| `watchdog` | **0** | 1 | 2048 B | 10 Hz | Checks command staleness. On trip, zeroes setpoint. |
| `diag` | **1** | 1 | 2048 B | 1 Hz | Heap, mode, brake state, gear, TEC/REC → CAN `0x600`. |
| `heartbeat` | **0** | 1 | 2048 B | 2 Hz | CAN `0x7FF` alive signal. |

### New/modified tasks vs previous revision

| Task | Change |
|------|--------|
| `gear` | **New** — reads 72V gear lines, drives gear output, publishes CAN 0x121 |
| `lighting` | **New** — signal lights L/R, mode lights Auto/Manual |
| `throttle` | **Modified** — now bidirectional: ADC read + PWM generate for 0–5V |
| `motor` | **Modified** — no longer PWM+DIR pins; writes 0–5V analog via LEDC PWM duty |
| `brake` | **Modified** — no longer GPIO output; SEB CAN command via `syntree_tx_task` |
| `safety` | **Modified** — now also forces gear to Neutral on ESTOP |

### Priority reasoning

- **5 (safety + CAN RX, Core 0):** Life-critical. E-stop within 1 ms. CAN RX drains hardware FIFOs. All pinned to Core 0.
- **4 (dispatch + mode + can_tx on Core 0; control + motor + syntree_tx on Core 1):** Queue-bound, efficient round-robin. Motor and control at 100 Hz on Core 1 never preempt safety on Core 0.
- **3 (throttle + gear + brake + lighting, Core 1):** Vehicle I/O — one notch below motor/control. Produce/consume data through atomics/queues.
- **2 (obstacle, Core 1):** Ultrasonic poll blocks up to 30 ms. Pinned Core 1 at lower priority.
- **1 (watchdog on Core 0; heartbeat on Core 0; diag on Core 1):** Background. Jitter-tolerant.

---

## Internal data flow

```
Public CAN bus
    │
    ▼
can_rx_task0 ──► can_rx_queue ──► dispatch_task
                                      │
                        0x300: ┌──────┼──────┐ 0x302: light_cmd
                          DriveCmd    │          (atomic store)
                               │      │
                               ▼      │
                          cmd_queue   │
                               │      │
                               ▼      ▼
                          control_task (100 Hz, Core 1)
                          ┌────────────────────────────┐
                          │ physics.resolve(cmd)       │
                          │   → steer_angle_mdeg       │──► syntree_tx_task → CAN 0x169
                          │   → motor_speed_mmps       │
                          │ obstacle_limit(speed)      │
                          │ pid.update(target, meas)   │
                          │   → throttle_effort_0_5V   │
                          │ gear_select(speed, mode)   │──► gear_task → drive gear lines
                          │ brake_arbitration          │
                          │   → brake_pressure_kpa     │──► syntree_tx_task → SEB CAN
                          └───────┬────────────────────┘
                                  │
                          setpoint_queue (overwrite)
                                  │
                                  ▼
                          motor_task (100 Hz, Core 1)
                          ┌────────────────────────────┐
                          │ if ESTOP → 0V output       │
                          │ else → LEDC PWM duty       │
                          │   → RC filter → 0–5V       │──► Motor controller
                          └────────────────────────────┘

Throttle input path (Manual mode):
  Physical throttle (0–5V) → voltage divider → ADC GPIO10
      │
      ▼
  throttle_task (100 Hz) → atomic store → CAN 0x120 (telemetry)
      │
      ▼
  control_task reads atomic, passes through to motor_task
      │
      ▼
  motor_task → LEDC PWM → 0–5V → Motor controller
```

### ESTOP path (bypasses all queues)

```
E-stop button (GPIO1 LOW) ──► safety_task (Core 0, prio 5)
    │                              │
    │    CAN 0x001 ────────────────┤
    │    HB timeout ───────────────┤
    │                              │
    │    ┌─────────────────────────┘
    │    ▼
    ├──► mode_set(ESTOP)   [atomic store]
    │
    ├──► motor_stop()      [0–5V output = 0V]
    ├──► brake_engage()    [SEB CAN: 100%]
    ├──► gear_neutral()    [all gear outputs OFF]
    ├──► steering_disable()[EPS-C disable]
    │
    └──► HARDWARE E-stop relay cuts motor power (independent of MCU)
```

---

## Hardware pin assignments (single ESP32-S3)

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| **Public CAN** | | | |
| CAN0 TX | 5 | Output | To SN65HVD230 TXD |
| CAN0 RX | 4 | Input | From SN65HVD230 RXD |
| **Private CAN (Syntree)** | | | |
| CAN1 TX | 9 | Output | To second SN65HVD230 TXD |
| CAN1 RX | 8 | Input | From second SN65HVD230 RXD |
| **Throttle (bidirectional 0–5V)** | | | |
| Throttle ADC | 10 | Input | 0–5V via 2:1 divider → 0–3.3V. ADC1_CH9 |
| Throttle DAC (MCP4725) | — | I2C | 12-bit DAC, 0–5V out. I2C addr 0x60 on bus with IMU |
| **Gear selector (72V discrete I/O)** | | | |
| Gear D IN | 13 | Input | TLP281 optoisolator ch1 → GPIO, active-low |
| Gear S IN | 26 | Input | TLP281 optoisolator ch2 → GPIO, active-low |
| Gear R IN | 14 | Input | TLP281 optoisolator ch3 → GPIO, active-low |
| Gear D OUT | 6 | Output | GPIO → transistor → 72V switch |
| Gear S OUT | 42 | Output | GPIO → transistor → 72V switch |
| Gear R OUT | 43 | Output | GPIO → transistor → 72V switch |
| **Safety inputs** | | | |
| E-stop button | 1 | Input | Active-low, internal pull-up |
| Brake lever | 2 | Input | Active-low, internal pull-up |
| **Mode switch** | | | |
| Mode switch | 11 | Input | Pull-up (Manual), GND (Auto) |
| **Obstacle sensor** | | | |
| Ultrasonic TRIG | 15 | Output | HC-SR04 trigger (10 µs pulse) |
| Ultrasonic ECHO | 16 | Input | HC-SR04 echo |
| **Encoder** | | | |
| Encoder A | 3 | Input | Speed feedback (PCNT) |
| Encoder B | 17 | Input | Speed feedback (PCNT) |
| **IMU** | | | |
| IMU SDA | 19 | I/O | I2C data (MPU6050 or similar) |
| IMU SCL | 20 | Output | I2C clock |
| **External watchdog** | | | |
| Watchdog toggle | 21 | Output | Toggled by safety_task every tick |
| **12V PSU** | | | |
| 12V PSU control | 44 | Output | GPIO → relay driver → 12V rail |
| **Lighting (12V via transistor)** | | | |
| Signal Left | 40 | Output | GPIO → transistor → 12V lamp |
| Signal Right | 41 | Output | GPIO → transistor → 12V lamp |
| Mode: Auto | 38 | Output | GPIO → transistor → 12V indicator |
| Mode: Manual | 39 | Output | GPIO → transistor → 12V indicator |
| **Steering servo (fallback)** | | | |
| Servo PWM | 18 | Output | LEDC, 50 Hz. Fallback only; primary steering via CAN |

---

## Configuration constants

```cpp
// ── CAN ────────────────────────────────────────────────────
constexpr int  kCanBitrateHz            = 500'000;
constexpr int  kCan0TxGpio              =       5;
constexpr int  kCan0RxGpio              =       4;
constexpr int  kCan1TxGpio              =       9;
constexpr int  kCan1RxGpio              =       8;
constexpr bool kSyntreeCanOutputEnabled = false;

// ── vehicle geometry (mm) ──────────────────────────────────
constexpr float kWheelbaseMM     = 1500.0f;
constexpr float kTrackWidthMM    =  800.0f;
constexpr float kWheelRadiusMM   =  200.0f;

// ── throttle — bidirectional 0–5V ──────────────────────────
constexpr int  kThrottleAdcGpio       =      10;
constexpr int  kThrottleAdcChannel    =       9;   // ADC1_CH9
constexpr int  kThrottleDacI2cAddr    =    0x60;   // MCP4725 on I2C0 (shares bus with IMU)
constexpr unsigned kThrottleDeadZone  =     200;
constexpr int  kThrottleMaxSpeedMmps  =    3000;

// ── gear selector — 72V discrete I/O ───────────────────────
constexpr int  kGearDGpioIn    =      13;   // TLP281 ch1, active-low
constexpr int  kGearSGpioIn    =      26;   // TLP281 ch2, active-low
constexpr int  kGearRGpioIn    =      14;   // TLP281 ch3, active-low
constexpr int  kGearDGpioOut   =       6;   // GPIO → transistor → 72V
constexpr int  kGearSGpioOut   =      42;   // (was 34 — OPI PSRAM conflict)
constexpr int  kGearRGpioOut   =      43;   // (was 35 — OPI PSRAM conflict)

// ── steering (Syntree EPS-C via CAN) ───────────────────────
constexpr float kSteerLimitDeg       =  45.0f;
constexpr float kSteerSlewRateDegS   = 180.0f;

// ── brake (Syntree SEB via CAN) ────────────────────────────
// No GPIO — brake is CAN-controlled. Fallback relay on GPIO14 reserved.

// ── safety inputs ──────────────────────────────────────────
constexpr int  kEstopGpio         =       1;
constexpr int  kBrakeLeverGpio    =       2;

// ── mode switch ────────────────────────────────────────────
constexpr int  kModeSwitchGpio    =      11;

// ── lighting — 12V via transistor ──────────────────────────
constexpr int  kSignalLeftGpio    =      40;
constexpr int  kSignalRightGpio   =      41;
constexpr int  kModeAutoLightGpio =      38;
constexpr int  kModeManualLightGpio=     39;

// ── 12V PSU ────────────────────────────────────────────────
constexpr int  k12vPsuGpio        =      44;   // (was 36 — OPI PSRAM conflict on N16R8)

// ── speed limits ───────────────────────────────────────────
constexpr int  kMaxSpeedFwdMmps    =  3000;
constexpr int  kMaxSpeedRevMmps    =   500;
constexpr int  kLowSpeedThreshMmps =    50;

// ── PID ────────────────────────────────────────────────────
constexpr float kPidKp           =  1.0f;
constexpr float kPidKi           =  0.1f;
constexpr float kPidKd           =  0.05f;
constexpr float kPidMaxIntegral  = 500.0f;

// ── obstacle ───────────────────────────────────────────────
constexpr unsigned kObstacleStopDistMM  =   300;
constexpr unsigned kObstacleClearDistMM =  3000;
constexpr int      kObstacleTrigGpio    =    15;
constexpr int      kObstacleEchoGpio    =    16;

// ── encoder ────────────────────────────────────────────────
constexpr int  kEncoderAGpio       =       3;
constexpr int  kEncoderBGpio       =      17;

// ── IMU ────────────────────────────────────────────────────
constexpr int  kImuSdaGpio         =      19;
constexpr int  kImuSclGpio         =      20;

// ── external watchdog ──────────────────────────────────────
constexpr int  kExtWatchdogGpio    =      21;

// ── timing ─────────────────────────────────────────────────
constexpr int  kControlLoopHz       = 100;
constexpr int  kHeartbeatIntervalMs = 500;
constexpr int  kJetsonHbTimeoutMs   = 1500;
constexpr int  kCmdStaleTimeoutMs   =  200;
constexpr int  kSafetyCheckHz       =   20;
```

---

## Safety architecture

### Multi-layer defense (defense in depth)

```
Layer 1 — HARDWARE (works even if MCU is dead)
  E-stop button → NC relay → physically disconnects motor power + engages brake
  Response: ~10 ms. No software involved.

Layer 2 — FIRMWARE SAFETY TASK (Core 0, priority 5)
  safety_task polls E-stop GPIO @ 20 Hz, checks heartbeats.
  On fault: mode_set(ESTOP) → motor 0V + brake 100% + gear Neutral.
  Response: <1 ms (one scheduler tick).

Layer 3 — CONTROL LOOP GUARD (Core 1, priority 4)
  motor_task checks g_mode == ESTOP before every throttle output write.
  control_task skips physics pipeline when mode != Auto.

Layer 4 — EXTERNAL WATCHDOG IC
  If safety_task fails to toggle GPIO21 within 100 ms, watchdog resets the MCU.
  On reset: throttle = 0V, gear = Neutral, brake engaged, servo disabled.
  Startup in Manual mode (safe default).
```

### ESTOP safe-state summary

| Output | ESTOP state |
|--------|-------------|
| Throttle (0–5V) | 0V |
| Gear lines | All OFF (Neutral) |
| Brake (SEB CAN) | 100% pressure |
| Steering (EPS-C CAN) | Disabled |
| Mode lights | Both OFF |
| Signal lights | Unchanged (preserve visibility) |
| 12V PSU | OFF (optional, configurable) |

---

## Error handling

| Failure | Detection | Response |
|---------|-----------|----------|
| E-stop pressed | GPIO1 LOW | `mode_set(ESTOP)` → all outputs safe state |
| CAN bus-off (public) | TWAI0 TEC > 255 | Log, auto-recovery via TWAI hardware |
| CAN bus-off (private) | TWAI1 TEC > 255 | Log, EPS/SEB commands stop → actuators safe state |
| Heartbeat timeout (Jetson) | >1500 ms no 0x7FF | `mode_set(ESTOP)` (AUTO mode only) |
| Command stale | >200 ms no 0x300 | Zero setpoint → controlled coast to stop |
| Obstacle timeout | Echo pulse >30 ms | Distance = UINT32_MAX (no reading → pass-through) |
| Encoder missing | speed returns 0 | PID operates on stale measurement (I-term saturates) |
| ADC read failure | raw = 0 | Throttle = 0 (fail-safe) |
| Gear input invalid | Multiple lines hot or none hot | Hold last valid gear, flag diag |
| Queue full (can_rx) | `xQueueSend` false | Frame dropped; TWAI FIFO provides back-pressure |
| Mode switch bouncing | GPIO @ 10 Hz | Natural debounce via 100 ms polling interval |
| External watchdog timeout | GPIO21 stuck | MCU hard reset → Manual, motor off, gear N |

---

## Startup sequence

```
 1. safety_init()            — E-stop + brake lever GPIOs, pull-ups
 2. can0_init()              — Public TWAI driver, start CAN
 3. can1_init()              — Private TWAI driver for Syntree bus
 4. mode_init()              — Mode switch GPIO, default = Manual
 5. throttle_init()          — ADC1_CH9 input + LEDC PWM output, 0V default
 6. gear_init()              — Gear input dividers + output drivers, all OFF
 7. lighting_init()          — Signal + mode light GPIOs, all OFF
 8. psu_init()               — 12V PSU GPIO, OFF initially
 9. obstacle_init()          — TRIG/ECHO GPIOs
10. encoder_init()           — PCNT on Encoder A/B
11. imu_init()               — I2C on SDA/SCL
12. pid_init()               — Load PID gains
13. watchdog_init()          — Arm command + external watchdog
14. Create queues             — can_rx(16), cmd(4), setpoint(4)
15. Create 17 tasks           — Core-pinned per task layout
16. ESP_LOGI("Ready")        — Mode = Manual, gear = Neutral
```

---

## Build

```bash
cd rt-esp32
pio run              # build
pio run -t upload    # flash to ESP32-S3
pio device monitor   # serial console
```

| Parameter | Value |
|-----------|-------|
| Target | `esp32-s3-devkitc-1` |
| Module | ESP32-S3-**N16R8** (16 MB Flash, 8 MB Octal PSRAM) |
| MCU | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Framework | `espidf` (ESP-IDF with FreeRTOS) |
| FreeRTOS tick | 1000 Hz |
| PSRAM | 8 MB Octal (OPI) — **consumes GPIO 33–37** |
| Public CAN bitrate | 500 kbit/s |
| Private CAN bitrate | 500 kbit/s |
| CAN transceiver | 2× SN65HVD230 (external) |

---

## Design principles

1. **Tasks communicate through queues, never shared state.** No mutexes, no semaphores. Exceptions: `g_mode` (atomic enum) and diagnostic atomics.
2. **ESTOP bypasses the queue pipeline.** The safety task (Core 0, prio 5) writes directly to throttle/brake/gear/steering. No queue, no delay.
3. **Core 0 = safety, Core 1 = computation + I/O.** Safety-critical tasks pinned to Core 0 with highest priority. Vehicle I/O and control loop pinned to Core 1.
4. **One CAN ID = one sender.** No duplicate IDs on a given bus (except heartbeat 0x7FF).
5. **Lower CAN ID = higher bus priority.** Safety IDs (0x00X) always win arbitration.
6. **All multi-byte CAN fields are big-endian (MSB first).**
7. **Syntree CAN output is disabled by default** (`kSyntreeCanOutputEnabled = false`). Enable only after DBC/protocol verification.
8. **72V circuits are galvanically isolated** from the ESP32 via voltage dividers (input) and optocouplers/relays (output). No 72V reaches the MCU.

---

*See also: [[notes/rtos-architecture]] for the FreeRTOS task design, [[notes/can-addressing-for-etrike]] for the CAN ID scheme, [[notes/physics-model]] for the tricycle kinematics, [[notes/can-protocol]] for CAN protocol theory.*
