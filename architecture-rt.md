# RT ESP32-S3 Architecture — Realtime Physics, Steering & CAN Gateway

## 1. Role

The RT ESP32-S3 owns **vehicle dynamics and CAN bridging**: tricycle kinematics, speed PID, steering angle computation, obstacle-based speed limiting, and gateway between the two CAN buses. It is the only node connected to both buses.

- **High-level CAN** (MCP2515 via SPI): communicates with Jetson — receives drive commands, sends telemetry.
- **Low-level CAN** (built-in TWAI): communicates with SYS, steering module, brake module, DC-DC converter — sends setpoints and forwards commands.

It converts ROS 2 `/cmd_vel`-style motion commands (received on high-level CAN from Jetson) into:
- **Speed + gear setpoints** sent to SYS ESP32-S3 on low-level CAN (`0x200`), and
- **Steering angle commands** sent to the drive-by-wire steering CAN module on low-level CAN (`0x230`).

It runs **9 FreeRTOS tasks** on a single ESP32-S3 at 240 MHz with a 1000 Hz scheduler tick.

---

## 2. CAN Interface

### 2.1 Dual-bus hardware

| Bus | Controller | Interface | GPIO | Notes |
|-----|-----------|-----------|------|-------|
| Low-level | Built-in TWAI | Direct | TX=5, RX=4 | SN65HVD230. Safety-critical actuation. |
| High-level | MCP2515 | SPI | SCK=36, MOSI=37, MISO=38, CS=39, INT=40 | SN65HVD230. Jetson communication. |

### 2.2 Messages Received

| Bus | CAN ID | Name | DLC | Payload | Source | Action |
|-----|--------|------|-----|---------|--------|--------|
| Low | `0x001` | SAFETY_ESTOP | 0 | (none) | SYS | `mode_set(Estop)`, disable steering, forward to high-level |
| Low | `0x011` | SYS_SAFETY_STATUS | 2 | `u8 estop`, `u8 hb_ok` | SYS | Forward to high-level CAN |
| Low | `0x110` | SYS_MODE_CMD | 1 | `u8 mode` (0=Manual, 1=Auto) | SYS | Switch between Manual/Auto |
| Low | `0x120` | SYS_THROTTLE_POS | 2 | `i16 speed_mmps` | SYS | Forward to high-level CAN |
| Low | `0x600` | SYS_DIAG | 8 | diag struct | SYS | Forward to high-level CAN |
| Low | `0x7FF` | HEARTBEAT | 0 | (none) | SYS | Update SYS heartbeat timestamp |
| High | `0x001` | SAFETY_ESTOP | 0 | (none) | Jetson | `mode_set(Estop)`, forward to low-level CAN |
| High | `0x300` | HOST_DRIVE_CMD | 8 | `i32 speed_mmps`, `i32 yaw_rate_mrad_s` | Jetson | Feed to physics model (Auto only) |
| High | `0x301` | HOST_BRAKE_REQUEST | 4 | `i32 brake_pressure_kpa` | Jetson | Brake pressure request (Auto only, RT-arbitrated) |
| High | `0x302` | HOST_LIGHT_CMD | 1 | `u8 lights` bitfield | Jetson | Forward to low-level CAN |
| High | `0x7FF` | HEARTBEAT | 0 | (none) | Jetson | Update Jetson heartbeat timestamp |

### 2.3 Messages Sent

