# I/O Data — E-Trike Distributed Control

Three nodes, two CAN buses. RT is the only dual-bus node and acts as gateway.

## Physical topology

```
┌──────────┐  High-Level CAN (500 kbit/s)  ┌──────────────┐  Low-Level CAN (500 kbit/s)  ┌──────────────┐
│  Jetson  │◄─────────────────────────────►│  RT ESP32-S3 │◄────────────────────────────►│ SYS ESP32-S3 │
│  Orin NX │                               │              │                              │              │
│          │                               │ CAN Gateway  │                              │ Low-Level    │
└──────────┘                               └──────────────┘                              │ CAN only     │
                                                                                         └──────┬───────┘
                                                                                                │
                                                                              ┌─────────────────┼─────────────────┐
                                                                              │                 │                 │
                                                                        ┌─────▼─────┐    ┌─────▼─────┐    ┌─────▼─────┐
                                                                        │  Brake    │    │ Steering   │    │   DC-DC   │
                                                                        │ CAN Mod.  │    │ CAN Mod.   │    │ Converter │
                                                                        │           │    │(drive-by-  │    │ 72V→12V   │
                                                                        │ (0x010)   │    │ wire,0x230)│    │ (0x012)   │
                                                                        └───────────┘    └───────────┘    └───────────┘
```

- **High-level CAN**: Jetson ↔ RT. Commands, telemetry, ESTOP, heartbeats.
- **Low-level CAN**: RT ↔ SYS + actuators (brake, steering, DC-DC converter). Setpoints, safety, body control.
- **RT gateway**: Forwards `0x001`, `0x011`, `0x120`, `0x600` (low→high) and `0x001`, `0x302` (high→low). Same CAN ID on both buses.
- SYS never talks directly to Jetson. All SYS → Jetson data is forwarded by RT.

---

## 1. Jetson — inputs (from High-Level CAN)

| Source | CAN ID | Signal | Type | Rate | Notes |
|--------|--------|--------|------|------|-------|
| RT (fwd) | `0x001` | SAFETY_ESTOP | (empty) | On event | ESTOP from low-level bus |
| RT (fwd) | `0x011` | `SysSafetyStatus` | `{u8 estop, u8 hb_ok}` (2 bytes) | 5 Hz | SYS safety telemetry |
| RT (fwd) | `0x120` | `SysThrottlePos` | `i16 speed_mmps` (2 bytes) | 100 Hz | SYS throttle reading |
| RT | `0x210` | `RtStateReport` | `{u8 mode, u8 steer_valid, u8 reversing}` (3 bytes) | 10 Hz | RT state telemetry |
| RT | `0x220` | `RtPidFeedback` | `{i16 sp, i16 meas, i16 out}` (6 bytes) | 10 Hz | PID debug |
| RT | `0x400` | `RtObstacleDist` | `u32 distance_mm` (4 bytes) | 10 Hz | Obstacle sensor |
| RT (fwd) | `0x600` | `SysDiag` | `{u8 mode, u8 brake, u8 hb, u8 estop, u16 heap, u8 tec, u8 rec}` (8 bytes) | 1 Hz | SYS diagnostics |
| RT | `0x7FF` | HEARTBEAT | (empty) | 2 Hz | RT alive signal |

**ROS 2 inputs** (local, converted to CAN):

| Source | Field | Type | Rate | Notes |
|--------|-------|------|------|-------|
| `/cmd_vel` | `linear.x` | `float64` [m/s] | ≤100 Hz | → `speed_mmps = linear.x × 1000` |
| `/cmd_vel` | `angular.z` | `float64` [rad/s] | ≤100 Hz | → `yaw_rate_mrad_s = angular.z × 1000` |
| `/emergency_stop` | estop flag | `bool` | On event | → CAN `0x001` on high-level |

## 2. Jetson — outputs (to High-Level CAN)

| CAN ID | Signal | Type | Rate | Notes |
|--------|--------|------|------|-------|
| `0x001` | SAFETY_ESTOP | (empty) | On event | Jetson-triggered ESTOP. RT forwards to low-level. |
| `0x300` | `HostDriveCmd` | `{i32 speed_mmps, i32 yaw_rate_mrad_s}` (8 bytes) | ≤100 Hz | Converted from `/cmd_vel` |
| `0x301` | `HostBrakeRequest` | `i32 brake_pressure_kpa` (4 bytes) | On demand | RT-arbitrated: max(rt_computed, jetson_request) |
| `0x302` | `HostLightCmd` | `u8` bitfield: b0=left, b1=right, b2=brake, b3=head (1 byte) | On change | RT forwards to low-level |
| `0x7FF` | HEARTBEAT | (empty) | 2 Hz | Jetson alive signal |

