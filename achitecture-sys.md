# SYS ESP32-S3 Architecture — Safety & Motor Actuation

## 1. Role

The SYS ESP32-S3 owns **safety and actuation**: E-stop monitoring, brake control, motor PWM, manual throttle ADC, mode switching, heartbeat watchdog, and system diagnostics. It is the final hardware output stage for the vehicle.

It runs **10 FreeRTOS tasks** on a single ESP32-S3 at 240 MHz with a 1000 Hz scheduler tick.

---

## 2. CAN Interface

### 2.1 Messages Received

| CAN ID | Name | DLC | Payload | Source | Action |
|--------|------|-----|---------|--------|--------|
| `0x001` | SAFETY_ESTOP | 0 | (none) | Any node | `mode_set(Estop)` — motor stop, brake engage |
| `0x200` | RT_DRIVE_SETPOINT | 8 | `i32 motor_speed_mmps` (bytes 0-3), `i32 steer_angle_mdeg` (bytes 4-7) | RT | Actuate motor in AUTO mode |
| `0x7FF` | HEARTBEAT | 0 | (none) | All | Update heartbeat timestamp |

### 2.2 Messages Sent

| CAN ID | Name | DLC | Payload | Rate | Notes |
|--------|------|-----|---------|------|-------|
| `0x010` | SYS_BRAKE_CMD | 1 | `u8 engage` (0/1) | On change | Brake state notification |
| `0x011` | SYS_SAFETY_STATUS | 2 | `u8 estop_active`, `u8 hb_ok` | 5 Hz | Safety telemetry |
| `0x110` | SYS_MODE_CMD | 1 | `u8 mode` (0=Manual, 1=Auto) | On change | Mode change → RT ESP32 |
| `0x120` | SYS_THROTTLE_POS | 2 | `i16 speed_mmps` | 100 Hz | Manual throttle reading |
| `0x600` | SYS_DIAG | 8 | See §2.3 | 1 Hz | System diagnostics |
| `0x7FF` | HEARTBEAT | 0 | (none) | 2 Hz | Alive signal |

### 2.3 CAN Payload Types

```cpp
// 0x200 RT_DRIVE_SETPOINT — RT → SYS (received)
struct RtDriveSetpoint {
    int32_t motor_speed_mmps;  // rear motor target [mm/s], range [-500, 3000]
    int32_t steer_angle_mdeg;  // NOT used by SYS (RT owns steering servo)
};

// 0x011 SYS_SAFETY_STATUS — SYS → Jetson
struct SysSafetyStatus {
    bool estop_active;         // true if E-stop button pressed or CAN 0x001 received
    bool heartbeat_ok;         // true if both RT and Jetson heartbeats are fresh
};

// 0x010 SYS_BRAKE_CMD — SYS → monitor
struct SysBrakeCmd {
    bool engage;               // true = brake applied
};

// 0x120 SYS_THROTTLE_POS — SYS → Jetson
struct SysThrottlePos {
    int16_t speed_mmps;        // ADC-mapped speed [mm/s], range [0, 3000]
};

// 0x600 SYS_DIAG — SYS → Jetson
struct SysDiag {
    uint8_t  mode;             // 0=Manual, 1=Auto, 2=Estop
    bool     brake_engaged;    // brake actuator state
    bool     heartbeat_ok;     // combined heartbeat status
    bool     estop_active;     // E-stop input state
    uint16_t free_heap_kb;     // ESP32 free heap in KiB
    uint8_t  tec;              // TWAI transmit error counter
    uint8_t  rec;              // TWAI receive error counter
};
```

---

## 3. Internal Data Types

### 3.1 Mode enum

```cpp
enum class SysMode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
```

### 3.2 ActuatorSetpoint — Received from RT ESP32

```cpp
struct ActuatorSetpoint {
    int32_t motor_speed_mmps  = 0;   // rear motor target [mm/s]
    int32_t steer_angle_mdeg  = 0;   // NOT used by SYS (RT owns steering)
};
```

### 3.3 ModeManager state

```cpp
// Internal: std::atomic<int> wrapping SysMode
// External API:
SysMode mode_get_current();
void    mode_set(SysMode m);       // ESTOP overrides: if current==Estop && m!=Estop → no-op
const char* mode_name(SysMode m);
```

### 3.4 SafetyMonitor state

```cpp
// Internal:
//   std::atomic<int> last_hb_rt_us      — last RT heartbeat timestamp
//   std::atomic<int> last_hb_jetson_us  — last Jetson heartbeat timestamp
//
// External API:
bool safety_estop_active();          // GPIO1 == LOW (active-low, pull-up)
bool safety_brake_lever_pressed();   // GPIO2 == LOW
bool safety_heartbeat_ok();          // both HB within kHeartbeatTimeoutMs (1500 ms)
```