| Bus | CAN ID | Name | DLC | Payload | Rate | Notes |
|-----|--------|------|-----|---------|------|-------|
| Low | `0x001` | SAFETY_ESTOP | 0 | (none) | On event | Forwarded from high-level, or originated by RT watchdog |
| Low | `0x200` | RT_DRIVE_SETPOINT | 5 | `i32 motor_speed_mmps`, `u8 gear` | 100 Hz | Consumed by SYS motor_task |
| Low | `0x230` | RT_STEER_CMD | 4 | `i32 angle_mdeg` | 100 Hz | Steering angle to drive-by-wire CAN module |
| Low | `0x302` | HOST_LIGHT_CMD | 1 | `u8 lights` bitfield | On change | Forwarded from high-level |
| Low | `0x7FF` | HEARTBEAT | 0 | (none) | 2 Hz | Alive signal |
| High | `0x001` | SAFETY_ESTOP | 0 | (none) | On event | Forwarded from low-level, or originated by RT |
| High | `0x011` | SYS_SAFETY_STATUS | 2 | `u8 estop`, `u8 hb_ok` | 5 Hz | Forwarded from low-level |
| High | `0x120` | SYS_THROTTLE_POS | 2 | `i16 speed_mmps` | 100 Hz | Forwarded from low-level |
| High | `0x210` | RT_STATE_REPORT | 3 | `u8 mode`, `u8 steer_valid`, `u8 reversing` | 10 Hz | Telemetry for Jetson |
| High | `0x220` | RT_PID_FEEDBACK | 6 | `i16 speed_sp`, `i16 speed_meas`, `i16 pid_out` | 10 Hz | PID debug for Jetson |
| High | `0x400` | RT_OBSTACLE_DIST | 4 | `u32 distance_mm` | 10 Hz | Obstacle sensor reading |
| High | `0x600` | SYS_DIAG | 8 | diag struct | 1 Hz | Forwarded from low-level |
| High | `0x7FF` | HEARTBEAT | 0 | (none) | 2 Hz | Alive signal |

### 2.4 CAN Payload Types

```cpp
// 0x300 HOST_DRIVE_CMD — Jetson → RT (high-level)
struct HostDriveCmd {
    int32_t speed_mmps;        // linear.x  [mm/s]   range: [-500, 3000]
    int32_t yaw_rate_mrad_s;   // angular.z [millirad/s]  range: ±3000
    // Serialized: MSB-first, 4 bytes each at offsets 0 and 4
};

// 0x301 HOST_BRAKE_REQUEST — Jetson → RT (high-level)
struct HostBrakeRequest {
    int32_t brake_pressure_kpa;  // desired brake pressure [kPa]; 0 = release
};

// 0x302 HOST_LIGHT_CMD — Jetson → RT (high-level), RT forwards to low-level
struct HostLightCmd {
    bool left_turn;    // b0
    bool right_turn;   // b1
    bool brake_light;  // b2
    bool headlight;    // b3
};

// 0x200 RT_DRIVE_SETPOINT — RT → SYS (low-level)
struct RtDriveSetpoint {
    int32_t motor_speed_mmps;  // range [-500, 3000]
    uint8_t gear;               // 0=N, 1=D, 2=S, 3=R
};

// 0x230 RT_STEER_CMD — RT → Steering module (low-level)
struct RtSteerCmd {
    int32_t angle_mdeg;         // ±45000 mdeg, +right
};

// 0x210 RT_STATE_REPORT — RT → Jetson (high-level)
struct RtStateReport {
    uint8_t mode;              // 0=Manual, 1=Auto, 2=Estop
    bool    steer_valid;
    bool    reversing;
};

// 0x220 RT_PID_FEEDBACK — RT → Jetson (high-level)
struct RtPidFeedback {
    int16_t speed_setpoint_mmps;
    int16_t speed_measured_mmps;
    int16_t pid_output;
};

// 0x400 RT_OBSTACLE_DIST — RT → Jetson (high-level)
struct RtObstacleDist {
    uint32_t distance_mm;
};
```

---

## 3. Internal Data Types

### 3.1 DriveCmd — Physics model input

```cpp
struct DriveCmd {
    int32_t speed_mmps      = 0;
    int32_t yaw_rate_mrad_s = 0;
};
```

### 3.2 ResolvedSetpoint — Physics model output

```cpp
struct ResolvedSetpoint {
    int32_t motor_speed_mmps = 0;
    int32_t steer_angle_mdeg = 0;
    uint8_t gear             = 0;   // 0=N, 1=D, 2=S, 3=R
    bool    steer_valid      = false;
    bool    reversing        = false;
};
```

### 3.3 PidState

