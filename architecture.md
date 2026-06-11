# E-Trike System Architecture

Three-node distributed control: **Jetson Orin NX** (ROS 2 perception/planning), **RT ESP32-S3** (realtime physics, steering & CAN gateway), **SYS ESP32-S3** (safety, motor actuation & body control).

Two physical CAN buses at 500 kbit/s. RT is the only node on both buses and bridges selected messages.

- **Low-level CAN**: RT, SYS, brake module, steering module, DC-DC converter (72V→12V). Safety-critical actuation and inter-MCU.
- **High-level CAN**: RT, Jetson. Commands, telemetry, ROS 2 bridge.

---

## 1. Topology

```
  ┌────────────────── High-Level CAN (500 kbit/s) ──────────────────┐
  │                                                                  │
  │  ┌──────────┐            ┌──────────────┐                       │
  │  │  Jetson  │            │  RT ESP32-S3 │                       │
  │  │  Orin NX │            │              │                       │
  │  │          │            │ Physics      │                       │
  │  │ ROS 2    │            │ Steering     │                       │
  │  │ Planning │            │ PID          │                       │
  │  └────┬─────┘            │ CAN Gateway  │                       │
  │       │                  └──────┬───────┘                       │
  │  TX:  0x300,0x301,    TX: 0x011,0x120, │                        │
  │       0x302,0x001           0x210,0x220,│                        │
  │                              0x400,0x600,│                       │
  │  RX:  0x011,0x120,          0x001,0x7FF │                        │
  │       0x210,0x220,      RX: 0x300,0x301,│                        │
  │       0x400,0x600,          0x302,0x001 │                        │
  │       0x001,0x7FF                       │                        │
  └─────────────────────────────────────────┘                        │
                                            │                        │
                 ┌──────────────────────────┘                        │
                 │                                                    │
  ┌──────────────▼─────── Low-Level CAN (500 kbit/s) ───────────────┐│
  │                                                                  ││
  │  ┌──────────────┐      ┌──────────────┐                         ││
  │  │  RT ESP32-S3 │      │ SYS ESP32-S3 │                         ││
  │  │  (gateway)   │      │              │                         ││
  │  └──────┬───────┘      │ Safety       │                         ││
  │         │              │ Motor        │                         ││
  │    TX:  0x200,0x230,   │ Brake        │                         ││
  │         0x302,0x001,   └──────┬───────┘                         ││
  │         0x7FF                 │                                  ││
  │                          TX:  0x010,0x011,                      ││
  │    RX:  0x001,0x011,          0x012,0x110,                      ││
  │         0x110,0x120,          0x120,0x600,                      ││
  │         0x600,0x7FF           0x001,0x7FF                       ││
  │                          RX:  0x001,0x200,                      ││
  │                               0x302,0x7FF                       ││
  └─────────────────────────────────────────────────────────────────┘│
                                        │                            │
                       ┌────────────────┼────────────────┐           │
                       │                │                │           │
                 ┌─────▼─────┐   ┌─────▼─────┐   ┌─────▼─────┐     │
                 │  Brake    │   │ Steering   │   │   DC-DC   │     │
                 │ CAN Mod.  │   │ CAN Mod.   │   │ Converter │     │
                 │ (0x010)   │   │ (0x230)    │   │ (0x012)   │     │
                 └───────────┘   └───────────┘   └───────────┘     │
                                                                    │
  ┌───────────┐                                                    │
  │  Motor    │  (analog: 0–5 V throttle via MCP4725,              │
  │Controller │   72 V gear lines via relay module, from SYS)      │
  └───────────┘                                                    │
```

---

## 2. CAN message catalog

### 2.1 Low-level CAN

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Prio |
|----|------|--------|-------------|-----|---------|--------|------|
| `0x001` | SAFETY_ESTOP | Any | All (bridged to high-level) | 0 | (none) | Event | Highest |
| `0x010` | SYS_BRAKE_CMD | SYS | Brake CAN module | 1 | u8 engage | Change | V.High |
| `0x011` | SYS_SAFETY_STATUS | SYS | RT (→ Jetson) | 2 | u8 estop, u8 hb_ok | 5 Hz | V.High |
| `0x012` | SYS_DCDC_CMD | SYS | DC-DC converter | 1 | u8 enable | Change | V.High |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | u8 mode (0=M, 1=A) | Change | High |
| `0x120` | SYS_THROTTLE_POS | SYS | RT (→ Jetson) | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x200` | RT_DRIVE_SETPOINT | RT | SYS | 5 | i32 speed_mmps, u8 gear | 100 Hz | Medium |
| `0x230` | RT_STEER_CMD | RT | Steering CAN module | 4 | i32 angle_mdeg | 100 Hz | Medium |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | SYS | 1 | u8 lights bitfield | Change | Medium |
| `0x600` | SYS_DIAG | SYS | RT (→ Jetson) | 8 | diag struct | 1 Hz | Lowest |
| `0x7FF` | HEARTBEAT | RT, SYS | RT, SYS | 0 | (none) | 2 Hz | Lowest |

### 2.2 High-level CAN

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Prio |
|----|------|--------|-------------|-----|---------|--------|------|
| `0x001` | SAFETY_ESTOP | RT (fwd), Jetson | Jetson, RT | 0 | (none) | Event | Highest |
| `0x011` | SYS_SAFETY_STATUS | RT (fwd) | Jetson | 2 | u8 estop, u8 hb_ok | 5 Hz | V.High |
| `0x120` | SYS_THROTTLE_POS | RT (fwd) | Jetson | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x210` | RT_STATE_REPORT | RT | Jetson | 3 | u8 mode, u8 steer_valid, u8 reversing | 10 Hz | Low |
| `0x220` | RT_PID_FEEDBACK | RT | Jetson | 6 | i16 sp, i16 meas, i16 out | 10 Hz | Low |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | i32 speed_mmps, i32 yaw_rate_mrad_s | ≤100 Hz | Medium |
| `0x301` | HOST_BRAKE_REQUEST | Jetson | RT | 4 | i32 brake_pressure_kpa | Demand | Medium |
| `0x302` | HOST_LIGHT_CMD | Jetson | RT (→ SYS) | 1 | u8 lights bitfield | Change | Medium |
| `0x400` | RT_OBSTACLE_DIST | RT | Jetson | 4 | u32 distance_mm | 10 Hz | Low |
| `0x600` | SYS_DIAG | RT (fwd) | Jetson | 8 | diag struct | 1 Hz | Lowest |
| `0x7FF` | HEARTBEAT | RT, Jetson | RT, Jetson | 0 | (none) | 2 Hz | Lowest |

