# RM-ESP32 — Receiver Module Architecture & Technical Specification

Modern **C++17** FreeRTOS application for the E-Trike **Remote Control / Receiver Gateway** (`rm-esp32`).

This firmware runs on an **ESP32** microcontroller, interfaces with a 6-channel RC/PWM receiver (such as FlySky FS-i6 with FS-iA6/iA6B) using the ESP-IDF RMT hardware peripheral, processes pulse durations with hysteresis and deadband filters, and broadcasts drive-by-wire, brake-by-wire, mode, and power requests onto the **Low-CAN bus** at **500 kbit/s** using canonical typed protocol codecs.

The project structure, coding standards, FreeRTOS task patterns, and CAN driver architecture are directly aligned with [`sys-esp32`](file:///e:/work/etrike/sys-esp32/README.md) and [`rt-esp32`](file:///e:/work/etrike/rt-esp32/README.md).

---

## 1. Project Layout & Codebase Structure

The codebase mirrors the standardized modular layout used across the E-Trike embedded nodes:

```
rm-esp32/
├── CMakeLists.txt              # Root ESP-IDF project configuration
├── platformio.ini              # Multi-target PlatformIO config (vehicle, bench, native)
├── sdkconfig.defaults          # ESP-IDF defaults (CONFIG_FREERTOS_HZ=1000, TWAI IRAM, etc.)
├── architecture.md             # Legacy RMT_14 code analysis & bug record
├── new-architecture.md         # Current C++17 architecture & technical specification
└── src/
    ├── CMakeLists.txt          # Source compilation registration
    ├── config.h                # Hardware pinout (retained 100%), timing & calibrations
    ├── can_driver.h            # Handle-based TWAI CAN driver interface & health types
    ├── can_driver.cpp          # Driver implementation with deterministic bus-off recovery
    ├── rc_receiver.h           # Modern RMT pulse measurement & decoded snapshot interface
    ├── rc_receiver.cpp         # 6-channel calibrated RMT driver & deadband/hysteresis logic
    ├── freertos_hooks.cpp      # Stack overflow and tick hook definitions
    └── main.cpp                # Multi-task FreeRTOS architecture, CAN dispatch & transmit
```

---

## 2. Hardware Wiring (GPIO Map)

All physical pin assignments from the original hardware design are preserved **100% without modification**.

### 2.1 RMT Inputs — 6 PWM Channels from RC Receiver

| Channel | GPIO | Signal Name | Physical Connection / Role | Calibrated Engineering Output |
| :--- | :--- | :--- | :--- | :--- |
| `CH0` | **GPIO 18** | `RC_DRIVE` | Steering / Drive-by-wire analog channel | Steering angle: $\pm 450.0^\circ$ $\rightarrow$ `VCU_SES_REQ` (`0x169`) |
| `CH1` | **GPIO 19** | `RC_BRAKE` | Brake-by-wire proportional lever/trigger | Stroke setpoint: $0\text{ mm} \dots 27\text{ mm}$ $\rightarrow$ `VCU_SEB_REQ` (`0x7B9`) |
| `CH2` | **GPIO 14** | `RC_AUX_ANALOG`| Auxiliary analog input (potentiometer / slider)| Auxiliary analog telemetry $\rightarrow$ `0x0AA` |
| `CH3` | **GPIO 32** | `RC_PASS` | Pass-through / expansion channel | Auxiliary telemetry $\rightarrow$ `0x112` / telemetry |
| `CH4` | **GPIO 13** | `RC_IGNITION` | Digital ignition switch (2-position switch) | System Power Request $\rightarrow$ `HMI_PWR_REQ` (`0x112`) |
| `CH5` | **GPIO 4** | `RC_GEAR` | 3-position gear selector (Park / Reverse / Drive) | Mode & Gear Request $\rightarrow$ `HMI_MODE_REQ` (`0x111`) |

### 2.2 TWAI / CAN Bus Connection

| Function | GPIO | Electrical Spec | Driver Configuration |
| :--- | :--- | :--- | :--- |
| **CAN TX** | **GPIO 21** | 3.3 V Logic to Transceiver TXD | `twai_node_onchip` / `driver/twai`, 500 kbit/s |
| **CAN RX** | **GPIO 22** | 3.3 V Logic from Transceiver RXD | Filter: accept all |

Bus specifications:
- **Bitrate**: 500 kbit/s.
- **Topology**: Connected to Low-CAN bus with standard $120\,\Omega$ termination at bus endpoints.
- **Frames**: Standard 11-bit identifiers, Classical CAN 2.0A/B, zero-allocation frames.

---

## 3. Transmitter Mapping: FlySky FS-i6 Hardware Optimization

The physical remote control unit is a **FlySky FS-i6** 6-channel 2.4 GHz AFHDS 2A transmitter paired with an FS-iA6 or FS-iA6B receiver.

### 3.1 Transmitter Physical Controls Inventory
- **Gimbals (Sticks)**:
  - Right Stick (Horizontal / Vertical): Channels 1 & 2
  - Left Stick (Vertical / Horizontal): Channels 3 & 4
- **Toggle Switches**:
  - `SWA`: 2-Position toggle (Top-Left)
  - `SWB`: 2-Position toggle (Top-Inner-Left)
  - `SWC`: **3-Position toggle (Top-Inner-Right)** $\rightarrow$ Dedicated hardware 3-state selector.
  - `SWD`: 2-Position toggle (Top-Right)
- **Rotary Potentiometers**:
  - `VRA` / `VRB`: Continuous analog dials (Center top).

### 3.2 Switch Allocation & Ergonomic Vehicle Mapping

Because the FS-i6 outputs up to 6 PWM channels simultaneously over standard servo headers, the controls are allocated to maximize driver safety and control ergonomics:

| Transmitter Control | Receiver Channel | GPIO Pin | Function | Pulse Output Range | Vehicle Behavior |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Right Stick (X)** | CH1 $\rightarrow$ `CH0` | **GPIO 18** | **Steering** | $1000\,\mu\text{s} \dots 2000\,\mu\text{s}$ | Left/Right steering setpoint ($\pm 450.0^\circ$) via `0x169` |
| **Left Stick (Y)** | CH2 $\rightarrow$ `CH1` | **GPIO 19** | **Brake** | $1000\,\mu\text{s} \dots 2000\,\mu\text{s}$ | Proportional brake stroke ($0\text{ mm} \dots 27\text{ mm}$) via `0x7B9` |
| **VRA (Rotary Dial)**| CH3 $\rightarrow$ `CH2` | **GPIO 14** | **Speed Limiter / Trim** | $1000\,\mu\text{s} \dots 2000\,\mu\text{s}$ | Auxiliary trim / max speed clamp (0% to 100%) |
| **VRB (Rotary Dial)**| CH4 $\rightarrow$ `CH3` | **GPIO 32** | **Pass-Through / Aux** | $1000\,\mu\text{s} \dots 2000\,\mu\text{s}$ | Auxiliary lighting / payload trigger |
| **SWB (2-Pos Switch)**| CH5 $\rightarrow$ `CH4`| **GPIO 13** | **Ignition / Enable** | $1000\,\mu\text{s} \text{ (UP)} / 2000\,\mu\text{s} \text{ (DOWN)}$| Ignition state (`HMI_PWR_REQ` `0x112`) |
| **SWC (3-Pos Switch)**| CH6 $\rightarrow$ `CH5`| **GPIO 4** | **Gear Selector** | $1000\,\mu\text{s} / 1500\,\mu\text{s} / 2000\,\mu\text{s}$ | **UP = Reverse, MID = Park/Neutral, DOWN = Drive** |

#### Why SWC is Dedicated to Gear Selection
`SWC` is the **only physical 3-position toggle switch** on the FlySky FS-i6. Mapping `SWC` to `CH5` gives a deterministic physical detent for **Reverse (Up)**, **Neutral/Park (Mid)**, and **Drive (Down)**, completely avoiding accidental shifts during driving.

#### Transmitter Configuration Instructions (FS-i6 Menu)
1. Power on transmitter, hold `OK` to access the main menu.
2. Navigate to `Functions setup` $\rightarrow$ `Aux. channels`.
3. Set `Channel 5` source to `SWB` (or `SWA`).
4. Set `Channel 6` source to `SWC`.
5. Hold `CANCEL` to save configuration.

---

## 4. RMT Pulse Timing & Calibration

The legacy implementation suffered from clock divider confusion ($\text{clk\_div}=40 \rightarrow 0.5\,\mu\text{s/tick}$, but treated as $1.0\,\mu\text{s/tick}$). The C++ architecture normalizes all readings to **true microseconds ($\mu\text{s}$)** in the hardware driver layer.

### 4.1 RMT Peripheral Settings
- **Source Clock**: APB (80 MHz).
- **Counter Divisor**: `clk_div = 80` $\rightarrow$ **1 counter tick = exactly $1.0\,\mu\text{s}$**.
- **Glitch Filter**: Enabled, threshold = $10\,\mu\text{s}$ (filters RF noise on servo lines).
- **Idle Threshold**: $25\,000\,\mu\text{s}$ (25 ms end-of-frame idle detector).
- **Buffer Size**: Ring buffer with 2048 bytes per channel.

### 4.2 Pulse Width Decoding Ranges & Deadbands

Standard RC PWM operates on a 50 Hz frame period (20 ms) with pulses nominally between $1000\,\mu\text{s}$ and $2000\,\mu\text{s}$ centered at $1500\,\mu\text{s}$:

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Valid Range** | $900\,\mu\text{s} \dots 2100\,\mu\text{s}$ | Pulses outside this window indicate signal loss / cable disconnect |
| **Center / Neutral** | $1500\,\mu\text{s}$ | Nominal center pulse width |
| **Deadband Window** | $\pm 30\,\mu\text{s}$ ($1470 \dots 1530\,\mu\text{s}$) | Eliminates stick drift around center |
| **Deadman Timeout** | $100\text{ ms}$ | Absence of valid edges for $>100\text{ ms}$ triggers fail-safe stop |

### 4.3 Channel Transfer Functions

1. **CH0: Steering Angle (`RC_DRIVE`)**:
   - Normalized: $u_{\text{steer}} = \text{clamp}\left(\frac{t_{\mu\text{s}} - 1500}{500}, -1.0, 1.0\right)$ (deadband applied around center).
   - Angle Setpoint: $\theta_{\text{target}} = u_{\text{steer}} \times 450.0^\circ$ (resolution: $0.1^\circ$).
2. **CH1: Brake Stroke (`RC_BRAKE`)**:
   - Released: $t_{\mu\text{s}} \le 1520\,\mu\text{s} \rightarrow 0.0\text{ mm}$ stroke.
   - Pulled: $t_{\mu\text{s}} \in (1520, 2000] \rightarrow \text{Stroke} = \frac{t_{\mu\text{s}} - 1520}{480} \times 27.0\text{ mm}$.
3. **CH4: Ignition Switch (`RC_IGNITION` via SWB)**:
   - $t_{\mu\text{s}} < 1500\,\mu\text{s}$ (UP) $\rightarrow$ **Ignition OFF** (`req_start = 0`).
   - $t_{\mu\text{s}} \ge 1500\,\mu\text{s}$ (DOWN) $\rightarrow$ **Ignition ON** (`req_start = 1`).
4. **CH5: Gear Selector (`RC_GEAR` via SWC)**:
   - **Reverse (R)**: $t_{\mu\text{s}} \in [900, 1300]\,\mu\text{s}$ (Switch UP).
   - **Park / Neutral (P/N)**: $t_{\mu\text{s}} \in [1350, 1650]\,\mu\text{s}$ (Switch MID).
   - **Drive (D)**: $t_{\mu\text{s}} \in [1700, 2100]\,\mu\text{s}$ (Switch DOWN).
   - $50\,\mu\text{s}$ hysteresis zones prevent jitter between switch positions.

---

## 5. Multi-Task FreeRTOS Architecture

Following `sys-esp32` and `rt-esp32`, the application uses prioritized, independent FreeRTOS tasks with atomic snapshots, eliminating cross-task blocking.

```
       [RC Receiver Hardware (GPIO 18, 19, 14, 32, 13, 4)]
                              │
                      1.0 µs RMT ISR
                              ▼
┌───────────────────────────────────────────────────────────┐
│ Task: task_rc_capture (Priority 8, 50 Hz / 20 ms)         │
│  - Drain ringbuffers for all 6 channels                   │
│  - Validate pulse widths & apply deadbands                │
│  - Publish atomic snapshot: g_rc_snapshot                 │
└─────────────────────────────┬─────────────────────────────┘
                              │ std::atomic<RcSnapshot>
                              ▼
┌───────────────────────────────────────────────────────────┐
│ Task: task_can_tx (Priority 4, 50 Hz / 20 ms)             │
│  - Read latest g_rc_snapshot                              │
│  - Evaluate vehicle interlocks (Ignition & Gear state)    │
│  - Encode canonical protocol frames:                      │
│      * 0x169: can::custom::ses::Command (Steering EPS-C)  │
│      * 0x7B9: can::custom::seb::Command (Brake SEB)       │
│      * 0x111: can::gen::HmiModeReq (Mode request)         │
│      * 0x112: can::gen::HmiPwrReq  (Power request)        │
│      * 0x001: SAFETY_ESTOP (if RC deadman triggered)      │
│  - Transmit via g_can.send()                              │
└───────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────┐
│ Task: task_can_control (Priority 2, 20 ms)                │
│  - Service TWAI bus-off recovery machine                  │
│  - Supervise error active / passive state transitions     │
└───────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────┐
│ Task: task_heartbeat (Priority 1, 10 Hz / 100 ms)         │
│  - Increment alive_ctr                                    │
│  - Publish telemetry & node status diagnostics            │
└───────────────────────────────────────────────────────────┘
```

### 5.1 Task Priority & Execution Schedule

| Task Name | Function | Priority | Rate | Role |
| :--- | :--- | :--- | :--- | :--- |
| `rc_capture` | `task_rc_capture` | **8** | 50 Hz (20 ms) | Captures RMT pulses, validates boundaries, publishes snapshot |
| `can_tx` | `task_can_tx` | **4** | 50 Hz (20 ms) | Encodes canonical frames and transmits to Low-CAN |
| `can_ctrl` | `task_can_control` | **2** | 50 Hz (20 ms) | Oversees bus-off auto-recovery and health snapshot |
| `heartbeat`| `task_heartbeat` | **1** | 10 Hz (100 ms)| Node liveness, diagnostics, and alive counter |

---

## 6. Canonical Protocol & CAN Command Mapping

The firmware eliminates legacy arbitrary byte packing (`0x0BB`, manual shifts) in favor of typed C++ contracts from [`protocol/compat/can.hpp`](file:///e:/work/etrike/protocol/compat/can_protocol.hpp):

### 6.1 Outgoing CAN Messages

| CAN ID | Message Key | Codec Type | Cycle | Payload Summary |
| :--- | :--- | :--- | :--- | :--- |
| `0x169` | `ses:vcu_ses_req` | `can::custom::ses::Command` | 20 ms | **Steering Setpoint**: Alignment enable, control enable, target angle ($0.1^\circ$), rolling counter, XOR8-complement checksum. Active only when Ignition is ON and Gear is D/R. |
| `0x7B9` | `seb:vcu_seb_req` | `can::custom::seb::Command` | 20 ms | **Brake Setpoint**: Stroke request ($0\dots 27\text{ mm}$), alignment enable, control enable, rolling counter, XOR8-complement checksum. |
| `0x111` | `hmi:hmi_mode_req` | `can::gen::HmiModeReq` | 1000 ms / on-change | **Mode Request**: `req_mode = AUTO` (when remote engaged) or `MANUAL`. SYS remains sole mode authority. |
| `0x112` | `hmi:hmi_pwr_req` | `can::gen::HmiPwrReq` | 1000 ms / on-change | **Power Request**: `req_start = ON (1)` when CH4 switch is high, `OFF (0)` when low. |
| `0x001` | `safety:safety_estop`| `can::gen::SafetyEstop` | Event-driven | **Emergency Stop**: DLC=0 broadcast if RC signal is lost for $>100\text{ ms}$ while engaged. |

### 6.2 Transmission Interlock & Safety Logic

To ensure vehicle safety:
1. **Neutral / Park / Ignition OFF**:
   - Steering setpoints are held at neutral ($0^\circ$). Control enable bit is set to `false`.
   - Brake setpoint holds resting position ($0\text{ mm}$ stroke) unless the RC brake channel is actively pulled.
2. **Drive (D) or Reverse (R) with Ignition ON**:
   - Steering setpoint actively tracks CH0 with control enable = `true`.
   - Brake setpoint actively tracks CH1 proportional input.
3. **Signal Loss / Fail-Safe**:
   - If pulse capture on any critical channel times out ($>100\text{ ms}$ without valid edge):
     - `signal_valid` is set to `false`.
     - Target steering angle ramps to $0^\circ$.
     - Brake stroke immediately commands **maximum emergency braking ($27.0\text{ mm}$)**.
     - Mode transitions to `MANUAL` / `ESTOP`.
     - Rate-limited `0x001 SAFETY_ESTOP` is emitted.

---

## 7. Resolution of Legacy Architecture Quirks & Bugs

The new C++ architecture systematically resolves all 10 quirks identified in [`rm-esp32/architecture.md`](file:///e:/work/etrike/rm-esp32/architecture.md#L456-L514):

| # | Legacy Quirk / Bug in `architecture.md` §9 | New C++ Resolution |
| :--- | :--- | :--- |
| **1** | **Dead code in change detection**: Condition `diff <= 30 || diff >= -30` was a tautology; `past_val` never refreshed. | Eliminated faulty condition. Replaced with proper absolute threshold hysteresis: `std::abs(val - last_val) > kDeadband`. Previous snapshot is refreshed deterministically every cycle. |
| **2** | **`channel_id` leaking across frames**: CH4/CH5 had no CAN ID assigned and transmitted previous channel's ID (`0x112`). | Replaced single shared transmit frame with dedicated, strongly-typed message encoders. Each message has an immutable canonical ID. |
| **3** | **Gear bands overlap & gaps**: `(2200, 2600)` and `> 2500` overlapped; gaps between $2150\dots 2200$ were unhandled. | Defined disjoint, exhaustive calibration ranges using FlySky FS-i6 SWC positions with clear hysteresis bands ($50\,\mu\text{s}$) between Reverse, Neutral/Park, and Drive. Unrecognized pulses default safely to Neutral. |
| **4** | **Ordering dependency within snapshot**: CH0/CH1 transmitted before CH4/CH5 were decoded in the loop. | Atomic 2-phase execution: Phase 1 decodes all 6 channels into a unified `RcSnapshot`. Phase 2 evaluates interlocks and transmits CAN frames using the full, synchronized state. |
| **5** | **`ret` never updated**: Return status of `twai_transmit` was ignored. | Driver returns `bool`. Every transmit is checked. Critical frames (`0x7B9`, `0x001`) utilize automatic retry and failure counter tracking. |
| **6** | **`msg2.identifier` capture-order bug**: Frame initialized before ID set, sending ID 0 on boot. | Zero-initialization bug eliminated; `can::Frame` requires explicit ID and DLC in constructor or uses canonical generated constant IDs. |
| **7** | **Clock tick / microsecond unit mismatch**: APB/40 clock divisor produced $0.5\,\mu\text{s}$ ticks but treated as microseconds. | Configured `clk_div = 80` so 1 tick = exactly $1.0\,\mu\text{s}$. All thresholds and comments are calibrated in standard SI microseconds ($\mu\text{s}$). |
| **8** | **Duplicate FreeRTOS task names**: `can_task` and `rmt_task` shared identical name strings. | Standardized descriptive names: `"rc_capture"`, `"can_tx"`, `"can_ctrl"`, `"heartbeat"`. |
| **9** | **5 Hz loop rate vs 20 ms RC period**: 200 ms cycle discarded ~90% of RC frames. | Upgraded capture and transmit tasks to **50 Hz (20 ms)**, running synchronously with standard RC servo frame rates for responsive control. |
| **10**| **Missing `0x112` payload handling**: No switch cases existed for `0x112`, emitting empty frames. | Converted to canonical [`HmiPwrReq`](file:///e:/work/etrike/protocol/contracts/hmi.yaml) (`0x112`) using generated serialization with valid start request bits and rolling counter. |

---

## 8. CAN Driver & Bus-Off Self-Healing

The handle-based TWAI driver is directly ported from `sys-esp32/src/can_driver.h`:

- **Preserves DLC=0**: Critical for `SAFETY_ESTOP` wire compatibility.
- **Asynchronous ISR Callbacks**: `on_rx_done`, `on_tx_done`, and `on_state_change` run in ISR context and wake tasks without polling.
- **Deterministic Bus-Off Machine**:
  1. On `TWAI_ERROR_BUS_OFF`, driver immediately ceases transmission and sets internal state.
  2. `task_can_control` detects the bus-off condition and initiates `twai_node_recover()`.
  3. Exponential backoff ($500\text{ ms} \dots 5000\text{ ms}$) prevents bus thrashing if physical bus short circuit is present.
  4. Once bus recovery completes (128 occurrences of 11 consecutive recessive bits), TX slots are reset and normal transmission resumes.
- **Health Telemetry**: TEC (Transmit Error Counter) and REC (Receive Error Counter) are reported to the heartbeat task for diagnostics.
