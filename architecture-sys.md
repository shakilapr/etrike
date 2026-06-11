# SYS ESP32-S3 Architecture — Safety, Motor Actuation & Body Control

## 1. Role

The SYS ESP32-S3 owns **safety, motor actuation, and vehicle body control**: E-stop monitoring, CAN brake actuation, DC-DC converter control, motor throttle (0–5 V bidirectional), gear selection (72 V D/S/R bidirectional), mode switching, heartbeat watchdog, signal lights, mode indicator lights, 12 V accessory power control, and system diagnostics.

SYS is connected to the **low-level CAN bus only**. All communication with Jetson goes through RT (CAN gateway). SYS does not have a direct path to the high-level CAN bus.

It runs **15 FreeRTOS tasks** on a single ESP32-S3 at 240 MHz with a 1000 Hz scheduler tick.

---

## 2. CAN Interface

SYS is on the **low-level CAN bus** only (built-in TWAI, GPIO 4/5, 500 kbit/s, SN65HVD230 transceiver).

### 2.1 Messages Received

| CAN ID | Name | DLC | Payload | Source | Action |
|--------|------|-----|---------|--------|--------|
| `0x001` | SAFETY_ESTOP | 0 | (none) | RT or (any on low-level) | `mode_set(Estop)` — motor stop, brake engage, DCDC off |
| `0x200` | RT_DRIVE_SETPOINT | 5 | `i32 motor_speed_mmps` (bytes 0-3), `u8 gear` (byte 4: 0=N, 1=D, 2=S, 3=R) | RT | Actuate motor + gear in AUTO mode |
| `0x302` | HOST_LIGHT_CMD | 1 | `u8 lights` bitfield: b0=left, b1=right, b2=brake, b3=head | RT (forwarded from Jetson) | Set signal lights in AUTO mode |
| `0x7FF` | HEARTBEAT | 0 | (none) | RT | Update RT heartbeat timestamp |

### 2.2 Messages Sent

| CAN ID | Name | DLC | Payload | Rate | Notes |
|--------|------|-----|---------|------|-------|
| `0x010` | SYS_BRAKE_CMD | 1 | `u8 engage` (0/1) | On change | Sent to brake CAN module |
| `0x011` | SYS_SAFETY_STATUS | 2 | `u8 estop_active`, `u8 hb_ok` | 5 Hz | RT forwards to Jetson on high-level CAN |
| `0x012` | SYS_DCDC_CMD | 1 | `u8 enable` (0/1) | On change | Sent to DC-DC converter (72V→12V) |
| `0x110` | SYS_MODE_CMD | 1 | `u8 mode` (0=Manual, 1=Auto) | On change | Mode change → RT |
| `0x120` | SYS_THROTTLE_POS | 2 | `i16 speed_mmps` | 100 Hz | RT forwards to Jetson on high-level CAN |
| `0x600` | SYS_DIAG | 8 | See §2.3 | 1 Hz | RT forwards to Jetson on high-level CAN |
| `0x7FF` | HEARTBEAT | 0 | (none) | 2 Hz | Alive signal to RT |

### 2.3 CAN Payload Types

```cpp
// 0x200 RT_DRIVE_SETPOINT — RT → SYS (received)
struct RtDriveSetpoint {
    int32_t motor_speed_mmps;  // range [-500, 3000]
    uint8_t gear;               // 0=N, 1=D, 2=S, 3=R
};

// 0x302 HOST_LIGHT_CMD — forwarded by RT → SYS (received)
struct HostLightCmd {
    bool left_turn;             // b0
    bool right_turn;            // b1
    bool brake_light;           // b2
    bool headlight;             // b3
};

// 0x010 SYS_BRAKE_CMD — SYS → Brake CAN module
struct SysBrakeCmd {
    bool engage;
};

// 0x012 SYS_DCDC_CMD — SYS → DC-DC converter (72V→12V)
struct SysDcdcCmd {
    bool enable;                // true = converter ON (12V rail energized)
};

// 0x011 SYS_SAFETY_STATUS — SYS → RT (→ forwarded to Jetson)
struct SysSafetyStatus {
    bool estop_active;
    bool heartbeat_ok;         // RT heartbeat on low-level bus
};

// 0x120 SYS_THROTTLE_POS — SYS → RT (→ forwarded to Jetson)
struct SysThrottlePos {
    int16_t speed_mmps;
};

// 0x600 SYS_DIAG — SYS → RT (→ forwarded to Jetson)
struct SysDiag {
    uint8_t  mode;
    bool     brake_engaged;
    bool     heartbeat_ok;
    bool     estop_active;
    uint16_t free_heap_kb;
    uint8_t  tec;
    uint8_t  rec;
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
    int32_t motor_speed_mmps  = 0;
    Gear    gear              = Gear::N;
};
```