> Bit-level signal layouts, byte ordering, and scaling are in [`can-dictionary.md`](can-dictionary.md).

### 2.3 CAN gateway forwarding (RT)

| Direction | IDs forwarded | Notes |
|-----------|--------------|-------|
| Low → High | `0x001`, `0x011`, `0x120`, `0x600` | Transparent — same ID, same payload |
| High → Low | `0x001`, `0x302` | Transparent |

**Not forwarded**: `0x300`, `0x301` (consumed by RT); `0x200`, `0x230` (RT-generated on low-level); `0x210`, `0x220`, `0x400` (RT-generated on high-level); `0x010`, `0x012`, `0x110` (low-level only); `0x7FF` (independent per bus).

---

## 3. Mode state machine

```
         ┌──────────┐
    ┌───▶│  MANUAL  │◀───┐
    │    └─────┬────┘    │
    │     switch=AUTO  switch=MANUAL
    │          │          │
    │    ┌─────▼────┐    │
    │    │   AUTO   │    │
    │    └─────┬────┘    │
    │          │          │
    │  ESTOP button / CAN 0x001 / HB timeout
    │          │          │
    │    ┌─────▼────┐    │
    └────│  ESTOP   │────┘  (cannot leave via switch)
         └──────────┘
```

| Mode | Behavior |
|------|----------|
| **MANUAL** | Rider steers / rides throttle. SYS reads throttle ADC + gear sense, passes through to motor controller via MCP4725 DAC + relays. Brake lever → SYS GPIO → CAN `0x010`. Steering module standalone (RT idle). DC-DC on. |
| **AUTO** | Jetson `/cmd_vel` → high-level CAN `0x300` → RT kinematics + PID → low-level `0x200` (SYS: speed+gear) + `0x230` (steering module). SYS drives MCP4725 DAC + gear relays. Lights from Jetson via `0x302` (RT forwards). |
| **ESTOP** | MCP4725 DAC = 0 V, all gear outputs OFF, brake engaged (`0x010`), DC-DC off (`0x012`), 12V relay OFF. Exit requires power-cycle or explicit CAN command (TBD). |

---

## 4. Signal flow

### 4.1 Manual mode

```
Throttle grip (0–5V) ──► SYS ADC ──► SYS MCP4725 (0–5V) ──► Motor controller
Gear selector (72V)  ──► TLP281 opto → SYS GPIO ──► relay module → 72V → ECU
Brake lever           ──► SYS GPIO ──► CAN 0x010 ──► Brake CAN module
Steering wheel        ──► Steering CAN module (standalone)
Signal lights         ──► Rider switches → SYS GPIO → relays → lamps
DC-DC converter       ──► SYS CAN 0x012 enable=1 → 12V rail on
```

### 4.2 Auto mode

```
Jetson /cmd_vel ──► High CAN 0x300 ──► RT kinematics + PID
                                          │
               ┌──────────────────────────┤
               ▼ (low CAN)                ▼ (low CAN)
   0x200 {speed, gear} → SYS        0x230 {angle} → Steering CAN module
               │
               ├──► MCP4725 (0–5V) → Motor controller
               ├──► Relay module (72V) → ECU gear wire
               └──► GPIO → Signal lights (from 0x302 fwd by RT)

Jetson ──► High CAN 0x301 ──► RT brake arbitration (TBD path to SYS)
Jetson ──► High CAN 0x302 ──► RT fwd → Low CAN 0x302 → SYS → light relays
SYS ────► Low CAN 0x010 ──► Brake CAN module (ESTOP / lever)
```

---

## 5. Responsibility split

| Concern | Jetson | RT | SYS |
|---------|--------|-----|-----|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| CAN gateway (low ↔ high) | | ✓ | |
| Tricycle kinematics | | ✓ | |
| Speed PID | | ✓ | |
| Steering angle compute + CAN TX (`0x230`) | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| E-stop GPIO + button | | | ✓ |
| Brake lever → CAN (`0x010`) | | | ✓ |
| DC-DC converter CAN control (`0x012`) | | | ✓ |
| Heartbeat monitoring | | ✓ (Jetson, high) | ✓ (RT, low) |
| Mode switch reading | | | ✓ |
| Throttle ADC read (0–5V) | | | ✓ |
| Throttle MCP4725 DAC output (0–5V) | | | ✓ |
| Gear 72V read (TLP281 opto) | | | ✓ |
| Gear 72V output (relay module) | | | ✓ |
| 12V accessory power relay | | | ✓ |
| Mode indicator lights | | | ✓ |
| Signal lights (turn, brake, head) | | | ✓ |
| System diagnostics | | | ✓ |