---

## 4. Control Mechanisms

### 4.1 Mode State Machine

```
                       ┌──────────────┐
           switch=0 ┌──│   MANUAL     │──┐ switch=1
          (pull-up) │  │  mode = 0    │  │ (GND)
                    │  └──────┬───────┘  │
                    │         │          │
                    ▼         │          ▼
              ┌──────────┐   │    ┌──────────┐
              │  ESTOP   │◀──┘    │   AUTO   │
              │  mode=2  │        │  mode=1  │
              └────┬─────┘        └──────────┘
                   │    ▲
    E-stop button ─┤    ├─ CAN 0x001
    HB timeout ────┤    │
    brake lever ───┘    └─ (any source triggers ESTOP)
```

**Rules:**
1. Mode switch (GPIO11) toggles between Manual (pull-up/open) and Auto (GND/closed).
2. ESTOP can be triggered by: E-stop button (GPIO1 LOW), CAN 0x001, or heartbeat timeout in AUTO.
3. **ESTOP cannot be cleared by the mode switch.** The switch is ignored while in ESTOP.
4. Brake lever (GPIO2) does NOT change mode — it engages the brake directly via `brake_task`.

### 4.2 Motor PWM Control

| Parameter | Value | Description |
|-----------|-------|-------------|
| PWM frequency | 20 kHz | Above audible range |
| Resolution | 13-bit | 0–8191 duty range |
| Direction GPIO | GPIO7 | HIGH = forward, LOW = reverse |
| Max speed | 3000 mm/s | Duty = speed / 3000 * 8191 |

**Mode-dependent behavior:**

| Mode | Motor behavior |
|------|---------------|
| MANUAL | Reads `throttle_read_mmps()` (ADC), drives motor directly |
| AUTO | Reads `setpoint_queue` (0x200 from RT), drives motor |
| ESTOP | `motor_stop()` — PWM=0, direction=0, ignores all inputs |

**Implementation** (`motor.cpp`):
```
motor_set_speed(speed):
  if mode == ESTOP → motor_stop(), return
  direction = speed >= 0 ? FORWARD : REVERSE
  speed = abs(speed)
  duty = speed * 8191 / 3000  (clamped to 8191)
  ledc_set_duty(duty)
```

### 4.3 Brake Actuator

Simple on/off solenoid/relay controlled by GPIO8.

| State | Condition | GPIO8 |
|-------|-----------|-------|
| Engaged | ESTOP mode OR brake lever pressed | HIGH |
| Released | AUTO or MANUAL, no lever | LOW |

**Implementation** (`brake.cpp`):
```
brake_task @ 20 Hz:
  if mode == ESTOP:
    engage if not engaged
  else if brake_lever_pressed():
    engage if not engaged
  else:
    release if engaged

On state change: send CAN 0x010 with engage flag
```

### 4.4 Manual Throttle (ADC)

| Parameter | Value | Description |
|-----------|-------|-------------|
| ADC channel | ADC1_CH5 | GPIO10 |
| Resolution | 12-bit | 0–4095 |
| Dead zone | 200 | Raw values below this → 0 speed |
| Max speed | 3000 mm/s | Mapped from ADC 4095 |
| Update rate | 100 Hz | Matches control loop |

**Implementation** (`throttle.cpp`):
```
read raw ADC
if raw < dead_zone (200): raw = 0
speed = raw * 3000 / 4095
store to atomic<int32_t>
send CAN 0x120 with speed value
```

### 4.5 Heartbeat Watchdog

| Parameter | Value | Description |
|-----------|-------|-------------|
| Timeout | 1500 ms | `kHeartbeatTimeoutMs` |
| Required sources | RT ESP32 + Jetson | Both must send 0x7FF within timeout |
| Check rate | 20 Hz | In safety_task |

**Implementation** (`safety_monitor.cpp`):
```
safety_heartbeat_ok():
  now = esp_timer_get_time()
  rt_elapsed = (now - last_hb_rt) / 1000
  jetson_elapsed = (now - last_hb_jetson) / 1000
  return rt_elapsed < 1500 && jetson_elapsed < 1500

safety_task @ 20 Hz:
  if estop_button_pressed AND mode != ESTOP:
    mode_set(ESTOP)
  if mode == AUTO AND heartbeat NOT ok:
    mode_set(ESTOP)  // lost contact with RT or Jetson → emergency stop
```

### 4.6 Safety Monitor

Monitors three independent safety signals:

