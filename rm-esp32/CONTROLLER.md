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

### Controller Layout Table (How & When Values Change)

| Control ID | Hardware Type & Location | Vehicle Function | When It Activates (Conditions) | How Values Change & Dynamic Response | Output Value Range |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Right Gimbal**<br>(CH1 / GPIO 18) | 2-Axis Spring-Centered<br>*(Lower Right Front)* | **Steering** | Active only when **Ignition is ON** and gear is in **Drive (D)** or **Reverse (R)**.<br>*(Holds center 0° in Neutral or Ign OFF)* | • Center deadband ($\pm 30\,\mu\text{s}$): $1470 \dots 1530\,\mu\text{s} \longrightarrow$ exactly **$0.0^\circ$** ($30000$ raw).<br>• Moving Left ($1500 \rightarrow 1050\,\mu\text{s}$): linearly turns rack to **$-45.0^\circ$** ($30000 \rightarrow 29550$ raw).<br>• Moving Right ($1500 \rightarrow 1950\,\mu\text{s}$): linearly turns rack to **$+45.0^\circ$** ($30000 \rightarrow 30450$ raw). | **$-45.0^\circ \dots +45.0^\circ$**<br>CAN `0x169`: $29550 \dots 30450$ |
| **Left Gimbal**<br>(CH2 / GPIO 19) | 2-Axis Spring-Centered<br>*(Lower Left Front)* | **Brake** | **Always active** (safety override).<br>Triggers whenever stick is moved past deadband threshold ($> 1520\,\mu\text{s}$). | • Rest / Center ($\le 1520\,\mu\text{s}$): **$0.0\,\text{mm}$** (Brake fully released).<br>• Moving forward ($1520 \rightarrow 1970\,\mu\text{s}$): stroke increases linearly from **$0.0 \dots 27.0\,\text{mm}$**.<br>• **Throttle Interlock**: If stroke $> 5.0\,\text{mm}$, motor throttle is instantly killed to $0.0\,\text{V}$ and CAN `0x0BB` sets brake flag bit (`0x10`).<br>• **Signal Loss Fail-Safe**: If RC drops for $>100\,\text{ms}$, instantly snaps to **$27.0\,\text{mm}$** max emergency lockup. | **$0.0\,\text{mm} \dots 27.0\,\text{mm}$**<br>CAN `0x7B9`: $600 \dots 1140$ raw |
| **VRA Dial**<br>(CH3 / GPIO 14) | Rotary Potentiometer<br>*(Top Center-Left)* | **Throttle / Speed** | Active only when **Ignition is ON**, gear is **D or R**, and **Brake is NOT pulled** ($\le 5\,\text{mm}$). | • Idle deadband ($\le 1050\,\mu\text{s}$): strictly **$0\%$ speed** ($0\,\text{mm/s}$, $0.0\,\text{V}$ DAC).<br>• Rotating clockwise ($1050 \rightarrow 1950\,\mu\text{s}$): linearly ramps speed from **$0 \dots 3000\,\text{mm/s}$** in Drive ($0 \dots 500\,\text{mm/s}$ in Reverse).<br>• Legacy DAC voltage linearly ramps across safe window from **$0.81\,\text{V} \dots 2.40\,\text{V}$** ($10480 \dots 31456$ raw). | **$0 \dots 3000\,\text{mm/s}$** (D)<br>**$0 \dots 500\,\text{mm/s}$** (R)<br>DAC: $0.81 \dots 2.40\,\text{V}$ |
| **SWB Switch**<br>(CH5 / GPIO 13) | 2-Position Toggle<br>*(Top Inner-Left)* | **Ignition Enable** | Evaluated continuously at 50 Hz. | • **UP ($\le 1500\,\mu\text{s}$, $\approx 1034\,\mu\text{s}$)**: Ignition **OFF**. All relays de-energized, motor power cut, DAC zeroed.<br>• **DOWN ($> 1500\,\mu\text{s}$, $\approx 2035\,\mu\text{s}$)**: Ignition **ON**. Energizes main contactor relay, enables Drive/Reverse shifting. | Boolean: **OFF** / **ON**<br>CAN `0x112`: `req_start = 0 / 1` |
| **SWC Switch**<br>(CH6 / GPIO 4) | 3-Position Toggle<br>*(Top Inner-Right)* | **Gear Selector** | Active when Ignition is ON. If Ignition is OFF, gear is locked in Neutral/Park. | • **UP ($\le 1300\,\mu\text{s}$, $\approx 1035\,\mu\text{s}$)**: **Reverse (R)**. Reverse relay energized, max speed clamped to $500\,\text{mm/s}$.<br>• **MID ($1350 \dots 1650\,\mu\text{s}$, $\approx 1535\,\mu\text{s}$)**: **Neutral / Park (N)**. Drive & Reverse relays open, motor unpowered.<br>• **DOWN ($\ge 1700\,\mu\text{s}$, $\approx 2035\,\mu\text{s}$)**: **Drive (D)**. Drive relay energized, max speed up to $3000\,\text{mm/s}$. | State: **R / N / D**<br>CAN `0x204`: `gear = 3 / 0 / 1`<br>CAN `0x0BB`: `0x09 / 0x03 / 0x05` |
| **VRB Dial**<br>(CH4 / GPIO 32) | Rotary Potentiometer<br>*(Top Center-Right)* | **Auxiliary Pass** | Continuous pass-through. | • Dial Min $\rightarrow$ Max ($1000 \dots 2000\,\mu\text{s}$): linearly normalized from **$0.0 \dots 1.0$** for custom aux telemetry. | Normalized **$0.0 \dots 1.0$** |

