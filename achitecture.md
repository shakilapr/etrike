# E-Trike System Architecture — Unified Single-ESP32 Design

Two-node distributed control: **Jetson Orin NX** (ROS 2 perception/planning) and a single **ESP32-S3** (realtime physics, steering, safety, and motor actuation). Communication over dual CAN bus at 500 kbit/s.

> **Design revision (2026-06):** The original dual-ESP32 (RT + SYS) layout was merged into one ESP32-S3. The inter-MCU UART link and its protocol are eliminated; RT-to-SYS setpoints now flow through a direct FreeRTOS queue. See [[#safety-architecture]] for the safety rationale and [[notes/rtos-architecture]] for the task-level design.

## Node topology

```
┌──────────────────────────────────────────────────────────────────┐
│                    Public CAN Bus (500 kbit/s)                    │
│                                                                   │
│  ┌──────────────┐            ┌──────────────────────────────┐    │
│  │   Jetson     │            │     SINGLE ESP32-S3          │    │
│  │   Orin NX    │            │                              │    │
│  │              │            │  Core 0: Safety & CAN RX     │    │
│  │  ROS 2       │◄══════════►│  Core 1: Physics & Control   │    │
│  │  Planning    │   CAN      │                              │    │
│  │  Bridge      │            │  ┌─ safety_task (E-stop, HB)│    │
│  └──────────────┘            │  ├─ can_rx / dispatch       │    │
│                              │  ├─ mode_task (switch poll) │    │
│                              │  ├─ control_task (physics + │    │
│                              │  │     PID + steering)      │    │
│                              │  ├─ motor_task (PWM output) │    │
│                              │  ├─ brake_task (FSM)        │    │
│                              │  ├─ throttle_task (ADC)     │    │
│                              │  ├─ obstacle_task (HC-SR04) │    │
│                              │  ├─ syntree_tx_task (EPS+   │    │
│                              │  │     SEB private CAN)     │    │
│                              │  ├─ watchdog_task           │    │
│                              │  ├─ diagnostics_task        │    │
│                              │  └─ heartbeat_task          │    │
│                              └──────────┬───────────────────┘    │
│                                         │                        │
│                              Private CAN Bus (500 kbit/s)        │
│                                         │                        │
│                              ┌──────────┴───────────────────┐    │
│                              │  Syntree EPS-C  │ Syntree SEB│    │
│                              │  (steering)     │  (brake)   │    │
│                              └──────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
```

## CAN message catalog

### Public bus: Jetson ↔ ESP32-S3

| ID | Name | Sender | Receiver | DLC | Payload | Rate |
|----|------|--------|----------|-----|---------|------|
| `0x001` | SAFETY_ESTOP | Any | ESP32 | 0 | (none) | On event |
| `0x011` | SAFETY_STATUS | ESP32 | Jetson | 2 | u8 estop, u8 hb_ok | 5 Hz |
| `0x120` | THROTTLE_POS | ESP32 | Jetson | 2 | i16 speed_mmps | 100 Hz |
| `0x210` | RT_STATE_REPORT | ESP32 | Jetson | 3 | u8 mode, u8 steer_ok, u8 rev | 10 Hz |
| `0x220` | RT_PID_FEEDBACK | ESP32 | Jetson | 6 | i16 sp, i16 meas, i16 out | 10 Hz |
| `0x300` | HOST_DRIVE_CMD | Jetson | ESP32 | 8 | i32 speed, i32 yaw | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQUEST | Jetson | ESP32 | 4 | i32 brake_pressure_kpa | On demand |
| `0x400` | OBSTACLE_DIST | ESP32 | Jetson | 4 | u32 distance_mm | 10 Hz |
| `0x600` | SYS_DIAG | ESP32 | Jetson | 8 | diag struct | 1 Hz |
| `0x7FF` | HEARTBEAT | Both | Both | 0 | (none) | 2 Hz |

### Private bus: ESP32-S3 → Syntree actuators

| ID | Name | Sender | Receiver | DLC | Payload | Rate |
|----|------|--------|----------|-----|---------|------|
| `0x169` | EPS_COMMAND | ESP32 | EPS-C | 8 | steer angle + checksum | 50 Hz |
| `0x201` | EPS_STATUS | EPS-C | ESP32 | 8 | angle feedback + faults | 10 Hz |
| `0x7B0` | SEB_COMMAND | ESP32 | SEB | 8 | brake pressure + checksum | 50 Hz |
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

| Mode | Behavior |
|------|----------|
| **Manual** | Rider steers mechanically. Throttle via ADC → motor PWM. Steering servo disabled. |
| **Auto** | Jetson sends `0x300` → physics model resolves kinematics → PID computes effort → motor PWM + steering servo active. |
| **Estop** | Motor PWM = 0, brake engaged, steering servo disabled. Exit requires power-cycle or explicit CAN command (TBD). |

**ESTOP is the only state that bypasses the queue pipeline.** The `safety_task` at priority 5 calls `motor_stop()` and `brake_engage()` directly — no queue, no delay.

---

## Responsibility map (unified)

| Concern | Jetson | ESP32-S3 |
|---------|--------|----------|
| Perception / planning | ✓ | |
| ROS 2 → CAN bridge | ✓ | |
| Tricycle kinematics | | **Core 1** |
| Speed PID (100 Hz) | | **Core 1** |
| Steering servo PWM (AUTO) | | **Core 1** |
| Obstacle speed limiting | | **Core 1** |
| Command staleness watchdog | | **Core 1** |
| Motor PWM + direction | | **Core 0** |
| Manual throttle ADC | | **Core 0** |
| E-stop GPIO + button | | **Core 0 (prio 5)** |
| Brake lever + actuator | | **Core 0** |
| Heartbeat monitoring | | **Core 0 (prio 5)** |
| Mode switch reading | | **Core 0** |
| Syntree EPS-C/SEB CAN | | **Core 1** |
| System diagnostics (1 Hz) | | **Core 1** |

---

## Core pinning & task layout

```
Core 0 — Safety & CAN I/O          Core 1 — Computation & Actuation
─────────────────────────────────  ─────────────────────────────────
Priority                            Priority
   5  safety_task      20 Hz           4  control_task     100 Hz
   5  can_rx_task0     event           4  motor_task       100 Hz
   5  can_rx_task1     event           4  syntree_tx_task   50 Hz
   4  dispatch_task    event           3  throttle_task    100 Hz
   4  mode_task        10 Hz           3  brake_task        20 Hz
   4  can_tx_task      5 Hz            2  obstacle_task     10 Hz
   1  heartbeat_task   2 Hz            1  diagnostics_task   1 Hz
   1  watchdog_task    10 Hz
```

| Task | Core | Priority | Stack | Period | Behavior |
|------|------|----------|-------|--------|----------|
| `safety` | **0** | **5** | 2048 B | 20 Hz | **Life-critical.** Polls E-stop GPIO + heartbeats. Calls `motor_stop()` and `brake_engage()` directly on fault. Toggles external watchdog GPIO. |
| `can_rx0` | **0** | 5 | 4096 B | event | Public CAN bus — blocks on `twai_receive()`, enqueues frames to `can_rx_queue`. |
| `can_rx1` | **0** | 5 | 4096 B | event | Private CAN bus — blocks on `twai_receive()` for Syntree status frames. |
| `dispatch` | **0** | 4 | 3072 B | event | Parses `can_rx_queue`. Routes `0x300` → `cmd_queue`, `0x301` → atomic, ESTOP IDs → `mode_set()`. |
| `mode` | **0** | 4 | 2048 B | 10 Hz | Reads mode switch GPIO. Calls `mode_set()`. Ignored in ESTOP. |
| `can_tx` | **0** | 4 | 3072 B | 5 Hz | Periodic safety status `0x011`. |
| `control` | **1** | 4 | 4096 B | 100 Hz | `vTaskDelayUntil`. Physics resolve → obstacle limit → PID update → `setpoint_queue`. |
| `motor` | **1** | 4 | 2048 B | 100 Hz | Reads `setpoint_queue`. Writes LEDC PWM + direction GPIO. In ESTOP, calls `motor_stop()`. |
| `syntree_tx` | **1** | 4 | 3072 B | 50 Hz | Sends EPS-C and SEB command frames on private CAN bus (when enabled). |
| `throttle` | **1** | 3 | 1536 B | 100 Hz | Reads ADC1_CH5, maps to speed, stores to atomic for CAN `0x120`. |
| `brake` | **1** | 3 | 1536 B | 20 Hz | Brake FSM: ESTOP → engage, lever → engage, else → release. |
| `obstacle` | **1** | 2 | 2048 B | 10 Hz | HC-SR04 ultrasonic poll → CAN `0x400`. |
| `watchdog` | **0** | 1 | 2048 B | 10 Hz | Checks command staleness. On trip, zeroes setpoint. |
| `diag` | **1** | 1 | 2048 B | 1 Hz | Heap, mode, brake state, TEC/REC → CAN `0x600`. |
| `heartbeat` | **0** | 1 | 2048 B | 2 Hz | CAN `0x7FF` alive signal. |

### Priority reasoning

- **5 (safety + CAN RX):** Safety task is life-critical — E-stop response within one scheduler tick (1 ms). CAN RX drains TWAI FIFOs before overflow. Non-negotiable at highest priority. Pin to Core 0.
- **4 (dispatch + mode + motor + control + syntree):** Dispatch and mode feed commands. Motor and control consume them. At 100 Hz, all these tasks block on queues/delays most of the time, so FreeRTOS round-robins efficiently. Control and motor are on Core 1 to avoid ever preempting safety.
- **3 (throttle + brake):** One notch below motor/control — they produce data through atomics/queues, never direct calls.
- **2 (obstacle):** Ultrasonic polling can block for up to 30 ms. Pinned to Core 1 at lower priority.
- **1 (watchdog, heartbeat, diag):** Background housekeeping. Jitter of tens of ms is harmless.

---

## Internal data flow

```
Public CAN bus
    │
    ▼
can_rx_task0 ──► can_rx_queue ──► dispatch_task
                                      │
                        0x300: ┌──────┤
                          DriveCmd    │ 0x301: brake_request_kpa
                               │      │        (atomic store)
                               ▼      │
                          cmd_queue   │
                               │      │
                               ▼      ▼
                          control_task (100 Hz, Core 1)
                          ┌────────────────────────┐
                          │ physics.resolve(cmd)   │
                          │   → steer_angle_mdeg   │──► steering.tick()
                          │   → motor_speed_mmps   │
                          │ obstacle_limit(speed)  │
                          │ pid.update(target,meas)│
                          │   → motor_effort_pwm   │
                          │ brake_arbitration      │
                          │   → brake_pressure_kpa │
                          └───────┬────────────────┘
                                  │
                          setpoint_queue (overwrite)
                                  │
                                  ▼
                          motor_task (100 Hz, Core 1)
                          ┌────────────────────────┐
                          │ if ESTOP → motor_stop  │
                          │ else → set_effort(pwm) │
                          │   → LEDC PWM + DIR pin │
                          └────────────────────────┘
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
    ├──► mode_set(ESTOP)  [atomic store]
    │
    ├──► motor_stop()     [PWM=0, DIR=0]
    ├──► brake_engage()   [GPIO8 HIGH]
    ├──► steering.disable()[servo limp]
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
| **Motor** | | | |
| Motor PWM | 6 | Output | LEDC, 20 kHz, 13-bit |
| Motor DIR | 7 | Output | HIGH=forward, LOW=reverse |
| **Brake** | | | |
| Brake actuator | 14 | Output | HIGH=engaged (solenoid/relay) |
| **Safety inputs** | | | |
| E-stop button | 1 | Input | Active-low, internal pull-up |
| Brake lever | 2 | Input | Active-low, internal pull-up |
| **Throttle** | | | |
| Throttle ADC | 10 | Input | ADC1_CH5, 12-bit, 0–3.3V |
| **Mode switch** | | | |
| Mode switch | 11 | Input | Pull-up (Manual), GND (Auto) |
| **Steering servo** | | | |
| Servo PWM | 18 | Output | LEDC, 50 Hz, 500–2500 µs |
| **Obstacle sensor** | | | |
| Ultrasonic TRIG | 15 | Output | HC-SR04 trigger (10 µs pulse) |
| Ultrasonic ECHO | 16 | Input | HC-SR04 echo (pulse width → distance) |
| **Encoder** | | | |
| Encoder A | 3 | Input | Speed feedback (PCNT) |
| Encoder B | 17 | Input | Speed feedback (PCNT) |
| **External watchdog** | | | |
| Watchdog toggle | 21 | Output | Toggled by safety_task every tick |

---

## Configuration constants

```cpp
// ── CAN ────────────────────────────────────────────────────
constexpr int kCanBitrateHz       = 500'000;
constexpr int kCan0TxGpio         =       5;
constexpr int kCan0RxGpio         =       4;
constexpr int kCan1TxGpio         =       9;  // Syntree private bus
constexpr int kCan1RxGpio         =       8;
constexpr bool kSyntreeCanOutputEnabled = false;  // enable after DBC verification

// ── vehicle geometry (mm) ──────────────────────────────────
constexpr float kWheelbaseMM     = 1500.0f;
constexpr float kTrackWidthMM    =  800.0f;
constexpr float kWheelRadiusMM   =  200.0f;

// ── steering actuator ──────────────────────────────────────
constexpr float kSteerLimitDeg       =  45.0f;
constexpr int   kSteerServoMinUs     =   500;
constexpr int   kSteerServoMaxUs     =  2500;
constexpr int   kSteerServoCenterUs  =  1500;
constexpr float kSteerSlewRateDegS   = 180.0f;
constexpr int   kSteerPwmFreqHz      =    50;
constexpr int   kSteerServoGpio      =    18;

// ── motor driver ───────────────────────────────────────────
constexpr int kMotorPwmGpio      =       6;
constexpr int kMotorDirGpio      =       7;
constexpr int kMotorPwmFreqHz    =  20'000;
constexpr int kMotorMaxSpeedMmps =   3'000;

// ── brake ──────────────────────────────────────────────────
constexpr int kBrakeGpio         =      14;

// ── safety inputs ──────────────────────────────────────────
constexpr int kEstopGpio         =       1;   // active-low
constexpr int kBrakeLeverGpio    =       2;   // active-low

// ── throttle ADC ───────────────────────────────────────────
constexpr int      kThrottleAdcChannel  = 5;   // ADC1_CH5 → GPIO10
constexpr unsigned kThrottleDeadZone    = 200;
constexpr int      kThrottleMaxSpeedMmps = 3000;

// ── mode switch ────────────────────────────────────────────
constexpr int kModeSwitchGpio    =      11;

// ── speed limits ───────────────────────────────────────────
constexpr int kMaxSpeedFwdMmps    =  3000;     // 3 m/s ≈ 10.8 km/h
constexpr int kMaxSpeedRevMmps    =   500;     // 0.5 m/s
constexpr int kLowSpeedThreshMmps =    50;     // freeze steering below

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
constexpr int kEncoderAGpio       =       3;
constexpr int kEncoderBGpio       =      17;

// ── external watchdog ──────────────────────────────────────
constexpr int kExtWatchdogGpio    =      21;

// ── timing ─────────────────────────────────────────────────
constexpr int kControlLoopHz       = 100;
constexpr int kHeartbeatIntervalMs = 500;
constexpr int kHeartbeatTimeoutMs  = 200;
constexpr int kSafetyCheckHz       =  20;
constexpr int kCmdStaleTimeoutMs   = 200;
```

---

## Safety architecture

### Multi-layer defense (defense in depth)

```
Layer 1 — HARDWARE (works even if MCU is dead)
  E-stop button → NC relay → physically disconnects motor power + engages brake solenoid
  Response: ~10 ms. No software involved.

Layer 2 — FIRMWARE SAFETY TASK (Core 0, priority 5)
  safety_task polls E-stop GPIO @ 20 Hz, checks heartbeats.
  On fault: mode_set(ESTOP) → motor_stop() + brake_engage() via atomic flags.
  Response: <1 ms (one scheduler tick).

Layer 3 — CONTROL LOOP GUARD (Core 1, priority 4)
  motor_task checks g_mode == ESTOP before every PWM write.
  control_task skips physics pipeline when mode != Auto.
  Defense-in-depth: if safety task somehow fails, motor loop catches it.

Layer 4 — EXTERNAL WATCHDOG IC
  If safety_task fails to toggle GPIO21 within 100 ms, watchdog resets the MCU.
  On reset: motor PWM = 0 (default GPIO state), brake engages, servo disables.
  Startup in Manual mode (safe default).
```

### What changed from dual-MCU

| Property | Dual-MCU | Single-MCU | Mitigation |
|----------|----------|------------|------------|
| Fault isolation | Two physical chips | One chip, two cores | Safety pinned to Core 0, control to Core 1. MPU-protected memory regions. |
| Heartbeat monitoring | Each MCU monitors the other | N/A (single MCU) | External watchdog IC monitors firmware liveness. Hardware E-stop relay is independent. |
| Memory corruption | Bounded to one chip | Potentially cross-core | FreeRTOS MPU: safety_task stack and data in protected region. Stack canary on all tasks. |
| CAN bus failure | Two TWAI controllers → bus-off on one doesn't affect other | Two TWAI controllers — identical isolation | Same hardware — no regression. |
| Power supply fault | Two regulators | One regulator | Hardware E-stop relay is NC (normally-closed) — power loss = motor disconnect = safe state. |

**The primary safety guarantee is unchanged:** the E-stop button physically disconnects motor power through a hardware relay. Software E-stop is defense-in-depth, not the primary layer.

---

## Error handling

| Failure | Detection | Response |
|---------|-----------|----------|
| E-stop pressed | GPIO1 LOW | `mode_set(ESTOP)` → motor stop, brake engage, steering disable |
| CAN bus-off (public) | TWAI0 TEC > 255 | Log error, auto-recovery via TWAI hardware |
| CAN bus-off (private) | TWAI1 TEC > 255 | Log error, EPS/SEB commands stop → actuators enter safe state |
| Heartbeat timeout (Jetson) | >200 ms since last 0x7FF | `mode_set(ESTOP)` (AUTO mode only) |
| Command stale | >200 ms since last 0x300 | Zero setpoint → controlled stop |
| Obstacle timeout | Echo pulse >30 ms | Distance = UINT32_MAX (no reading → passes through) |
| Encoder missing | `encoder_get_speed_mmps()` returns 0 | PID operates on stale measurement (I-term saturates) |
| ADC read failure | `adc1_get_raw()` returns 0 | Throttle = 0 (fail-safe) |
| Queue full (can_rx) | `xQueueSend` returns false | Frame dropped; TWAI FIFO provides back-pressure |
| Mode switch bouncing | GPIO reads @ 10 Hz | Natural debounce via 100 ms polling interval |
| External watchdog timeout | safety_task fails to toggle GPIO21 | MCU hard reset → safe state (Manual mode, motor off) |

---

## Startup sequence

```
 1. safety_init()            — Configure E-stop + brake lever GPIOs with pull-ups
 2. can0_init()              — Install public TWAI driver, start CAN
 3. can1_init()              — Install private TWAI driver for Syntree bus
 4. mode_init()              — Configure mode switch GPIO, default = Manual
 5. throttle_init()          — Configure ADC1_CH5, 12-bit, 0–3.3V range
 6. motor_init()             — Configure LEDC PWM 20 kHz + direction GPIO, stop motor
 7. brake_init()             — Configure brake GPIO, release brake
 8. steering_init()          — Configure LEDC PWM 50 Hz, disable servo
 9. obstacle_init()          — Configure TRIG/ECHO GPIOs
10. pid_init()               — Load PID gains from config
11. watchdog_init()          — Record initial timestamp, arm external watchdog
12. Create queues             — can_rx(16), cmd(4), setpoint(4)
13. Create 15 tasks           — See task layout above, core-pinned
14. ESP_LOGI("Ready")        — Mode = Manual
```

---

## Build

```bash
cd esp32
pio run              # build
pio run -t upload    # flash to ESP32-S3
pio device monitor   # serial console
```

| Parameter | Value |
|-----------|-------|
| Target | `esp32-s3-devkitc-1` |
| MCU | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Framework | `espidf` (ESP-IDF with FreeRTOS) |
| FreeRTOS tick | 1000 Hz |
| Public CAN bitrate | 500 kbit/s |
| Private CAN bitrate | 500 kbit/s |
| CAN transceiver | SN65HVD230 (external) |

---

## Design principles

1. **Tasks communicate through queues, never shared state.** No mutexes, no semaphores. Each queue is a thread-safe pipe. Exceptions: `g_mode` (atomic enum, fits in one word) and diagnostic atomics.

2. **ESTOP bypasses the queue pipeline.** The safety task (Core 0, prio 5) writes directly to motor/brake/steering. No queue, no delay.

3. **Core 0 = safety, Core 1 = computation.** Safety-critical tasks pinned to Core 0 with highest priority. Control loop pinned to Core 1. If Core 1 crashes, Core 0 still runs safety + CAN RX + external watchdog toggle.

4. **One CAN ID = one sender.** No duplicate IDs on a given bus (except heartbeat 0x7FF).

5. **Lower CAN ID = higher bus priority.** Safety IDs (0x00X) always win arbitration.

6. **All multi-byte CAN fields are big-endian (MSB first).**

7. **Syntree CAN output is disabled by default** (`kSyntreeCanOutputEnabled = false`). Enable only after DBC/protocol verification to prevent unintended actuator commands.

---

*See also: [[notes/rtos-architecture]] for the FreeRTOS task design, [[notes/can-addressing-for-etrike]] for the CAN ID scheme, [[notes/physics-model]] for the tricycle kinematics, [[notes/can-protocol]] for CAN protocol theory.*
