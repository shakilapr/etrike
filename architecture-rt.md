# RT ESP32-S3 Architecture — Realtime Physics & Steering

## 1. Role

The RT ESP32-S3 owns **vehicle dynamics**: tricycle kinematics, speed PID, steering angle computation, and obstacle-based speed limiting. It converts ROS 2 `/cmd_vel`-style motion commands (received via CAN from Jetson) into:
- **Speed + gear setpoints** sent to the SYS ESP32-S3 (CAN `0x200`), and
- **Steering angle commands** sent directly to the drive-by-wire steering CAN module (CAN `0x230`).

It runs **7 FreeRTOS tasks** on a single ESP32-S3 at 240 MHz with a 1000 Hz scheduler tick.

---

## 2. CAN Interface

### 2.1 Messages Received

| CAN ID | Name | DLC | Payload | Source | Action |
|--------|------|-----|---------|--------|--------|
| `0x001` | SAFETY_ESTOP | 0 | (none) | Any node | `mode_set(Estop)`, disable steering |
| `0x110` | SYS_MODE_CMD | 1 | `u8 mode` (0=Manual, 1=Auto) | SYS | Switch between Manual/Auto |
| `0x300` | HOST_DRIVE_CMD | 8 | `i32 speed_mmps` (bytes 0-3), `i32 yaw_rate_mrad_s` (bytes 4-7) | Jetson | Feed to physics model (Auto only) |
| `0x301` | HOST_BRAKE_REQUEST | 4 | `i32 brake_pressure_kpa` (bytes 0-3) | Jetson | Brake pressure request (Auto only, RT-arbitrated) |
| `0x7FF` | HEARTBEAT | 0 | (none) | All | Alive signal (no action required) |

### 2.2 Messages Sent

| CAN ID | Name | DLC | Payload | Rate | Notes |
|--------|------|-----|---------|------|-------|
| `0x200` | RT_DRIVE_SETPOINT | 5 | `i32 motor_speed_mmps`, `u8 gear` (0=N, 1=D, 2=S, 3=R) | 100 Hz | Consumed by SYS motor_task |
| `0x210` | RT_STATE_REPORT | 3 | `u8 mode`, `u8 steer_valid`, `u8 reversing` | 10 Hz | Telemetry for Jetson |
| `0x220` | RT_PID_FEEDBACK | 6 | `i16 speed_sp`, `i16 speed_meas`, `i16 pid_out` | 10 Hz | PID debug for Jetson |
| `0x230` | RT_STEER_CMD | 4 | `i32 angle_mdeg` | 100 Hz | Steering angle to drive-by-wire CAN module |
| `0x400` | RT_OBSTACLE_DIST | 4 | `u32 distance_mm` | 10 Hz | Obstacle sensor reading |
| `0x7FF` | HEARTBEAT | 0 | (none) | 2 Hz | Alive signal |

### 2.3 CAN Payload Types

```cpp
// 0x300 HOST_DRIVE_CMD — Jetson → RT
struct HostDriveCmd {
    int32_t speed_mmps;        // linear.x  [mm/s]   range: [-500, 3000]
    int32_t yaw_rate_mrad_s;   // angular.z [millirad/s]  range: ±3000
    // Serialized: MSB-first, 4 bytes each at offsets 0 and 4
};

// 0x301 HOST_BRAKE_REQUEST — Jetson → RT
struct HostBrakeRequest {
    int32_t brake_pressure_kpa;  // desired brake pressure [kPa]; 0 = release
    // Serialized: MSB-first, 4 bytes at offset 0
};

// 0x200 RT_DRIVE_SETPOINT — RT → SYS
struct RtDriveSetpoint {
    int32_t motor_speed_mmps;  // rear motor target [mm/s], range [-500, 3000]
    uint8_t gear;               // 0=N, 1=D, 2=S, 3=R (derived from speed sign + mode)
    // Serialized: MSB-first, 4 bytes speed at offset 0, 1 byte gear at offset 4
};
// Gear derivation:
//   speed > 0  → gear = D (1)  (or S (2) if sport mode requested)
//   speed == 0 → gear = N (0)
//   speed < 0  → gear = R (3)

// 0x230 RT_STEER_CMD — RT → Steering CAN module
struct RtSteerCmd {
    int32_t angle_mdeg;         // front steer angle [millideg], +right, -left
    // Serialized: MSB-first, 4 bytes at offset 0
};

// 0x400 RT_OBSTACLE_DIST — RT → Jetson
struct RtObstacleDist {
    uint32_t distance_mm;      // 0 = no reading, UINT32_MAX = timeout
};

// 0x210 RT_STATE_REPORT — RT → Jetson
struct RtStateReport {
    uint8_t mode;              // 0=Manual, 1=Auto, 2=Estop
    bool    steer_valid;       // true if steering is actively controlled
    bool    reversing;         // true if motor direction is reverse
};

// 0x220 RT_PID_FEEDBACK — RT → Jetson
struct RtPidFeedback {
    int16_t speed_setpoint_mmps;
    int16_t speed_measured_mmps;
    int16_t pid_output;
};
```