---

### Brake In-Depth: When & How It Operates

#### 1. When Does the Brake Engage?
- **Normal Braking**: Whenever the driver pushes the Left Gimbal forward past the **$1520\,\mu\text{s}$ threshold**. Below this (at the spring center rest of $\approx 1500\,\mu\text{s}$), the brake stroke is guaranteed **$0.0\,\text{mm}$ (zero drag)**.
- **Fail-Safe Emergency Braking**: If the RC signal drops or the transmitter is powered off for **$> 100\,\text{ms}$**, the deadman supervisor automatically asserts **$27.0\,\text{mm}$ full emergency brake**.
- **Throttle Cutoff Interlock**: The moment brake stroke exceeds **$5.0\,\text{mm}$**, motor throttle is instantly overridden to **$0\,\text{mm/s}$ ($0.0\,\text{V}$ DAC)** and the legacy CAN frame `0x0BB` sets bit 4 (`0x10`). The motor cannot fight the mechanical brakes.

#### 2. How Do the Values Change?
The brake stroke is calculated by the proportional linear transfer formula:
$$\text{Stroke (mm)} = \text{clamp}\left(\frac{\text{Pulse} - 1520\,\mu\text{s}}{450\,\mu\text{s}}, 0.0, 1.0\right) \times 27.0\,\text{mm}$$

| Stick Position | Pulse ($\mu\text{s}$) | Physical Stroke | CAN `0x7B9` Raw Code | Action / State |
| :--- | :--- | :--- | :--- | :--- |
| **Released (Center)** | $\le 1520\,\mu\text{s}$ | **$0.0\,\text{mm}$** | `600` | Brake fully off, drive allowed |
| **Light Drag** | $\approx 1600\,\mu\text{s}$ | **$4.8\,\text{mm}$** | `696` | Initial brake pad engagement |
| **Throttle Cut Trigger**| $> 1603\,\mu\text{s}$ | **$> 5.0\,\text{mm}$** | $> 700$ | Motor power cut, CAN brake bit asserted |
| **Half Braking** | $\approx 1745\,\mu\text{s}$ | **$13.5\,\text{mm}$** | `870` | Moderate vehicle deceleration |
| **Emergency Lockup** | $\ge 1970\,\mu\text{s}$ | **$27.0\,\text{mm}$** | `1140` | Maximum hydraulic caliper stroke |
| **Signal Loss Drop** | Disconnected | **$27.0\,\text{mm}$** | `1140` | Automatic fail-safe emergency stop |

*(Note: If you prefer pulling the stick backward to brake instead of pushing forward, simply invert Channel 2 in your FlySky FS-i6 transmitter under `Functions setup > Reverse > Ch2: Rev`).*

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
