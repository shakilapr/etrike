# FlySky FS-i6 RC Controller Mapping (RM-ESP32)

Quick reference for vehicle driving controls and switch mapping.

---

## 1. Controller Layout & Vehicle Control Snap

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
         │   ▼  (0-27mm) │           │       ▼       │ (±450.0°)
         └───────────────┘           └───────────────┘
            LEFT GIMBAL                 RIGHT GIMBAL
```

---

## 2. Controls & Actions Table

| Control Key / Stick | Physical Switch | Action / Position | Vehicle Response | CAN Message |
| :--- | :--- | :--- | :--- | :--- |
| **Ignition** | **SWB** (Top Inner-Left) | **UP** | Ignition **OFF** (vehicle disarmed) | `0x112 HMI_PWR_REQ` (`req_start=0`) |
| | | **DOWN** | Ignition **ON** (vehicle armed & ready) | `0x112 HMI_PWR_REQ` (`req_start=1`) |
| **Gear Selector** | **SWC** (Top Inner-Right) | **UP** | **Reverse (R)** | `0x204` / `0x0BB` (`mode=0x09`) |
| | *(Only 3-pos switch)* | **MID** | **Neutral / Park (N)** | `0x204` / `0x0BB` (`mode=0x03`) |
| | | **DOWN** | **Drive (D)** | `0x204` / `0x0BB` (`mode=0x05`) |
| **Throttle / Speed Trim**| **VRA** (Left Dial) | **0% $\rightarrow$ 100%** | Sets motor drive speed ($0\dots 3000\text{ mm/s}$ in Drive, $0\dots 500\text{ mm/s}$ in Reverse) | `0x204 RT_DRIVE_CMD` & `0x0AA THROTTLE` |
| **Steering** | **Right Stick (Horizontal)**| **Left / Right** | Proportional steering column turn: **$-450.0^\circ \dots +450.0^\circ$** (active only in D or R) | `0x169 VCU_SES_REQ` |
| **Brake** | **Left Stick (Vertical)** | **Pull Down** | Proportional brake stroke: **$0.0\text{ mm} \dots 27.0\text{ mm}$** | `0x7B9 VCU_SEB_REQ` |
| **Auxiliary** | **VRB** (Right Dial) | **0% $\rightarrow$ 100%** | Reserved expansion channel | Telemetry |

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