| Signal | GPIO | Active | Response |
|--------|------|--------|----------|
| E-stop button | GPIO1 | LOW (pull-up, active-low) | `mode_set(ESTOP)` |
| Brake lever | GPIO2 | LOW (pull-up, active-low) | Engage brake (does NOT change mode) |
| Heartbeat timeout | (CAN) | >1500 ms since last 0x7FF | `mode_set(ESTOP)` (AUTO only) |

**Redundancy**: The safety_task runs at priority 5 (tied for highest). It preempts all other tasks including motor control. ESTOP is also checked in `motor_set_speed()` as a defense-in-depth measure.

---

## 5. RTOS Task Layout

```
Priority
   5    can_rx_task     ── CAN RX ── can_rx_queue (16 slots)
        safety_task     ── GPIO poll @ 20 Hz ── ESTOP / heartbeat check
        │                   (preempts everything)
        │
   4    dispatch_task   ◀── can_rx_queue
        │    parses: 0x200 → setpoint_queue (4 slots, overwrite)
        │            0x001 → mode_set(Estop)
        │
   4    mode_task       ── Mode switch GPIO @ 10 Hz
        │    Reads physical switch, calls mode_set()
        │
   4    motor_task      ◀── setpoint_queue
        │    100 Hz fixed-rate
        │    AUTO:  reads setpoint_queue → motor_set_speed()
        │    MANUAL: reads throttle_read_mmps() → motor_set_speed()
        │    ESTOP:  motor_stop()
        │
   3    throttle_task   ── ADC read @ 100 Hz → CAN 0x120
        │    (MANUAL mode source; in AUTO, data is telemetry only)
        │
   3    brake_task      ── Brake FSM @ 20 Hz → CAN 0x010 on change
        │
   2    can_tx_task     ── Safety status @ 5 Hz → CAN 0x011
        │
   1    diagnostics_task ── System health @ 1 Hz → CAN 0x600
   1    heartbeat_task  ── CAN 0x7FF @ 2 Hz
```

### 5.1 Task details

| Task | Priority | Stack | Period | Behavior |
|------|----------|-------|--------|----------|
| `can_rx` | **5** | 4096 B | Event-driven | Blocks on `twai_receive()` with 100 ms timeout. Copies frame to `can_rx_queue`. |
| `safety` | **5** | 2048 B | **20 Hz fixed** | **Life-critical.** Polls E-stop GPIO and heartbeats. Calls `mode_set(ESTOP)` on fault. Tied at highest priority with CAN RX. |
| `dispatch` | **4** | 3072 B | Event-driven | Blocks on `can_rx_queue`. Routes 0x200 → setpoint_queue, 0x001 → ESTOP. |
| `mode` | **4** | 2048 B | **10 Hz** | Reads mode switch GPIO. Calls `mode_set()`. Ignored in ESTOP. |
| `motor` | **4** | 2048 B | **100 Hz fixed** | `vTaskDelayUntil`. Reads setpoint (AUTO) or throttle (MANUAL). In ESTOP, calls `motor_stop()` regardless of inputs. |
| `throttle` | **3** | 1536 B | **100 Hz fixed** | Reads ADC1_CH5. Maps to speed. Sends CAN 0x120. |
| `brake` | **3** | 1536 B | **20 Hz fixed** | Brake FSM: ESTOP→engage, lever→engage, else→release. Sends CAN 0x010 on state change. |
| `can_tx` | **2** | 3072 B | **5 Hz fixed** | Sends safety status (0x011). |
| `diag` | **1** | 2048 B | **1 Hz fixed** | Collects heap, mode, brake, TEC/REC. Sends CAN 0x600. |
| `hb` | **1** | 2048 B | **2 Hz fixed** | Sends empty CAN 0x7FF. |

### 5.2 Priority reasoning

- **5 (safety + CAN RX)**: Safety task is life-critical — E-stop response must happen within one scheduler tick (1 ms). CAN RX must drain the TWAI FIFO before overflow. Both are non-negotiable at the highest priority.
- **4 (dispatch + mode + motor)**: Three equal-priority tasks. `motor_task` runs at 100 Hz but blocks on `setpoint_queue` most iterations → `dispatch_task` and `mode_task` get CPU via round-robin.
- **3 (throttle + brake)**: Below motor — they feed data through queues/atomics, never direct calls.
- **2 (can_tx)**: Safety status below actuation — status telemetry must never delay motor/brake response.
- **1 (diag + hb)**: Background telemetry only. Jitter of 50-100 ms is acceptable.

### 5.3 Queue design