---

## 3. Internal Data Types

### 3.1 DriveCmd — Physics model input

```cpp
struct DriveCmd {
    int32_t speed_mmps      = 0;   // linear velocity [mm/s]
    int32_t yaw_rate_mrad_s = 0;   // angular velocity [millirad/s]
};
```

### 3.2 ResolvedSetpoint — Physics model output

```cpp
struct ResolvedSetpoint {
    int32_t motor_speed_mmps = 0;   // rear motor target [mm/s]
    int32_t steer_angle_mdeg = 0;   // front steer angle [millideg], +right
    uint8_t gear             = 0;   // 0=N, 1=D, 2=S, 3=R
    bool    steer_valid      = false;
    bool    reversing        = false;
};
```

### 3.3 PidState — PID controller internal state

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

The RT ESP32 uses the **inverse bicycle model** to convert motion commands into steering angles:

$$\delta = \arctan\left(\frac{L \cdot \omega}{|v|}\right)$$

Where:
- $\delta$ = front wheel steer angle [rad]
- $L$ = wheelbase (1500 mm)
- $\omega$ = commanded yaw rate [rad/s]
- $v$ = commanded forward speed [m/s]

**Implementation** (`physics_model.cpp`):
```
1. Convert mm/s → m/s, mrad/s → rad/s
2. If |v| > low_speed_threshold (50 mm/s):
     δ = atan2(L · ω, |v|)
     store δ as steer_hold_rad
     steer_valid = true
   Else:
     δ = steer_hold_rad · 0.8  (decay toward straight)
     steer_valid = false
3. Clamp δ to ±steer_limit (45°)
4. Clamp v to [max_rev, max_fwd] ([-500, 3000] mm/s)
5. Set reversing flag if v < 0
6. Determine gear: v > 0 → D, v == 0 → N, v < 0 → R
```

### 4.2 Speed PID

Standard parallel-form PID with anti-windup:

$$u(t) = K_p e(t) + K_i \int_0^t e(\tau)d\tau + K_d \frac{de(t)}{dt}$$

| Parameter | Value | Description |
|-----------|-------|-------------|
| $K_p$ | 1.0 | Proportional gain |
| $K_i$ | 0.1 | Integral gain |
| $K_d$ | 0.05 | Derivative gain |
| Integral clamp | ±500 | Anti-windup limit |
| Update rate | 100 Hz | Matches control loop |

**Implementation** (`speed_pid.cpp`):
- First sample after reset: skip D-term (avoids derivative kick)
- Integral clamped to `±kPidMaxIntegral` (500)
- dt ≤ 0: return previous output (safety)
- Output = P + I + D (unclamped; motor driver clamps at actuator)

### 4.3 Steering — Drive-by-Wire via CAN

Steering is actuated by a **CAN-based drive-by-wire module**. RT sends steering angle commands directly on the CAN bus — no PWM/servo hardware on the RT board.