### 3.4 LightState

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
SysMode mode_get_current();
void    mode_set(SysMode m);       // ESTOP overrides: if current==Estop && m!=Estop → no-op
const char* mode_name(SysMode m);
```

### 3.6 SafetyMonitor state

```cpp
// Internal:
//   std::atomic<int> last_hb_rt_us   — last RT heartbeat timestamp (low-level CAN)

// External API:
bool safety_estop_active();          // GPIO1 == LOW (active-low, pull-up)
bool safety_brake_lever_pressed();   // GPIO2 == LOW
bool safety_heartbeat_ok();          // RT heartbeat within kHeartbeatTimeoutMs (1500 ms)
```

> **Note**: SYS monitors only RT heartbeat on the low-level CAN bus. RT monitors Jetson heartbeat on the high-level CAN bus. If Jetson is lost, RT sends zero setpoints (controlled stop) or triggers ESTOP. SYS does not need direct Jetson heartbeat visibility.

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
2. ESTOP can be triggered by: E-stop button (GPIO1 LOW), CAN 0x001, or RT heartbeat timeout in AUTO.
3. **ESTOP cannot be cleared by the mode switch.**
4. Brake lever (GPIO2) does NOT change mode — it engages the brake directly via `brake_task`.

### 4.2 Throttle — Bidirectional 0–5 V

| Parameter | Value | Description |
|-----------|-------|-------------|
| ADC channel | ADC1_CH5 | GPIO10 — read throttle grip (MANUAL) |
| ADC resolution | 12-bit | 0–4095 |
| DAC device | MCP4725 (I2C) | SDA=GPIO15, SCL=GPIO16, addr=0x60 — output to motor controller (AUTO) |
| DAC resolution | 12-bit | 0–4095 → 0–5.0 V (VCC=5V, no op-amp needed) |
| Dead zone (read) | 200 | ADC raw values below this → 0 speed |
| Max speed | 3000 mm/s | |
| Update rate | 100 Hz | |

**Mode-dependent behavior:**

| Mode | Throttle behavior |
|------|------------------|
| MANUAL | Read ADC → map to speed → write same value to MCP4725 (pass-through) + send CAN 0x120 |
| AUTO | Read setpoint speed from CAN 0x200 → write MCP4725 = abs(speed) / 3000 × 4095 |
| ESTOP | MCP4725 output = 0 V |

> Direction is handled via gear lines (D = forward, R = reverse). MCP4725 powered from 5V rail — outputs 0–5V directly, no op-amp needed.

### 4.3 Gear Selection — Bidirectional 72 V

#### 4.3.1 Gear Read (Manual mode)

| Signal | GPIO | Conditioning |
|--------|------|-------------|
| Gear D sense | GPIO12 | TLP281 optoisolator ch1 (72V → 3.3V, isolated) |
| Gear S sense | GPIO13 | TLP281 optoisolator ch2 (72V → 3.3V, isolated) |
| Gear R sense | GPIO14 | TLP281 optoisolator ch3 (72V → 3.3V, isolated) |

#### 4.3.2 Gear Output (Auto mode)

| Signal | GPIO | Driver |
|--------|------|--------|
| Gear D output | GPIO33 | 4-ch 5V relay ch1 (GPIO→IN, 72V→1A fuse→COM→NO→ECU) |
| Gear S output | GPIO34 | 4-ch 5V relay ch2 (GPIO→IN, 72V→1A fuse→COM→NO→ECU) |
| Gear R output | GPIO35 | 4-ch 5V relay ch3 (GPIO→IN, 72V→1A fuse→COM→NO→ECU) |

#### 4.3.3 Protection circuit

```
72V Batt ──┬──[1A fast-blow fuse]──┬── RELAY COM (ch1 D) ── NO ──┬── ECU Gear D wire ───┬─ [TVS SMCJ90CA] ── GND
           │                       ├── RELAY COM (ch2 S) ── NO ──┼── ECU Gear S wire ───┼─ [TVS SMCJ90CA] ── GND
           │                       └── RELAY COM (ch3 R) ── NO ──┼── ECU Gear R wire ───┴─ [TVS SMCJ90CA] ── GND
           │                                                      │
           └──────────────────────────────────────────────────────┘
