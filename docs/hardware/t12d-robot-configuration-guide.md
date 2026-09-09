# RadioLink T12D & R16F Ground Robot Configuration Guide

Official reference guide for configuring the **RadioLink T12D** (FHSS V2.1) and **R16F V1.0** receiver specifically as an input device for the **E-Trike autonomous vehicle / ground robot** running [`rm-esp32-t12d`](file:///e:/work/etrike/rm-esp32-t12d).

---

## 1. Ground Robot Control Philosophy

For a ground vehicle, standard RC aircraft flight conventions (where throttle stays mechanically friction-locked wherever left) introduce unacceptable human-factor failure modes. The T12D must be configured for **ground robotics**:

```
               LEFT GIMBAL                                   RIGHT GIMBAL
        ┌───────────────────────┐                     ┌───────────────────────┐
        │           ▲           │                     │           ▲           │
        │           │           │                     │           │ SPEED (+) │
        │   ◄───────┼───────►   │ AUX PROPORTIONAL    │   ◄───────┼───────►   │ STEERING
        │   AUX X   │   AUX Y   │ (Pan/Tilt / Impl)   │  LEFT     │    RIGHT  │ (±45.0° Rack)
        │           ▼           │                     │           ▼ SPEED (-) │
        └───────────────────────┘                     └───────────────────────┘
            Spring Centered                               Spring Centered
                                                    (Release = 0 mm/s Stop)
```

### Core Architecture: Requests vs Authoritative Actions
The radio produces **requests**, never direct actuator outputs:
```
RadioLink T12D ──► R16F SBUS ──► MCU UART ──► Input Driver (Validation & Timeout)
                                                    │
                                                    ▼
                                            Command Interpreter
                                         (Velocity/Steering/Mode)
                                                    │
                                                    ▼
                                             Safety Arbiter
                                      (ESTOP, Interlocks, State)
                                                    │
                                                    ▼
                                            Low-CAN Actuators
                                           (SES, SEB, MTR, SYS)
```

---

## 2. 12-Channel Automotive HMI Mapping

| Channel | T12D Control | Physical Location & Type | Vehicle Purpose | MCU Interpretation & Range | CAN Target |
| :---: | :--- | :--- | :--- | :--- | :--- |
| **CH1** | **Right Stick X** | 2-Axis Gimbal (Spring Centered) | **Steering** | Proportional: $\pm 45.0^\circ$ rack angle ($29550\dots 30450$ raw) | `0x169 VCU_SES_REQ` |
| **CH2** | **Right Stick Y** | 2-Axis Gimbal (**Spring Centered**) | **Signed Longitudinal Velocity** | Proportional: $-1000\dots +3000\text{ mm/s}$ (Forward / Reverse with neutral dwell) | `0x204 RT_DRIVE_CMD` |
| **CH3** | **Left Stick X** | 2-Axis Gimbal (Spring Centered) | **Implement / Aux X** | Proportional: $0.0\dots 1.0$ (Pan / lateral implement) | Telemetry / Aux |
| **CH4** | **Left Stick Y** | 2-Axis Gimbal (Spring Centered) | **Implement / Aux Y** | Proportional: $0.0\dots 1.0$ (Tilt / vertical implement) | Telemetry / Aux |
| **CH5** | **SWA Switch** | Top-Left Outer (2-Position) | **Drive Enable Request** | Edge-Qualified: UP = Disabled, DOWN = Arming Request | Safety Arbiter |
| **CH6** | **SWB Switch** | Top-Left Inner (2-Position) | **Park / Brake Hold** | Semantic Request: UP = Park/Hold, DOWN = Drive | SYS / SEB |
| **CH7** | **SWC Switch** | Top-Right Inner (3-Position) | **Drive Envelope** | 3-Tier: UP = Precision (0.75 m/s), MID = Normal (1.8 m/s), DOWN = Fast (3.0 m/s) | Speed Arbiter |
| **CH8** | **SWD Switch** | Top-Right Outer (2-Position) | **Manual / Auto Request** | Asymmetric: UP = Manual, DOWN = Auto Request | `0x110 SYS_MODE_CMD` |
| **CH9** | **VRA Knob** | Top Center-Left Rotary | **Spare / Auxiliary** | Removed from driving path (auxiliary / implement speed) | Aux / Telemetry |
| **CH10**| **VRB Knob** | Top Center-Right Rotary | **Spare / Auxiliary** | Removed from steering dynamics (deterministic steering) | Aux / Telemetry |
| **CH11**| *Unassigned* | Software NULL | **Spare / Expansion** | Reserved (Lights / Horn) | — |
| **CH12**| *Unassigned* | Software NULL | **Spare / Expansion** | Reserved | — |

---

## 3. Critical Mechanical & Transmitter Setup Rules

### 3.1 Self-Centering Speed Stick (Right Stick Y)
- **Problem**: Aircraft throttles remain wherever they are left. If the operator drops or lets go of the transmitter, the robot continues moving.
- **Solution**: Follow RadioLink's official mechanical gimbal adjustment tutorial to enable **self-centering spring tension** on the longitudinal stick.
- **Result**: Releasing the stick immediately snaps velocity to **$0\text{ mm/s}$ (idle stop)**.

### 3.2 Mandatory Switch Self-Check (Power-On Safety)
The T12D features **Switch Self Check**, which warns if switches are in an active or unsafe state when powered on:
- Enable via: `MAIN MENU` $\longrightarrow$ `Transmitter Settings` $\longrightarrow$ `SWITCH SELF CHECK` $\longrightarrow$ `ENABLE`.
- Require at boot:
  - **SWA**: `UP` (Drive Disabled)
  - **SWB**: `UP` (Park / Brake Hold Engaged)
  - **SWC**: `UP` (Precision Mode — safest 25% envelope)
  - **SWD**: `UP` (Manual Mode)
- *If any switch is bumped in the storage case, the T12D beeps and refuses to transmit until corrected.*

### 3.3 Zero Out Trims & Disable Transmitter Curves
- **Digital Trims**: Set `CH1 TRIM = 0` and `CH2 TRIM = 0`. Never use transmitter trims to correct steering bias. Centralize mechanical offsets and deadbands in MCU firmware.
- **No Transmitter Curves / Rates**: Keep `D/R` and `CURVES` linear ($100\%$). Shifting velocity curves on the transmitter cascades with motor controller acceleration curves and complicates testing.
- **Channel Speed (CH SPEED)**: Keep `CH SPEED = OFF / 0s`. Never delay vehicle disable or brake commands in transmitter firmware. Acceleration shaping is owned by RT/SYS.
- **Programmable Mixes**: Ensure `PROG.MIX 1` through `PROG.MIX 8` are set to `INH` (Inhibited). Reusing switches in flight mixes can cause unintended dual-channel crosstalk.
- **Endpoints**: Set `END POINTS` to `100% / 100%` across all channels. Use **VRA** and MCU software limits to govern speed.

### 3.4 Direction Reversal & Neutral Dwell Protection
When the Right Stick Y controls both forward and reverse:
- Crossing neutral ($|y| < \text{deadband}$) triggers **Neutral / Deceleration**.
- The MCU controller will **not command reverse torque** until **BOTH** conditions are simultaneously met:
  $$\text{measured\_vehicle\_speed} \le 50\text{ mm/s} \quad \mathbf{AND} \quad \text{neutral\_dwell\_timer} \ge 200\text{ ms}$$
- A timer alone must **never** authorize reverse torque while the vehicle is still rolling forward, preventing severe inverter over-current and gearbox damage.

### 3.5 Physical Labeling Standard (ISO 2575 / UN Reg 121)
Physical vinyl or engraved labels on the T12D faceplate:
```
  [SWA]              [SWB]              [SWC]              [SWD]
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│    DRIVE    │    │   PARK /    │    │  PRECISION  │    │   MANUAL /  │
│   ENABLE    │    │    HOLD     │    │   NORMAL    │    │     AUTO    │
│             │    │             │    │    FAST     │    │   REQUEST   │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
  UP: DISABLE        UP: PARK/HOLD      UP: 0.75 m/s (PREC) UP: MANUAL
  DN: ENABLE         DN: DRIVE          MID: 1.8 m/s (NORM) DN: AUTO REQ
                                        DN: 3.0 m/s (FAST)
```

---

## 4. Software Safety Architecture & Operator Arming (ISO 13850)

### 4.1 Edge-Qualified SWA Arming Sequence
SWA is an arming request, not merely a level-triggered signal:
- At boot or after RF recovery, drive starts **Disarmed**.
- Arming requires:
  1. Link observed healthy for $\ge 500\text{ ms}$.
  2. SWA first observed in the **DISABLED** position (`UP`).
  3. Right Stick Y confirmed centered in neutral.
  4. Deliberate operator transition `DISABLED (UP) -> ENABLED (DOWN)`.
- Fllipping SWA `UP` disarms drive **immediately and unconditionally**.

### 4.2 Separation of Physical ESTOP from RF Loss
Per ISO 13850:
1. **Physical Emergency Stop**: Controlled exclusively by the vehicle's hardwired safety chain and red mushroom button. The radio controller **cannot** reset physical ESTOP. Resetting the emergency stop device must not restart the machine.
2. **RF Link Degradation & Loss**:
   - $0\dots 50\text{ ms}$: Normal operation.
   - $50\dots 100\text{ ms}$: Stale frames, hold previous safe state, log warning.
   - $> 100\text{ ms}$: RC link lost. Traction demand forced to $0\text{ mm/s}$, controlled safe braking requested, drive disarmed. Re-arm sequence required upon RF recovery.

---

## 5. Summary Checklist for Commissioning

- [ ] **Gimbal Mechanical Check**: Right stick Y self-centers reliably to spring neutral.
- [ ] **RF Module**: `Internal Module` $\rightarrow$ `FHSS V2.1` frozen.
- [ ] **Switch Self-Check**: Enabled for `SWA=UP, SWB=UP, SWC=UP, SWD=UP`.
- [ ] **Model Memory**: Verified as `ROBOT_MAIN`.
- [ ] **Mixes & Delays**: `PROG.MIX 1..8 = INH`, `CH SPEED = 0s`.
- [ ] **Trims & Curves**: Trims zeroed, curves linear ($100\%$).
- [ ] **MONITOR Truth Test**: Moving Right Stick X moves ONLY CH1; Right Stick Y moves ONLY CH2; SWA moves ONLY CH5.
- [ ] **Receiver Mode**: R16F solid **RED + BLUE LEDs** (PWM + SBUS).
- [ ] **RF Separation**: Transmitter $\ge 1.0\text{ m}$ from receiver during bench testing.
- [ ] **Failsafe**: Transmitter powered off $\rightarrow$ vehicle clamps brakes ($27.0\text{ mm}$), speed $0\text{ mm/s}$, `0x001 SAFETY_ESTOP` emitted.

---

## 6. Hardware Wiring & Receiver Commissioning

### 6.1 Wiring Overview Diagram

```text
                  RadioLink R16F Receiver
                     ┌────────────────┐
                     │ [S] CH16 (Top) ├─── (Inverted SBUS Signal) ───► GPIO 16 (ESP32 RX)
                     │ [+] 5V   (Mid) ├─── (Receiver Power)      ───► 5V Power Rail
                     │ [-] GND  (Bot) ├─── (Common Ground)       ───► GND
                     └────────────────┘

                     SN65HVD230 / VP230 CAN Transceiver
                     ┌────────────────┐
                     │  TXD           ├─── (CAN Send)            ───► GPIO 5 (ESP32 TX)
                     │  RXD           ├─── (CAN Receive)         ───► GPIO 4 (ESP32 RX)
                     │  VCC           ├─── (3.3V Power)          ───► 3.3V
                     │  GND           ├─── (Common Ground)       ───► GND
                     │  CANH / CANL   ├─────────────────────────────► Low CAN Bus
                     └────────────────┘
```

### 6.2 Receiver (R16F) Pin Connections

Pin ordering on 3-pin headers (top to bottom):
- **Top Row (S)**: Signal
- **Middle Row (+)**: Positive 5V
- **Bottom Row (-)**: Ground (GND)

| R16F Pin | Connect To | Description / Notes |
| :--- | :--- | :--- |
| **CH16 Signal** (Top) | **ESP32 GPIO 16** | Single-wire SBUS output (100 kbps, 8E2 inverted) |
| **CH16 +5V** (Middle) | **ESP32 5V / VIN** | Receiver 5V power supply (~50 mA) |
| **CH16 GND** (Bottom) | **ESP32 GND** | Common Ground |

> [!CAUTION]
> **Power Connection Rule**: Power the R16F using the **5V and GND pins on channel rows (CH1..CH16)**. Do **NOT** power the receiver through the dedicated telemetry EXT port!

### 6.3 Low-CAN Transceiver (SN65HVD230 / VP230) Connections

| CAN Module Pin | Connect To | Description / Notes |
| :--- | :--- | :--- |
| **TXD** | **ESP32 GPIO 5** | MCU CAN Transmit (Unified with RT/SYS) |
| **RXD** | **ESP32 GPIO 4** | MCU CAN Receive (Unified with RT/SYS) |
| **VCC** | **ESP32 3.3V** | Transceiver 3.3V logic power |
| **GND** | **ESP32 GND** | Common Ground |
| **CANH / CANL** | **Vehicle Low CAN Bus** | Differential CAN lines (500 kbit/s, Classic CAN 2.0A) |

### 6.4 Step-by-Step Receiver Setup & SBUS Mode Activation

#### Step A: Select Protocol on Transmitter (T12D)
1. Power on T12D transmitter.
2. Go to **SYSTEM / MODEL MENU** $\rightarrow$ **PROTOCOL**.
3. Set Protocol to **FHSS V2.1** (required to carry all 12 channels).

#### Step B: Bind Receiver (R16F)
1. Place T12D transmitter approximately $0.5\dots 1.0\text{ m}$ away from R16F receiver.
2. Power on the R16F receiver (via ESP32 5V rail).
3. Press and hold the **ID SET** button on the side of R16F for $>1\text{ second}$.
4. The LED will flash rapidly and then remain **solid**, confirming successful binding.

#### Step C: Enable SBUS Output Mode (Crucial!)
By default, the R16F outputs individual PWM servo pulses. You must switch physical **CH16** to **SBUS mode**:
1. With receiver powered on, **short-press** the **ID SET** button **once**.
2. Check the dual status LEDs on the R16F:
   - **RED LED**: ON
   - **BLUE LED**: ON
   - **Meaning**: **Red + Blue ON = PWM + SBUS Mode**. CH16 is now active as the inverted 100 kbps SBUS serial line.

