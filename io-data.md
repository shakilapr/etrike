# I/O Data — E-Trike Unified Single-ESP32 Design

Two physical nodes, two CAN buses, one ESP32-S3 with dual-core FreeRTOS.

## Topology

```
┌──────────┐   Public CAN (500 kbit/s)   ┌─────────────────────────────────┐   Private CAN (500 kbit/s)   ┌──────────┐
│  Jetson  │◄───────────────────────────►│        SINGLE ESP32-S3          │◄───────────────────────────►│ Syntree  │
│  Orin NX │                             │                                 │                             │ EPS-C    │
│          │                             │  Core 0: Safety + CAN I/O       │                             │ SEB      │
│          │                             │  Core 1: Physics + Actuation    │                             └──────────┘
└──────────┘                             └─────────────────────────────────┘
```

- **Public CAN**: Jetson ↔ ESP32. All telemetry, commands, ESTOP, heartbeats.
- **Private CAN**: ESP32 ↔ Syntree EPS-C (steering) and SEB (brake). Not visible to Jetson.
- **Internal**: Core 0 → Core 1 via FreeRTOS queues (`setpoint_queue`, `obstacle_mm` atomic, `brake_request` atomic). No UART, no inter-MCU protocol.

---

## 1. Public CAN bus — Jetson ↔ ESP32-S3

### 1.1 Jetson → ESP32

| CAN ID | Name | DLC | Payload | Rate | Handler |
|--------|------|-----|---------|------|---------|
| `0x001` | SAFETY_ESTOP | 0 | (empty) | On event | `dispatch_task` → `mode = Estop` |
| `0x300` | HOST_DRIVE_CMD | 8 | `i32 speed_mmps` (0–3), `i32 yaw_rate_mrad_s` (4–7) | ≤100 Hz | `dispatch_task` → `cmd_queue` → `control_task` |
| `0x301` | HOST_BRAKE_REQUEST | 4 | `i32 brake_pressure_kpa` (0–3) | On demand | `dispatch_task` → `g_brake_request_kpa` atomic |
| `0x7FF` | HEARTBEAT | 0 | (empty) | 2 Hz | Jetson alive tracking |

### 1.2 ESP32 → Jetson

| CAN ID | Name | DLC | Payload | Rate | Source |
|--------|------|-----|---------|------|--------|
| `0x011` | SAFETY_STATUS | 2 | `u8 estop_active`, `u8 heartbeat_ok` | 5 Hz | `can_tx_task` from `g_safety` state |
| `0x120` | THROTTLE_POS | 2 | `i16 speed_mmps` | 100 Hz | `throttle_task` (ADC mapped) |
| `0x210` | RT_STATE_REPORT | 3 | `u8 mode`, `u8 steer_valid`, `u8 reversing` | 10 Hz | `control_task` telemetry |
| `0x220` | RT_PID_FEEDBACK | 6 | `i16 speed_sp`, `i16 speed_meas`, `i16 pid_out` | 10 Hz | `control_task` PID debug |
| `0x400` | OBSTACLE_DIST | 4 | `u32 distance_mm` | 10 Hz | `obstacle_task` (HC-SR04) |
| `0x600` | SYS_DIAG | 8 | `u8 mode, u8 brake, u8 hb_ok, u8 estop, u16 heap_kb, u8 tec, u8 rec` | 1 Hz | `diagnostics_task` |
| `0x7FF` | HEARTBEAT | 0 | (empty) | 2 Hz | `heartbeat_task` |

### 1.3 Payload types