```

| Protection | Part | Rating | Purpose |
|-----------|------|--------|---------|
| Inline fuse | 1A fast-blow | 72V, 1A | Overcurrent/short-circuit protection on 72V rail before relays |
| TVS diode (×3) | SMCJ90CA (bidirectional) | 90–100V standoff, 1500W peak | Clamp inductive kickback/transients from ECU gear wires to GND |

> One TVS diode per gear wire (D, S, R), wired directly from ECU gear wire to common ground. Bidirectional — clamps both positive and negative transients.

**Mode-dependent behavior:**

| Mode | Gear behavior |
|------|--------------|
| MANUAL | Read GPIO12-14 → mirror to GPIO33-35 (pass-through) |
| AUTO | Read gear from CAN 0x200 → energize corresponding output |
| ESTOP | All gear outputs OFF (N) |

### 4.4 Brake — CAN Module

SYS sends `0x010 SYS_BRAKE_CMD` on low-level CAN to the brake CAN module.

| State | Condition | CAN 0x010 |
|-------|-----------|-----------|
| Engaged | ESTOP mode OR brake lever pressed | `engage = 1` |
| Released | AUTO or MANUAL, no lever | `engage = 0` |

**Implementation** (`brake.cpp`):
```
brake_task @ 20 Hz:
  if mode == ESTOP:     engage
  else if brake_lever:  engage
  else:                 release

On state change: send CAN 0x010
```

> **Design gap**: RT brake arbitration result has no path to SYS. Jetson braking requests (`0x301`) and RT obstacle-emergency braking are computed but not actuated. SYS brakes only on ESTOP or physical lever.

### 4.5 DC-DC Converter — 72V → 12V via CAN

The DC-DC converter is a standalone CAN module on the low-level bus. It converts the 72 V traction battery to a 12 V rail. SYS controls it via `0x012 SYS_DCDC_CMD`.

| State | Condition | CAN 0x012 |
|-------|-----------|-----------|
| ON | MANUAL or AUTO mode | `enable = 1` |
| OFF | ESTOP mode | `enable = 0` |

**Implementation** (`dcdc_task.cpp`):
```
dcdc_task @ 5 Hz:
  if mode == ESTOP:  send 0x012 enable=0 (if currently on)
  else:              send 0x012 enable=1 (if currently off)

On state change only — not every tick.
```

The 12 V accessory power relay (GPIO27) is a secondary cut — it is also opened on ESTOP as defense-in-depth, but the primary 12 V kill is via the DC-DC converter.

### 4.6 Signal Lights

| Signal | GPIO | Active | Notes |
|--------|------|--------|-------|
| Left turn | GPIO18 | HIGH | Blink: 500 ms ON, 500 ms OFF |
| Right turn | GPIO19 | HIGH | Blink: 500 ms ON, 500 ms OFF |
| Brake light | GPIO21 | HIGH | Solid |
| Headlight | GPIO22 | HIGH | On/off |

**Mode-dependent behavior:**

| Mode | Light control |
|------|--------------|
| MANUAL | Rider physical switches → SYS reads → outputs (future: switch GPIOs TBD) |
| AUTO | RT forwards Jetson `0x302` → SYS receives on low-level → outputs |
| ESTOP | Brake light ON, all others OFF |

### 4.7 Mode Indicator Lights

| Light | GPIO | Active for modes |
|-------|------|-----------------|
| AUTO indicator | GPIO25 | ON in AUTO |
| MANUAL indicator | GPIO26 | ON in MANUAL |
| (ESTOP) | — | Both OFF |

### 4.8 12 V Accessory Power Relay

| Signal | GPIO | Active | Notes |
|--------|------|--------|-------|
| 12V power relay | GPIO27 | HIGH = ON | Secondary cut on ESTOP (primary: DC-DC converter off) |

- MANUAL / AUTO: Relay ON
- ESTOP: Relay OFF
- Startup default: OFF

### 4.9 Heartbeat Watchdog

SYS monitors **RT heartbeat only** on low-level CAN (`0x7FF`). RT is responsible for monitoring Jetson on the high-level bus and taking action (zero setpoints or ESTOP) if Jetson is lost.

| Parameter | Value | Description |
|-----------|-------|-------------|
| Timeout | 1500 ms | `kHeartbeatTimeoutMs` |
| Required source | RT ESP32 | Must send 0x7FF on low-level CAN within timeout |
| Check rate | 20 Hz | In safety_task |

**Implementation** (`safety_monitor.cpp`):
```
safety_heartbeat_ok():
  elapsed = (esp_timer_get_time() - last_hb_rt) / 1000
  return elapsed < 1500