---

## 6. Design principles

1. **Queues over shared state.** Tasks communicate through thread-safe queues. No mutexes, no semaphores.
2. **ESTOP bypasses queues.** The safety task preempts everything and writes directly to actuators.
3. **One CAN ID = one sender per bus.** No duplicate senders on the same bus (except heartbeat `0x7FF`).
4. **Lower CAN ID = higher bus priority.** Safety IDs (`0x00X`) win arbitration.
5. **All multi-byte CAN fields are big-endian (MSB first).**
6. **Manual mode is pass-through, not dead.** SYS reads physical inputs and mirrors them to outputs. CAN bus is live for telemetry but does not override the rider.
7. **Actuators are standalone CAN modules.** Brake, steering, and DC-DC converter are commanded via CAN, not bit-banged.
8. **RT is the only dual-bus node.** No direct Jetson ↔ SYS path. All cross-bus traffic goes through RT.

---

## 7. RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

### 7.1 Role

Converts ROS 2 motion commands (high-level CAN `0x300` from Jetson) into:
- **Speed + gear** → low-level CAN `0x200` → SYS
- **Steering angle** → low-level CAN `0x230` → steering CAN module

Bridges selected CAN messages between buses (§2.3).

**9 FreeRTOS tasks** on ESP32-S3 @ 240 MHz, 1000 Hz tick.

### 7.2 Dual CAN hardware

| Bus | Controller | Interface | GPIO | Transceiver |
|-----|-----------|-----------|------|-------------|
| Low-level | Built-in TWAI | Direct | TX=5, RX=4 | SN65HVD230 |
| High-level | MCP2515 | SPI | SCK=36, MOSI=37, MISO=38, CS=39, INT=40 | SN65HVD230 |

### 7.3 CAN messages received

| Bus | ID | Name | Payload | Action |
|-----|-----|------|---------|--------|
| Low | `0x001` | SAFETY_ESTOP | — | `mode_set(Estop)`, forward to high |
| Low | `0x011` | SYS_SAFETY_STATUS | `{u8 estop, u8 hb_ok}` | Forward to high |
| Low | `0x110` | SYS_MODE_CMD | `u8 mode` | `mode_set(Manual/Auto)` |
| Low | `0x120` | SYS_THROTTLE_POS | `i16 speed_mmps` | Forward to high |
| Low | `0x600` | SYS_DIAG | 8 bytes | Forward to high |
| Low | `0x7FF` | HEARTBEAT | — | Feed SYS heartbeat |
| High | `0x001` | SAFETY_ESTOP | — | `mode_set(Estop)`, forward to low |
| High | `0x300` | HOST_DRIVE_CMD | `{i32 speed, i32 yaw}` | → `cmd_queue` |
| High | `0x301` | HOST_BRAKE_REQUEST | `i32 pressure_kpa` | → atomic store |
| High | `0x302` | HOST_LIGHT_CMD | `u8` bitfield | Forward to low |
| High | `0x7FF` | HEARTBEAT | — | Feed Jetson heartbeat |

### 7.4 CAN messages sent

| Bus | ID | Name | Payload | Rate |
|-----|-----|------|---------|------|
| Low | `0x001` | SAFETY_ESTOP | — | Event |
| Low | `0x200` | RT_DRIVE_SETPOINT | `{i32 speed, u8 gear}` | 100 Hz |
| Low | `0x230` | RT_STEER_CMD | `i32 angle_mdeg` | 100 Hz |
| Low | `0x302` | HOST_LIGHT_CMD (fwd) | `u8` bitfield | Change |
| Low | `0x7FF` | HEARTBEAT | — | 2 Hz |
| High | `0x001` | SAFETY_ESTOP (fwd) | — | Event |
| High | `0x011` | SYS_SAFETY_STATUS (fwd) | `{u8 estop, u8 hb_ok}` | 5 Hz |
| High | `0x120` | SYS_THROTTLE_POS (fwd) | `i16 speed_mmps` | 100 Hz |
| High | `0x210` | RT_STATE_REPORT | `{u8 mode, u8 steer_valid, u8 reversing}` | 10 Hz |
| High | `0x220` | RT_PID_FEEDBACK | `{i16 sp, i16 meas, i16 out}` | 10 Hz |
| High | `0x400` | RT_OBSTACLE_DIST | `u32 distance_mm` | 10 Hz |
| High | `0x600` | SYS_DIAG (fwd) | 8 bytes | 1 Hz |
| High | `0x7FF` | HEARTBEAT | — | 2 Hz |

### 7.5 Internal data types

```cpp
enum class Mode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };

struct DriveCmd {
    int32_t speed_mmps      = 0;   // [-500, 3000]
    int32_t yaw_rate_mrad_s = 0;   // [-3000, 3000]
};

struct ResolvedSetpoint {
    int32_t motor_speed_mmps = 0;
    int32_t steer_angle_mdeg = 0;  // ±45000, +right
    uint8_t gear             = 0;  // Gear enum
    bool    steer_valid      = false;
    bool    reversing        = false;
};

struct PidState {
    float kp = 1.0f, ki = 0.1f, kd = 0.05f;
    float integral    = 0.0f;
    float prev_error  = 0.0f;
    float output      = 0.0f;
    bool  first_call  = true;
};
```

### 7.6 Control mechanisms

#### Tricycle kinematics