### HostDriveCmd payload (0x300)

| Offset | Size | Field | Unit | Range |
|--------|------|-------|------|-------|
| 0 | 4 B | `speed_mmps` | mm/s | [-500, 3000] |
| 4 | 4 B | `yaw_rate_mrad_s` | millirad/s | [-3000, 3000] |

---

## 3. RT ESP32-S3 — inputs

### 3.1 High-level CAN inputs (MCP2515 SPI, from Jetson)

| CAN ID | Signal | Type | Rate | Handler |
|--------|--------|------|------|---------|
| `0x001` | SAFETY_ESTOP | (empty) | On event | `dispatch_task` → `mode_set(Estop)`, forward to low-level |
| `0x300` | `HostDriveCmd` | 8 bytes | ≤100 Hz | `dispatch_task` → `cmd_queue` |
| `0x301` | `HostBrakeRequest` | 4 bytes | On demand | `dispatch_task` → `g_brake_request_kpa` (atomic) |
| `0x302` | `HostLightCmd` | 1 byte | On change | `dispatch_task` → forward to low-level CAN |
| `0x7FF` | HEARTBEAT | (empty) | 2 Hz | Jetson alive tracking |

### 3.2 Low-level CAN inputs (built-in TWAI, from SYS)

| CAN ID | Signal | Type | Rate | Handler |
|--------|--------|------|------|---------|
| `0x001` | SAFETY_ESTOP | (empty) | On event | `dispatch_task` → `mode_set(Estop)`, forward to high-level |
| `0x011` | `SysSafetyStatus` | 2 bytes | 5 Hz | `dispatch_task` → forward to high-level CAN |
| `0x110` | `SysModeCmd` | `u8 mode` (1 byte) | On change | `dispatch_task` → `mode_set(Manual/Auto)` |
| `0x120` | `SysThrottlePos` | 2 bytes | 100 Hz | `dispatch_task` → forward to high-level CAN |
| `0x600` | `SysDiag` | 8 bytes | 1 Hz | `dispatch_task` → forward to high-level CAN |
| `0x7FF` | HEARTBEAT | (empty) | 2 Hz | SYS alive tracking |

### 3.3 Local sensor inputs

| Signal | GPIO | Type | Rate | Notes |
|--------|------|------|------|-------|
| HC-SR04 ECHO | 8 | pulse width → `u32` [mm] | 10 Hz | `obstacle_task` → CAN `0x400` |
| Encoder A | 1 | pulse count (PCNT) | — | Speed feedback |
| Encoder B | 2 | pulse count (PCNT) | — | Speed feedback |
| I2C SDA | 10 | I2C | — | IMU (optional) |
| I2C SCL | 11 | I2C | — | IMU (optional) |

---

## 4. RT ESP32-S3 — outputs

### 4.1 High-level CAN outputs (MCP2515 SPI, to Jetson)

| CAN ID | Signal | Source | Rate |
|--------|--------|--------|------|
| `0x001` | SAFETY_ESTOP | Forwarded from low-level, or RT watchdog | On event |
| `0x011` | `SysSafetyStatus` | Forwarded from low-level CAN | 5 Hz |
| `0x120` | `SysThrottlePos` | Forwarded from low-level CAN | 100 Hz |
| `0x210` | `RtStateReport` | Internal state | 10 Hz |
| `0x220` | `RtPidFeedback` | PID controller | 10 Hz |
| `0x400` | `RtObstacleDist` | `obstacle_task` | 10 Hz |
| `0x600` | `SysDiag` | Forwarded from low-level CAN | 1 Hz |
| `0x7FF` | HEARTBEAT | Internal | 2 Hz |

### 4.2 Low-level CAN outputs (built-in TWAI, to SYS + actuators)

| CAN ID | Signal | Type | Rate | Notes |
|--------|--------|------|------|-------|
| `0x001` | SAFETY_ESTOP | (empty) | On event | Forwarded from high-level, or RT watchdog |
| `0x200` | `RtDriveSetpoint` | `{i32 motor_speed_mmps, u8 gear}` (5 bytes) | 100 Hz | Speed + gear → SYS |
| `0x230` | `RtSteerCmd` | `i32 angle_mdeg` (4 bytes) | 100 Hz | → Steering CAN module |
| `0x302` | `HostLightCmd` | `u8` bitfield (1 byte) | On change | Forwarded from high-level CAN |
| `0x7FF` | HEARTBEAT | (empty) | 2 Hz | RT alive on low-level |