| Parameter | Value | Description |
|-----------|-------|-------------|
| CAN ID | `0x230` | RT_STEER_CMD |
| Payload | `i32 angle_mdeg` | Steering angle in millidegrees (+right, -left) |
| Range | ±45° | ±45000 mdeg (clamped by kinematics) |
| Rate | 100 Hz | Per control loop tick |
| Sender | RT ESP32-S3 | Only in AUTO mode |
| Receiver | Steering CAN module | Drive-by-wire actuator |

**CAN steering behavior by mode:**

| Mode | Steering behavior |
|------|------------------|
| MANUAL | RT does NOT send `0x230`. Steering module operates standalone from rider input. |
| AUTO | RT sends `0x230` at 100 Hz with resolved steer angle from kinematics. |
| ESTOP | RT stops sending `0x230`. Steering module should center/lock (TBD by module spec). |

### 4.4 Obstacle Speed Limiting

Linear interpolation between stop distance and clear distance:

$$v_{limited} = v_{target} \cdot \frac{d_{obstacle} - d_{stop}}{d_{clear} - d_{stop}}$$

| Parameter | Value | Description |
|-----------|-------|-------------|
| Stop distance | 300 mm | Speed forced to 0 at or below this |
| Clear distance | 3000 mm | Full speed allowed at or beyond this |

```
if distance ≤ stop_dist:    return 0
if distance ≥ clear_dist:   return target
return target · (distance - stop_dist) / (clear_dist - stop_dist)
```

### 4.5 Command Staleness Watchdog

If Jetson stops sending `0x300 HOST_DRIVE_CMD` for longer than the timeout:

| Parameter | Value | Description |
|-----------|-------|-------------|
| Timeout | 500 ms | `kCmdStaleTimeoutMs` |
| Check rate | 10 Hz | Every 100 ms |
| Action on stale | Send zero setpoint via CAN 0x200 + 0x230 (straight, stop) | Controlled stop |

**Implementation** (`watchdog.cpp`):
- `watchdog_feed()` called on every valid 0x300 frame
- `watchdog_is_stale()` compares `esp_timer_get_time() - last_feed > timeout`
- On stale: send zero speed + gear=N (0x200) and zero angle (0x230), set tripped flag
- On resume: clear tripped flag, log warning

### 4.6 Brake Arbitration (Max-Select)

Jetson can request braking via `0x301 HOST_BRAKE_REQUEST`. RT itself computes emergency braking (obstacle stop, command staleness). The final brake pressure is the **maximum** of all sources — the most conservative value always wins:

```
brake_pressure_kpa = max(rt_computed, jetson_request)
```

| Source | Signal | When | Priority |
|--------|--------|------|----------|
| RT computed | Obstacle emergency, staleness | Obstacle within stop range, or command timeout | Always beats Jetson |
| Jetson request | `0x301 HOST_BRAKE_REQUEST` | Planned deceleration, hill hold, precision docking | Increases above RT floor |

- RT floor is `0` today (motor regen handles normal deceleration). Future: obstacle stop → hard brake.
- Jetson can **increase** brake pressure but never **decrease** below RT's safety floor.
- No conflict possible — max-select is commutative and the most conservative source wins.
- **Gap**: RT-computed brake pressure currently has no CAN path to SYS. The arbitrated result is computed in `control_task` but `0x200 RT_DRIVE_SETPOINT` carries only speed + gear. To close this gap, either add a brake field to `0x200` or define a new CAN ID (e.g. `0x201 RT_BRAKE_CMD`). Until resolved, SYS brake actuation is driven solely by ESTOP state and the physical brake lever GPIO.

---

## 5. RTOS Task Layout

