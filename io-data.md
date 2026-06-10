# I/O Data — E-Trike Distributed Control

Three physical nodes, two CAN buses, one direct inter-MCU link.

## Physical topology

```
┌──────────┐   Public CAN (500 kbit/s)   ┌──────────────┐   Inter-MCU link   ┌──────────────┐   Private CAN   ┌──────────┐
│  Jetson  │◄───────────────────────────►│  RT ESP32-S3 │◄──────────────────►│ SYS ESP32-S3 │◄──────────────►│ Syntree  │
│  Orin NX │                             │              │  (UART or SPI)     │              │  (500 kbit/s)  │ EPS-C    │
│          │                             │              │  framed + CRC-8   │              │                │ SEB      │
└──────────┘                             └──────────────┘                    └──────────────┘                └──────────┘
```

- **Public CAN**: Jetson ↔ RT. All telemetry, commands, ESTOP, heartbeats.
- **Inter-MCU link**: RT ↔ SYS. Setpoints, status, obstacle distance. Framed protocol with SOF `0xA5 0x5A` and CRC-8.
- **Private CAN**: SYS ↔ Syntree EPS-C (steering) and SEB (brake). Not visible to Jetson or RT.

SYS never talks directly to Jetson. All SYS → Jetson data is **mirrored** by RT over the public CAN bus.

---

## 1. Jetson — inputs

| Source | Signal | Type | Rate | Notes |
|--------|--------|------|------|-------|
| ROS 2 `/cmd_vel` | `linear.x` | `float64` [m/s] | ≤100 Hz | Forward velocity |
| ROS 2 `/cmd_vel` | `angular.z` | `float64` [rad/s] | ≤100 Hz | Yaw rate |
| ROS 2 `/emergency_stop` | estop flag | `bool` | On event | Host-side ESTOP |
| CAN `0x011` | `SysSafetyStatus` | struct (2 bytes) | 5 Hz | Mirrored SYS safety |
| CAN `0x120` | `SysThrottlePos` | `int16_t` [mm/s] | 100 Hz | Mirrored SYS throttle |
| CAN `0x210` | `RtStateReport` | struct (3 bytes) | 10 Hz | RT state telemetry |
| CAN `0x220` | `RtPidFeedback` | struct (6 bytes) | 10 Hz | PID debug |
| CAN `0x400` | `RtObstacleDist` | `uint32_t` [mm] | 10 Hz | Obstacle sensor |
| CAN `0x600` | `SysDiag` | struct (8 bytes) | 1 Hz | Mirrored SYS diagnostics |
| CAN `0x7FF` | heartbeat | (empty) | 2 Hz | RT alive signal |

## 2. Jetson — outputs

| Destination | CAN ID | Signal | Type | Rate | Notes |
|-------------|--------|--------|------|------|-------|
| RT | `0x300` | `HostDriveCmd` | `{int32_t speed_mmps, int32_t yaw_rate_mrad_s}` (8 bytes) | ≤100 Hz | Converted from `/cmd_vel`: `speed_mmps = linear.x * 1000`, `yaw_rate_mrad_s = angular.z * 1000` |
| RT | `0x301` | `HostBrakeRequest` | `int32_t brake_pressure_kpa` (4 bytes) | On demand | Planned deceleration, hill hold, precision docking. RT-arbitrated: max(rt_computed, jetson_request) |
| RT | `0x003` | `HostEstop` | (empty) | On event | Host-triggered ESTOP |
| RT | `0x7FF` | heartbeat | (empty) | 2 Hz | Jetson alive signal |

### HostDriveCmd payload

| Offset | Size | Field | Unit | Range |
|--------|------|-------|------|-------|
| 0 | 4 B | `speed_mmps` | mm/s | [-500, 3000] (clamped by RT) |
| 4 | 4 B | `yaw_rate_mrad_s` | millirad/s | [-3000, 3000] |

---

## 3. RT ESP32-S3 — inputs

### 3.1 Public CAN inputs (from Jetson)