$$\delta = \arctan\left(\frac{L \cdot \omega}{|v|}\right) \quad L = 1500\text{ mm}$$

```
physics_resolve(cmd):
  1. Convert mm/s→m/s, mrad/s→rad/s
  2. If |v| > 50 mm/s: δ = atan2(L·ω, |v|), steer_valid = true
     Else: δ = steer_hold · 0.8 (decay), steer_valid = false
  3. Clamp δ to ±45° (±45000 mdeg)
  4. Clamp v to [-500, 3000] mm/s
  5. reversing = v < 0
  6. gear: v > 0 → D, v == 0 → N, v < 0 → R
```

#### Speed PID

| Param | Value |
|-------|-------|
| Kp | 1.0 |
| Ki | 0.1 |
| Kd | 0.05 |
| Integral clamp | ±500 |
| Rate | 100 Hz |

Anti-windup: integral clamped to ±500. First sample after reset: skip D-term.

#### Steering (CAN drive-by-wire)

| Mode | Behavior |
|------|----------|
| MANUAL | RT does NOT send `0x230` — module standalone |
| AUTO | RT sends `0x230` at 100 Hz |
| ESTOP | RT stops — module should center/lock |

#### Obstacle speed limiting

$$v_{limited} = v_{target} \cdot \frac{d - 300}{3000 - 300}$$

Clamped: if d ≤ 300 mm → 0, if d ≥ 3000 mm → target unchanged.

#### Command staleness watchdog

| Param | Value |
|-------|-------|
| Timeout | 500 ms |
| Check rate | 10 Hz |
| Action | Zero setpoints on low-level (`0x200` + `0x230`) |

`watchdog_feed()` on every valid `0x300`. `watchdog_is_stale()` compares `esp_timer_get_time() - last_feed > 500ms`.

#### Brake arbitration (max-select)

```
brake_kpa = max(rt_computed, jetson_request)
```

Jetson can increase but never decrease below RT's safety floor. **Gap**: arbitrated result has no CAN path to SYS (§13).

### 7.7 RTOS task layout

