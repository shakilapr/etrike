# SYS ESP32-S3 Architecture — Safety, Motor Actuation & Body Control

## 1. Role

The SYS ESP32-S3 owns **safety, motor actuation, and vehicle body control**: E-stop monitoring, CAN brake actuation, motor throttle (0–5 V bidirectional), gear selection (72 V D/S/R bidirectional), mode switching, heartbeat watchdog, signal lights, mode indicator lights, 12 V accessory power control, and system diagnostics. It is the final hardware output stage for the vehicle.

It runs **14 FreeRTOS tasks** on a single ESP32-S3 at 240 MHz with a 1000 Hz scheduler tick.

---

## 2. CAN Interface

### 2.1 Messages Received

| CAN ID | Name | DLC | Payload | Source | Action |
|--------|------|-----|---------|--------|--------|
| `0x001` | SAFETY_ESTOP | 0 | (none) | Any node | `mode_set(Estop)` — motor stop, brake engage |
| `0x200` | RT_DRIVE_SETPOINT | 5 | `i32 motor_speed_mmps` (bytes 0-3), `u8 gear` (byte 4: 0=N, 1=D, 2=S, 3=R) | RT | Actuate motor + gear in AUTO mode |
| `0x302` | HOST_LIGHT_CMD | 1 | `u8 lights` bitfield: b0=left turn, b1=right turn, b2=brake light, b3=headlight | Jetson | Set signal lights in AUTO mode |
| `0x7FF` | HEARTBEAT | 0 | (none) | All | Update heartbeat timestamp |

### 2.2 Messages Sent

| CAN ID | Name | DLC | Payload | Rate | Notes |
|--------|------|-----|---------|------|-------|
| `0x010` | SYS_BRAKE_CMD | 1 | `u8 engage` (0/1) | On change | Sent to brake CAN module |
| `0x011` | SYS_SAFETY_STATUS | 2 | `u8 estop_active`, `u8 hb_ok` | 5 Hz | Safety telemetry |
| `0x110` | SYS_MODE_CMD | 1 | `u8 mode` (0=Manual, 1=Auto) | On change | Mode change → RT ESP32 |
| `0x120` | SYS_THROTTLE_POS | 2 | `i16 speed_mmps` | 100 Hz | Manual throttle reading (telemetry in AUTO) |
| `0x600` | SYS_DIAG | 8 | See §2.3 | 1 Hz | System diagnostics |
| `0x7FF` | HEARTBEAT | 0 | (none) | 2 Hz | Alive signal |

### 2.3 CAN Payload Types

