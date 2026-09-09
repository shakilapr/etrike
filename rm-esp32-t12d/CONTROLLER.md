# RadioLink T12D RC Controller Mapping (RM-ESP32-T12D)

Vehicle driving controls, switch mapping, safety interlocks, and CAN signals for the electric three-wheeler (`etrike`).

---

## 1. Controller Layout & Physical Mapping

### Visual Layout Map

```
              [SWA]   [SWB]             [SWC]   [SWD]
             (2-pos) (2-pos)           (3-pos) (2-pos)
                │       │                 │       │
          DRIVE ENABLE PARK / HOLD   GEAR SELECT  AUTO MODE
          UP:   OFF    UP:   RELEASE   UP:   REV  UP:   MANUAL (MOD:M)
          DOWN: ON     DOWN: PARK      MID:  NEU  DOWN: AUTO   (MOD:A)
                                       DOWN: DRV

                      [VRA]             [VRB]
                     (dial)            (dial)
                        │                 │
                   AUX ANALOG 1      AUX ANALOG 2
                    (0.0..1.0)        (0.0..1.0)

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
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Right Stick (X)** | 2-Axis Gimbal (Spring) | Lower Right | **CH1** | **Steering Rack** | Horizontal (Left / Right) | $1050 \dots 1950\,\mu\text{s}$ | $-45.0^\circ \dots +45.0^\circ$ | `0x169` `VCU_SES_REQ` (`raw: 29550..30450`) |
| **Right Stick (Y)** | 2-Axis Gimbal (Spring) | Lower Right | **CH2** | **Service Brake Stroke** | Vertical (Push Forward) | $1520 \dots 1950\,\mu\text{s}$ | $0.0 \dots 27.0\,\text{mm}$ stroke | `0x7B9` `VCU_SEB_REQ` (`raw: 600..1140`) |
| **Left Stick (Y)** | 2-Axis Gimbal (Ratcheted) | Lower Left | **CH3** | **Motor Throttle** | Vertical (Up / Down) | $1050 \dots 1950\,\mu\text{s}$ | $0\% \dots 100\%$ ($0 \dots 3000\,\text{mm/s}$) | `0x204` `RT_DRIVE_CMD` (`RT_MotorSpeed`) |
| **Left Stick (X)** | 2-Axis Gimbal | Lower Left | **CH4** | **Reserved / Spare** | Horizontal (Left / Right) | $1000 \dots 2000\,\mu\text{s}$ | Unused | — |
| **SWA Switch** | 2-Position Toggle | Top Far-Left | **CH5** | **Drive Enable Request** | UP / DOWN | $\approx 1000$ / $\approx 2000\,\mu\text{s}$ | `EN:OFF` (Disabled) / `EN:ON` (Enable Request) | `0x113` `SYS_PWR_CMD` (`power_state: 0/1`) |
| **SWB Switch** | 2-Position Toggle | Top Inner-Left | **CH6** | **Electric Park Brake (Hold)** | UP / DOWN | $\approx 1000$ / $\approx 2000\,\mu\text{s}$ | `PRK:OFF` ($0\,\text{mm}$) / `PRK:HOLD` ($15\,\text{mm}$) | `0x7B9` `VCU_SEB_REQ` (`VCU_SEB_Stroke_Req`) |
| **SWC Switch** | 3-Position Toggle | Top Inner-Right | **CH7** | **Transmission Gear Selector** | UP / MID / DOWN | $1000$ / $1500$ / $2000\,\mu\text{s}$ | `[R]` Reverse / `[N]` Neutral / `[D]` Drive | `0x204` `RT_DRIVE_CMD` (`gear: 3/0/1`) |
| **SWD Switch** | 2-Position Toggle | Top Far-Right | **CH8** | **Manual / Autonomous Mode** | UP / DOWN | $\approx 1000$ / $\approx 2000\,\mu\text{s}$ | `MOD:M` (Manual) / `MOD:A` (Auto Request) | `0x110` `SYS_MODE_CMD` (`mode: 0/1`) |
| **VRA Knob** | Rotary Potentiometer | Top Center-Left | **CH9** | **Aux Analog Knob 1** | Dial Rotation | $1000 \dots 2000\,\mu\text{s}$ | $0.0 \dots 1.0$ | Telemetry / Aux |
| **VRB Knob** | Rotary Potentiometer | Top Center-Right | **CH10** | **Aux Analog Knob 2** | Dial Rotation | $1000 \dots 2000\,\mu\text{s}$ | $0.0 \dots 1.0$ | Telemetry / Aux |

---

## 2. Operating Principles & Safety Interlocks

### A. SWA: Direct Drive Enable Request
- **UP (`EN:OFF`)**:
  - Drive request disabled (`drive_enable_req = false`).
  - Motor target velocity clamped to $0\text{ mm/s}$ and powertrain command broadcasts disabled (`0x113 SYS_PWR_CMD`).
- **DOWN (`EN:ON`)**:
  - Driver requests drive enable (`drive_enable_req = true`).
  - Drive is active whenever `signal_valid && drive_enable_req && !park_hold_req`.
  - Vehicle safety, contactors, and high-voltage power-up sequences are managed downstream by `sys-esp32`.

---

### B. SWB: Electric Park Brake / Brake Hold (Inverted per Request)
- **UP (`PRK:OFF`)**:
  - Brake disengages to **$0.0\,\text{mm}$ resting stroke** (`0x7B9 raw: 600`), allowing the wheels to roll freely.
- **DOWN (`PRK:HOLD`)**:
  - Electronic brake actuator clamps to **$15.0\,\text{mm}$ holding stroke** (`0x7B9 raw: 900`).
  - Motor transmission state is clamped strictly to **Neutral (`[N]`)** with $0\,\text{mm/s}$ target velocity.

---

### C. Right Gimbal: Steering & Service Brake
- **Horizontal Axis (CH1) — Steering Rack**:
  - Center ($1500\,\mu\text{s} \pm 30\,\mu\text{s}$ deadband): $0.0^\circ$ (Straight ahead).
  - Full Left ($1050\,\mu\text{s}$): $-45.0^\circ$ (Hard Left, CAN `0x169` raw `29550`).
  - Full Right ($1950\,\mu\text{s}$): $+45.0^\circ$ (Hard Right, CAN `0x169` raw `30450`).
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

### E. VRA & VRB: Auxiliary Analog Inputs
- Knobs **VRA (CH9)** and **VRB (CH10)** provide proportional auxiliary inputs ($0.0 \dots 1.0$) for implement control or auxiliary functions.
- Rotary dials are isolated from the primary driving and braking paths, ensuring predictable steering and braking behavior.

---

### F. SWD: Manual vs Autonomous Mode Request
- **UP**: **Manual Mode** (`MOD:M`, CAN `0x110` mode = `0`).
- **DOWN**: **Autonomous Mode Request** (`MOD:A`, CAN `0x110` mode = `1`).

---

## 3. Serial Monitor Log Reference

```text
I (14502) tx: STR:+0.0 BRK:0.0 MTR:+1800[D] EN:ON PRK:OFF MOD:M
```

- **`STR:+0.0`**: Steering angle in degrees ($-45.0^\circ \dots +45.0^\circ$).
- **`BRK:0.0`**: Service brake stroke in mm ($0.0 \dots 27.0\,\text{mm}$).
- **`MTR:+1800[D]`**: Motor speed setpoint in mm/s and gear (`[D]`, `[N]`, or `[R]`).
- **`EN:ON`**: Powertrain enable request (`ON` = live, `OFF` = safe/disabled).
- **`PRK:OFF`**: Park brake status (`OFF` = released, `HOLD` = 15mm hold engaged).
- **`MOD:M`**: Vehicle mode (`M` = Manual, `A` = Autonomous).