### 4.3 RT→SYS Setpoint payload (0x200)

| Offset | Size | Field | Unit | Range |
|--------|------|-------|------|-------|
| 0 | 4 B | `motor_speed_mmps` | mm/s | [-500, 3000] |
| 4 | 1 B | `gear` | enum | 0=N, 1=D, 2=S, 3=R |

### 4.4 Internal control pipeline

```
HostDriveCmd {speed_mmps, yaw_rate_mrad_s}   (from high-level CAN 0x300)
    │
    ▼
physics_resolve()
    │  δ = atan2(L·ω, |v|), clamp ±45°
    │  low speed → decay steer toward straight
    ▼
ResolvedSetpoint {motor_speed_mmps, steer_angle_mdeg, steer_valid, reversing, gear}
    │
    ▼
resolve_drive_setpoint(…)
    │  obstacle_limit(speed, obstacle_mm)
    │  pid_update(speed_sp, measured, dt)
    │  brake max-select: max(rt_computed, jetson_request)   ← (TBD: no CAN path to SYS yet)
    ▼
Low-level CAN:
    0x200 {motor_speed_mmps, gear}     → SYS
    0x230 {steer_angle_mdeg}           → Steering CAN module
```

---

## 5. SYS ESP32-S3 — inputs

### 5.1 Low-level CAN inputs (from RT)

| CAN ID | Signal | Type | Rate | Handler |
|--------|--------|------|------|---------|
| `0x001` | SAFETY_ESTOP | (empty) | On event | `dispatch_task` → `mode_set(Estop)` |
| `0x200` | `RtDriveSetpoint` | 5 bytes | 100 Hz | `dispatch_task` → `setpoint_queue` |
| `0x302` | `HostLightCmd` | 1 byte | On change | `dispatch_task` → `g_light_state` (atomic) |
| `0x7FF` | RT HEARTBEAT | (empty) | 2 Hz | `safety_task` → feed RT heartbeat |

### 5.2 Local hardware inputs

| Signal | GPIO | Type | Conditioning | Rate | Notes |
|--------|------|------|-------------|------|-------|
| E-stop button | 1 | digital, active-low | Pull-up, debounced | 20 Hz | `safety_task` |
| Brake lever | 2 | digital, active-low | Pull-up | 20 Hz | `safety_task` |
| Throttle read | 10 | ADC1_CH5, 12-bit [0–4095] | Voltage divider 5V→3.3V | 100 Hz | `throttle_task` maps to mm/s |
| Mode switch | 11 | digital, pull-up | HIGH=Manual, LOW=Auto | 10 Hz | `mode_task` |
| Gear D sense | 12 | digital | TLP281 optoisolator ch1 | 50 Hz | `gear_task`, isolated from 72V |
| Gear S sense | 13 | digital | TLP281 optoisolator ch2 | 50 Hz | `gear_task`, isolated from 72V |
| Gear R sense | 14 | digital | TLP281 optoisolator ch3 | 50 Hz | `gear_task`, isolated from 72V |

---

## 6. SYS ESP32-S3 — outputs

### 6.1 Low-level CAN outputs (to actuators + RT)

| CAN ID | Signal | Type | Rate | Notes |
|--------|--------|------|------|-------|
| `0x010` | `SysBrakeCmd` | `u8 engage` (1 byte) | On change | → Brake CAN module |
| `0x011` | `SysSafetyStatus` | `{u8 estop, u8 hb_ok}` (2 bytes) | 5 Hz | → RT (fwd to Jetson) |
| `0x012` | `SysDcdcCmd` | `u8 enable` (1 byte) | On change | → DC-DC converter (72V→12V) |
| `0x110` | `SysModeCmd` | `u8 mode` (1 byte) | On change | → RT |
| `0x120` | `SysThrottlePos` | `i16 speed_mmps` (2 bytes) | 100 Hz | → RT (fwd to Jetson) |
| `0x600` | `SysDiag` | 8 bytes | 1 Hz | → RT (fwd to Jetson) |
| `0x7FF` | HEARTBEAT | (empty) | 2 Hz | SYS alive on low-level |

### 6.2 Local hardware outputs