```cpp
struct PidState {
    float kp = 1.0f, ki = 0.1f, kd = 0.05f;
    float integral    = 0.0f;
    float prev_error  = 0.0f;
    float output      = 0.0f;
    bool  first_call  = true;
};
```

### 3.4 Mode enum

```cpp
enum class Mode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
```

---

## 4. Control Mechanisms

### 4.1 Tricycle Kinematics (Bicycle Model)

$$\delta = \arctan\left(\frac{L \cdot \omega}{|v|}\right)$$

Where $L$ = wheelbase (1500 mm), $\omega$ = yaw rate [rad/s], $v$ = forward speed [m/s].

**Implementation** (`physics_model.cpp`):
```
1. Convert mm/s → m/s, mrad/s → rad/s
2. If |v| > low_speed_threshold (50 mm/s):
     δ = atan2(L · ω, |v|), steer_valid = true
   Else:
     δ = steer_hold_rad · 0.8, steer_valid = false
3. Clamp δ to ±45° (±45000 mdeg)
4. Clamp v to [-500, 3000] mm/s
5. reversing = v < 0
6. gear: v > 0 → D, v == 0 → N, v < 0 → R
```

### 4.2 Speed PID

Standard parallel-form PID with anti-windup:

| Parameter | Value | Description |
|-----------|-------|-------------|
| $K_p$ | 1.0 | Proportional gain |
| $K_i$ | 0.1 | Integral gain |
| $K_d$ | 0.05 | Derivative gain |
| Integral clamp | ±500 | Anti-windup limit |
| Update rate | 100 Hz | |

### 4.3 Steering — Drive-by-Wire via CAN (low-level)

| Parameter | Value |
|-----------|-------|
| CAN ID | `0x230` RT_STEER_CMD |
| Payload | `i32 angle_mdeg` (±45000) |
| Rate | 100 Hz (AUTO only) |
| Bus | Low-level CAN |

| Mode | Steering behavior |
|------|------------------|
| MANUAL | RT does NOT send `0x230`. Module operates standalone. |
| AUTO | RT sends `0x230` at 100 Hz. |
| ESTOP | RT stops sending `0x230`. Module should center/lock. |

### 4.4 Obstacle Speed Limiting

$$v_{limited} = v_{target} \cdot \frac{d_{obstacle} - d_{stop}}{d_{clear} - d_{stop}}$$

| Parameter | Value |
|-----------|-------|
| Stop distance | 300 mm |
| Clear distance | 3000 mm |

### 4.5 Command Staleness Watchdog

| Parameter | Value | Description |
|-----------|-------|-------------|
| Timeout | 500 ms | `kCmdStaleTimeoutMs` |
| Check rate | 10 Hz | |
| Action on stale | Zero setpoint on low-level (0x200 + 0x230) | Controlled stop |

**Implementation** (`watchdog.cpp`):
- `watchdog_feed()` called on every valid 0x300 frame (high-level)
- `watchdog_is_stale()` compares `esp_timer_get_time() - last_feed > timeout`
- On stale: send zero speed + gear=N (0x200) and zero angle (0x230)
- On resume: clear tripped flag, log warning

### 4.6 Brake Arbitration (Max-Select)

```
brake_pressure_kpa = max(rt_computed, jetson_request)
```

| Source | Signal | When |
|--------|--------|------|
| RT computed | Obstacle emergency, staleness | Obstacle within stop range, or command timeout |
| Jetson request | `0x301` (high-level) | Planned deceleration, hill hold |

- Jetson can **increase** but never **decrease** below RT's safety floor.
- **Gap**: RT-computed brake pressure currently has no CAN path to SYS. The arbitrated result is computed in `control_task` but `0x200 RT_DRIVE_SETPOINT` carries only speed + gear. To close this, add a brake field to `0x200` or define a new CAN ID (e.g. `0x201 RT_BRAKE_CMD`) on low-level CAN. Until resolved, SYS brake actuation is driven solely by ESTOP state and the physical brake lever GPIO.

### 4.7 CAN Gateway Forwarding

The `gateway_task` bridges messages between buses. Forwarding is transparent (same CAN ID, same payload).

