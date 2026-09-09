# RM-ESP32-T12D — Automotive HMI Receiver Gateway (RadioLink T12D / R16F SBUS)

Gateway firmware for the **RadioLink T12D** transmitter and **RadioLink R16F V1.0** receiver running on the **ESP32** / **ESP32-S3** microcontroller over single-wire **SBUS**.

Designed specifically for the electric three-wheeler (`etrike`), adhering to **NHTSA, SAE, FHWA, ISO 2575, UN Regulation 121, and ISO 13850** automotive HMI and machinery safety standards.

---

## 1. System Overview & HMI Architecture

The `rm-esp32-t12d` node interfaces the RadioLink T12D transmitter (FHSS V2.1 protocol) with the vehicle's **Low CAN bus (500 kbit/s, Classic CAN 2.0A)**.

Unlike hobby RC aircraft conventions (where throttles remain mechanically friction-locked wherever left), this gateway implements an **Automotive Ground-Vehicle HMI**:

```
              [SWA] (2-pos)             [SWB] (2-pos)             [SWC] (3-pos)             [SWD] (2-pos)
              ┌───────────┐             ┌───────────┐             ┌───────────┐             ┌───────────┐
              │   DRIVE   │             │   PARK /  │             │ PRECISION │             │  MANUAL / │
              │   ENABLE  │             │    HOLD   │             │  NORMAL / │             │    AUTO   │
              │  REQUEST  │             │           │             │   FAST    │             │  REQUEST  │
              └───────────┘             └───────────┘             └───────────┘             └───────────┘
               UP: DISABLE               UP: PARK/HOLD             UP:  PRECISION (0.75 m/s) UP: MANUAL
               DN: ENABLE REQ            DN: DRIVE                 MID: NORMAL    (1.8 m/s)  DN: AUTO REQ
                                                                   DN:  FAST     (3.0 m/s)

                     [VRA] (rotary)                            [VRB] (rotary)
                     ┌───────────┐                             ┌───────────┐
                     │   SPARE / │                             │  SPARE /  │
                     │ AUXILIARY │                             │ AUXILIARY │
                     └───────────┘                             └───────────┘
                       (Reserved)                                (Reserved)

           LEFT GIMBAL (Spring Centered)              RIGHT GIMBAL (Spring Centered)
           ┌───────────────────────────┐              ┌───────────────────────────┐
           │             ▲             │              │             ▲             │
           │             │             │              │             │ FORWARD (+) │
           │     ◄───────┼───────►     │ IMPLEMENT    │     ◄───────┼───────►     │ STEERING (X)
           │     AUX X   │   AUX Y     │ (Pan / Tilt) │    LEFT     │    RIGHT    │ (±45.0° Rack)
           │             ▼             │              │             ▼ REVERSE (-) │
           └───────────────────────────┘              └───────────────────────────┘
                   (Auxiliary)                                (Vehicle Drive)
```

---

## 2. 12-Channel Automotive Mapping Table

| Channel | Physical Control | Function | Signal Interpretation & Range | CAN Target |
| :---: | :--- | :--- | :--- | :--- |
| **CH1** | **Right Stick X** | **Steering** | Proportional: $\pm 45.0^\circ$ rack angle ($29550\dots 30450$ raw, $\pm 30\,\mu\text{s}$ deadband) | `0x169 VCU_SES_REQ` |
| **CH2** | **Right Stick Y** | **Signed Velocity** | Proportional: $-1000\dots +3000\text{ mm/s}$ (Spring self-centering, neutral $= 0$) | `0x204 RT_DRIVE_CMD` |
| **CH3** | **Left Stick X** | **Implement / Aux X** | Proportional: $0.0\dots 1.0$ (Pan / lateral implement) | Telemetry / Aux |
| **CH4** | **Left Stick Y** | **Implement / Aux Y** | Proportional: $0.0\dots 1.0$ (Tilt / vertical implement) | Telemetry / Aux |
| **CH5** | **SWA Switch** | **Drive Enable Request** | Edge-Qualified: UP = Disabled, DOWN = Arming Request | Safety Arbiter |
| **CH6** | **SWB Switch** | **Park / Brake Hold** | Semantic Request: UP = Park/Hold ($15\text{ mm}$ SEB hold), DOWN = Drive | `0x7B9` / `0x204` |
| **CH7** | **SWC Switch** | **Drive Envelope** | 3-Tier: UP = Precision ($0.75\text{ m/s}$), MID = Normal ($1.8\text{ m/s}$), DOWN = Fast ($3.0\text{ m/s}$) | Speed Arbiter |
| **CH8** | **SWD Switch** | **Manual / Auto Request** | Asymmetric: UP = Manual, DOWN = Auto Request | `0x110 SYS_MODE_CMD` |
| **CH9** | **VRA Knob** | **Spare / Auxiliary** | Removed from driving path (auxiliary / implement speed) | Aux / Telemetry |
| **CH10**| **VRB Knob** | **Spare / Auxiliary** | Removed from steering dynamics (deterministic steering) | Aux / Telemetry |
| **CH11**| *Unassigned* | **Spare / Expansion** | Reserved | — |
| **CH12**| *Unassigned* | **Spare / Expansion** | Reserved | — |