| Signal | GPIO | Type | Conditioning | Notes |
|--------|------|------|-------------|-------|
| Throttle output | — | I2C (SDA=15, SCL=16) | MCP4725 DAC, 12-bit, VCC=5V → 0–5V | → Motor controller |
| Gear D output | 33 | digital | 4-ch 5V relay ch1: GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS SMCJ90CA to GND. | → Motor controller ECU |
| Gear S output | 34 | digital | 4-ch 5V relay ch2: GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS SMCJ90CA to GND. | → Motor controller ECU |
| Gear R output | 35 | digital | 4-ch 5V relay ch3: GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS SMCJ90CA to GND. | → Motor controller ECU |
| Left turn | 18 | digital | GPIO → relay → lamp | |
| Right turn | 19 | digital | GPIO → relay → lamp | |
| Brake light | 21 | digital | GPIO → relay → lamp | |
| Headlight | 22 | digital | GPIO → relay → lamp | |
| AUTO mode LED | 25 | digital | GPIO → LED | |
| MANUAL mode LED | 26 | digital | GPIO → LED | |
| 12V power relay | 27 | digital | GPIO → relay → 12V bus | Secondary cut on ESTOP |

### 6.3 Actuator control logic

```
AUTO mode:
  setpoint_queue.speed_mmps ──► MCP4725 DAC = abs(speed) / 3000 × 4095
  setpoint_queue.gear        ──► gear output relays (D/S/R)
  g_light_state (from 0x302) ──► signal light GPIOs

MANUAL mode:
  throttle ADC read           ──► MCP4725 DAC = same value (pass-through)
  gear sense GPIOs            ──► gear output relays (mirror)
  (lights: rider switches, GPIOs TBD)

ESTOP mode:
  MCP4725 DAC = 0 V
  gear outputs = all OFF (N)
  0x010 engage = 1 (brake on)
  0x012 enable = 0 (DCDC off)
  12V relay = OFF
  brake light = ON, others OFF

Steering (AUTO):
  RT sends 0x230 directly to Steering CAN module on low-level bus.
  SYS is not involved in steering actuation.

Brake (any mode):
  ESTOP or lever pressed → SYS sends 0x010 engage=1 → Brake CAN module.
```

---

## 7. CAN ID catalog

### 7.1 Low-level CAN (RT ↔ SYS + actuators)

| ID | Name | Dir | DLC | Payload | Rate |
|----|------|-----|-----|---------|------|
| `0x001` | SAFETY_ESTOP | RT→SYS, SYS→RT | 0 | (empty) | On event |
| `0x010` | SYS_BRAKE_CMD | SYS→Brake mod. | 1 | `u8 engage` | On change |
| `0x011` | SYS_SAFETY_STATUS | SYS→RT | 2 | `{u8 estop, u8 hb_ok}` | 5 Hz |
| `0x012` | SYS_DCDC_CMD | SYS→DC-DC conv. | 1 | `u8 enable` | On change |
| `0x110` | SYS_MODE_CMD | SYS→RT | 1 | `u8 mode` (0=M, 1=A) | On change |
| `0x120` | SYS_THROTTLE_POS | SYS→RT | 2 | `i16 speed_mmps` | 100 Hz |
| `0x200` | RT_DRIVE_SETPOINT | RT→SYS | 5 | `{i32 speed, u8 gear}` | 100 Hz |
| `0x230` | RT_STEER_CMD | RT→Steering mod. | 4 | `i32 angle_mdeg` | 100 Hz |
| `0x302` | HOST_LIGHT_CMD | RT→SYS (fwd) | 1 | `u8` bitfield | On change |
| `0x600` | SYS_DIAG | SYS→RT | 8 | diag struct | 1 Hz |
| `0x7FF` | HEARTBEAT | RT, SYS | 0 | (empty) | 2 Hz |

### 7.2 High-level CAN (Jetson ↔ RT)

| ID | Name | Dir | DLC | Payload | Rate |
|----|------|-----|-----|---------|------|
| `0x001` | SAFETY_ESTOP | Jetson→RT, RT→Jetson | 0 | (empty) | On event |
| `0x011` | SYS_SAFETY_STATUS | RT→Jetson (fwd) | 2 | `{u8 estop, u8 hb_ok}` | 5 Hz |
| `0x120` | SYS_THROTTLE_POS | RT→Jetson (fwd) | 2 | `i16 speed_mmps` | 100 Hz |
| `0x210` | RT_STATE_REPORT | RT→Jetson | 3 | `{u8 mode, u8 steer_valid, u8 reversing}` | 10 Hz |
| `0x220` | RT_PID_FEEDBACK | RT→Jetson | 6 | `{i16 sp, i16 meas, i16 out}` | 10 Hz |
| `0x300` | HOST_DRIVE_CMD | Jetson→RT | 8 | `{i32 speed, i32 yaw}` | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQUEST | Jetson→RT | 4 | `i32 brake_pressure_kpa` | On demand |
| `0x302` | HOST_LIGHT_CMD | Jetson→RT | 1 | `u8` bitfield | On change |
| `0x400` | RT_OBSTACLE_DIST | RT→Jetson | 4 | `u32 distance_mm` | 10 Hz |
| `0x600` | SYS_DIAG | RT→Jetson (fwd) | 8 | diag struct | 1 Hz |
| `0x7FF` | HEARTBEAT | Jetson, RT | 0 | (empty) | 2 Hz |