safety_task @ 20 Hz:
  if estop_button_pressed AND mode != ESTOP:
    mode_set(ESTOP)
  if mode == AUTO AND heartbeat NOT ok:
    mode_set(ESTOP)  // lost contact with RT → emergency stop
```

### 4.10 Safety Monitor

| Signal | GPIO | Active | Response |
|--------|------|--------|----------|
| E-stop button | GPIO1 | LOW (pull-up, active-low) | `mode_set(ESTOP)` |
| Brake lever | GPIO2 | LOW (pull-up, active-low) | Engage brake (does NOT change mode) |
| RT heartbeat timeout | (CAN) | >1500 ms since last 0x7FF from RT | `mode_set(ESTOP)` (AUTO only) |

**Redundancy**: safety_task at priority 5. ESTOP also checked in `motor_set_speed()`, `gear_set_outputs()`, and `dcdc_set_output()`.

---

## 5. RTOS Task Layout

```
Priority
   5    can_rx_task     ── Low-level CAN RX ── can_rx_queue (16 slots)
        safety_task     ── GPIO poll @ 20 Hz ── ESTOP / RT heartbeat check
        │
   4    dispatch_task   ◀── can_rx_queue
        │    parses: 0x200 → setpoint_queue (4 slots, overwrite)
        │            0x302 → g_light_state (atomic store)
        │            0x001 → mode_set(Estop)
        │
   4    mode_task       ── Mode switch GPIO @ 10 Hz
        │
   4    motor_task      ◀── setpoint_queue
        │    100 Hz: AUTO → dac_set_throttle() + gear_set_outputs()
        │           MANUAL → pass-through from ADC + gear_mirror()
        │           ESTOP → dac=0, gears off
        │
   3    throttle_task   ── ADC read @ 100 Hz → CAN 0x120
   3    gear_task       ── Gear FSM @ 50 Hz
   3    brake_task      ── Brake FSM @ 20 Hz → CAN 0x010
   3    lights_task     ── Light FSM @ 20 Hz (turn blink, ESTOP=brake ON)
   3    dcdc_task       ── DC-DC control @ 5 Hz → CAN 0x012
        │
   2    indicator_task  ── Mode LEDs @ 5 Hz
   2    power_task      ── 12V relay @ 5 Hz
   2    can_tx_task     ── Safety status @ 5 Hz → CAN 0x011
        │
   1    diagnostics_task ── System health @ 1 Hz → CAN 0x600
   1    heartbeat_task  ── Low-level CAN 0x7FF @ 2 Hz