```
Pri 5  can_rx_low   ── TWAI → can_rx_low_queue (16)
      can_rx_high  ── MCP2515 SPI → can_rx_high_queue (16)

Pri 4  dispatch     ◀── both RX queues
           Routes: high 0x300→cmd_queue, high 0x301→atomic, high 0x302→gw_tx_low
                   low 0x011→gw_tx_high, low 0x120→gw_tx_high, low 0x600→gw_tx_high
                   any 0x001→mode_set(Estop)+gateway, low 0x110→mode_set

Pri 4  control      ◀── cmd_queue (4, overwrite)
           100 Hz: kinematics + PID + obstacle + brake max-select → setpoint_queue

Pri 3  can_tx_low   ◀── setpoint_queue + gw_tx_low_queue → 0x200, 0x230, 0x302 on TWAI
      can_tx_high  ◀── telemetry + gw_tx_high_queue → 0x011,0x120,0x210,0x220,0x400,0x600 on MCP2515

Pri 2  obstacle     ── HC-SR04 @ 10 Hz → high CAN 0x400

Pri 1  watchdog     ── 10 Hz staleness check
      heartbeat    ── 2 Hz 0x7FF on both buses
```

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx_low` | 5 | 4096 B | Event | `twai_receive()`, copy to queue |
| `can_rx_high` | 5 | 4096 B | Event | MCP2515 SPI poll/INT, copy to queue |
| `dispatch` | 4 | 4096 B | Event | Parse both RX queues, route + gateway |
| `control` | 4 | 4096 B | 100 Hz | Kinematics, PID, obstacle, brake, gear derivation |
| `can_tx_low` | 3 | 3072 B | Event | Serialize 0x200, 0x230, 0x302 → TWAI |
| `can_tx_high` | 3 | 3072 B | Event | Serialize telemetry → MCP2515 SPI |
| `obstacle` | 2 | 2048 B | 10 Hz | HC-SR04 trigger/echo → CAN 0x400 |
| `watchdog` | 1 | 2048 B | 10 Hz | Staleness → zero setpoints |
| `heartbeat` | 1 | 2048 B | 2 Hz | 0x7FF on both buses |

### 7.8 Queue design

| Queue | Slots | Pattern |
|-------|-------|---------|
| `can_rx_low_queue` | 16 | `xQueueSend` timeout=0 (drop) |
| `can_rx_high_queue` | 16 | `xQueueSend` timeout=0 (drop) |
| `cmd_queue` | 4 | `xQueueOverwrite` |
| `setpoint_queue` | 4 | `xQueueOverwrite` |
| `gw_tx_low_queue` | 8 | `xQueueSend` timeout=0 |
| `gw_tx_high_queue` | 8 | `xQueueSend` timeout=0 |

### 7.9 Hardware pin assignments

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX (low) | 5 | Out | SN65HVD230 |
| CAN RX (low) | 4 | In | SN65HVD230 |
| SPI SCK | 36 | Out | MCP2515 (high CAN) |
| SPI MOSI | 37 | Out | MCP2515 |
| SPI MISO | 38 | In | MCP2515 |
| SPI CS | 39 | Out | MCP2515 |
| MCP INT | 40 | In | MCP2515 interrupt |
| Ultrasonic TRIG | 7 | Out | HC-SR04 |
| Ultrasonic ECHO | 8 | In | HC-SR04 |
| Encoder A | 1 | In | Speed feedback (PCNT) |
| Encoder B | 2 | In | Speed feedback (PCNT) |
| I2C SDA | 10 | I/O | IMU (optional) |
| I2C SCL | 11 | Out | IMU (optional) |

### 7.10 Configuration constants

```cpp
namespace rt {
constexpr float kWheelbaseMM = 1500.0f;
constexpr float kSteerLimitDeg = 45.0f;
constexpr int   kSteerLimitMdeg = 45000;
constexpr int   kMaxSpeedFwdMmps = 3000, kMaxSpeedRevMmps = 500;
constexpr int   kLowSpeedThreshMmps = 50;
constexpr float kPidKp = 1.0f, kPidKi = 0.1f, kPidKd = 0.05f, kPidMaxIntegral = 500.0f;
constexpr unsigned kObstacleStopMM = 300, kObstacleClearMM = 3000;
constexpr int kControlLoopHz = 100, kCmdStaleTimeoutMs = 500, kHeartbeatMs = 500;
constexpr int kCanLowBitrateHz = 500000, kCanHighBitrateHz = 500000;
} // namespace rt
```

### 7.11 Error handling

| Failure | Detection | Response |
|---------|-----------|----------|
| Low CAN bus-off | TWAI TEC > 255 | Log, auto-recover; ESTOP if persistent |
| High CAN bus-off | MCP2515 error flags | Log, auto-recover; zero setpoints until restored |
| Command stale | Watchdog 500 ms | Zero setpoints on low-level |
| Obstacle timeout | Echo > 30 ms | Distance = UINT32_MAX |
| Encoder missing | Speed = 0 | PID on stale measurement |
| Steering CAN TX fail | TWAI TX errors | Log, module should hold last angle |
| Gateway queue full | `xQueueSend` fail | Drop (except 0x001 — direct TX) |

### 7.12 Startup

```
1. can_low_init() → TWAI driver, low-level CAN
2. can_high_init() → SPI + MCP2515 config, high-level CAN
3. obstacle_init() → TRIG/ECHO GPIOs
4. pid_init() → load gains
5. watchdog_init() → initial timestamp
6. Create queues (6)
7. Create 9 tasks
8. ESP_LOGI("Ready")
```

---

## 8. SYS ESP32-S3 — Safety, Motor Actuation & Body Control

### 8.1 Role

Owns safety (E-stop, brake lever, RT heartbeat watchdog), motor actuation (0–5V throttle via MCP4725, 72V gear via relay module), DC-DC converter control, signal lights, mode indicators, 12V accessory power, and diagnostics.

Connected to **low-level CAN only**. All Jetson communication goes through RT.

**15 FreeRTOS tasks** on ESP32-S3 @ 240 MHz, 1000 Hz tick.

### 8.2 CAN interface

Single CAN bus: built-in TWAI, GPIO 4/5, 500 kbit/s, SN65HVD230 transceiver.

### 8.3 CAN messages received

| ID | Name | Payload | Source | Action |
|----|------|---------|--------|--------|
| `0x001` | SAFETY_ESTOP | — | RT or any | `mode_set(Estop)` |
| `0x200` | RT_DRIVE_SETPOINT | `{i32 speed, u8 gear}` | RT | → `setpoint_queue` |
| `0x302` | HOST_LIGHT_CMD (fwd) | `u8` bitfield | RT | → `g_light_state` |
| `0x7FF` | HEARTBEAT | — | RT | Feed RT heartbeat |

### 8.4 CAN messages sent

| ID | Name | Payload | Rate | Notes |
|----|------|---------|------|-------|
| `0x010` | SYS_BRAKE_CMD | `u8 engage` | Change | → Brake CAN module |
| `0x011` | SYS_SAFETY_STATUS | `{u8 estop, u8 hb_ok}` | 5 Hz | → RT (fwd to Jetson) |
| `0x012` | SYS_DCDC_CMD | `u8 enable` | Change | → DC-DC converter |
| `0x110` | SYS_MODE_CMD | `u8 mode` | Change | → RT |
| `0x120` | SYS_THROTTLE_POS | `i16 speed_mmps` | 100 Hz | → RT (fwd to Jetson) |
| `0x600` | SYS_DIAG | 8 bytes | 1 Hz | → RT (fwd to Jetson) |
| `0x7FF` | HEARTBEAT | — | 2 Hz | → RT |

### 8.5 Internal data types

```cpp
enum class SysMode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };

struct ActuatorSetpoint {
    int32_t motor_speed_mmps = 0;
    Gear    gear             = Gear::N;
};

struct LightState {
    bool left_turn = false, right_turn = false;
    bool brake_light = false, headlight = false;
};

// Mode manager
SysMode mode_get_current();
void    mode_set(SysMode m);  // ESTOP overrides: if Estop && m != Estop → no-op

// Safety monitor
bool safety_estop_active();        // GPIO1 LOW
bool safety_brake_lever_pressed(); // GPIO2 LOW
bool safety_heartbeat_ok();        // RT HB within 1500 ms (low-level CAN)
```

> SYS monitors only RT heartbeat on low-level CAN. RT monitors Jetson on high-level. If Jetson lost, RT sends zero setpoints → controlled stop.

### 8.6 Control mechanisms

#### Mode state machine

```
                    ┌──────────┐
        switch=0 ┌──│  MANUAL  │──┐ switch=1
       (pull-up) │  │  mode=0  │  │ (GND)
                 │  └────┬─────┘  │
                 ▼       │        ▼
           ┌──────────┐  │  ┌──────────┐
           │  ESTOP   │◀─┘  │   AUTO   │
           │  mode=2  │     │  mode=1  │
           └────┬─────┘     └──────────┘
                │    ▲
 E-stop btn ────┤    ├─ CAN 0x001
 HB timeout ────┤    │
 brake lever ───┘    └─ (any → ESTOP)