```
Forwarding rules:
  Low → High:  0x001, 0x011, 0x120, 0x600
  High → Low:  0x001, 0x302
```

| Direction | CAN ID | Trigger | Action |
|-----------|--------|---------|--------|
| Low → High | `0x001` | ESTOP from SYS | `mode_set(Estop)`, push to high-level TX queue |
| Low → High | `0x011` | SYS safety status | Push to high-level TX queue |
| Low → High | `0x120` | SYS throttle pos | Push to high-level TX queue |
| Low → High | `0x600` | SYS diagnostics | Push to high-level TX queue |
| High → Low | `0x001` | ESTOP from Jetson | `mode_set(Estop)`, push to low-level TX queue |
| High → Low | `0x302` | Jetson light cmd | Push to low-level TX queue |

**Not forwarded** (consumed/generated locally):
- `0x300`, `0x301`: Jetson → RT only (physics input)
- `0x200`, `0x230`: RT generates on low-level
- `0x210`, `0x220`, `0x400`: RT generates on high-level
- `0x110`: SYS → RT only (mode)
- `0x010`, `0x012`: SYS → actuators (RT doesn't need these)
- `0x7FF`: Independent heartbeats per bus

---

## 5. RTOS Task Layout

```
Priority
   5    can_rx_low_task    ── Low-level CAN RX (TWAI) ── can_rx_low_queue (16)
        can_rx_high_task   ── High-level CAN RX (MCP2515) ── can_rx_high_queue (16)
        │
   4    dispatch_task      ◀── can_rx_low_queue + can_rx_high_queue
        │    Routes by (bus, CAN ID):
        │    High 0x300 → cmd_queue (4 slots, overwrite)
        │    High 0x301 → g_brake_request_kpa (atomic)
        │    High 0x302 → gateway_tx_low_queue
        │    Low  0x011 → gateway_tx_high_queue
        │    Low  0x120 → gateway_tx_high_queue
        │    Low  0x600 → gateway_tx_high_queue
        │    Any  0x001 → mode_set(Estop) + gateway to other bus
        │    Low  0x110 → mode_set(Manual/Auto)
        │
   4    control_task       ◀── cmd_queue
        │    100 Hz fixed-rate (vTaskDelayUntil)
        │    ┌─ physics_resolve(cmd) → ResolvedSetpoint
        │    ├─ obstacle_limit(speed, distance)
        │    ├─ pid_update(setpoint, encoder)
        │    ├─ brake max-select (rt_computed, jetson_request)
        │    └─ resolve_drive_setpoint(…) → setpoint_queue (4 slots, overwrite)
        │
   3    can_tx_low_task    ◀── setpoint_queue + gateway_tx_low_queue
        │    Serializes 0x200 (speed+gear), 0x230 (angle), 0x302 (forwarded lights)
        │
   3    can_tx_high_task   ◀── telemetry + gateway_tx_high_queue
        │    Serializes 0x210, 0x220, 0x400 (own) + 0x011, 0x120, 0x600 (forwarded)
        │
   2    obstacle_task      ── HC-SR04 @ 10 Hz → high-level CAN 0x400
        │
   1    watchdog_task      ── Staleness check @ 10 Hz
   1    heartbeat_task     ── Both buses: 0x7FF @ 2 Hz
```

### 5.1 Task details

| Task | Priority | Stack | Period | Behavior |
|------|----------|-------|--------|----------|
| `can_rx_low` | **5** | 4096 B | Event-driven | Blocks on `twai_receive()`. Copies to `can_rx_low_queue`. |
| `can_rx_high` | **5** | 4096 B | Event-driven | Polls MCP2515 via SPI (or INT-driven). Copies to `can_rx_high_queue`. |
| `dispatch` | **4** | 4096 B | Event-driven | Blocks on both RX queues. Routes per (bus, CAN ID). Handles gateway forwarding. |
| `control` | **4** | 4096 B | **100 Hz fixed** | Kinematics, PID, brake arbitration, gear derivation. |
| `can_tx_low` | **3** | 3072 B | Event-driven | Sends 0x200, 0x230, 0x302 on low-level CAN (TWAI). |
| `can_tx_high` | **3** | 3072 B | Event-driven | Sends 0x011(fwd), 0x120(fwd), 0x210, 0x220, 0x400, 0x600(fwd) on high-level CAN (MCP2515 SPI). |
| `obstacle` | **2** | 2048 B | **10 Hz** | HC-SR04 → high-level CAN 0x400. |
| `watchdog` | **1** | 2048 B | **10 Hz** | Checks staleness of 0x300. On trip, zero setpoints. |
| `heartbeat` | **1** | 2048 B | **2 Hz** | Sends 0x7FF on **both** buses. |

### 5.2 Priority reasoning

- **5 (dual CAN RX)**: Both RX tasks at highest priority. TWAI FIFO and MCP2515 RX buffer must be drained before overflow.
- **4 (dispatch + control)**: Dispatch feeds cmd_queue and gateway queues; control consumes. Equal priority → round-robin.
- **3 (dual CAN TX)**: One notch below control. Low-level TX slightly more urgent than high-level (actuation > telemetry).
- **2 (obstacle)**: Slow sensor, 30 ms per reading.
- **1 (watchdog + heartbeat)**: Background housekeeping.

### 5.3 Queue design

| Queue | Type | Slots | Pattern |
|-------|------|-------|---------|
| `can_rx_low_queue` | `Queue<CanFrame, 16>` | 16 | `xQueueSend` timeout=0 |
| `can_rx_high_queue` | `Queue<CanFrame, 16>` | 16 | `xQueueSend` timeout=0 |
| `cmd_queue` | `Queue<DriveCmd, 4>` | 4 | `xQueueOverwrite` |
| `setpoint_queue` | `Queue<ResolvedSetpoint, 4>` | 4 | `xQueueOverwrite` |
| `gateway_tx_low_queue` | `Queue<CanFrame, 8>` | 8 | `xQueueSend` timeout=0 |
| `gateway_tx_high_queue` | `Queue<CanFrame, 8>` | 8 | `xQueueSend` timeout=0 |

---

## 6. Hardware Pin Assignments

### 6.1 Low-level CAN (built-in TWAI)

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX | 5 | Output | To SN65HVD230 TXD |
| CAN RX | 4 | Input | From SN65HVD230 RXD |

### 6.2 High-level CAN (external MCP2515 via SPI)

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| SPI SCK | 36 | Output | MCP2515 SCK |
| SPI MOSI | 37 | Output | MCP2515 SI |
| SPI MISO | 38 | Input | MCP2515 SO |
| SPI CS | 39 | Output | MCP2515 CS (chip select) |
| MCP INT | 40 | Input | MCP2515 INT (interrupt, RX ready) |

### 6.3 Sensors

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| Ultrasonic TRIG | 7 | Output | HC-SR04 trigger (10 µs pulse) |
| Ultrasonic ECHO | 8 | Input | HC-SR04 echo (pulse width → distance) |
| Encoder A | 1 | Input | Speed feedback (PCNT) |
| Encoder B | 2 | Input | Speed feedback (PCNT) |
| I2C SDA | 10 | I/O | IMU (optional) |
| I2C SCL | 11 | Output | IMU (optional) |

---

## 7. Configuration Constants

```cpp
namespace rt {

// Vehicle geometry
constexpr float kWheelbaseMM        = 1500.0f;
constexpr float kTrackWidthMM       =  800.0f;
constexpr float kWheelRadiusMM      =  200.0f;

// Steering (CAN drive-by-wire)
constexpr float kSteerLimitDeg      =   45.0f;
constexpr int   kSteerLimitMdeg     =  45000;

// Speed limits
constexpr int   kMaxSpeedFwdMmps    =   3000;
constexpr int   kMaxSpeedRevMmps    =    500;
constexpr int   kLowSpeedThreshMmps =     50;

// PID
constexpr float kPidKp = 1.0f, kPidKi = 0.1f, kPidKd = 0.05f;
constexpr float kPidMaxIntegral = 500.0f;

// Obstacle
constexpr unsigned kObstacleStopDistMM  =  300;
constexpr unsigned kObstacleClearDistMM = 3000;

// Timing
constexpr int kControlLoopHz       =  100;
constexpr int kCmdStaleTimeoutMs   =  500;
constexpr int kHeartbeatIntervalMs =  500;

// CAN (low-level, built-in TWAI)
constexpr int kCanLowBitrateHz  = 500000;
constexpr int kCanLowTxGpio     =      5;
constexpr int kCanLowRxGpio     =      4;

// CAN (high-level, external MCP2515)
constexpr int kCanHighBitrateHz = 500000;
constexpr int kSpiSckGpio       =     36;
constexpr int kSpiMosiGpio      =     37;
constexpr int kSpiMisoGpio      =     38;
constexpr int kSpiCsGpio        =     39;
constexpr int kMcpIntGpio       =     40;

// CAN IDs (TX on low-level)
constexpr uint16_t kCanIdDriveSetpoint = 0x200;
constexpr uint16_t kCanIdSteerCmd      = 0x230;

// CAN IDs (TX on high-level)
constexpr uint16_t kCanIdStateReport   = 0x210;
constexpr uint16_t kCanIdPidFeedback   = 0x220;
constexpr uint16_t kCanIdObstacleDist  = 0x400;

// CAN IDs (gateway forwarding — keep same ID on target bus)
constexpr uint16_t kCanIdEstop        = 0x001;
constexpr uint16_t kCanIdSafetyStatus = 0x011;
constexpr uint16_t kCanIdThrottlePos  = 0x120;
constexpr uint16_t kCanIdLightCmd     = 0x302;
constexpr uint16_t kCanIdSysDiag      = 0x600;

// GPIO (sensors)
constexpr int kObstacleTrigGpio = 7;
constexpr int kObstacleEchoGpio = 8;
constexpr int kEncoderAGpio     = 1;
constexpr int kEncoderBGpio     = 2;
constexpr int kImuSdaGpio       = 10;
constexpr int kImuSclGpio       = 11;

// Gear
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };

} // namespace rt
```

---

## 8. Error Handling Strategy

| Failure | Detection | Response |
|---------|-----------|----------|
| Low-level CAN bus-off | TWAI TEC > 255 | Log error, auto-recovery. ESTOP if persists. |
| High-level CAN bus-off | MCP2515 error flags | Log error, auto-recovery. Zero setpoints (controlled stop) until restored. |
| Command stale | Watchdog (500 ms) | Zero setpoints on low-level CAN. |
| Obstacle timeout | Echo pulse > 30 ms | Distance = UINT32_MAX. |
| Encoder missing | Speed = 0 | PID operates on stale measurement. |
| Steering CAN TX fail | TWAI TX error counter | Log warning, module should hold last angle. |
| Gateway queue full | `xQueueSend` returns false | Frame dropped. Safety IDs (0x001) use queue-jump path (direct TX, not queued). |

---

## 9. Startup Sequence

```
 1. can_low_init()          — Install TWAI driver, start low-level CAN
 2. can_high_init()         — Init SPI, configure MCP2515, start high-level CAN
 3. obstacle_init()         — Configure TRIG/ECHO GPIOs
 4. pid_init()              — Load PID gains from config
 5. watchdog_init()         — Record initial timestamp
 6. Create queues           — can_rx_low(16), can_rx_high(16), cmd(4), setpoint(4),
                               gateway_tx_low(8), gateway_tx_high(8)
 7. Create 9 tasks          — See task layout above
 8. ESP_LOGI("Ready")
```

---

## 10. Build

```bash
cd rt-esp32
pio run              # build
pio run -t upload    # flash to ESP32-S3
pio device monitor   # serial console
```

Target: `esp32-s3-devkitc-1` (Espressif ESP32-S3, dual-core Xtensa LX7 @ 240 MHz)
Framework: `espidf` (ESP-IDF with FreeRTOS)
External: MCP2515 CAN controller (SPI) for high-level CAN bus