```

### 5.1 Task details

| Task | Priority | Stack | Period | Behavior |
|------|----------|-------|--------|----------|
| `can_rx` | **5** | 4096 B | Event-driven | `twai_receive()` with 100 ms timeout. Copies to `can_rx_queue`. |
| `safety` | **5** | 2048 B | **20 Hz** | Polls E-stop GPIO, RT heartbeat. Calls `mode_set(ESTOP)` on fault. |
| `dispatch` | **4** | 3072 B | Event-driven | Routes 0x200 → setpoint_queue, 0x302 → light state, 0x001 → ESTOP. |
| `mode` | **4** | 2048 B | **10 Hz** | Reads mode switch GPIO. Ignored in ESTOP. |
| `motor` | **4** | 2048 B | **100 Hz** | DAC throttle + gear outputs per mode. |
| `throttle` | **3** | 1536 B | **100 Hz** | Reads ADC1_CH5, sends CAN 0x120. |
| `gear` | **3** | 1536 B | **50 Hz** | Gear FSM: read sense or setpoint, drive outputs. |
| `brake` | **3** | 1536 B | **20 Hz** | Brake FSM, sends CAN 0x010. |
| `lights` | **3** | 1536 B | **20 Hz** | Drives signal light GPIOs with blink timing. ESTOP: brake light ON. |
| `dcdc` | **3** | 1024 B | **5 Hz** | DC-DC converter FSM, sends CAN 0x012. |
| `indicator` | **2** | 1024 B | **5 Hz** | AUTO/MANUAL LEDs. |
| `power` | **2** | 1024 B | **5 Hz** | 12V relay. ESTOP → OFF, else → ON. |
| `can_tx` | **2** | 3072 B | **5 Hz** | Sends CAN 0x011 safety status. |
| `diag` | **1** | 2048 B | **1 Hz** | Sends CAN 0x600. |
| `hb` | **1** | 2048 B | **2 Hz** | Sends CAN 0x7FF. |

### 5.2 Priority reasoning

- **5 (safety + CAN RX)**: Life-critical. E-stop within 1 scheduler tick.
- **4 (dispatch + mode + motor)**: Motor blocks on delay → dispatch and mode get CPU via round-robin.
- **3 (throttle + gear + brake + lights + dcdc)**: Actuation tasks below motor. DCDC at 5 Hz — converter state changes infrequently.
- **2 (indicator + power + can_tx)**: Auxiliary body control and telemetry.
- **1 (diag + hb)**: Background only.

### 5.3 Queue design

| Queue | Type | Slots | Pattern |
|-------|------|-------|---------|
| `can_rx_queue` | `Queue<CanFrame, 16>` | 16 | `xQueueSend` timeout=0 (drop if full) |
| `setpoint_queue` | `Queue<ActuatorSetpoint, 4>` | 4 | `xQueueOverwrite` (only latest matters) |

---

## 6. Hardware Pin Assignments

### 6.1 Communication (low-level CAN only)

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

| Signal | GPIO | Direction | Conditioning |
|--------|------|-----------|-------------|
| Throttle read | 10 | Input (ADC1_CH5) | Voltage divider 5V→3.3V |
| Throttle output | — | I2C (SDA=15, SCL=16) | MCP4725 DAC, addr=0x60, VCC=5V → 0–5V out |

### 6.4 Gear (bidirectional 72 V)

| Signal | GPIO | Direction | Conditioning |
|--------|------|-----------|-------------|
| Gear D sense | 12 | Input | TLP281 optoisolator ch1 |
| Gear S sense | 13 | Input | TLP281 optoisolator ch2 |
| Gear R sense | 14 | Input | TLP281 optoisolator ch3 |
| Gear D output | 33 | Output | 4-ch 5V relay ch1: GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS SMCJ90CA to GND. |
| Gear S output | 34 | Output | 4-ch 5V relay ch2: GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS SMCJ90CA to GND. |
| Gear R output | 35 | Output | 4-ch 5V relay ch3: GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS SMCJ90CA to GND. |

### 6.5 Signal lights

| Signal | GPIO | Direction |
|--------|------|-----------|
| Left turn | 18 | Output |
| Right turn | 19 | Output |
| Brake light | 21 | Output |
| Headlight | 22 | Output |

### 6.6 Indicators & power

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| AUTO mode LED | 25 | Output | |
| MANUAL mode LED | 26 | Output | |
| 12V power relay | 27 | Output | HIGH=ON, secondary cut on ESTOP |
| Mode switch | 11 | Input | Pull-up (Manual), GND (Auto) |

### 6.7 GPIO summary

```
GPIO 1   : ESTOP button (IN, active-low)
GPIO 2   : Brake lever (IN, active-low)
GPIO 4   : CAN RX (low-level)
GPIO 5   : CAN TX (low-level)
GPIO 10  : Throttle ADC read (IN, ADC1_CH5)
GPIO 11  : Mode switch (IN, pull-up=Manual, GND=Auto)
GPIO 12  : Gear D sense (IN, TLP281 ch1 — 72V optoisolated)
GPIO 13  : Gear S sense (IN, TLP281 ch2 — 72V optoisolated)
GPIO 14  : Gear R sense (IN, TLP281 ch3 — 72V optoisolated)
GPIO 15  : I2C SDA → MCP4725 throttle DAC
GPIO 16  : I2C SCL → MCP4725 throttle DAC
GPIO 18  : Left turn signal (OUT)
GPIO 19  : Right turn signal (OUT)
GPIO 21  : Brake light (OUT)
GPIO 22  : Headlight (OUT)
GPIO 25  : AUTO mode LED (OUT)
GPIO 26  : MANUAL mode LED (OUT)
GPIO 27  : 12V power relay (OUT)
GPIO 33  : Gear D output (OUT, 4-ch relay ch1 → 1A fuse → ECU, TVS to GND)
GPIO 34  : Gear S output (OUT, 4-ch relay ch2 → 1A fuse → ECU, TVS to GND)
GPIO 35  : Gear R output (OUT, 4-ch relay ch3 → 1A fuse → ECU, TVS to GND)
```

---

## 7. Configuration Constants

```cpp
namespace sys {

// CAN (low-level only)
constexpr int     kCanBitrateHz         = 500'000;
constexpr int     kCanTxGpio            =       5;
constexpr int     kCanRxGpio            =       4;

// Throttle
constexpr int     kThrottleAdcChannel    =       5;   // ADC1_CH5 → GPIO10
constexpr int     kThrottleI2cSdaGpio    =      15;   // MCP4725 SDA
constexpr int     kThrottleI2cSclGpio    =      16;   // MCP4725 SCL
constexpr uint8_t kThrottleDacI2cAddr    =    0x60;   // MCP4725 I2C address
constexpr unsigned kThrottleDeadZone     =     200;
constexpr int     kThrottleMaxSpeedMmps  =   3'000;
constexpr int     kThrottleDacMaxVal     =    4095;   // 12-bit MCP4725, VCC=5V → 0–5V out

// Gear
constexpr int     kGearDSenseGpio       =      12;
constexpr int     kGearSSenseGpio       =      13;
constexpr int     kGearRSenseGpio       =      14;
constexpr int     kGearDOutGpio         =      33;
constexpr int     kGearSOutGpio         =      34;
constexpr int     kGearROutGpio         =      35;

// Safety
constexpr int     kEstopGpio            =       1;
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

// Turn signal blink
constexpr int     kTurnBlinkOnMs        =     500;
constexpr int     kTurnBlinkOffMs       =     500;

// Timing
constexpr int     kControlLoopHz        =     100;
constexpr int     kHeartbeatIntervalMs  =     500;
constexpr int     kHeartbeatTimeoutMs   =   1'500;
constexpr int     kSafetyCheckHz        =      20;
constexpr int     kGearCheckHz          =      50;

// CAN IDs (TX on low-level)
constexpr uint16_t kCanIdBrakeCmd       =   0x010;
constexpr uint16_t kCanIdSafetyStatus   =   0x011;
constexpr uint16_t kCanIdDcdcCmd        =   0x012;
constexpr uint16_t kCanIdModeCmd        =   0x110;
constexpr uint16_t kCanIdThrottlePos    =   0x120;
constexpr uint16_t kCanIdDiag           =   0x600;

} // namespace sys
```

---

## 8. Error Handling Strategy

| Failure | Detection | Response |
|---------|-----------|----------|
| E-stop pressed | GPIO1 LOW | `mode_set(ESTOP)` → throttle=0, gears off, brake engage, DCDC off, 12V relay off |
| CAN bus-off | TWAI TEC > 255 | Log error, auto-recovery |
| RT heartbeat timeout | >1500 ms since last 0x7FF from RT | `mode_set(ESTOP)` (AUTO only) |
| Brake lever pulled | GPIO2 LOW | Engage brake |
| ADC read failure | `adc1_get_raw()` returns 0 | Throttle = 0 |
| DAC write failure | (no feedback) | Log warning |
| Gear sense conflict | Multiple lines HIGH | Treat as N |
| DCDC CAN TX fail | TWAI TX error counter | Log warning, 12V relay provides backup cut |
| Queue full | `xQueueSend` returns false | Frame dropped |

---

## 9. Startup Sequence

```
 1. can_driver_init()       — Install TWAI driver, start low-level CAN
 2. mode_manager_init()      — Configure mode switch GPIO
 3. safety_monitor_init()    — Configure E-stop + brake lever GPIOs
 4. throttle_init()          — Configure ADC1_CH5, init I2C, init MCP4725 (output=0)
 5. gear_init()              — Configure gear GPIOs (IN + OUT, default LOW)
 6. lights_init()            — Configure signal light + indicator GPIOs (OUT, LOW)
 7. power_init()             — Configure 12V relay GPIO (OUT, LOW=OFF)
 8. brake_init()             — Release brake (send CAN 0x010 engage=0)
 9. dcdc_init()              — DCDC off (send CAN 0x012 enable=0)
10. diagnostics_init()       — (no-op)
11. Create queues            — can_rx(16), setpoint(4)
12. Create 15 tasks          — See task layout above
13. power_task enables 12V relay
14. dcdc_task enables DC-DC converter (CAN 0x012 enable=1, if not ESTOP)
15. ESP_LOGI("Ready")
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
CAN: Built-in TWAI (low-level CAN only)