```

Rules: Mode switch GPIO11 (pull-up=Manual, GND=Auto). ESTOP cannot be cleared by switch. Brake lever does not change mode.

#### Throttle — MCP4725 I2C DAC (0–5V)

| Parameter | Value |
|-----------|-------|
| DAC device | MCP4725, I2C addr 0x60 |
| I2C pins | SDA=GPIO15, SCL=GPIO16 |
| Resolution | 12-bit (0–4095) |
| Output | 0–5V (VCC=5V, no op-amp) |
| ADC read | ADC1_CH5, GPIO10, 12-bit, voltage divider 5V→3.3V |
| Dead zone | 200 (raw ADC) |
| Max speed | 3000 mm/s |

| Mode | Throttle behavior |
|------|------------------|
| MANUAL | ADC read → MCP4725 write (pass-through) |
| AUTO | `setpoint.speed` → `abs(speed)/3000 × 4095` → MCP4725 |
| ESTOP | MCP4725 = 0 |

> MCP4725 outputs 0–5V directly (powered from 5V rail). Direction via gear lines. No op-amp needed.

#### Gear — TLP281 input + relay output (72V)

**Input stage** (manual mode sense):

| Signal | GPIO | Conditioning |
|--------|------|-------------|
| Gear D sense | 12 | TLP281 optoisolator ch1 (72V→3.3V, galvanic isolation) |
| Gear S sense | 13 | TLP281 optoisolator ch2 |
| Gear R sense | 14 | TLP281 optoisolator ch3 |

**Output stage** (auto mode, mimic 72V to ECU):

| Signal | GPIO | Path |
|--------|------|------|
| Gear D out | 33 | 4-ch 5V relay ch1: GPIO→IN, 72V→1A fuse→COM→NO→ECU |
| Gear S out | 34 | 4-ch 5V relay ch2: GPIO→IN, 72V→1A fuse→COM→NO→ECU |
| Gear R out | 35 | 4-ch 5V relay ch3: GPIO→IN, 72V→1A fuse→COM→NO→ECU |

**Protection circuit**:

```
72V Batt ──┬──[1A fast-blow fuse]──┬── RELAY COM (D) ── NO ──┬── ECU Gear D ───┬─ [TVS SMCJ90CA] ── GND
           │                       ├── RELAY COM (S) ── NO ──┼── ECU Gear S ───┼─ [TVS SMCJ90CA] ── GND
           │                       └── RELAY COM (R) ── NO ──┼── ECU Gear R ───┴─ [TVS SMCJ90CA] ── GND
```

| Protection | Part | Rating | Purpose |
|-----------|------|--------|---------|
| Fuse | 1A fast-blow | 72V, 1A | Overcurrent/short on 72V rail |
| TVS ×3 | SMCJ90CA bidirectional | 90–100V standoff, 1500W peak | Clamp transients to GND |

| Mode | Gear behavior |
|------|--------------|
| MANUAL | Read TLP281 GPIOs → mirror to relays (pass-through) |
| AUTO | Read gear from CAN `0x200` → energize corresponding relay |
| ESTOP | All relays OFF (N) |

#### Brake — CAN module

| Condition | CAN `0x010` |
|-----------|------------|
| ESTOP or brake lever pressed | `engage = 1` |
| AUTO/MANUAL, no lever | `engage = 0` |

Sent on state change only. Brake lever on GPIO2 (active-low, pull-up).

> **Gap**: RT brake arbitration result never reaches SYS. Jetson `0x301` braking and RT emergency braking are computed but not actuated.

#### DC-DC converter — CAN (`0x012`)

| Condition | CAN `0x012` |
|-----------|------------|
| MANUAL or AUTO | `enable = 1` (12V rail on) |
| ESTOP | `enable = 0` (12V rail dead) |

Sent on state change. The 12V accessory relay (GPIO27) is a secondary cut.

#### Signal lights

| Signal | GPIO | Notes |
|--------|------|-------|
| Left turn | 18 | Blink 500ms on/off while active |
| Right turn | 19 | |
| Brake light | 21 | Solid |
| Headlight | 22 | On/off |

| Mode | Control |
|------|---------|
| MANUAL | Rider switches → GPIOs (TBD: switch GPIO assignments) |
| AUTO | `g_light_state` from CAN `0x302` (forwarded by RT) |
| ESTOP | Brake light ON, all others OFF |

#### Mode indicators, 12V relay

| Signal | GPIO | Notes |
|--------|------|-------|
| AUTO LED | 25 | ON in AUTO |
| MANUAL LED | 26 | ON in MANUAL; both OFF = ESTOP |
| 12V relay | 27 | HIGH=ON; OFF on ESTOP |

#### Heartbeat watchdog

| Parameter | Value |
|-----------|-------|
| Required source | RT (low-level `0x7FF`) |
| Timeout | 1500 ms |
| Check rate | 20 Hz |
| Action | ESTOP if in AUTO and RT HB lost |

#### Safety monitor

| Signal | GPIO | Active | Response |
|--------|------|--------|----------|
| E-stop button | 1 | LOW | `mode_set(Estop)` |
| Brake lever | 2 | LOW | Engage brake (no mode change) |
| RT HB timeout | CAN | >1500 ms | `mode_set(Estop)` (AUTO only) |

`motor_set_speed()`, `gear_set_outputs()`, and `dcdc_set_output()` all check for ESTOP as defense-in-depth.

### 8.7 RTOS task layout

```
Pri 5  can_rx      ── TWAI → can_rx_queue (16)
       safety      ── GPIO poll @ 20 Hz → ESTOP / HB check