```
Priority
   5    can_rx_task     ── CAN RX ── can_rx_queue (16 slots)
                                       │
   4    dispatch_task   ◀──────────────┘
        │    parses: 0x300 → cmd_queue (4 slots, overwrite)
        │            0x301 → g_brake_request_kpa (atomic store)
        │            0x001 → mode_set(Estop)
        │            0x110 → mode_set(Manual/Auto)
        │
   4    control_task    ◀── cmd_queue
        │    100 Hz fixed-rate (vTaskDelayUntil)
        │    ┌─ physics_resolve(cmd) → ResolvedSetpoint
        │    ├─ obstacle_limit(speed, distance)
        │    ├─ pid_update(setpoint, encoder)
        │    ├─ brake max-select (rt_computed, jetson_request)
        │    └─ resolve_drive_setpoint(…, brake_kpa) → RtToSysSetpoint
        │    output → setpoint_queue (4 slots, overwrite)
        │
   3    can_tx_task     ◀── setpoint_queue
        │    Serializes ResolvedSetpoint → CAN 0x200 (speed + gear)
        │    Serializes steer_angle_mdeg → CAN 0x230
        │
   2    obstacle_task   ── HC-SR04 @ 10 Hz → CAN 0x400
        │
   1    watchdog_task   ── Staleness check @ 10 Hz
   1    heartbeat_task  ── CAN 0x7FF @ 2 Hz
```

### 5.1 Task details

| Task | Priority | Stack | Period | Behavior |
|------|----------|-------|--------|----------|
| `can_rx` | **5** (highest) | 4096 B | Event-driven | Blocks on `twai_receive()` with 100 ms timeout. Copies frame into `can_rx_queue`. Never blocks the TWAI ISR. |
| `dispatch` | **4** | 3072 B | Event-driven | Blocks on `can_rx_queue` with `portMAX_DELAY`. Parses CAN ID, routes to `cmd_queue` or sets mode. |
| `control` | **4** | 4096 B | **100 Hz fixed** | `vTaskDelayUntil` jitter-controlled. Non-blocking read from `cmd_queue`. Computes kinematics, PID, brake arbitration, gear derivation. |
| `can_tx` | **3** | 3072 B | Event-driven | Blocks on `setpoint_queue` with 50 ms timeout. Serializes speed+gear to CAN 0x200, angle to CAN 0x230. |
| `obstacle` | **2** | 2048 B | **10 Hz** | Triggers HC-SR04 ultrasonic, measures echo pulse, pushes to CAN 0x400. |
| `watchdog` | **1** | 2048 B | **10 Hz** | Checks `watchdog_is_stale()`. On trip, sends zero setpoint via CAN. |
| `heartbeat` | **1** | 2048 B | **2 Hz** | Sends empty 0x7FF frame. |

### 5.2 Priority reasoning

- **5**: CAN RX must drain the TWAI hardware FIFO before overflow (3-5 frames deep).
- **4**: Dispatch feeds `cmd_queue`, control consumes it. Equal priority → FreeRTOS round-robins. Since control blocks on delay 90% of the time, dispatch gets ample CPU.
- **3**: CAN TX is one notch below control — control produces setpoints first, TX follows.
- **2**: Obstacle sensor polling is slow (up to 30 ms per reading). Lower priority prevents it from delaying the control loop.
- **1**: Watchdog and heartbeat are background housekeeping. Tens of ms of jitter are harmless.

### 5.3 Queue design

| Queue | Type | Slots | Pattern |
|-------|------|-------|---------|
| `can_rx_queue` | `Queue<CanFrame, 16>` | 16 | `xQueueSend` with timeout=0 (drop if full) |
| `cmd_queue` | `Queue<DriveCmd, 4>` | 4 | `xQueueOverwrite` (only latest matters) |
| `setpoint_queue` | `Queue<ResolvedSetpoint, 4>` | 4 | `xQueueOverwrite` (only latest matters) |

- **Overwrite pattern** for `cmd_queue` and `setpoint_queue`: if the consumer falls behind, old values are silently dropped. Only the latest command/setpoint matters.
- **Drop pattern** for `can_rx_queue`: if full, the frame is lost. The TWAI hardware FIFO already buffers frames, so queue-full means severe overload.

---