```cpp
// 0x200 RT_DRIVE_SETPOINT — RT → SYS (received)
struct RtDriveSetpoint {
    int32_t motor_speed_mmps;  // rear motor target [mm/s], range [-500, 3000]
    uint8_t gear;               // 0=N, 1=D, 2=S, 3=R
};

// 0x302 HOST_LIGHT_CMD — Jetson → SYS (received)
struct HostLightCmd {
    bool left_turn;             // b0: left turn signal
    bool right_turn;            // b1: right turn signal
    bool brake_light;           // b2: brake light
    bool headlight;             // b3: headlight
    // bits 4-7: reserved
};

// 0x011 SYS_SAFETY_STATUS — SYS → Jetson
struct SysSafetyStatus {
    bool estop_active;         // true if E-stop button pressed or CAN 0x001 received
    bool heartbeat_ok;         // true if both RT and Jetson heartbeats are fresh
};

// 0x010 SYS_BRAKE_CMD — SYS → Brake CAN module
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

### 3.2 Gear enum

```cpp
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };
```

### 3.3 ActuatorSetpoint — Received from RT ESP32

```cpp
struct ActuatorSetpoint {
    int32_t motor_speed_mmps  = 0;   // rear motor target [mm/s]
    Gear    gear              = Gear::N;  // target gear selection
};
```

### 3.4 LightState — Signal lights

```cpp
struct LightState {
    bool left_turn   = false;
    bool right_turn  = false;
    bool brake_light = false;
    bool headlight   = false;
};
```

### 3.5 ModeManager state

```cpp
// Internal: std::atomic<int> wrapping SysMode
// External API:
SysMode mode_get_current();
void    mode_set(SysMode m);       // ESTOP overrides: if current==Estop && m!=Estop → no-op
const char* mode_name(SysMode m);
```

### 3.6 SafetyMonitor state

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

### 4.2 Throttle — Bidirectional 0–5 V

The throttle interface is **bidirectional**:
- **Read** (ADC): In MANUAL mode, read the rider's throttle grip (0–5 V analog) via voltage divider → 0–3.3 V → ADC.
- **Output** (DAC): In AUTO mode, output a 0–5 V analog signal to the motor controller via DAC + op-amp level shift.

| Parameter | Value | Description |
|-----------|-------|-------------|
| ADC channel | ADC1_CH5 | GPIO10 — read throttle grip (MANUAL) |
| ADC resolution | 12-bit | 0–4095 |
| DAC channel | DAC_CH1 | GPIO17 — output to motor controller (AUTO) |
| DAC resolution | 8-bit | 0–255 → 0–3.3 V |
| Output scaling | Op-amp ×1.52 | 0–3.3 V → 0–5.0 V |
| Dead zone (read) | 200 | ADC raw values below this → 0 speed |
| Max speed | 3000 mm/s | Mapped from full-scale |
| Update rate | 100 Hz | Matches control loop |

**Mode-dependent behavior:**

| Mode | Throttle behavior |
|------|------------------|
| MANUAL | Read ADC → map to speed → output same voltage to DAC (pass-through) + send CAN 0x120 |
| AUTO | Read setpoint speed from CAN 0x200 → output DAC voltage = speed / 3000 × 255 |
| ESTOP | DAC output = 0 V (motor stop) |

**DAC voltage mapping:**
```
DAC output (0–255) = speed_mmps / 3000 × 255   (for forward)
                    = 0                          (for reverse — motor controller handles direction via gear)
```
> **Note**: Throttle DAC outputs 0–5 V proportional to speed magnitude. Direction (forward/reverse) is communicated to the motor controller via the 72 V gear lines (D = forward, R = reverse).

### 4.3 Gear Selection — Bidirectional 72 V

The gear interface provides **three lines** (D, S, R), each carrying **72 V when active**. The motor controller uses these lines to determine direction/mode. SYS must both read and drive them.

#### 4.3.1 Gear Read (Manual mode)

| Signal | GPIO | Conditioning | Notes |
|--------|------|-------------|-------|
| Gear D sense | GPIO12 | 72 V → 3.3 V divider (~22:1) | HIGH = D selected by rider |
| Gear S sense | GPIO13 | 72 V → 3.3 V divider (~22:1) | HIGH = S selected by rider |
| Gear R sense | GPIO14 | 72 V → 3.3 V divider (~22:1) | HIGH = R selected by rider |

- If none are HIGH → Neutral (N)
- Only one line should be active at a time

#### 4.3.2 Gear Output (Auto mode)

| Signal | GPIO | Driver | Notes |
|--------|------|--------|-------|
| Gear D output | GPIO33 | MOSFET/relay → 72 V | Energized when gear = D or S in AUTO |
| Gear S output | GPIO34 | MOSFET/relay → 72 V | Energized when gear = S in AUTO |
| Gear R output | GPIO35 | MOSFET/relay → 72 V | Energized when gear = R in AUTO |

**Mode-dependent behavior:**

| Mode | Gear behavior |
|------|--------------|
| MANUAL | Read GPIO12-14 → mirror to GPIO33-35 (pass-through) |
| AUTO | Read gear from CAN 0x200 → energize corresponding output line |
| ESTOP | All gear outputs OFF (N) |

**Gear mapping from CAN 0x200:**
```
gear = setpoint.gear   (from RT, derived from speed sign:
                        speed > 0 → D, speed == 0 → N, speed < 0 → R)
