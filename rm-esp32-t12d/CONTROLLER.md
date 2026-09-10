# RadioLink T12D RC Controller Mapping (RM-ESP32-T12D)

Vehicle driving controls, switch mapping, safety interlocks, and CAN signals for the electric three-wheeler (`etrike`).

---

## 1. Controller Layout & Physical Mapping

### Visual Layout Map

```
              [SWA]   [SWB]             [SWC]   [SWD]
             (3-pos) (3-pos)           (3-pos) (2-pos)
                │       │                 │       │
          TARGET SELECT PARK / HOLD   GEAR SELECT  DRIVE ENABLE
          UP:   BARE    UP:   HOLD     UP:   REV   UP:   OFF
          MID:  SYS     MID:  RELEASE  MID:  NEU   DOWN: ON (ARM)
          DOWN: RT      DOWN: RELEASE  DOWN: DRV

                      [VRA]             [VRB]
                     (dial)            (dial)
                        │                 │
                   AUX ANALOG 1      AUX ANALOG 2
                    (0.0..1.0)        (0.0..1.0)

              [VRC - shoulder]   [VRD - shoulder]
                 (0.0..1.0)         (0.0..1.0)

          ┌───────────────┐           ┌───────────────┐
          │       ▲       │           │       ▲       │
          │       │       │           │       │       │ SERVICE BRAKE
          │   ◄───┼───►   │ (Spare)   │   ◄───┼───►   │ (0.0 to 27.0mm)
          │       ▼       │           │       ▼       │
          │    THROTTLE   │           │   STEER (X)   │
          │  (0% to 100%) │           │   BRAKE (Y)   │ (Spring Centered)
          └───────────────┘           └───────────────┘
             LEFT GIMBAL                 RIGHT GIMBAL
```

---

### Physical Controls & Channel Summary Table

| Control ID | Hardware Type | Location | Channel / SBUS | Function | Physical Action | Pulse Range ($\mu\text{s}$) | Vehicle Output Range | Primary CAN ID / Frame |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Right Stick (X)** | 2-Axis Gimbal (Spring) | Lower Right | **CH1** | **Steering Rack** | Horizontal (Left / Right) | $1050 \dots 1950\,\mu\text{s}$ | $-45.0^\circ \dots +45.0^\circ$ | `0x169` `VCU_SES_REQ` / `0x303` `HOST_STEER_CMD` |
| **Right Stick (Y)** | 2-Axis Gimbal (Spring) | Lower Right | **CH2** | **Service Brake Stroke** | Vertical (Push Forward) | $1520 \dots 1950\,\mu\text{s}$ | $0.0 \dots 27.0\,\text{mm}$ stroke | `0x7B9` `VCU_SEB_REQ` / `0x301` `HOST_BRAKE_REQ` |
| **Left Stick (Y)** | 2-Axis Gimbal (Ratcheted) | Lower Left | **CH3** | **Motor Throttle** | Vertical (Up / Down) | $1050 \dots 1950\,\mu\text{s}$ | $0\% \dots 100\%$ ($0 \dots 3000\,\text{mm/s}$) | `0x204` `RT_DRIVE_CMD` / `0x300` `HOST_DRIVE_CMD` |
| **Left Stick (X)** | 2-Axis Gimbal | Lower Left | **CH4** | **Reserved / Spare** | Horizontal (Left / Right) | $1000 \dots 2000\,\mu\text{s}$ | Unused | — |
| **SWA Switch** | 3-Position Toggle | Top Far-Left | **CH5** | **Target Selection** | UP / MID / DOWN | $1000$ / $1500$ / $2000\,\mu\text{s}$ | `BARE` / `SYS` / `RT` | Target Bus Cluster Selection |
| **SWB Switch** | 3-Position Toggle | Top Inner-Left | **CH6** | **Electric Park Brake (Hold)** | UP / MID / DOWN | $1000$ / $1500$ / $2000\,\mu\text{s}$ | UP: `PRK:HOLD` ($15\,\text{mm}$) / MID,DOWN: `PRK:OFF` ($0\,\text{mm}$) | `0x7B9` `VCU_SEB_REQ` / `0x301` `HOST_BRAKE_REQ` |
| **SWC Switch** | 3-Position Toggle | Top Inner-Right | **CH7** | **Transmission Gear Selector** | UP / MID / DOWN | $1000$ / $1500$ / $2000\,\mu\text{s}$ | `[R]` Reverse / `[N]` Neutral / `[D]` Drive | `0x204` `RT_DRIVE_CMD` / `0x300` `HOST_DRIVE_CMD` |
| **SWD Switch** | 2-Position Toggle | Top Far-Right | **CH8** | **Drive Enable Request** | UP / DOWN | $\approx 1000$ / $\approx 2000\,\mu\text{s}$ | `ARM:OFF` (Disabled) / `ARM:ON` (Enable Request) | `0x113` `SYS_PWR_CMD` / `0x112` `HMI_PWR_REQ` |
| **VRA Knob** | Rotary Potentiometer | Top Center-Left | **CH9** | **Aux Analog Knob 1** | Dial Rotation | $1000 \dots 2000\,\mu\text{s}$ | $0.0 \dots 1.0$ | Telemetry / Aux |
| **VRB Knob** | Rotary Potentiometer | Top Center-Right | **CH10** | **Aux Analog Knob 2** | Dial Rotation | $1000 \dots 2000\,\mu\text{s}$ | $0.0 \dots 1.0$ | Telemetry / Aux |
| **VRC Knob** | Rotary Shoulder Knob | Top-Left Shoulder | **CH11** | **Aux Analog Knob 3** | Knob Rotation | $1000 \dots 2000\,\mu\text{s}$ | $0.0 \dots 1.0$ | Telemetry / Aux |
| **VRD Knob** | Rotary Shoulder Knob | Top-Right Shoulder | **CH12** | **Aux Analog Knob 4** | Knob Rotation | $1000 \dots 2000\,\mu\text{s}$ | $0.0 \dots 1.0$ | Telemetry / Aux |

