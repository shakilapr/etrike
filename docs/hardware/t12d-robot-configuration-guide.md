# RadioLink T12D & R16F Ground Robot Configuration Guide

Official reference guide for configuring the **RadioLink T12D** (FHSS V2.1) and **R16F V1.0** receiver specifically as an input device for the **E-Trike autonomous vehicle / ground robot** running [`rm-esp32-t12d`](file:///e:/work/etrike/rm-esp32-t12d).

---

## 1. Ground Robot Control Philosophy

For a ground vehicle, standard RC aircraft flight conventions (where throttle stays mechanically friction-locked wherever left) introduce unacceptable human-factor failure modes. The T12D must be configured for **ground robotics**:

```
               LEFT GIMBAL                                   RIGHT GIMBAL
        ┌───────────────────────┐                     ┌───────────────────────┐
        │           ▲           │                     │           ▲           │
        │           │           │                     │           │ BRAKE     │ SERVICE BRAKE
        │   ◄───────┼───────►   │ MOTOR THROTTLE      │   ◄───────┼───────►   │ (0..27.0mm, ±220us DB)
        │   AUX X   │ THROTTLE  │ (0..100%, Ratchet)  │  STEER L  │  STEER R  │ STEERING
        │           ▼           │                     │           ▼ BRAKE     │ (±45.0° Rack)
        └───────────────────────┘                     └───────────────────────┘
            Ratcheted Throttle Y                          Spring Centered X & Y
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
| **CH1** | **Right Stick X** | 2-Axis Gimbal (Spring Centered) | **Steering** | Proportional: $\pm 45.0^\circ$ rack angle ($29550\dots 30450$ raw, $\pm 30\,\mu\text{s}$ center deadband) | `0x169 VCU_SES_REQ` |
| **CH2** | **Right Stick Y** | 2-Axis Gimbal (Spring Centered) | **Service Brake** | Proportional: $0.0 \dots 27.0\text{ mm}$ stroke (Wide $\pm 220\,\mu\text{s}$ center deadband) | `0x7B9 VCU_SEB_REQ` |
| **CH3** | **Left Stick Y** | 2-Axis Gimbal (Ratcheted) | **Motor Throttle** | Proportional: $0\% \dots 100\%$ ($1160\dots 1900\,\mu\text{s}$, rest $\le 1160\,\mu\text{s} = 0\%$) | `0x204 RT_DRIVE_CMD` |
| **CH4** | **Left Stick X** | 2-Axis Gimbal (Spring Centered) | **Spare / Auxiliary** | Proportional: $0.0\dots 1.0$ (Implement lateral) | Telemetry / Aux |
| **CH5** | **SWA Switch** | Top-Left Outer (3-Position) | **Target Selection** | 3-Tier: UP = BARE, MID = SYS, DOWN = RT | Mode Arbiter |
| **CH6** | **SWB Switch** | Top-Left Inner (3-Position) | **Park / Brake Hold** | Semantic Request: UP = Park Hold (15mm), MID/DOWN = Released (0mm) | SYS / SEB |
| **CH7** | **SWC Switch** | Top-Right Inner (3-Position) | **Transmission Gear** | 3-Tier: UP = Reverse (`R`), MID = Neutral (`N`), DOWN = Drive (`D`) | Gear Arbiter |
| **CH8** | **SWD Switch** | Top-Right Outer (2-Position) | **Drive Enable Request** | Direct: UP = Disabled (OFF), DOWN = Enable Request (ON) | Safety Arbiter |
| **CH9** | **VRA Knob** | Top Center-Left Rotary | **Speed Governor** | Proportional: $0.0 \dots 1.0$ (Dynamically scales max speed ceiling 0..100% for D and R) | Motor Governor |
| **CH10**| **VRB Knob** | Top Center-Right Rotary | **Aux Analog Knob 2** | Proportional: $0.0 \dots 1.0$ | Aux / Telemetry |
| **CH11**| **VRC Knob** | Top-Left Shoulder Switch/Rotary| **Aux Brake Pull-Down** | Rest at 0 (1500us); 0 to -10% deadband; -10% to -100% applies 0..27mm brake | `0x7B9` Service Brake |
| **CH12**| **VRD Knob** | Top-Right Shoulder Switch/Rotary| **Aux Brake Pull-Down** | Rest at 0 (1500us); 0 to -10% deadband; -10% to -100% applies 0..27mm brake | `0x7B9` Service Brake |

---

## 3. Critical Mechanical & Transmitter Setup Rules

### 3.1 Self-Centering Steering & Service Brake (Right Stick)
- **Problem**: Accidental deflection during steering must not cause unintentional braking.
- **Solution**:
  - Right Stick X & Y are spring-centered.
  - Right Stick Y utilizes a wide **$\pm 220\,\mu\text{s}$ center deadband** ($1280\dots 1720\,\mu\text{s}$) so full left/right steering sweeps do not accidentally engage the caliper.
  - Deflecting beyond deadband pushes or pulls progressive caliper stroke up to $27.0\text{ mm}$.

### 3.2 Mandatory Switch Self-Check (Power-On Safety)
The T12D features **Switch Self Check**, which warns if switches are in an active or unsafe state when powered on:
- Enable via: `MAIN MENU` $\longrightarrow$ `Transmitter Settings` $\longrightarrow$ `SWITCH SELF CHECK` $\longrightarrow$ `ENABLE`.
- Require at boot:
  - **SWA**: `UP` (BARE Mode)
  - **SWB**: `UP` (Park / Brake Hold Engaged)
  - **SWC**: `MID` (Neutral Gear `N`)
  - **SWD**: `UP` (Drive Disabled / Disarmed)
- *If any switch is bumped in the storage case, the T12D beeps and refuses to transmit until corrected.*

### 3.3 Zero Out Trims & Disable Transmitter Curves
- **Digital Trims**: Set `CH1 TRIM = 0` and `CH2 TRIM = 0`. Never use transmitter trims to correct steering bias. Centralize mechanical offsets and deadbands in MCU firmware.
- **No Transmitter Curves / Rates**: Keep `D/R` and `CURVES` linear ($100\%$). Shifting velocity curves on the transmitter cascades with motor controller acceleration curves and complicates testing.
- **Channel Speed (CH SPEED)**: Keep `CH SPEED = OFF / 0s`. Never delay vehicle disable or brake commands in transmitter firmware. Acceleration shaping is owned by RT/SYS.
- **Programmable Mixes**: Ensure `PROG.MIX 1` through `PROG.MIX 8` are set to `INH` (Inhibited). Reusing switches in flight mixes can cause unintended dual-channel crosstalk.
- **Endpoints**: Set `END POINTS` to `100% / 100%` across all channels. Use **VRA** and MCU software limits to govern speed.

### 3.4 Physical Labeling Standard (ISO 2575 / UN Reg 121)
Physical vinyl or engraved labels on the T12D faceplate:
```
  [SWA]              [SWB]              [SWC]              [SWD]
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  OPERATING  │    │   PARK /    │    │    GEAR     │    │    DRIVE    │
│    MODE     │    │    HOLD     │    │   SELECT    │    │   ENABLE    │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
  UP:  BARE          UP:  PARK/HOLD     UP:  REVERSE (R)   UP: DISABLE
  MID: SYS           MID: DRIVE (0mm)   MID: NEUTRAL (N)   DN: ENABLE (ARM)
  DN:  RT            DN:  DRIVE (0mm)   DN:  DRIVE   (D)
```

---

## 4. Software Safety Architecture & Operator Arming (ISO 13850)

### 4.1 Direct Switch Control (SWD Drive Enable)
- Drive is enabled immediately when SWD is switched **DOWN** (provided park is released, link is healthy, and brake is not holding).
- Flipping SWD **UP** disables drive immediately.

### 4.2 Separation of Physical ESTOP from RF Loss
Per ISO 13850:
1. **Physical Emergency Stop**: Controlled exclusively by the vehicle's hardwired safety chain and red mushroom button. The radio controller **cannot** reset physical ESTOP. Resetting the emergency stop device must not restart the machine.
2. **RF Link Degradation & Loss**:
   - $0\dots 50\text{ ms}$: Normal operation.
   - $50\dots 150\text{ ms}$: Stale frames, hold previous safe state, log warning.
   - $> 150\text{ ms}$: RC link lost. Traction demand forced to $0\text{ mm/s}$, safe park braking ($15.0\text{ mm}$) commanded, transmission set to Neutral. Normal operation resumes automatically once valid RF link is restored.

---

## 5. Summary Checklist for Commissioning

- [ ] **Gimbal Mechanical Check**: Right stick X & Y self-center reliably to spring neutral. Left stick ratcheted throttle sits cleanly at bottom idle ($\le 1160\,\mu\text{s}$).
- [ ] **RF Module**: `Internal Module` $\rightarrow$ `FHSS V2.1` frozen.
- [ ] **Switch Self-Check**: Enabled for `SWA=UP, SWB=UP, SWC=MID, SWD=UP`.
- [ ] **AUX-CH Assignments**: CH1=STK-R-X, CH2=STK-R-Y, CH3=STK-L-Y, CH5=SWA, CH6=SWB, CH7=SWC, CH8=SWD, CH9=VRA, CH11=VRC, CH12=VRD.
- [ ] **Model Memory**: Dedicated vehicle profile verified.
- [ ] **Mixes & Delays**: `PROG.MIX 1..8 = INH`, `CH SPEED = 0s`.
- [ ] **Trims & Curves**: Trims zeroed, curves linear ($100\%$).
- [ ] **Receiver Mode**: R16F solid **RED + BLUE LEDs** (PWM + SBUS).
- [ ] **RF Separation**: Transmitter $\ge 1.0\text{ m}$ from receiver during bench testing.
- [ ] **Failsafe**: Transmitter powered off $\rightarrow$ vehicle engages park brake ($15.0\text{ mm}$), speed $0\text{ mm/s}$, gear Neutral.

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