Sport mode (S) selected by Jetson via higher-level command (TBD) — RT relays it.
```

### 4.4 Brake — CAN Module

The brake system has its own CAN module. SYS sends `0x010 SYS_BRAKE_CMD` to engage/release.

| State | Condition | CAN 0x010 |
|-------|-----------|-----------|
| Engaged | ESTOP mode OR brake lever pressed | `engage = 1` |
| Released | AUTO or MANUAL, no lever | `engage = 0` |

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

> **Design gap**: RT performs brake arbitration (max-select of RT-computed + Jetson `0x301`) but the result has no CAN path to SYS. `0x200 RT_DRIVE_SETPOINT` carries only speed + gear. Until a brake field is added to `0x200` or a new CAN ID is defined, SYS brake actuation relies solely on ESTOP state and the physical brake lever GPIO. Jetson-requested braking via `0x301` is computed by RT but never reaches the brake actuator.

### 4.5 Signal Lights

Four signal lights controlled via GPIO → relay → lamps.

| Signal | GPIO | Active | Notes |
|--------|------|--------|-------|
| Left turn | GPIO18 | HIGH | Blink pattern handled by lights_task |
| Right turn | GPIO19 | HIGH | Blink pattern handled by lights_task |
| Brake light | GPIO21 | HIGH | Solid when braking |
| Headlight | GPIO22 | HIGH | On/off |

**Mode-dependent behavior:**

| Mode | Light control |
|------|--------------|
| MANUAL | Rider physical switches → SYS reads → outputs to relays (future: switch GPIOs TBD) |
| AUTO | Jetson sends `0x302 HOST_LIGHT_CMD` → SYS outputs to relays |
| ESTOP | Brake light ON, all others OFF |

**Turn signal blink pattern** (in `lights_task`):
- 500 ms ON, 500 ms OFF, repeating while active
- Canceled when `left_turn` and `right_turn` are both false

### 4.6 Mode Indicator Lights

Two indicator LEDs showing current operating mode.

| Light | GPIO | Active for modes |
|-------|------|-----------------|
| AUTO indicator | GPIO25 | ON in AUTO mode |
| MANUAL indicator | GPIO26 | ON in MANUAL mode |
| (ESTOP implicit) | — | Both OFF = ESTOP (or use dedicated LED, TBD) |

### 4.7 12 V Accessory Power

A GPIO-controlled relay switches the 12 V accessory power bus.

| Signal | GPIO | Active | Notes |
|--------|------|--------|-------|
| 12V power relay | GPIO27 | HIGH = ON | Cut on ESTOP, restored on power-cycle or explicit command |

**Behavior:**
- MANUAL / AUTO: Relay ON (12 V bus energized)
- ESTOP: Relay OFF (accessories depowered)
- Startup default: OFF, enabled after mode_manager_init confirms not in ESTOP

### 4.8 Heartbeat Watchdog

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

### 4.9 Safety Monitor

Monitors three independent safety signals:

| Signal | GPIO | Active | Response |
|--------|------|--------|----------|
| E-stop button | GPIO1 | LOW (pull-up, active-low) | `mode_set(ESTOP)` |
| Brake lever | GPIO2 | LOW (pull-up, active-low) | Engage brake (does NOT change mode) |
| Heartbeat timeout | (CAN) | >1500 ms since last 0x7FF | `mode_set(ESTOP)` (AUTO only) |

**Redundancy**: The safety_task runs at priority 5 (tied for highest). It preempts all other tasks including motor control. ESTOP is also checked in `motor_set_speed()` and `gear_set_outputs()` as defense-in-depth.

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
        │            0x302 → g_light_state (atomic store)
        │            0x001 → mode_set(Estop)
        │
   4    mode_task       ── Mode switch GPIO @ 10 Hz
        │    Reads physical switch, calls mode_set()
        │
   4    motor_task      ◀── setpoint_queue
        │    100 Hz fixed-rate
        │    AUTO:  reads setpoint_queue → dac_set_throttle() + gear_set_outputs()
        │    MANUAL: reads throttle_read_mmps() → dac_set_throttle() + gear_mirror()
        │    ESTOP:  dac_set_throttle(0), gear_outputs_off()
        │
   3    throttle_task   ── ADC read @ 100 Hz → CAN 0x120
        │    (MANUAL mode source; in AUTO, data is telemetry only)
        │
   3    gear_task       ── Gear FSM @ 50 Hz
        │    MANUAL: read gear sense GPIOs, mirror to output GPIOs
        │    AUTO: read gear from setpoint, drive output GPIOs
        │    ESTOP: all gear outputs OFF
        │
   3    brake_task      ── Brake FSM @ 20 Hz → CAN 0x010 on change
        │
   3    lights_task     ── Light FSM @ 20 Hz
        │    MANUAL: (future) read switch GPIOs
        │    AUTO: read g_light_state (from 0x302)
        │    ESTOP: brake light ON, all others OFF
        │    Handles turn signal blink timing
        │
   2    indicator_task  ── Mode indicator LEDs @ 5 Hz
        │    Updates AUTO/MANUAL indicator GPIOs
        │
   2    power_task      ── 12V relay control @ 5 Hz
        │    ESTOP → OFF, else → ON
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
| `safety` | **5** | 2048 B | **20 Hz fixed** | **Life-critical.** Polls E-stop GPIO and heartbeats. Calls `mode_set(ESTOP)` on fault. |
| `dispatch` | **4** | 3072 B | Event-driven | Blocks on `can_rx_queue`. Routes 0x200 → setpoint_queue, 0x302 → light state, 0x001 → ESTOP. |
| `mode` | **4** | 2048 B | **10 Hz** | Reads mode switch GPIO. Calls `mode_set()`. Ignored in ESTOP. |
| `motor` | **4** | 2048 B | **100 Hz fixed** | `vTaskDelayUntil`. AUTO: DAC from setpoint speed. MANUAL: pass-through from ADC. ESTOP: DAC=0. |
| `throttle` | **3** | 1536 B | **100 Hz fixed** | Reads ADC1_CH5. Maps to speed. Sends CAN 0x120. |
| `gear` | **3** | 1536 B | **50 Hz fixed** | Reads gear sense GPIOs (MANUAL) or setpoint gear (AUTO). Drives output relays. ESTOP: all off. |
| `brake` | **3** | 1536 B | **20 Hz fixed** | Brake FSM: ESTOP→engage, lever→engage, else→release. Sends CAN 0x010 on state change. |
| `lights` | **3** | 1536 B | **20 Hz fixed** | Reads `g_light_state`. Drives signal light GPIOs with blink timing for turn signals. ESTOP: brake light ON. |
| `indicator` | **2** | 1024 B | **5 Hz** | Updates AUTO/MANUAL indicator GPIOs based on current mode. |
| `power` | **2** | 1024 B | **5 Hz** | Controls 12V relay GPIO. ESTOP → OFF, else → ON. |
| `can_tx` | **2** | 3072 B | **5 Hz fixed** | Sends safety status (0x011). |
| `diag` | **1** | 2048 B | **1 Hz fixed** | Collects heap, mode, brake, TEC/REC. Sends CAN 0x600. |
| `hb` | **1** | 2048 B | **2 Hz fixed** | Sends empty CAN 0x7FF. |

### 5.2 Priority reasoning

- **5 (safety + CAN RX)**: Safety task is life-critical — E-stop response must happen within one scheduler tick (1 ms). CAN RX must drain the TWAI FIFO before overflow.
- **4 (dispatch + mode + motor)**: Three equal-priority tasks. `motor_task` blocks on delay most of the time → dispatch and mode get CPU via round-robin.
- **3 (throttle + gear + brake + lights)**: Below motor — they feed data through queues/atomics, never direct calls. Gear at 50 Hz balances responsiveness with CPU budget.
- **2 (indicator + power + can_tx)**: Auxiliary body control and telemetry. Must never delay safety or motor actuation.
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

### 6.1 Communication

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX | 5 | Output | To SN65HVD230 TXD |
| CAN RX | 4 | Input | From SN65HVD230 RXD |

### 6.2 Safety inputs

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| E-stop button | 1 | Input | Active-low, internal pull-up |
| Brake lever | 2 | Input | Active-low, internal pull-up |

### 6.3 Throttle (bidirectional 0–5 V)

| Signal | GPIO | Direction | Conditioning | Notes |
|--------|------|-----------|-------------|-------|
| Throttle read | 10 | Input (ADC1_CH5) | Voltage divider 5V→3.3V | Rider throttle grip |
| Throttle output | 17 | Output (DAC_CH1) | Op-amp ×1.52 → 0–5 V | To motor controller |

### 6.4 Gear (bidirectional 72 V)

| Signal | GPIO | Direction | Conditioning | Notes |
|--------|------|-----------|-------------|-------|
| Gear D sense | 12 | Input | 72V→3.3V divider | Read gear selector |
| Gear S sense | 13 | Input | 72V→3.3V divider | |
| Gear R sense | 14 | Input | 72V→3.3V divider | |
| Gear D output | 33 | Output | MOSFET/relay → 72V | Drive motor controller |
| Gear S output | 34 | Output | MOSFET/relay → 72V | |
| Gear R output | 35 | Output | MOSFET/relay → 72V | |

### 6.5 Signal lights

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| Left turn | 18 | Output | GPIO → relay → lamp |
| Right turn | 19 | Output | GPIO → relay → lamp |
| Brake light | 21 | Output | GPIO → relay → lamp |
| Headlight | 22 | Output | GPIO → relay → lamp |

### 6.6 Indicators & power

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| AUTO mode LED | 25 | Output | HIGH = AUTO mode |
| MANUAL mode LED | 26 | Output | HIGH = MANUAL mode |
| 12V power relay | 27 | Output | HIGH = ON, LOW = OFF (ESTOP) |
| Mode switch | 11 | Input | Pull-up (Manual), GND (Auto) |

### 6.7 GPIO summary by function

```
GPIO 1   : ESTOP button (IN, active-low)
GPIO 2   : Brake lever  (IN, active-low)
GPIO 4   : CAN RX
GPIO 5   : CAN TX
GPIO 10  : Throttle ADC read (IN, ADC1_CH5)
GPIO 11  : Mode switch (IN, pull-up=Manual, GND=Auto)
GPIO 12  : Gear D sense (IN, 72V divider)
GPIO 13  : Gear S sense (IN, 72V divider)
GPIO 14  : Gear R sense (IN, 72V divider)
GPIO 17  : Throttle DAC out (OUT, DAC_CH1 → op-amp → 0-5V)
GPIO 18  : Left turn signal (OUT)
GPIO 19  : Right turn signal (OUT)
GPIO 21  : Brake light (OUT)
GPIO 22  : Headlight (OUT)
GPIO 25  : AUTO mode LED (OUT)
GPIO 26  : MANUAL mode LED (OUT)
GPIO 27  : 12V power relay (OUT)
GPIO 33  : Gear D output (OUT, relay → 72V)
GPIO 34  : Gear S output (OUT, relay → 72V)
GPIO 35  : Gear R output (OUT, relay → 72V)
```

---

## 7. Configuration Constants

```cpp
namespace sys {

// CAN
constexpr int     kCanBitrateHz         = 500'000;
constexpr int     kCanTxGpio            =       5;
constexpr int     kCanRxGpio            =       4;

// Throttle (bidirectional 0–5 V)
constexpr int     kThrottleAdcChannel   =       5;   // ADC1_CH5 → GPIO10
constexpr int     kThrottleDacChannel   =       1;   // DAC_CH1 → GPIO17
constexpr unsigned kThrottleDeadZone    =     200;
constexpr int     kThrottleMaxSpeedMmps =   3'000;
constexpr float   kThrottleDacScale     =   1.52f;  // op-amp gain: 3.3V → 5.0V

// Gear (bidirectional 72 V)
constexpr int     kGearDSenseGpio       =      12;
constexpr int     kGearSSenseGpio       =      13;
constexpr int     kGearRSenseGpio       =      14;
constexpr int     kGearDOutGpio         =      33;
constexpr int     kGearSOutGpio         =      34;
constexpr int     kGearROutGpio         =      35;

// Safety inputs
constexpr int     kEstopGpio            =       1;   // active-low, internal pull-up
constexpr int     kBrakeLeverGpio       =       2;

// Mode switch
constexpr int     kModeSwitchGpio       =      11;

// Signal lights
constexpr int     kLightLeftTurnGpio    =      18;
constexpr int     kLightRightTurnGpio   =      19;
constexpr int     kLightBrakeGpio       =      21;
constexpr int     kLightHeadGpio        =      22;

// Mode indicators
constexpr int     kLedAutoGpio          =      25;
constexpr int     kLedManualGpio        =      26;

// 12V power
constexpr int     kPower12vRelayGpio    =      27;

// Turn signal blink timing
constexpr int     kTurnBlinkOnMs        =     500;
constexpr int     kTurnBlinkOffMs       =     500;

// Timing
constexpr int     kControlLoopHz        =     100;
constexpr int     kHeartbeatIntervalMs  =     500;
constexpr int     kHeartbeatTimeoutMs   =   1'500;
constexpr int     kSafetyCheckHz        =      20;
constexpr int     kGearCheckHz          =      50;

} // namespace sys
```

---

## 8. Error Handling Strategy

| Failure | Detection | Response |
|---------|-----------|----------|
| E-stop pressed | GPIO1 LOW | `mode_set(ESTOP)` → throttle DAC=0, all gear outputs off, brake engage, 12V off |
| CAN bus-off | TWAI TEC > 255 | Log error, auto-recovery via TWAI hardware |
| Heartbeat timeout | >1500 ms since last 0x7FF from RT or Jetson | `mode_set(ESTOP)` (AUTO mode only) |
| Brake lever pulled | GPIO2 LOW | Engage brake (does NOT change mode) |
| ADC read failure | `adc1_get_raw()` returns 0 | Throttle = 0 (fail-safe) |
| DAC write failure | (no hardware feedback) | Log warning, throttle_task continues with last known value |
| Gear sense conflict | Multiple gear lines HIGH simultaneously | Treat as N (fail-safe — no gear output) |
| Gear output fault | (no hardware feedback) | Log warning, diagnostic reports expected vs actual |
| Queue full (can_rx) | `xQueueSend` returns false | Frame dropped, TWAI FIFO provides back-pressure |
| Mode switch bouncing | GPIO reads in `mode_task` @ 10 Hz | Natural debounce via 100 ms polling interval |

---

## 9. Startup Sequence

```
 1. can_driver_init()       — Install TWAI driver, start CAN
 2. mode_manager_init()      — Configure mode switch GPIO with pull-up
 3. safety_monitor_init()    — Configure E-stop + brake lever GPIOs with pull-ups
 4. throttle_init()          — Configure ADC1_CH5 (12-bit) + DAC_CH1 (8-bit), DAC=0
 5. gear_init()              — Configure gear sense GPIOs (IN) + gear output GPIOs (OUT, default LOW)
 6. lights_init()            — Configure signal light + indicator GPIOs (OUT, default LOW)
 7. power_init()             — Configure 12V relay GPIO (OUT, default LOW = OFF)
 8. brake_init()             — Release brake (send CAN 0x010 engage=0)
 9. diagnostics_init()       — (no-op, ready)
10. Create queues            — can_rx(16), setpoint(4)
11. Create 14 tasks          — See task layout above
12. power_task enables 12V relay (if not ESTOP)
13. ESP_LOGI("Ready")
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