```
0x300 HOST_DRIVE_CMD (Jetson → ESP32, 8 bytes)
  [0..3]  int32_t speed_mmps        mm/s,      range [-500, +3000]
  [4..7]  int32_t yaw_rate_mrad_s   millirad/s, range [-3000, +3000]

0x301 HOST_BRAKE_REQUEST (Jetson → ESP32, 4 bytes)
  [0..3]  int32_t brake_pressure_kpa   kPa, 0 = release, RT-arbitrated via max-select

0x011 SAFETY_STATUS (ESP32 → Jetson, 2 bytes)
  [0]     uint8_t estop_active   0/1
  [1]     uint8_t heartbeat_ok   0/1

0x120 THROTTLE_POS (ESP32 → Jetson, 2 bytes)
  [0..1]  int16_t speed_mmps     mm/s, range [0, 3000]

0x210 RT_STATE_REPORT (ESP32 → Jetson, 3 bytes)
  [0]     uint8_t mode          0=Manual, 1=Auto, 2=Estop
  [1]     uint8_t steer_valid   0/1
  [2]     uint8_t reversing     0/1

0x220 RT_PID_FEEDBACK (ESP32 → Jetson, 6 bytes)
  [0..1]  int16_t speed_setpoint_mmps
  [2..3]  int16_t speed_measured_mmps
  [4..5]  int16_t pid_output

0x400 OBSTACLE_DIST (ESP32 → Jetson, 4 bytes)
  [0..3]  uint32_t distance_mm     UINT32_MAX = no reading

0x600 SYS_DIAG (ESP32 → Jetson, 8 bytes)
  [0]     uint8_t  mode          0=Manual, 1=Auto, 2=Estop
  [1]     uint8_t  brake_engaged 0/1
  [2]     uint8_t  heartbeat_ok  0/1
  [3]     uint8_t  estop_active  0/1
  [4..5]  uint16_t free_heap_kb
  [6]     uint8_t  tec           TWAI tx error counter
  [7]     uint8_t  rec           TWAI rx error counter

0x7FF HEARTBEAT (both directions, 0 bytes)
  (empty frame — alive signal)
```

---

## 2. Private CAN bus — ESP32-S3 → Syntree actuators

### 2.1 ESP32 → Syntree

| CAN ID | Name | DLC | Payload | Rate | Source |
|--------|------|-----|---------|------|--------|
| `0x169` | EPS_COMMAND | 8 | steer angle + flags + checksum | 50 Hz | `syntree_tx_task` from `g_target_steer_angle_mdeg` atomic |
| `0x7B0` | SEB_COMMAND | 8 | brake pressure + flags + checksum | 50 Hz | `syntree_tx_task` from `g_target_brake_pressure_kpa` atomic |

### 2.2 Syntree → ESP32

| CAN ID | Name | DLC | Payload | Rate | Handler |
|--------|------|-----|---------|------|---------|
| `0x201` | EPS_STATUS | 8 | angle feedback + fault bits | 10 Hz | `can_rx_task1` → `g_syntree_fault_bits` atomic |
| `0x721` | SEB_STATUS | 8 | pressure feedback + fault bits | 10 Hz | `can_rx_task1` → `g_syntree_fault_bits` atomic |

---

## 3. ESP32-S3 — local hardware I/O

### 3.1 Digital inputs (Core 0 — safety polling)

| Signal | GPIO | Type | Active | Rate | Reader |
|--------|------|------|--------|------|--------|
| E-stop button | 1 | digital input | LOW (pull-up) | 20 Hz | `safety_task` |
| Brake lever | 2 | digital input | LOW (pull-up) | 20 Hz | `safety_task` |
| Mode switch | 11 | digital input | LOW = Auto (pull-up) | 10 Hz | `mode_task` |

### 3.2 Analog inputs (Core 1)

| Signal | GPIO | Type | Range | Rate | Reader |
|--------|------|------|-------|------|--------|
| Throttle | 10 (ADC1_CH5) | 12-bit ADC | 0–4095, dead-zone 200 | 100 Hz | `throttle_task` |

### 3.3 Sensor inputs (Core 1)

| Signal | GPIO | Type | Rate | Reader |
|--------|------|------|------|--------|
| HC-SR04 TRIG | 7 | digital output (10 µs pulse) | 10 Hz | `obstacle_task` |
| HC-SR04 ECHO | 8 | digital input (pulse width → mm) | 10 Hz | `obstacle_task` |
| Encoder A | 1 | pulse counter (PCNT) | — | `control_task` (TBD) |
| Encoder B | 2 | pulse counter (PCNT) | — | `control_task` (TBD) |

### 3.4 Actuator outputs

| Signal | GPIO | Type | Config | Writer |
|--------|------|------|--------|--------|
| Motor PWM | 6 | LEDC output | 20 kHz, 13-bit (0–8191) | `motor_task` (Core 1) |
| Motor DIR | 7 | digital output | HIGH = forward | `motor_task` (Core 1) |
| Brake solenoid | 8 | digital output | HIGH = engaged | `brake_task` (Core 1) |
| Steering servo | 6 (rt config) | LEDC output | 50 Hz, 500–2500 µs | `control_task` via `steering_servo` (Core 1) |

