# FlySky FS-i6 RC Controller Mapping (RM-ESP32)

Vehicle driving controls and switch mapping for the electric three-wheeler (`etrike`).

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
                   DISCONNECTED      DISCONNECTED
                   (RF Excluded)     (RF Excluded)

          ┌───────────────┐           ┌───────────────┐
          │       ▲       │           │       ▲       │
          │       │       │           │   ◄───┼───►   │ STEERING
          │       ▼       │           │       ▼       │ (±45.0° Rack)
          │    THROTTLE   │           │     BRAKE     │
          │  (0% - 100%)  │           │   (0 - 27mm)  │
          └───────────────┘           └───────────────┘
             LEFT GIMBAL                 RIGHT GIMBAL
```

### Controller Layout Table

| Control ID | Hardware Type | Location | Pin / GPIO | Function | Action | Pulse ($\mu\text{s}$) | Vehicle Output | CAN ID |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Right Gimbal** | 2-Axis Gimbal (Spring) | Lower Right | CH1 / GPIO 18 | **Steering** | Horizontal (X) | $1050 \dots 1950$ | $-45.0^\circ \dots +45.0^\circ$ | `0x169` (raw: $29550 \dots 30450$) |
| **Right Gimbal** | 2-Axis Gimbal (Spring) | Lower Right | CH2 / GPIO 19 | **Brake** | Vertical (Y) | $1520 \dots 1970$ | $0.0 \dots 27.0\text{ mm}$ stroke | `0x7B9` (raw: $600 \dots 1140$) |
| **Left Gimbal** | 2-Axis Gimbal (Ratcheted/Spring) | Lower Left | CH3 / GPIO 14 | **Throttle** | Vertical (Y) | $1050 \dots 1950$ | $0 \dots 100\%$ speed | `0x204` ($0 \dots 3000\text{ mm/s}$)<br>`0x0AA` ($0.81 \dots 2.40\text{ V}$) |
| **Left Gimbal** | 2-Axis Gimbal | Lower Left | CH4 / GPIO 32 | *Spare* | Horizontal (X) | $1000 \dots 2000$ | $0.0 \dots 1.0$ pass-through | Telemetry logging |
| **SWB Switch** | 2-Position Toggle | Top Inner-Left | CH5 / GPIO 13 | **Ignition** | UP / DOWN | $1034$ / $2035$ | OFF / ON | `0x112` (`req_start` 0 / 1)<br>`0x0BB` (`0x00` / active) |
| **SWC Switch** | 3-Position Toggle | Top Inner-Right | CH6 / GPIO 4 | **Gear** | UP / MID / DOWN | $1035$ / $1535$ / $2035$| Reverse / Neutral / Drive | `0x204` (`gear` 3 / 0 / 1)<br>`0x0BB` (`0x09` / `0x03` / `0x05`) |
| **VRA / VRB** | Rotary Potentiometers | Top Center | — | *Unavailable* | Dial Rotation | — | Not transmitted (6-ch limit) | — |
| **SWA / SWD** | 2-Position Toggles | Top Outer L/R | — | *Unavailable* | Toggle | — | Not transmitted (6-ch limit) | — |

---

## 2. Operating Principles & Detailed Explanations

### A. Radio Channel Architecture & Why VRA/VRB Are Inactive
- The FlySky FS-i6 radio is strictly limited to **6 RF transmission channels**.
- **Channels 1 to 4 are hardwired to the gimbals (sticks)** in transmitter firmware:
  - CH1: Right Stick Horizontal (Steering)
  - CH2: Right Stick Vertical (Brake)
  - CH3: Left Stick Vertical (Throttle)
  - CH4: Left Stick Horizontal (Spare)
- **Channels 5 and 6 are the ONLY assignable auxiliary channels** in the transmitter setup menu (`Functions setup > Aux. channels`):
  - In our setup, **Channel 5 is assigned to SWB** (Ignition) and **Channel 6 is assigned to SWC** (Gear Selector).
  - Because all 6 channels are occupied, **VRA, VRB, SWA, and SWD transmit no RF signal** and are physically inactive.

---

### B. Throttle: When & How It Operates

#### 1. When Does It Engage?
- Throttle commands are active only when **Ignition is ON**, gear is **Drive (D)** or **Reverse (R)**, and **Brake is released** ($\le 5.0\,\text{mm}$).
- When in Neutral (N) or Ignition OFF, throttle output is locked to $0\,\text{mm/s}$ ($0.0\,\text{V}$ DAC).

#### 2. How Do Values Change?
- **Idle Cutoff ($\le 1050\,\mu\text{s}$)**: At stick bottom ($\approx 1000 \dots 1050\,\mu\text{s}$), throttle output is forced strictly to $0\%$ ($0\,\text{mm/s}$, $0.0\,\text{V}$ DAC). This prevents vehicle creeping and prevents motor controller High-Pedal-Disable lockout when shifting gears.
- **Linear Acceleration ($1050 \dots 1950\,\mu\text{s}$)**:
  - **Drive (D)**: Linearly ramps speed setpoint from $0 \dots 3000\,\text{mm/s}$ ($10.8\,\text{km/h}$) on canonical CAN `0x204`.
  - **Reverse (R)**: Linearly ramps speed setpoint from $0 \dots 500\,\text{mm/s}$ ($1.8\,\text{km/h}$) on canonical CAN `0x204`.
  - **Legacy Throttle DAC (`0x0AA`)**: Linearly maps across the motor controller's safe analog window ($10480 \dots 31456$ raw $\longrightarrow 0.81\,\text{V} \dots 2.40\,\text{V}$).

---

### C. Brake: When & How It Operates

#### 1. When Does It Engage?
- **Released (Idle)**: When the stick is centered at spring rest ($\le 1520\,\mu\text{s}$), the brake is **completely disengaged ($0.0\,\text{mm}$)**.
- **Engagement**: Pushing the Right Stick upward past the **$1520\,\mu\text{s}$ threshold** engages the electro-mechanical brake caliper.
- **Throttle Cutoff Interlock**: When brake stroke reaches **$> 5.0\,\text{mm}$** ($> 1603\,\mu\text{s}$), motor throttle is **instantly forced to $0\,\text{mm/s}$ ($0.0\,\text{V}$ DAC)** and the CAN brake flag bit (`0x10`) on `0x0BB` is asserted. The motor will never fight the brakes.
- **Fail-Safe Override**: If the RC signal drops or the transmitter is turned off for **$> 100\,\text{ms}$**, the emergency brake automatically clamps to **$27.0\,\text{mm}$ (maximum emergency stroke)**.

#### 2. How Do Values Change?
- **Linear Stroke Transfer**:
  $$\text{Stroke (mm)} = \text{clamp}\left(\frac{\text{Pulse} - 1520\,\mu\text{s}}{450\,\mu\text{s}}, 0.0, 1.0\right) \times 27.0\,\text{mm}$$
- **Raw CAN Encoding (`0x7B9`)**:
  $$\text{stroke\_raw} = (\text{Stroke} + 30.0) \times 20$$
  - $\le 1520\,\mu\text{s} \longrightarrow 0.0\,\text{mm}$ ($600$ raw)
  - $\approx 1745\,\mu\text{s} \longrightarrow 13.5\,\text{mm}$ ($870$ raw)
  - $\ge 1970\,\mu\text{s} \longrightarrow 27.0\,\text{mm}$ ($1140$ raw)

*(Tip: If you prefer pulling backward to brake instead of pushing forward, simply invert Channel 2 on your FlySky transmitter under `Functions setup > Reverse > Ch2: Rev`).*

---

### D. Steering: When & How It Operates

#### 1. When Does It Engage?
- Active only when **Ignition is ON** and gear is in **Drive (D)** or **Reverse (R)**.
- When in Neutral (N) or Ignition OFF, steering automatically centers to $0.0^\circ$.

#### 2. How Do Values Change?
- **Deadband**: $\pm 30\,\mu\text{s}$ ($1470 \dots 1530\,\mu\text{s}$) holds the rack at exactly $0.0^\circ$ ($30000$ raw) to eliminate hand tremor jitter.
- **Rack Limit**: Calibrated to the vehicle's $\pm 45.0^\circ$ mechanical rack limit.
- **Left ($1500 \rightarrow 1050\,\mu\text{s}$)**: Turns rack left from $0.0^\circ \rightarrow -45.0^\circ$ (CAN `0x169`: $30000 \rightarrow 29550$ raw).
- **Right ($1500 \rightarrow 1950\,\mu\text{s}$)**: Turns rack right from $0.0^\circ \rightarrow +45.0^\circ$ (CAN `0x169`: $30000 \rightarrow 30450$ raw).

---

### E. Ignition & Gear Switching: When & How It Operates

#### 1. Ignition (SWB 2-Position Toggle)
- **UP ($1034\,\mu\text{s}$)**: Vehicle is **OFF / Park**. CAN `0x112` (`req_start=0`), CAN `0x0BB` (`0x00`). Relays open, motor drive disabled.
- **DOWN ($2035\,\mu\text{s}$)**: Vehicle is **ON**. CAN `0x112` (`req_start=1`), main contactor closed.

#### 2. Gear Selector (SWC 3-Position Toggle)
- **UP ($1035\,\mu\text{s}$)**: **Reverse (R)** — CAN `0x204` (`gear=3`), CAN `0x0BB` (`0x09`). Speed capped at $500\,\text{mm/s}$.
- **MID ($1535\,\mu\text{s}$)**: **Neutral / Park (N)** — CAN `0x204` (`gear=0`), CAN `0x0BB` (`0x03`). Motor disabled.
- **DOWN ($2035\,\mu\text{s}$)**: **Drive (D)** — CAN `0x204` (`gear=1`), CAN `0x0BB` (`0x05`). Speed allowed up to $3000\,\text{mm/s}$.

---

## 3. Step-by-Step Driving Sequence

1. **Power On**: Turn on FlySky FS-i6 transmitter. Center Right Stick, pull Left Stick fully down ($\le 1050\,\mu\text{s}$), set `SWB` UP (OFF), and `SWC` MID (Neutral).
2. **Arm Ignition**: Flip `SWB` **DOWN** (Ignition ON).
3. **Select Direction**: Flip `SWC` **DOWN** for Drive (`D`) or **UP** for Reverse (`R`).
4. **Steer & Drive**:
   - Push **Left Stick (Y)** upward to accelerate ($0 \dots 100\%$).
   - Move **Right Stick (X)** left/right to steer front wheel ($\pm 45.0^\circ$).
   - Push **Right Stick (Y)** upward to apply mechanical brakes ($0 \dots 27\,\text{mm}$).
5. **Stop / Park**: Pull **Left Stick** fully down (idle throttle), push **Right Stick** to brake to a complete stop, then set `SWC` to **MID** (Neutral) and `SWB` to **UP** (Ignition OFF).

---

## 4. Safety Fail-Safe & Deadman Guard

- **RC Signal Drop / Transmitter Power Off**:
  - If pulse signal is lost for $>100\text{ ms}$ on any safety-critical channel (CH1, CH2, CH3, CH5, CH6):
    - Motor speed immediately forced to **0 mm/s** (relays cut, DAC zeroed).
    - Emergency brake instantly applied to **maximum stroke ($27.0\text{ mm}$)**.
    - Steering snapped to **$0.0^\circ$ (center)**.
    - **`0x001 SAFETY_ESTOP`** broadcast onto CAN bus.

---

## 5. Transmitter Menu Setup (FS-i6)

Ensure auxiliary channels match receiver wiring:
1. Power on transmitter, hold **`OK`** $\rightarrow$ `Functions setup` $\rightarrow$ `Aux. channels`.
2. Set **`Channel 5`** $\longrightarrow$ **`SWB`** (Ignition).
3. Set **`Channel 6`** $\longrightarrow$ **`SWC`** (Gear Selector: R / N / D).
4. Long press **`CANCEL`** to save settings.
5. (Optional) Under `Functions setup > Reverse`, if you prefer pulling down on the Right Stick to brake, set **`Ch2: Rev`**.