| CAN ID | Signal | Type | Rate | Handler |
|--------|--------|------|------|---------|
| `0x300` | `HostDriveCmd` | `{int32_t speed_mmps, int32_t yaw_rate_mrad_s}` | ≤100 Hz | `dispatch_task` → `cmd_queue` |
| `0x301` | `HostBrakeRequest` | `int32_t brake_pressure_kpa` | On demand | `dispatch_task` → `g_brake_request_kpa` atomic; passes to `resolve_drive_setpoint()` for max-select arbitration |
| `0x003` | `HostEstop` | (empty) | On event | `dispatch_task` → `mode = Estop` |
| `0x001` | `SysEstop` | (empty) | On event | `dispatch_task` → `mode = Estop` (mirrored from SYS) |
| `0x7FF` | heartbeat | (empty) | 2 Hz | Jetson alive tracking |

### 3.2 Inter-MCU inputs (from SYS)

| Message type | Signal | Type | Rate | Handler |
|-------------|--------|------|------|---------|
| `0x20` | `SysToRtStatus` | 14 bytes (see §7.3) | ~50 Hz | `intermcu_rx_task` |
| `0x21` | SysHeartbeat | (empty) | 2 Hz | `intermcu_rx_task` → SYS alive tracking |

### 3.3 Local sensor inputs

| Signal | GPIO | Type | Rate | Notes |
|--------|------|------|------|-------|
| HC-SR04 echo | 8 | pulse width → `uint32_t` [mm] | 10 Hz | `obstacle_task` polls, stores in atomic |
| Encoder A | 1 | pulse count | — | Speed feedback (PCNT, TODO) |
| Encoder B | 2 | pulse count | — | Speed feedback (PCNT, TODO) |

## 4. RT ESP32-S3 — outputs

### 4.1 Public CAN outputs (to Jetson)

| CAN ID | Signal | Source | Rate |
|--------|--------|--------|------|
| `0x002` | `RtEstop` | On ESTOP detection | On event |
| `0x011` | `SysSafetyStatus` | Mirrored from SYS inter-MCU status | 5 Hz |
| `0x120` | `SysThrottlePos` | Mirrored from SYS inter-MCU status | 100 Hz |
| `0x210` | `RtStateReport` | Internal state | 10 Hz |
| `0x220` | `RtPidFeedback` | PID controller | 10 Hz |
| `0x400` | `RtObstacleDist` | `g_obstacle.distance_mm()` | 10 Hz |
| `0x600` | `SysDiag` | Mirrored from SYS inter-MCU status | 1 Hz |
| `0x7FF` | heartbeat | Internal | 2 Hz |

### 4.2 Inter-MCU outputs (to SYS)

| Message type | Signal | Type | Rate | Notes |
|-------------|--------|------|------|-------|
| `0x10` | `RtToSysSetpoint` | 13 bytes (see §7.1) | 100 Hz | Physics + PID + obstacle output |
| `0x11` | RtHeartbeat | (empty) | 2 Hz | RT alive signal |
| `0x12` | `RtObstacleDistance` | 4 bytes | 10 Hz | Obstacle distance for SYS manual-mode limiting |

### 4.3 RT→SYS Setpoint payload

| Offset | Size | Field | Unit | Range |
|--------|------|-------|------|-------|
| 0 | 4 B | `motor_effort_pwm` | raw PWM | [-8191, +8191] (13-bit signed) |
| 4 | 4 B | `steer_angle_mdeg` | millideg | [-45000, +45000] (+right) |
| 8 | 4 B | `brake_pressure_kpa` | kPa | [0, …] (0 = release) |
| 12 | 1 B | `flags` | bitfield | ESTOP, AutoEnable, BrakeEnable, EpsEnable |

### 4.4 Internal control pipeline (within RT)