Pri 4  dispatch    ◀── can_rx_queue: 0x200→setpoint, 0x302→light_state, 0x001→ESTOP
       mode        ── GPIO11 @ 10 Hz → mode_set(), CAN 0x110
       motor       ◀── setpoint_queue (4, overwrite)
             100 Hz: AUTO→MCP4725+gear, MANUAL→pass-through, ESTOP→all off

Pri 3  throttle    ── ADC @ 100 Hz → CAN 0x120
       gear        ── Gear FSM @ 50 Hz
       brake       ── Brake FSM @ 20 Hz → CAN 0x010
       lights      ── Light FSM @ 20 Hz (blink timing, ESTOP=brake ON)
       dcdc        ── DCDC FSM @ 5 Hz → CAN 0x012

Pri 2  indicator   ── LEDs @ 5 Hz
       power       ── 12V relay @ 5 Hz
       can_tx      ── Safety status @ 5 Hz → CAN 0x011

Pri 1  diag        ── System health @ 1 Hz → CAN 0x600
       hb          ── 0x7FF @ 2 Hz
```

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx` | 5 | 4096 B | Event | `twai_receive()`, copy to queue |
| `safety` | 5 | 2048 B | 20 Hz | ESTOP GPIO, RT HB timeout |
| `dispatch` | 4 | 3072 B | Event | Route 0x200, 0x302, 0x001 |
| `mode` | 4 | 2048 B | 10 Hz | Mode switch, CAN 0x110 |
| `motor` | 4 | 2048 B | 100 Hz | MCP4725 DAC + gear outputs |
| `throttle` | 3 | 1536 B | 100 Hz | ADC read, CAN 0x120 |
| `gear` | 3 | 1536 B | 50 Hz | TLP281 read / setpoint → relays |
| `brake` | 3 | 1536 B | 20 Hz | Brake FSM, CAN 0x010 |
| `lights` | 3 | 1536 B | 20 Hz | Light GPIOs + blink timing |
| `dcdc` | 3 | 1024 B | 5 Hz | DCDC FSM, CAN 0x012 |
| `indicator` | 2 | 1024 B | 5 Hz | Mode LEDs |
| `power` | 2 | 1024 B | 5 Hz | 12V relay |
| `can_tx` | 2 | 3072 B | 5 Hz | CAN 0x011 |
| `diag` | 1 | 2048 B | 1 Hz | CAN 0x600 |
| `hb` | 1 | 2048 B | 2 Hz | CAN 0x7FF |

### 8.8 Queue design

| Queue | Slots | Pattern |
|-------|-------|---------|
| `can_rx_queue` | 16 | `xQueueSend` timeout=0 (drop) |
| `setpoint_queue` | 4 | `xQueueOverwrite` |

### 8.9 Hardware pin assignments

| Signal | GPIO | Direction | Conditioning |
|--------|------|-----------|-------------|
| CAN TX (low) | 5 | Out | SN65HVD230 |
| CAN RX (low) | 4 | In | SN65HVD230 |
| E-stop button | 1 | In | Active-low, pull-up |
| Brake lever | 2 | In | Active-low, pull-up |
| Throttle read | 10 | In (ADC1_CH5) | Voltage divider 5V→3.3V |
| Throttle output | — | I2C (SDA=15, SCL=16) | MCP4725, addr 0x60, VCC=5V |
| Gear D sense | 12 | In | TLP281 optoisolator ch1 |
| Gear S sense | 13 | In | TLP281 optoisolator ch2 |
| Gear R sense | 14 | In | TLP281 optoisolator ch3 |
| Gear D output | 33 | Out | Relay ch1 → 1A fuse → ECU, TVS to GND |
| Gear S output | 34 | Out | Relay ch2 → 1A fuse → ECU, TVS to GND |
| Gear R output | 35 | Out | Relay ch3 → 1A fuse → ECU, TVS to GND |
| Mode switch | 11 | In | Pull-up=Manual, GND=Auto |
| Left turn | 18 | Out | Relay → lamp |
| Right turn | 19 | Out | Relay → lamp |
| Brake light | 21 | Out | Relay → lamp |
| Headlight | 22 | Out | Relay → lamp |
| AUTO LED | 25 | Out | |
| MANUAL LED | 26 | Out | |
| 12V relay | 27 | Out | Secondary cut on ESTOP |

### 8.10 Configuration constants

```cpp
namespace sys {
// CAN
constexpr int kCanBitrateHz = 500000, kCanTxGpio = 5, kCanRxGpio = 4;
// Throttle
constexpr int kThrottleAdcChannel = 5;        // ADC1_CH5 → GPIO10
constexpr int kThrottleI2cSda = 15, kThrottleI2cScl = 16;
constexpr uint8_t kThrottleDacI2cAddr = 0x60; // MCP4725
constexpr unsigned kThrottleDeadZone = 200;
constexpr int kThrottleMaxSpeedMmps = 3000;
constexpr int kThrottleDacMaxVal = 4095;      // 12-bit, VCC=5V
// Gear
constexpr int kGearDSense = 12, kGearSSense = 13, kGearRSense = 14;
constexpr int kGearDOut = 33, kGearSOut = 34, kGearROut = 35;
// Safety
constexpr int kEstopGpio = 1, kBrakeLeverGpio = 2, kModeSwitchGpio = 11;
// Lights
constexpr int kLightLeftTurn = 18, kLightRightTurn = 19;
constexpr int kLightBrake = 21, kLightHead = 22;
// Indicators & power
constexpr int kLedAuto = 25, kLedManual = 26, kPower12vRelay = 27;
// Turn blink
constexpr int kTurnBlinkOnMs = 500, kTurnBlinkOffMs = 500;
// Timing
constexpr int kControlLoopHz = 100, kHeartbeatMs = 500;
constexpr int kHeartbeatTimeoutMs = 1500, kSafetyCheckHz = 20, kGearCheckHz = 50;
} // namespace sys
```