---

## 2. Operating Principles & Safety Interlocks

### A. SWA: 3-Position Target Selection (BARE / SYS / RT)
- **UP ($\le 1300\,\mu\text{s}$)**: **`BARE`**
  - Direct actuator control (SES, SEB, MTR) on Low-CAN without SYS, RT, or Host.
- **MID ($1301 \dots 1699\,\mu\text{s}$)**: **`SYS`**
  - Direct connection to `sys-esp32` on Low-CAN, emulating `rt-esp32` and HMI commands.
- **DOWN ($\ge 1700\,\mu\text{s}$)**: **`RT`**
  - Direct connection to `rt-esp32` on High-CAN, emulating autonomous Host computer commands.

---

### B. SWB: Electric Park Brake / Brake Hold
- **UP (`PRK:HOLD`, $\le 1300\,\mu\text{s}$)**:
  - Electronic brake actuator clamps to **$15.0\,\text{mm}$ holding stroke** (`0x7B9 raw: 900`).
  - Motor transmission state is clamped strictly to **Neutral (`[N]`)** with $0\,\text{mm/s}$ target velocity.
- **MID or DOWN (`PRK:OFF`, $> 1300\,\mu\text{s}$)**:
  - Brake disengages to **$0.0\,\text{mm}$ resting stroke** (`0x7B9 raw: 600`), allowing the vehicle to drive.

---

### C. Right Gimbal: Steering & Service Brake
- **Horizontal Axis (CH1) — Steering Rack**:
  - Center ($1500\,\mu\text{s} \pm 30\,\mu\text{s}$ deadband): $0.0^\circ$ (Straight ahead).
  - Full Left ($1050\,\mu\text{s}$): $-45.0^\circ$ (Hard Left).
  - Full Right ($1950\,\mu\text{s}$): $+45.0^\circ$ (Hard Right).
- **Vertical Axis (CH2) — Service Brake**:
  - Spring-centered rest ($\le 1520\,\mu\text{s}$): $0.0\,\text{mm}$ (Released).
  - Push Forward ($> 1520\,\mu\text{s}$ to $1950\,\mu\text{s}$): Progressively applies service brake up to $27.0\,\text{mm}$ full clamp.

---

### D. Left Gimbal & SWC: Throttle & Gear Selection
- **Left Stick Vertical (CH3) — Motor Throttle**:
  - Bottom idle ($\le 1050\,\mu\text{s}$): $0\%$ demand ($0\,\text{mm/s}$).
  - Pushing Up ($1050 \dots 1950\,\mu\text{s}$): Smoothly ramps motor setpoint from $0\%$ to $100\%$.
- **SWC — Transmission Gear**:
  - **UP**: Reverse (`[R]`), max speed $500\,\text{mm/s}$ ($1.8\,\text{km/h}$).
  - **MID**: Neutral (`[N]`), motor torque locked to $0\,\text{mm/s}$.
  - **DOWN**: Drive (`[D]`), max speed $3000\,\text{mm/s}$ ($10.8\,\text{km/h}$).
- **Brake-Over-Throttle Interlock**: Whenever total brake stroke is **$> 5.0\,\text{mm}$**, motor throttle is instantly clamped to $0\,\text{mm/s}$.

---

### E. SWD: 2-Position Drive Enable Request (Arming)
- **UP (`ARM:OFF`, $< 1500\,\mu\text{s}$)**:
  - Drive request disabled (`drive_enable_req = false`).
  - Motor target velocity clamped to $0\text{ mm/s}$ and powertrain command broadcasts disabled.
- **DOWN (`ARM:ON`, $\ge 1500\,\mu\text{s}$)**:
  - Driver requests drive enable (`drive_enable_req = true`).
  - Drive is active whenever `signal_valid && drive_enable_req && !park_hold_req`.

---

### F. VRA, VRB, VRC, VRD: Auxiliary Proportional Inputs
- **VRA (CH9)** & **VRB (CH10)**: Top center rotary potentiometers ($0.0 \dots 1.0$).
- **VRC (CH11)** & **VRD (CH12)**: Shoulder rotary knobs ($0.0 \dots 1.0$).
- Rotary controls remain decoupled from primary driving and braking paths, ensuring deterministic vehicle behavior.

---

## 3. Serial Monitor Log Reference

```text
I (1336831) tx: STR: +0.0 BRK: 0.0  THR:  0% MTR:   +0[N]  ARM:OFF PRK:HOLD  MOD:BARE RF:OK
```

Organized into 4 distinct spatial clusters with fixed widths:
- **Steering & Braking**: `STR:+0.0` ($-45.0^\circ \dots +45.0^\circ$), `BRK: 0.0` ($0.0 \dots 27.0\,\text{mm}$).
- **Propulsion**: `THR:  0%` ($0 \dots 100\%$), `MTR:   +0` commanded velocity in mm/s with current gear (`[D]`, `[N]`, or `[R]`).
- **Safety Interlocks**: `ARM:OFF` (`ON` = drive armed / Auto mode, `OFF` = disarmed), `PRK:HOLD` (`HOLD` = park brake held, `OFF` = released).
- **Control Context**: `MOD:BARE` (`BARE`, `SYS`, or `RT`), `RF:OK` (`OK` = SBUS valid, `LOST` = failsafe active).