---

## 8. Mode state machine

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
    │  ESTOP button / CAN 0x001 / HB timeout
    │          │             │
    │     ┌────▼─────┐      │
    └─────┤  ESTOP   │──────┘
          │  mode=2  │  (cannot leave via switch)
          └──────────┘
```

- **Manual**: Rider steers. SYS reads throttle ADC + gear lines, passes through to motor controller via MCP4725 DAC + relays. Brake lever → SYS GPIO → CAN `0x010`. RT idle on steering. DC-DC converter on.
- **Auto**: Jetson `/cmd_vel` → high-level CAN `0x300` → RT kinematics → low-level `0x200` (SYS) + `0x230` (steering). SYS drives MCP4725 DAC + gear relays. Lights from Jetson via `0x302` (RT forwards).
- **Estop**: MCP4725 DAC=0V, all gears OFF, brake engaged (CAN `0x010`), DC-DC off (CAN `0x012`), 12V relay OFF. Exit requires power-cycle.

---

## 9. RT ESP32-S3 — RTOS task layout (9 tasks)

| Task | Prio | Rate | Input | Action |
|------|------|------|-------|--------|
| `can_rx_low` | 5 | Event-driven | TWAI | Low-level CAN frames → `can_rx_low_queue` |
| `can_rx_high` | 5 | Event-driven | MCP2515 SPI | High-level CAN frames → `can_rx_high_queue` |
| `dispatch` | 4 | Event-driven | both RX queues | Route by (bus, ID): 0x300→`cmd_queue`, ESTOP→mode, gateway forwarding |
| `control` | 4 | 100 Hz | `cmd_queue` | Physics + PID + obstacle → `setpoint_queue` |
| `can_tx_low` | 3 | Event-driven | `setpoint_queue` + gateway queue | 0x200, 0x230, 0x302 → TWAI |
| `can_tx_high` | 3 | Event-driven | telemetry + gateway queue | 0x011, 0x120, 0x210, 0x220, 0x400, 0x600 → MCP2515 |
| `obstacle` | 2 | 10 Hz | HC-SR04 | Poll → CAN 0x400 |
| `watchdog` | 1 | 10 Hz | — | Staleness → zero setpoints |
| `heartbeat` | 1 | 2 Hz | — | CAN 0x7FF on both buses |

## 10. SYS ESP32-S3 — RTOS task layout (15 tasks)

| Task | Prio | Rate | Input | Action |
|------|------|------|-------|--------|
| `can_rx` | 5 | Event-driven | TWAI (low-level) | CAN frames → `can_rx_queue` |
| `safety` | 5 | 20 Hz | GPIO 1,2 + RT HB | ESTOP, brake lever, HB timeout → `mode_set(Estop)` |
| `dispatch` | 4 | Event-driven | `can_rx_queue` | 0x200→`setpoint_queue`, 0x302→light state, 0x001→ESTOP |
| `mode` | 4 | 10 Hz | GPIO 11 | Mode switch → `mode_set(Auto/Manual)`, send CAN 0x110 |
| `motor` | 4 | 100 Hz | `setpoint_queue` | AUTO: MCP4725 DAC + gear. MANUAL: ADC pass-through. ESTOP: all off. |
| `throttle` | 3 | 100 Hz | ADC1_CH5 (GPIO10) | Read, send CAN 0x120 |
| `gear` | 3 | 50 Hz | GPIO 12-14 + setpoint | Read sense or setpoint, drive relays GPIO 33-35 |
| `brake` | 3 | 20 Hz | — | ESTOP/lever → CAN 0x010 engage; else release |
| `lights` | 3 | 20 Hz | `g_light_state` | Drive GPIO 18-22 with blink timing |
| `dcdc` | 3 | 5 Hz | — | ESTOP → CAN 0x012 enable=0; else enable=1 |
| `indicator` | 2 | 5 Hz | — | Mode LEDs GPIO 25-26 |
| `power` | 2 | 5 Hz | — | 12V relay GPIO 27 |
| `can_tx` | 2 | 5 Hz | — | Send CAN 0x011 safety status |
| `diag` | 1 | 1 Hz | — | Heap, mode, TEC/REC → CAN 0x600 |
| `hb` | 1 | 2 Hz | — | CAN 0x7FF on low-level |