## 6. Hardware Pin Assignments

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX | 5 | Output | To SN65HVD230 TXD |
| CAN RX | 4 | Input | From SN65HVD230 RXD |
| Ultrasonic TRIG | 7 | Output | HC-SR04 trigger (10 µs pulse) |
| Ultrasonic ECHO | 8 | Input | HC-SR04 echo (pulse width → distance) |
| Encoder A | 1 | Input | Speed feedback (PCNT) |
| Encoder B | 2 | Input | Speed feedback (PCNT) |
| I2C SDA | 10 | I/O | IMU (optional, for yaw-rate feedback) |
| I2C SCL | 11 | Output | IMU (optional) |

> **Note**: Steering servo PWM (formerly GPIO6, LEDC 50 Hz) is **removed**. Steering is now CAN-based via `0x230 RT_STEER_CMD` to the drive-by-wire steering module.

---

## 7. Configuration Constants

```cpp
namespace rt {

// Vehicle geometry
constexpr float kWheelbaseMM        = 1500.0f;   // front–rear axle
constexpr float kTrackWidthMM       =  800.0f;   // rear wheel spacing
constexpr float kWheelRadiusMM      =  200.0f;   // driven wheel

// Steering actuator (CAN drive-by-wire)
constexpr float kSteerLimitDeg      =   45.0f;
constexpr int   kSteerLimitMdeg     =  45000;    // ±45° in millidegrees

// Speed limits
constexpr int   kMaxSpeedFwdMmps    =   3000;    // 3 m/s ≈ 10.8 km/h
constexpr int   kMaxSpeedRevMmps    =    500;    // 0.5 m/s
constexpr int   kLowSpeedThreshMmps =     50;    // freeze steering below this

// PID
constexpr float kPidKp              =   1.0f;
constexpr float kPidKi              =   0.1f;
constexpr float kPidKd              =   0.05f;
constexpr float kPidMaxIntegral     = 500.0f;

// Obstacle
constexpr unsigned kObstacleStopDistMM  =  300;
constexpr unsigned kObstacleClearDistMM = 3000;

// Timing
constexpr int kControlLoopHz        =    100;
constexpr int kCmdStaleTimeoutMs    =    500;
constexpr int kHeartbeatIntervalMs  =    500;

// CAN
constexpr int kCanBitrateHz         = 500000;
constexpr int kCanTxGpio            =      5;
constexpr int kCanRxGpio            =      4;

// CAN IDs (TX)
constexpr uint16_t kCanIdDriveSetpoint = 0x200;
constexpr uint16_t kCanIdSteerCmd      = 0x230;
constexpr uint16_t kCanIdStateReport   = 0x210;
constexpr uint16_t kCanIdPidFeedback   = 0x220;
constexpr uint16_t kCanIdObstacleDist  = 0x400;

// GPIO
constexpr int kObstacleTrigGpio     =      7;
constexpr int kObstacleEchoGpio     =      8;
constexpr int kEncoderAGpio         =      1;
constexpr int kEncoderBGpio         =      2;
constexpr int kImuSdaGpio           =     10;
constexpr int kImuSclGpio           =     11;

// Gear
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };

} // namespace rt
```

---

## 8. Error Handling Strategy

| Failure | Detection | Response |
|---------|-----------|----------|
| CAN bus-off | TWAI alerts, TEC > 255 | Log error, attempt auto-recovery (TWAI hardware retry) |
| Command stale | Watchdog (500 ms timeout) | Send zero setpoint → controlled stop |
| Obstacle timeout | Echo pulse > 30 ms | Set distance to UINT32_MAX (no reading) |
| Encoder missing | `encoder_get_speed_mmps()` returns 0 | PID operates on stale measurement (I-term saturates) |
| Steering CAN TX fail | TWAI TX error counter rising | Log warning, continue (steering module should hold last valid angle or center) |

---

## 9. Startup Sequence

```
1. can_driver_init()       — Install TWAI driver, start CAN
2. obstacle_init()          — Configure TRIG/ECHO GPIOs
3. pid_init()               — Load PID gains from config
4. watchdog_init()          — Record initial timestamp
5. Create queues            — can_rx(16), cmd(4), setpoint(4)
6. Create 7 tasks           — See task layout above
7. ESP_LOGI("Ready")
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