---

## 3. Core Safety Rules & Operating Principles

### A. Right Gimbal Driving Dynamics
- **Deterministic Steering (CH1)**: Steering calibration is fixed and deterministic. VRB does **not** dynamically change steering curves; vehicle steering feels identical on every drive. Speed-dependent steering rate limits belong in RT/SYS.
- **Signed Velocity & Spring Neutral (CH2)**: Right Stick Y springs to neutral. Releasing the stick snaps commanded speed to $0\text{ mm/s}$ (regenerative deceleration to stop).
- **Strict Direction Reversal Protection**: Moving from forward to reverse requires:
  $$\text{measured\_vehicle\_speed} \le 50\text{ mm/s} \quad \mathbf{AND} \quad \text{neutral\_dwell\_timer} \ge 200\text{ ms}$$
  A timer alone never authorizes reverse torque while the vehicle is moving forward.

### B. Edge-Qualified SWA Arming Sequence
- On boot or following RF link recovery, drive starts `Disarmed`.
- Arming requires:
  1. RF link observed healthy for $\ge 500\text{ ms}$.
  2. SWA first observed in the **DISABLED** position (`UP`).
  3. Right Stick Y confirmed in neutral ($|y| \le 0.04$).
  4. Deliberate operator transition `DISABLED (UP) -> ENABLED (DOWN)`.
- Toggling SWA `UP` disarms drive **immediately and unconditionally**.

### C. Semantic Park / Brake Hold (SWB)
- `SWB = UP`: Driver requests `PARK_HOLD`. Commands electro-hydraulic caliper `0x7B9 VCU_SEB_REQ` to hold $15.0\text{ mm}$ stroke ($900$ raw) and neutral motor torque.
- `SWB = DOWN`: Driver requests `DRIVE`. Releasing Park requires stick in neutral before traction engages.

### D. Single-Control Speed Envelope (SWC)
Speed limits are unambiguous and controlled solely by Switch C:
- **PRECISION (UP)**: $0\dots 0.75\text{ m/s}$ ($2.7\text{ km/h}$) — yard maneuvering / docking.
- **NORMAL (MID)**: $0\dots 1.8\text{ m/s}$ ($6.5\text{ km/h}$) — standard transport.
- **FAST (DOWN)**: $0\dots 3.0\text{ m/s}$ ($10.8\text{ km/h}$) — conditionally granted by SYS based on vehicle health.

### E. Asymmetric Authority Transition (SWD)
- **Manual $\to$ Auto**: Requires vehicle stationary ($|\text{speed}| < 50\text{ mm/s}$), stick in neutral, and system health checks.
- **Auto $\to$ Manual**: Flipping SWD `UP` immediately and unconditionally revokes autonomous authority.

