# FlySky FS-i6 RC Controller Mapping (RM-ESP32)

Quick reference for vehicle driving controls and switch mapping.

---

## 1. Controller Layout & Physical Mapping

### Visual Layout Map

```
              [SWA]   [SWB]             [SWC]   [SWD]
             (2-pos) (2-pos)           (3-pos) (2-pos)
                       │                 │
                  IGNITION            GEAR SELECT
                  UP:   OFF           UP:   REVERSE (R)
                  DOWN: ON            MID:  NEUTRAL / PARK (N)
                                      DOWN: DRIVE (D)
                      [VRA]             [VRB]
                     (dial)            (dial)
                        │                 │
                    THROTTLE /         AUXILIARY /
                    SPEED TRIM         EXPANSION
                    (0% - 100%)       (Pass-through)

          ┌───────────────┐           ┌───────────────┐
          │   ▲           │           │       ▲       │
          │ ◄─┼─► BRAKE   │           │   ◄───┼───►   │ STEERING
          │   ▼  (0-27mm) │           │       ▼       │ (±45.0° Rack)
          └───────────────┘           └───────────────┘
             LEFT GIMBAL                 RIGHT GIMBAL
```

### Controller Layout Table

| Control ID | Hardware Type | Physical Location | Receiver Pin | ESP32 GPIO | Vehicle Function | Axis / Action |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Right Gimbal** | 2-Axis Spring-Centered Gimbal | Lower Right Front | **CH1** | **GPIO 18** | **Steering** | Horizontal (X-Axis): Left $\longleftrightarrow$ Right |
| **Left Gimbal** | 2-Axis Spring-Centered Gimbal | Lower Left Front | **CH2** | **GPIO 19** | **Brake** | Vertical (Y-Axis): Pull Down $\longleftrightarrow$ Center |
| **VRA** | Rotary Potentiometer Dial | Top Center-Left | **CH3** | **GPIO 14** | **Throttle / Speed** | Continuous Dial: Counter-Clockwise (0%) $\longleftrightarrow$ Clockwise (100%) |
| **VRB** | Rotary Potentiometer Dial | Top Center-Right | **CH4** | **GPIO 32** | **Auxiliary Expansion** | Continuous Dial: Min $\longleftrightarrow$ Max (Pass-through) |
| **SWB** | 2-Position Toggle Switch | Top Inner-Left | **CH5** | **GPIO 13** | **Ignition Enable** | 2-Pos: **UP** (OFF) $\longleftrightarrow$ **DOWN** (ON) |
| **SWC** | 3-Position Toggle Switch | Top Inner-Right | **CH6** | **GPIO 4** | **Gear Selector** | 3-Pos: **UP** (Reverse) $\longleftrightarrow$ **MID** (Neutral) $\longleftrightarrow$ **DOWN** (Drive) |
| **SWA** | 2-Position Toggle Switch | Top Outer-Left | — | — | *Unassigned / Spare* | Aux Toggle |
| **SWD** | 2-Position Toggle Switch | Top Outer-Right | — | — | *Unassigned / Spare* | Aux Toggle |

---

## 2. Controls & Actions Table with Exact Operating Ranges