| Queue | Type | Slots | Pattern |
|-------|------|-------|---------|
| `can_rx_queue` | `Queue<CanFrame, 16>` | 16 | `xQueueSend` timeout=0 (drop if full) |
| `setpoint_queue` | `Queue<ActuatorSetpoint, 4>` | 4 | `xQueueOverwrite` (only latest matters) |

- **Overwrite** for `setpoint_queue`: if the motor loop is slow, old setpoints are stale and harmful. Always use the latest.
- **Drop** for `can_rx_queue`: hardware FIFO provides back-pressure; queue drop means systematic overload.

---

## 6. Hardware Pin Assignments

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX | 5 | Output | To SN65HVD230 TXD |
| CAN RX | 4 | Input | From SN65HVD230 RXD |
| Motor PWM | 6 | Output | LEDC, 20 kHz, 13-bit |
| Motor DIR | 7 | Output | HIGH = forward, LOW = reverse |
| Brake actuator | 8 | Output | HIGH = engaged (solenoid/relay) |
| E-stop button | 1 | Input | Active-low, internal pull-up |
| Brake lever | 2 | Input | Active-low, internal pull-up |
| Throttle ADC | 10 | Input | ADC1_CH5, 12-bit, 0-3.3V |
| Mode switch | 11 | Input | Pull-up (Manual), GND (Auto) |

---

## 7. Configuration Constants

```cpp
namespace sys {

// CAN
constexpr int   kCanBitrateHz       = 500'000;
constexpr int   kCanTxGpio          =       5;
constexpr int   kCanRxGpio          =       4;

// Motor driver
constexpr int   kMotorPwmGpio       =       6;
constexpr int   kMotorDirGpio       =       7;
constexpr int   kMotorPwmFreqHz     =  20'000;
constexpr int   kMotorMaxSpeedMmps  =   3'000;

// Brake
constexpr int   kBrakeGpio          =       8;

// Safety inputs
constexpr int   kEstopGpio          =       1;   // active-low, internal pull-up
constexpr int   kBrakeLeverGpio     =       2;

// Throttle ADC
constexpr int   kThrottleAdcChannel  =      5;   // ADC1_CH5 → GPIO10
constexpr unsigned kThrottleDeadZone =    200;
constexpr int   kThrottleMaxSpeedMmps = 3'000;

// Mode switch
constexpr int   kModeSwitchGpio     =      11;

// Timing
constexpr int   kControlLoopHz      =     100;
constexpr int   kHeartbeatIntervalMs =    500;
constexpr int   kHeartbeatTimeoutMs  =  1'500;
constexpr int   kSafetyCheckHz       =     20;

} // namespace sys
```

---

## 8. Error Handling Strategy

| Failure | Detection | Response |
|---------|-----------|----------|
| E-stop pressed | GPIO1 LOW | `mode_set(ESTOP)` → motor stop, brake engage |
| CAN bus-off | TWAI TEC > 255 | Log error, auto-recovery via TWAI hardware |
| Heartbeat timeout | >1500 ms since last 0x7FF from RT or Jetson | `mode_set(ESTOP)` (AUTO mode only) |
| Brake lever pulled | GPIO2 LOW | Engage brake (does NOT change mode) |
| ADC read failure | `adc1_get_raw()` returns 0 | Throttle = 0 (fail-safe) |
| Queue full (can_rx) | `xQueueSend` returns false | Frame dropped, TWAI FIFO provides back-pressure |
| Mode switch bouncing | GPIO reads in `mode_task` @ 10 Hz | Natural debounce via 100 ms polling interval |

---

## 9. Startup Sequence

```
 1. can_driver_init()       — Install TWAI driver, start CAN
 2. mode_manager_init()      — Configure mode switch GPIO with pull-up
 3. safety_monitor_init()    — Configure E-stop + brake lever GPIOs with pull-ups
 4. throttle_init()          — Configure ADC1_CH5, 12-bit, 0-3.3V range
 5. motor_init()             — Configure LEDC PWM 20 kHz + direction GPIO, stop motor
 6. brake_init()             — Configure brake GPIO, release brake
 7. diagnostics_init()       — (no-op, ready)
 8. Create queues            — can_rx(16), setpoint(4)
 9. Create 10 tasks          — See task layout above
10. ESP_LOGI("Ready")
```

---

## 10. Build

```bash
cd sys-esp32
pio run              # build
pio run -t upload    # flash to ESP32-S3
pio device monitor   # serial console
```

Target: `esp32-s3-devkitc-1` (Espressif ESP32-S3, dual-core Xtensa LX7 @ 240 MHz)
Framework: `espidf` (ESP-IDF with FreeRTOS)