```
DriveCmd {speed_mmps, yaw_rate_mrad_s}
    │
    ▼
PhysicsModel::resolve()
    │  steer = atan2(L·ω, |v|),  clamp ±45°
    │  pure yaw → min-radius arc at limit angle
    │  low speed → decay steer toward straight
    ▼
ResolvedSetpoint {motor_speed_mmps, steer_angle_mdeg, steer_valid, steer_saturated, reversing}
    │
    ▼
resolve_drive_setpoint(…, brake_request_kpa)  (control_logic.cpp)
    │  obstacle_limit(speed, obstacle_mm)
    │  pid.update(speed_sp, measured, dt)
    │  motor_effort_pwm = clamp(pid_output, ±8191)
    │  brake_pressure_kpa = max(rt_computed, brake_request_kpa)   ← max-select arbitration
    ▼
RtToSysSetpoint {motor_effort_pwm, steer_angle_mdeg, brake_pressure_kpa, flags}
    │
    ▼
inter-MCU link → SYS
```

---

## 5. SYS ESP32-S3 — inputs

### 5.1 Inter-MCU inputs (from RT)

| Message type | Signal | Type | Rate | Handler |
|-------------|--------|------|------|---------|
| `0x10` | `RtToSysSetpoint` | 13 bytes | 100 Hz | `dispatch_task` → `setpoint_queue` |
| `0x11` | RtHeartbeat | (empty) | 2 Hz | `can_rx_task` → `g_safety.feed_heartbeat_rt()` |
| `0x12` | `RtObstacleDistance` | 4 bytes | 10 Hz | `dispatch_task` → `g_obstacle_mm` |

### 5.2 Private CAN inputs (from Syntree actuators)

| CAN ID | Signal | Type | Rate | Notes |
|--------|--------|------|------|-------|
| `0x201` | `SyntreeEpsStatus` | raw 8 bytes | ~50 Hz | EPS-C steering feedback |
| `0x721` | `SyntreeSebStatus` | raw 8 bytes | ~50 Hz | SEB brake feedback |

### 5.3 Local hardware inputs

| Signal | GPIO | Type | Rate | Notes |
|--------|------|------|------|-------|
| E-stop button | 1 | digital (active-low) | 20 Hz | `safety_task` polls |
| Brake lever | 2 | digital (active-low) | 20 Hz | `safety_task` polls |
| Throttle ADC | 10 (ADC1_CH5) | 12-bit analog [0-4095] | 100 Hz | `throttle_task` maps to mm/s |
| Mode switch | 11 | digital (LOW=Auto) | 10 Hz | `mode_task` polls |

## 6. SYS ESP32-S3 — outputs

### 6.1 Inter-MCU outputs (to RT)

| Message type | Signal | Type | Rate | Notes |
|-------------|--------|------|------|-------|
| `0x20` | `SysToRtStatus` | 14 bytes (see §7.3) | ~50 Hz | Mode, ESTOP, HB, brake, steering feedback, fault bits |
| `0x21` | SysHeartbeat | (empty) | 2 Hz | SYS alive signal |

### 6.2 Private CAN outputs (to Syntree actuators)

| CAN ID | Signal | Type | Rate | Notes |
|--------|--------|------|------|-------|
| `0x169` | `SyntreeEpsCommand` | raw 8 bytes | 50 Hz | EPS-C steering target |
| `0x7B0` | `SyntreeSebCommand` | raw 8 bytes | 50 Hz | SEB brake target |

### 6.3 Local hardware outputs

| Signal | GPIO | Type | Notes |
|--------|------|------|-------|
| Motor PWM | 6 | LEDC 20 kHz, 13-bit | Traction motor effort |
| Motor DIR | 7 | digital (HIGH=fwd) | Direction control |

### 6.4 Actuator control logic (within SYS)

```
AUTO mode:
  setpoint_queue ──► motor_task ──► motor.set_effort(effort_pwm)

MANUAL mode:
  throttle.poll() ──► throttle.read_mmps()
  obstacle_mm ──► limit_forward_speed_for_obstacle()
  motor_task ──► motor.set_speed(limited_speed_mmps)

ESTOP mode:
  motor_task ──► motor.stop()
  brake_task ──► brake.engage()

Steering (AUTO, via Syntree private CAN):
  RtToSysSetpoint.steer_angle_mdeg ──► SyntreeEpsCommand ──► CAN 0x169

Brake (AUTO, via Syntree private CAN):
  RtToSysSetpoint.brake_pressure_kpa ──► SyntreeSebCommand ──► CAN 0x7B0
```