### 3.5 CAN bus hardware

| Signal | GPIO | Bus | Driver |
|--------|------|-----|--------|
| CAN TX (public) | 5 | Public | `can_driver0` (Core 0, `can_rx_task0`) |
| CAN RX (public) | 4 | Public | `can_driver0` |
| CAN TX (private) | 9 | Private (Syntree) | `can_driver1` (Core 1, `can_rx_task1`) |
| CAN RX (private) | 10 | Private (Syntree) | `can_driver1` |

---

## 4. Internal data flow (inter-core)

### 4.1 Queues

| Queue | Type | Slots | Producer (Core) | Consumer (Core) | Pattern |
|-------|------|-------|-----------------|-----------------|---------|
| `can_rx_queue` | `Queue<Frame, 16>` | 16 | `can_rx_task0` (0) | `dispatch_task` (0) | `xQueueSend` timeout=0 |
| `cmd_queue` | `Queue<DriveCmd, 4>` | 4 | `dispatch_task` (0) | `control_task` (1) | `xQueueOverwrite` |
| `setpoint_queue` | `Queue<Setpoint, 4>` | 4 | `control_task` (1) | `motor_task` (1) | `xQueueOverwrite` |

### 4.2 Shared atomics (lock-free, single-writer)

| Variable | Type | Writer (Core) | Readers | Notes |
|----------|------|---------------|---------|-------|
| `g_mode` | `atomic<int>` | `dispatch_task` (0), `safety_task` (0), `mode_task` (0) | `control_task` (1), `motor_task` (1), `brake_task` (1) | Mode FSM — single owner per transition |
| `g_obstacle_mm` | `atomic<unsigned>` | `obstacle_task` (1) | `control_task` (1), `motor_task` (1) | UINT32_MAX = no reading |
| `g_brake_request_kpa` | `atomic<int32_t>` | `dispatch_task` (0) | `control_task` (1) | From 0x301; max-select in control |
| `g_target_steer_angle_mdeg` | `atomic<int32_t>` | `control_task` (1) | `syntree_tx_task` (1) | ±45000 mdeg |
| `g_target_brake_pressure_kpa` | `atomic<int32_t>` | `control_task` (1) | `syntree_tx_task` (1) | 0 = release |
| `g_syntree_fault_bits` | `atomic<uint16_t>` | `can_rx_task1` (1) | `diagnostics_task` (1), `safety_task` (0) | Aggregated EPS+SEB faults |
| `g_throttle_mmps` | `atomic<int32_t>` | `throttle_task` (1) | `motor_task` (1), `can_tx_task` (0) | ADC-mapped speed |
| `g_safety_estop` | `atomic<bool>` | `safety_task` (0) | `can_tx_task` (0), `diagnostics_task` (1) | E-stop button state |
| `g_safety_hb_ok` | `atomic<bool>` | `safety_task` (0) | `can_tx_task` (0), `diagnostics_task` (1) | Heartbeat status |

### 4.3 Internal setpoint struct

```
Setpoint (control_task → motor_task via setpoint_queue, 12 bytes)
  [0..3]  int32_t motor_effort_pwm      signed PWM [-8191, +8191]
  [4..7]  int32_t steer_angle_mdeg      target for steering servo [+right]
  [8..11] int32_t brake_pressure_kpa    target for SEB brake [0 = release]
```

---

## 5. Control pipeline (Core 1, 100 Hz)

```
┌─────────────────────────────────────────────────────────┐
│                    control_task (100 Hz)                 │
│                                                         │
│  DriveCmd {speed_mmps, yaw_rate_mrad_s}                 │
│      │  (from cmd_queue, produced by dispatch_task)     │
│      ▼                                                  │
│  PhysicsModel::resolve()                                │
│      │  δ = atan2(L·ω, |v|),  clamp ±45°              │
│      │  pure yaw → min-radius arc at limit angle       │
│      │  low speed → decay steer toward straight        │
│      ▼                                                  │
│  ResolvedSetpoint {motor_speed_mmps, steer_angle_mdeg,  │
│                    steer_valid, steer_saturated, rev}   │
│      │                                                  │
│      ├─► obstacle_limit(speed, g_obstacle_mm)          │
│      │     linear interp: stop_dist→0, clear_dist→full  │
│      │                                                  │
│      ├─► pid.update(target_speed, measured, dt)        │
│      │     Kp=1.0 Ki=0.1 Kd=0.05, anti-windup ±500     │
│      │     motor_effort = clamp(pid_out, ±8191)        │
│      │                                                  │
│      └─► brake = max(0, g_brake_request_kpa)           │
│            max-select: Jetson can increase, never       │
│            decrease below RT safety floor               │
│                                                         │
│  Setpoint → setpoint_queue (consumed by motor_task)     │
│  g_target_steer_angle_mdeg ← steer_angle_mdeg          │
│  g_target_brake_pressure_kpa ← brake                   │
└─────────────────────────────────────────────────────────┘
```