### F. Separation of Physical ESTOP from RF Loss (ISO 13850)
- **Physical Emergency Stop**: Controlled exclusively by the vehicle's hardwired safety chain and red mushroom button. Radio controller switches **cannot** reset physical ESTOP. Resetting the emergency stop device must not restart the machine.
- **Graduated RF Link States**:
  - $0\dots 50\text{ ms}$: Normal operation (`LinkState::Normal`).
  - $50\dots 100\text{ ms}$: Stale frames, hold previous safe state, log warning (`LinkState::Degraded`).
  - $> 100\text{ ms}$: RC link lost (`LinkState::Lost`). Motor setpoint forced to $0\text{ mm/s}$, controlled safe stop commanded, drive disarmed. Re-arm sequence required upon RF recovery.

---

## 4. Hardware Wiring & Level Divider

Only **three connections** are required between the R16F receiver and the ESP32:

| R16F Pin | ESP32 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **CH16 Signal** (Top) | **GPIO 16** | SBUS Serial RX | Inverted UART1 RX (100 kbps, 8E2) |
| **CH16 +5V** (Middle) | **5V Logic Rail** | Receiver Power | 3–12 V supply (~50 mA @ 5V). Do NOT use EXT port to power receiver! |
| **CH16 GND** (Bottom) | **GND** | Ground | Common reference ground |

### CAN Bus Wiring:
| Net | ESP32 Pin | Direction | Connected To |
| :--- | :--- | :--- | :--- |
| **CAN TX** | **GPIO 21** | Output | Low CAN Transceiver TXD |
| **CAN RX** | **GPIO 22** | Input | Low CAN Transceiver RXD |

---

## 5. Receiver & Transmitter Setup

### 5.1 RadioLink T12D Transmitter Configuration
1. Power on T12D.
2. In the System/Model menu, ensure protocol is set to **FHSS V2.1** (required for 12 proportional channels).
3. Assign physical controls:
   - CH1: Right Stick X (Steering)
   - CH2: Right Stick Y (Velocity)
   - CH3: Left Stick X (Aux X)
   - CH4: Left Stick Y (Aux Y)
   - CH5: SWA (Drive Enable)
   - CH6: SWB (Park / Brake Hold)
   - CH7: SWC (Drive Envelope)
   - CH8: SWD (Manual / Auto)

### 5.2 Binding R16F Receiver
1. Place transmitter and receiver ~60 cm apart.
2. Power on the R16F receiver.
3. Press and hold the **ID SET** button on the R16F for $>1$ second.
4. The receiver LED will flash rapidly. Release the button.
5. Once bound, the LED remains solid.

### 5.3 Enable SBUS Output Mode on R16F
1. With receiver powered, **short press** the ID SET button once.
2. Verify LEDs:
   - **RED LED**: ON
   - **BLUE LED**: ON
   - **Interpretation**: Red + Blue ON indicates **PWM + SBUS mode**. CH16 is now the SBUS serial output.

---

## 6. Serial Telemetry Logging

To prevent flooding serial monitors at high CAN frequencies (50 Hz), `rm-esp32-t12d` uses a **dual-rate change-driven + 2 Hz decimated logger**:
- High-priority state changes (arming, link loss, park transitions, error clusters) are logged immediately.
- Steady-state periodic frames are logged at 2 Hz:
  ```
  [RM-TX] Link:OK Frame:2450 | ARM:ON PARK:OFF ENV:NORMAL(1800) | V: 1250 mm/s STR:  12.4 deg | CAN TX: [0x169, 0x204]
  ```

---

## 7. Building, Flashing & Testing

### Building with PlatformIO
```powershell
cd rm-esp32-t12d

# Build vehicle firmware
pio run -e vehicle

# Build bench firmware (emulated SYS safety authority)
pio run -e bench

# Flash to ESP32
pio run -e vehicle -t upload

# Monitor telemetry serial log (115200 baud)
pio run -e vehicle -t upload -t monitor
```

### Running Native Unit Tests
```powershell
C:\TDM-GCC-64\bin\g++.exe -std=c++17 -Wall -Wextra -Irm-esp32-t12d/src rm-esp32-t12d/test/test_rm_t12d_suite.cpp -o rm-esp32-t12d/test/test_rm_t12d_suite.exe
rm-esp32-t12d/test/test_rm_t12d_suite.exe
```
All 11 test suites (116 assertions) validate SBUS bit-unpacking, graduated link loss, asymmetric hysteresis, strict direction reversal, edge-qualified arming, and CAN codecs.