---

## 7. Inter-MCU protocol (RT ↔ SYS)

Transport: high-speed UART or SPI. Framed with SOF `0xA5 0x5A`, sequence number, CRC-8.

### 7.1 RtToSysSetpoint (RT → SYS, 13 bytes)

| Offset | Size | Field | Unit | Range |
|--------|------|-------|------|-------|
| 0 | 4 B | `motor_effort_pwm` | raw PWM | [-8191, +8191] |
| 4 | 4 B | `steer_angle_mdeg` | millideg | [-45000, +45000] |
| 8 | 4 B | `brake_pressure_kpa` | kPa | [0, …] |
| 12 | 1 B | `flags` | bitfield | bit 0=ESTOP, bit 1=AutoEnable, bit 2=BrakeEnable, bit 3=EpsEnable |

### 7.2 RtObstacleDistance (RT → SYS, 4 bytes)

| Offset | Size | Field | Unit | Notes |
|--------|------|-------|------|-------|
| 0 | 4 B | `distance_mm` | mm | UINT32_MAX = no reading |

### 7.3 SysToRtStatus (SYS → RT, 14 bytes)

| Offset | Size | Field | Type | Notes |
|--------|------|-------|------|-------|
| 0 | 1 B | `mode` | `uint8_t` | 0=Manual, 1=Auto, 2=Estop |
| 1 | 1 B | `estop_active` | `bool` | |
| 2 | 1 B | `heartbeat_ok` | `bool` | RT link heartbeat within timeout |
| 3 | 1 B | `brake_engaged` | `bool` | |
| 4 | 4 B | `actual_steer_angle_mdeg` | `int32_t` | EPS-C feedback [+right] |
| 8 | 4 B | `brake_pressure_kpa` | `int32_t` | SEB feedback |
| 12 | 2 B | `syntree_fault_bits` | `uint16_t` | Aggregated fault flags |

---

## 8. CAN ID catalog

### Public bus (Jetson ↔ RT)

| ID | Name | Dir | DLC | Payload |
|----|------|-----|-----|---------|
| `0x001` | SYS_ESTOP | RT→Jetson | 0 | (mirrored from SYS) |
| `0x002` | RT_ESTOP | RT→Jetson | 0 | RT-triggered ESTOP |
| `0x003` | HOST_ESTOP | Jetson→RT | 0 | Jetson-triggered ESTOP |
| `0x011` | SYS_SAFETY_STATUS | RT→Jetson | 2 | `{u8 estop, u8 hb_ok}` (mirrored) |
| `0x120` | SYS_THROTTLE_POS | RT→Jetson | 2 | `i16 speed_mmps` (mirrored) |
| `0x210` | RT_STATE_REPORT | RT→Jetson | 3 | `{u8 mode, u8 steer_valid, u8 rev}` |
| `0x220` | RT_PID_FEEDBACK | RT→Jetson | 6 | `{i16 sp, i16 meas, i16 out}` |
| `0x300` | HOST_DRIVE_CMD | Jetson→RT | 8 | `{i32 speed, i32 yaw}` |
| `0x301` | HOST_BRAKE_REQUEST | Jetson→RT | 4 | `i32 brake_pressure_kpa` |
| `0x400` | RT_OBSTACLE_DIST | RT→Jetson | 4 | `u32 distance_mm` |
| `0x600` | SYS_DIAG | RT→Jetson | 8 | `{u8 mode, u8 brake, u8 hb, u8 estop, u16 heap, u8 tec, u8 rec}` |
| `0x7FF` | HEARTBEAT | Both | 0 | Alive signal |