### 8.11 Error handling

| Failure | Detection | Response |
|---------|-----------|----------|
| E-stop pressed | GPIO1 LOW | ESTOP → DAC=0, gears off, brake engage, DCDC off, 12V off |
| CAN bus-off | TWAI TEC > 255 | Log, auto-recover |
| RT HB timeout | >1500 ms no `0x7FF` | ESTOP (AUTO only) |
| Brake lever | GPIO2 LOW | Engage brake |
| ADC fail | `adc1_get_raw()==0` | Throttle = 0 |
| Gear sense conflict | Multiple lines HIGH | Treat as N (fail-safe) |
| DCDC CAN TX fail | TWAI TX errors | 12V relay provides backup cut |
| Queue full | `xQueueSend` fail | Frame dropped |

### 8.12 Startup

```
1. can_driver_init()     → TWAI, low-level CAN
2. mode_manager_init()   → GPIO11
3. safety_monitor_init() → GPIO1, GPIO2
4. throttle_init()       → ADC1_CH5 + I2C + MCP4725 (output=0)
5. gear_init()           → GPIO12-14 (IN), GPIO33-35 (OUT, LOW)
6. lights_init()         → GPIO18-22,25-26 (OUT, LOW)
7. power_init()          → GPIO27 (OUT, LOW)
8. brake_init()          → CAN 0x010 engage=0
9. dcdc_init()           → CAN 0x012 enable=0
10. Create queues        → can_rx(16), setpoint(4)
11. Create 15 tasks
12. power_task → 12V relay ON (if not ESTOP)
13. dcdc_task → CAN 0x012 enable=1 (if not ESTOP)
14. ESP_LOGI("Ready")
```

---

## 9. CAN bus device maps

### Low-level

```
 Low-Level CAN (500 kbit/s)
  ├── RT ESP32-S3 (TWAI)       TX: 0x200,0x230,0x302,0x001,0x7FF
  │                             RX: 0x001,0x011,0x110,0x120,0x600,0x7FF
  ├── SYS ESP32-S3              TX: 0x010,0x011,0x012,0x110,0x120,0x600,0x001,0x7FF
  │                             RX: 0x001,0x200,0x302,0x7FF
  ├── Brake CAN module          Listens: 0x010
  ├── Steering CAN module       Listens: 0x230
  └── DC-DC converter (72→12V) Listens: 0x012
```

### High-level

```
 High-Level CAN (500 kbit/s)
  ├── Jetson Orin NX            TX: 0x300,0x301,0x302,0x001,0x7FF
  │                             RX: 0x001,0x011,0x120,0x210,0x220,0x400,0x600,0x7FF
  └── RT ESP32-S3 (MCP2515)     TX: 0x011,0x120,0x210,0x220,0x400,0x600,0x001,0x7FF
                                 RX: 0x001,0x300,0x301,0x302,0x7FF
```

---

## 10. Hardware summary

| Node | Controller | MCU | Framework | CAN interfaces |
|------|-----------|-----|-----------|---------------|
| Jetson | Orin NX | — | ROS 2 | 1× CAN (high-level) |
| RT | ESP32-S3 @ 240 MHz | Xtensa LX7 | ESP-IDF + FreeRTOS | TWAI (low) + MCP2515 SPI (high) |
| SYS | ESP32-S3 @ 240 MHz | Xtensa LX7 | ESP-IDF + FreeRTOS | TWAI (low only) |

| Parameter | Value |
|-----------|-------|
| CAN bitrate (both) | 500 kbit/s |
| CAN transceiver | SN65HVD230 |
| FreeRTOS tick | 1000 Hz |
| RT tasks | 9 |
| SYS tasks | 15 |

---

## 11. Build

```bash
# RT ESP32-S3
cd rt-esp32
pio run              # build
pio run -t upload    # flash
pio device monitor   # serial

# SYS ESP32-S3
cd sys-esp32
pio run && pio run -t upload && pio device monitor
```

---

## 12. Known design gaps

| # | Gap | Impact | Resolution |
|---|-----|--------|------------|
| 1 | RT brake arbitration result has no CAN path to SYS (`0x200` has only speed + gear) | Jetson `0x301` + RT obstacle braking never actuated | Add brake field to `0x200` or new `0x201 RT_BRAKE_CMD` |
| 2 | No CAN message for Jetson to request S (Sport) gear | AUTO can only select D/N/R | Add gear/sport field to `0x300` |
| 3 | Manual mode light switches not assigned GPIOs | Rider can't control signals/headlight in MANUAL | Assign GPIOs, read in `lights_task` |

---

## 13. Reference documents

| File | Content |
|------|---------|
| [`can-dictionary.md`](can-dictionary.md) | Bit-level CAN signal layouts, byte ordering, scaling for all IDs on both buses |
| [`rt-esp32/README.md`](rt-esp32/README.md) | RT build & test quick reference |
| [`sys-esp32/README.md`](sys-esp32/README.md) | SYS build & test quick reference |
