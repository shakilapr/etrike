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

| Control ID | Hardware Type | Location | Pin / GPIO | Function | Action | Pulse ($\mu\text{s}$) | Vehicle Output | CAN ID |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Right Gimbal** | 2-Axis Gimbal (Spring) | Lower Right | CH1 / GPIO 18 | **Steering** | Horizontal (X) | $1050 \dots 1950$ | $-45.0^\circ \dots +45.0^\circ$ | `0x169` (raw: $29550 \dots 30450$) |
| **Left Gimbal** | 2-Axis Gimbal (Spring) | Lower Left | CH2 / GPIO 19 | **Brake** | Vertical (Y) | $1520 \dots 1970$ | $0.0 \dots 27.0\text{ mm}$ stroke | `0x7B9` (raw: $600 \dots 1140$) |
| **VRA Dial** | Rotary Potentiometer | Top Center-Left | CH3 / GPIO 14 | **Throttle** | Dial CW/CCW | $1050 \dots 1950$ | $0 \dots 100\%$ speed | `0x204` ($0 \dots 3000\text{ mm/s}$)<br>`0x0AA` ($0.81 \dots 2.40\text{ V}$) |
| **SWB Switch** | 2-Position Toggle | Top Inner-Left | CH5 / GPIO 13 | **Ignition** | UP / DOWN | $1034$ / $2035$ | OFF / ON | `0x112` (`req_start` 0 / 1)<br>`0x0BB` (`0x00` / active) |
| **SWC Switch** | 3-Position Toggle | Top Inner-Right | CH6 / GPIO 4 | **Gear** | UP / MID / DOWN | $1035$ / $1535$ / $2035$| Reverse / Neutral / Drive | `0x204` (`gear` 3 / 0 / 1)<br>`0x0BB` (`0x09` / `0x03` / `0x05`) |
| **VRB Dial** | Rotary Potentiometer | Top Center-Right | CH4 / GPIO 32 | **Auxiliary** | Dial CW/CCW | $1000 \dots 2000$ | $0.0 \dots 1.0$ normalized | Telemetry pass-through |
| **SWA / SWD** | 2-Position Toggles | Top Outer L/R | — | *Spare* | UP / DOWN | — | Unassigned | — |

---

## 2. Operating Principles & Detailed Explanations

### A. Brake: When & How It Operates

#### 1. When Does It Engage?
- **Released (Idle)**: When the stick is centered at spring rest ($\le 1520\,\mu\text{s}$), the brake is **completely disengaged ($0.0\,\text{mm}$)**.
- **Normal Engagement**: Begins engaging immediately as the stick is pushed past the **$1520\,\mu\text{s}$ threshold**.
- **Throttle Cutoff Interlock**: When brake stroke reaches **$> 5.0\,\text{mm}$** ($> 1603\,\mu\text{s}$), motor throttle is **instantly forced to $0\,\text{mm/s}$ ($0.0\,\text{V}$ DAC)** and the CAN brake flag bit (`0x10`) on `0x0BB` is asserted. The motor will not fight the calipers.
- **Fail-Safe Override**: If the RC signal drops or the transmitter is turned off for **$> 100\,\text{ms}$**, the emergency brake automatically clamps to **$27.0\,\text{mm}$ (maximum emergency stroke)**.

#### 2. How Do Values Change?
- **Linear Stroke Transfer**:
  $$\text{Stroke (mm)} = \text{clamp}\left(\frac{\text{Pulse} - 1520\,\mu\text{s}}{450\,\mu\text{s}}, 0.0, 1.0\right) \times 27.0\,\text{mm}$$
- **Raw CAN Encoding (`0x7B9`)**:
  $$\text{stroke\_raw} = (\text{Stroke} + 30.0) \times 20$$
  - $\le 1520\,\mu\text{s} \longrightarrow 0.0\,\text{mm}$ ($600$ raw)
  - $\approx 1745\,\mu\text{s} \longrightarrow 13.5\,\text{mm}$ ($870$ raw)
  - $\ge 1970\,\mu\text{s} \longrightarrow 27.0\,\text{mm}$ ($1140$ raw)

*(Tip: To brake by pulling backward instead of pushing forward, invert Channel 2 on your FlySky transmitter under `Functions setup > Reverse > Ch2: Rev`).*

---

### B. Steering: When & How It Operates

#### 1. When Does It Engage?
- Steering control is active only when **Ignition is ON** and gear is in **Drive (D)** or **Reverse (R)**.
- When in Neutral (N) or Ignition OFF, steering automatically centers to $0.0^\circ$.

#### 2. How Do Values Change?
- **Deadband**: $\pm 30\,\mu\text{s}$ ($1470 \dots 1530\,\mu\text{s}$) holds the rack at exactly $0.0^\circ$ ($30000$ raw) to prevent hand tremor jitter.
- **Rack Limit**: $\pm 45.0^\circ$ mechanical rack limit.
- **Moving Left ($1500 \rightarrow 1050\,\mu\text{s}$)**: Turns rack left from $0.0^\circ \rightarrow -45.0^\circ$ (CAN `0x169`: $30000 \rightarrow 29550$ raw).
- **Moving Right ($1500 \rightarrow 1950\,\mu\text{s}$)**: Turns rack right from $0.0^\circ \rightarrow +45.0^\circ$ (CAN `0x169`: $30000 \rightarrow 30450$ raw).

---

### C. Throttle / Speed: When & How It Operates

#### 1. When Does It Engage?
- Throttle commands are active only when **Ignition is ON**, gear is **Drive (D)** or **Reverse (R)**, and **Brake is released** ($\le 5.0\,\text{mm}$).

#### 2. How Do Values Change?
- **Idle Cutoff**: $\le 1050\,\mu\text{s}$ is forced strictly to $0\%$ throttle ($0\,\text{mm/s}$, $0.0\,\text{V}$ DAC). This prevents vehicle creep and motor controller High-Pedal-Disable lockout when shifting gears.
- **Linear Acceleration ($1050 \dots 1950\,\mu\text{s}$)**:
  - **Drive (D)**: Speed scales linearly from $0 \dots 3000\,\text{mm/s}$ ($10.8\,\text{km/h}$).
  - **Reverse (R)**: Speed scales linearly from $0 \dots 500\,\text{mm/s}$ ($1.8\,\text{km/h}$).
  - **Legacy DAC (`0x0AA`)**: Linearly maps across the motor controller's safe analog window ($10480 \dots 31456$ raw $\longrightarrow 0.81\,\text{V} \dots 2.40\,\text{V}$).

---

### D. Ignition & Gear Switching: When & How It Operates

- **Ignition (SWB)**:
  - **Switch UP ($\le 1500\,\mu\text{s}$, $\approx 1034\,\mu\text{s}$)**: Ignition OFF. All relays open, motor power cut, DAC zeroed.
  - **Switch DOWN ($> 1500\,\mu\text{s}$, $\approx 2035\,\mu\text{s}$)**: Ignition ON. Main contactor closes, system armed.
- **Gear Selector (SWC)**:
  - **Switch UP ($\le 1300\,\mu\text{s}$, $\approx 1035\,\mu\text{s}$)**: Reverse (R). Reverse relay closes, speed capped at $500\,\text{mm/s}$.
  - **Switch MID ($1350 \dots 1650\,\mu\text{s}$, $\approx 1535\,\mu\text{s}$)**: Neutral / Park (N). Relays open, motor unpowered.
  - **Switch DOWN ($\ge 1700\,\mu\text{s}$, $\approx 2035\,\mu\text{s}$)**: Drive (D). Drive relay closes, speed up to $3000\,\text{mm/s}$.

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