### Private bus (SYS ↔ Syntree)

| ID | Name | Dir | DLC | Notes |
|----|------|-----|-----|-------|
| `0x169` | EPS_COMMAND | SYS→EPS-C | 8 | 20 ms period |
| `0x201` | EPS_STATUS | EPS-C→SYS | 8 | Feedback |
| `0x7B0` | SEB_COMMAND | SYS→SEB | 8 | 20 ms period |
| `0x721` | SEB_STATUS | SEB→SYS | 8 | Feedback |

---

## 9. Mode state machine (shared)

```
          ┌──────────┐
    ┌────►│  MANUAL  │◄────┐
    │     │  mode=0  │     │
    │     └────┬─────┘     │
    │   switch=AUTO   switch=MANUAL
    │          │             │
    │     ┌────▼─────┐      │
    │     │   AUTO   │      │
    │     │  mode=1  │      │
    │     └────┬─────┘      │
    │          │             │
    │  ESTOP button / CAN 0x001-0x003 / HB timeout
    │          │             │
    │     ┌────▼─────┐      │
    └─────┤  ESTOP   │──────┘
          │  mode=2  │  (cannot leave via switch)
          └──────────┘
```

- **Manual**: Rider steers mechanically. Throttle via ADC. SYS drives motor from throttle. SYS speed-limited by obstacle distance.
- **Auto**: Jetson → CAN `0x300` → RT physics → inter-MCU `0x10` → SYS motor + EPS-C + SEB.
- **Estop**: Motor stopped, brake engaged, steering disabled. Exit requires power-cycle.

---

## 10. RT ESP32-S3 — RTOS task layout

| Task | Prio | Rate | Input queue | Action |
|------|------|------|-------------|--------|
| `can_rx` | 5 | Event-driven | — | CAN frames → `can_rx_queue` |
| `intermcu_rx` | 5 | Event-driven | — | Inter-MCU frames → routing |
| `dispatch` | 4 | Event-driven | `can_rx_queue` | Parse 0x300→`cmd_queue`, ESTOP→mode |
| `control` | 4 | 100 Hz | `cmd_queue` | Physics + PID + obstacle → `setpoint_queue` |
| `can_tx` | 3 | Event-driven | `setpoint_queue` | Inter-MCU `0x10` to SYS |
| `obstacle` | 2 | 10 Hz | — | HC-SR04 poll → CAN `0x400` |
| `watchdog` | 1 | 10 Hz | — | Staleness → zero setpoint |
| `hb` | 1 | 2 Hz | — | CAN `0x7FF` |

## 11. SYS ESP32-S3 — RTOS task layout

| Task | Prio | Rate | Input | Action |
|------|------|------|-------|--------|
| `can_rx` | 5 | Event-driven | — | Private CAN frames → routing (heartbeat track) |
| `intermcu_rx` | 5 | Event-driven | — | Inter-MCU frames → `setpoint_queue`, heartbeat feed |
| `safety` | 5 | 20 Hz | GPIO | ESTOP button, brake lever, HB timeout → `mode_set(Estop)` |
| `dispatch` | 4 | Event-driven | `setpoint_queue` | Inter-MCU `0x10` → setpoint, `0x12` → obstacle |
| `mode` | 4 | 10 Hz | GPIO | Mode switch → `mode_set(Auto/Manual)`, notify RT via inter-MCU |
| `motor` | 4 | 100 Hz | `setpoint_queue` | AUTO: `set_effort()`, MANUAL: `set_speed(limited)`, ESTOP: `stop()` |
| `throttle` | 3 | 100 Hz | ADC | Read, publish for RT mirror |
| `brake` | 3 | 20 Hz | — | ESTOP/lever→engage, else→release |
| `can_tx` | 2 | 5 Hz | — | Private CAN EPS-C + SEB commands |
| `diag` | 1 | 1 Hz | — | Heap, mode, brake, TEC/REC → RT mirror |
| `hb` | 1 | 2 Hz | — | Inter-MCU SysHeartbeat |