---

## 6. Mode-dependent actuator behavior

| Mode | Motor | Steering | Brake | Throttle |
|------|-------|----------|-------|----------|
| **Manual** | `motor.set_speed(throttle)` — limited by obstacle distance | Disabled (rider steers mechanically) | Lever → engage | ADC → speed |
| **Auto** | `motor.set_effort(effort_pwm)` — from `setpoint_queue` | Servo active via `g_target_steer_angle_mdeg` | SEB via `g_target_brake_pressure_kpa` | Telemetry only |
| **Estop** | `motor.stop()` — PWM=0, DIR=0 | Disabled | Engaged (solenoid ON) | Ignored |

---

## 7. ESTOP — cross-cutting concern

```
Trigger sources                    Response (safety_task, Core 0, prio 5)
─────────────────────────────      ─────────────────────────────────────
E-stop button (GPIO1 LOW)    →     mode = ESTOP
CAN 0x001 (from Jetson)      →     motor_stop()       — direct call, no queue
Heartbeat timeout (>1500ms)  →     brake_engage()     — direct call, no queue
Syntree fault bits           →     steering disable   — servo PWM → center
                                   g_target_brake_pressure_kpa = MAX
                                   CAN 0x011 safety_status (estop=1)
```

---

## 8. Core pinning & RTOS task layout (14 tasks)

### Core 0 — Safety + CAN I/O

| Task | Priority | Stack | Period | Input | Action |
|------|----------|-------|--------|-------|--------|
| `safety` | **5** | 2048 B | 20 Hz | GPIO 1, 2, HB timers | ESTOP → `motor_stop()` + `brake_engage()` directly |
| `can_rx0` | **5** | 4096 B | event | Public CAN | Enqueue frames to `can_rx_queue` |
| `can_rx1` | **5** | 4096 B | event | Private CAN | Update `g_syntree_fault_bits` |
| `dispatch` | **4** | 3072 B | event | `can_rx_queue` | Parse 0x300→`cmd_queue`, 0x301→atomic, ESTOP→mode |
| `mode` | **4** | 2048 B | 10 Hz | GPIO 11 | Mode switch → `g_mode` |
| `can_tx` | **4** | 3072 B | 5 Hz | `g_safety`, `g_throttle` | Send 0x011, 0x120 |
| `heartbeat` | **1** | 2048 B | 2 Hz | — | Send 0x7FF |
| `watchdog` | **1** | 2048 B | 10 Hz | `g_watchdog` | Staleness → zero setpoint |

### Core 1 — Computation + Actuation

| Task | Priority | Stack | Period | Input | Action |
|------|----------|-------|--------|-------|--------|
| `control` | **4** | 4096 B | 100 Hz | `cmd_queue`, `g_obstacle_mm`, `g_brake_request` | Physics + PID + brake → `setpoint_queue` |
| `motor` | **4** | 2048 B | 100 Hz | `setpoint_queue` | AUTO: `set_effort()`, MANUAL: `set_speed(limited)`, ESTOP: `stop()` |
| `syntree_tx` | **4** | 3072 B | 50 Hz | `g_target_steer`, `g_target_brake` | Private CAN EPS-C + SEB commands |
| `throttle` | **3** | 1536 B | 100 Hz | ADC1_CH5 | Map to mm/s → `g_throttle_mmps` |
| `brake` | **3** | 1536 B | 20 Hz | Mode, brake lever | ESTOP/lever → engage, else → release |
| `obstacle` | **2** | 2048 B | 10 Hz | HC-SR04 | Poll → `g_obstacle_mm`, CAN 0x400 |
| `diagnostics` | **1** | 2048 B | 1 Hz | All state | CAN 0x600 |