| Control Input | Channel / GPIO | Physical Action | RC Pulse ($\mu\text{s}$) | Vehicle Output & Physical Range | CAN Frame & Payload |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Steering** | **CH1**<br>(GPIO 18) | **Center (Neutral)** | $1470 \dots 1530\,\mu\text{s}$ | **$0.0^\circ$** (Deadband $\pm 30\,\mu\text{s}$) | `0x169 VCU_SES_REQ`<br>`target_angle_raw = 30000` ($0.0^\circ$) |
| | | **Full Left** | $1050\,\mu\text{s}$ | **$-45.0^\circ$** (Full Left mechanical rack limit) | `0x169`<br>`target_angle_raw = 29550` ($-45.0^\circ$) |
| | | **Full Right** | $1950\,\mu\text{s}$ | **$+45.0^\circ$** (Full Right mechanical rack limit) | `0x169`<br>`target_angle_raw = 30450` ($+45.0^\circ$) |
| **Brake** | **CH2**<br>(GPIO 19) | **Rest (Released)** | $\le 1520\,\mu\text{s}$ | **$0.0\text{ mm}$** stroke (Brake off) | `0x7B9 VCU_SEB_REQ`<br>`stroke_raw = 600` ($0.0\text{ mm}$) |
| | | **Half Pull** | $\approx 1745\,\mu\text{s}$ | **$13.5\text{ mm}$** stroke | `0x7B9`<br>`stroke_raw = 870` ($13.5\text{ mm}$) |
| | | **Full Pull** | $\ge 1970\,\mu\text{s}$ | **$27.0\text{ mm}$** (Emergency Max Lockup) | `0x7B9`<br>`stroke_raw = 1140` ($27.0\text{ mm}$) |
| **Throttle / Speed** | **CH3 / VRA**<br>(GPIO 14) | **Idle (Cutoff)** | $\le 1050\,\mu\text{s}$ | **$0\%$** speed ($0\text{ mm/s}$, $0.0\,\text{V}$ DAC output) | `0x204` `speed = 0`<br>`0x0AA` `raw = 0` ($0.0\,\text{V}$) |
| | | **Drive Active** | $1050 \dots 1950\,\mu\text{s}$ | Linear **$0 \dots 3000\text{ mm/s}$** (D)<br>Linear **$0 \dots 500\text{ mm/s}$** (R) | `0x204` `motor_speed_mmps = 0..3000`<br>`0x0AA` `10480..31456` ($0.8\dots 2.4\,\text{V}$) |
| | | **Full Throttle** | $\ge 1950\,\mu\text{s}$ | **$100\%$** ($3000\text{ mm/s}$ D / $500\text{ mm/s}$ R) | `0x204` `speed = 3000`<br>`0x0AA` `raw = 31456` ($2.4\,\text{V}$) |
| **Ignition (SWB)** | **CH5**<br>(GPIO 13) | **Switch UP (Pos 1)** | $\le 1500\,\mu\text{s}$ ($\approx 1034\,\mu\text{s}$) | **Ignition OFF** (All relays de-energized) | `0x112` `req_start = 0`<br>`0x0BB` `data[0] = 0x00` |
| *(2-position switch)*| | **Switch DOWN (Pos 2)**| $> 1500\,\mu\text{s}$ ($\approx 2035\,\mu\text{s}$) | **Ignition ON** (Vehicle armed & ready) | `0x112` `req_start = 1` |
| **Gear Select (SWC)**| **CH6**<br>(GPIO 4) | **Switch UP (Pos 1)** | $\le 1300\,\mu\text{s}$ ($\approx 1035\,\mu\text{s}$) | **Reverse (R)** (Reverse relay ON, max $500\text{ mm/s}$) | `0x204` `gear = 3 (R)`<br>`0x0BB` `data[0] = 0x09` |
| *(3-position switch)*| | **Switch MID (Pos 2)** | $1350 \dots 1650\,\mu\text{s}$ ($\approx 1535\,\mu\text{s}$) | **Neutral / Park (N)** (Relays open, motor unpowered) | `0x204` `gear = 0 (N)`<br>`0x0BB` `data[0] = 0x03` |
| | | **Switch DOWN (Pos 3)**| $\ge 1700\,\mu\text{s}$ ($\approx 2035\,\mu\text{s}$) | **Drive (D)** (Drive relay ON, max $3000\text{ mm/s}$) | `0x204` `gear = 1 (D)`<br>`0x0BB` `data[0] = 0x05` |
| **Auxiliary (VRB)** | **CH4**<br>(GPIO 32) | **Dial Min $\rightarrow$ Max**| $1000 \dots 2000\,\mu\text{s}$ | Normalized $0.0 \dots 1.0$ | Pass-through telemetry |

---

## 3. Step-by-Step Driving Sequence

1. **Power On**: Turn on FlySky FS-i6 transmitter. Center sticks, set `SWB` UP (OFF), `SWC` MID (Neutral).
2. **Arm Ignition**: Flip `SWB` **DOWN** (Ignition ON).
3. **Select Direction**: Flip `SWC` **DOWN** for Drive (`D`) or **UP** for Reverse (`R`).
4. **Steer & Drive**:
   - Turn **Right Stick (X)** left/right to steer front wheel.
   - Rotate **VRA dial** clockwise to accelerate.
   - Pull **Left Stick (Y)** down to apply mechanical brakes.
5. **Stop / Park**: Rotate **VRA dial** fully counter-clockwise (0 throttle), pull brake, and set `SWC` to **MID** (Neutral/Park).

---

## 4. Safety Fail-Safe & Deadman Guard

- **RC Signal Drop / Transmitter Power Off**:
  - If pulse signal is lost for $>100\text{ ms}$:
    - Motor speed immediately forced to **0 mm/s** (relays cut).
    - Emergency brake instantly applied to **maximum stroke ($27.0\text{ mm}$)**.
    - Steering snapped to **$0.0^\circ$ (center)**.
    - **`0x001 SAFETY_ESTOP`** broadcast onto CAN bus.

---

## 5. Transmitter Menu Setup (FS-i6)

Ensure auxiliary channels match receiver wiring:
1. Power on transmitter, hold **`OK`** $\rightarrow$ `Functions setup` $\rightarrow$ `Aux. channels`.
2. Set **`Channel 5`** $\longrightarrow$ **`SWB`** (Ignition).
3. Set **`Channel 6`** $\longrightarrow$ **`SWC`** (Gear).
4. Hold **`CANCEL`** to save.
