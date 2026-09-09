# RadioLink T12D RC Controller Mapping (RM-ESP32-T12D)

Vehicle driving controls and switch mapping for the electric three-wheeler (`etrike`) using the **RadioLink T12D** transmitter and **R16F V1.0** receiver over **SBUS**.

---

## 1. Controller Layout & Physical Mapping

### Visual Layout Map

```
              [SWA]   [SWB]             [SWC]   [SWD]
             (2-pos) (2-pos)           (3-pos) (2-pos)
                │       │                 │       │
             AUTONOMY IGNITION          GEAR   AUX / ESTOP
             UP: OFF  UP:   OFF         UP:  REV (R)
             DN: ON   DOWN: ON          MID: NEU (N)
                                        DN:  DRV (D)
                      [VRA]             [VRB]
                     (dial)            (dial)
                        │                 │
                   SPEED GOVERNOR      BRAKE TRIM
                   (0% - 100% max)

          ┌───────────────┐           ┌───────────────┐
          │       ▲       │           │       ▲       │
          │       │       │           │   ◄───┼───►   │ STEERING
          │       ▼       │           │       ▼       │ (±45.0° Rack)
          │    THROTTLE   │           │     BRAKE     │
          │  (0% - 100%)  │           │   (0 - 27mm)  │
          └───────────────┘           └───────────────┘
             LEFT GIMBAL                 RIGHT GIMBAL
```

### Channel Mapping Table

| T12D Control | Type | SBUS Channel | Range (11-bit) | Pulse ($\mu\text{s}$) | Vehicle Action | CAN ID | CAN Encoding |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Right Stick (X)** | 2-Axis Gimbal | **CH1** | $172\dots 1811$ | $1050\dots 1950$ | **Steering** ($\pm 45.0^\circ$) | `0x169` | `VCU_SES_REQ` ($29550\dots 30450$ raw) |
| **Right Stick (Y)** | 2-Axis Gimbal | **CH2** | $172\dots 1811$ | $1520\dots 1970$ | **Brake** ($0\dots 27.0\text{ mm}$) | `0x7B9` | `VCU_SEB_REQ` ($600\dots 1140$ raw) |
| **Left Stick (Y)** | 2-Axis Gimbal | **CH3** | $172\dots 1811$ | $1050\dots 1950$ | **Throttle** ($0\dots 100\%$) | `0x204` | `RT_DRIVE_CMD` ($0\dots 3000\text{ mm/s}$) |
| **Left Stick (X)** | 2-Axis Gimbal | **CH4** | $172\dots 1811$ | $1000\dots 2000$ | **Spare / Yaw** ($0\dots 1.0$) | — | Telemetry logging |
| **SWB Switch** | 2-Pos Toggle | **CH5** | $172$ / $1811$ | $1000$ / $2000$ | **Ignition** (OFF / ON) | `0x113` | `SYS_PWR_CMD` (`power_state` 0 / 1) |
| **SWC Switch** | 3-Pos Toggle | **CH6** | $172$ / $992$ / $1811$ | $1000$ / $1500$ / $2000$| **Gear** (REV / NEU / DRV) | `0x110` / `0x204` | `SYS_MODE_CMD` & `RT_DRIVE_CMD` |
| **SWA Switch** | 2-Pos Toggle | **CH7** | $172$ / $1811$ | $1000$ / $2000$ | **Autonomy Override** | — | Mode flag |
| **SWD Switch** | 2-Pos Toggle | **CH8** | $172$ / $1811$ | $1000$ / $2000$ | **Aux / Fast ESTOP** | `0x001` | `SAFETY_ESTOP` |
| **VRA Knob** | Rotary Pot | **CH9** | $172\dots 1811$ | $1000\dots 2000$ | **Speed Limit Governor** | — | $0\dots 100\%$ scale trim |
| **VRB Knob** | Rotary Pot | **CH10** | $172\dots 1811$ | $1000\dots 2000$ | **Brake Sensitivity** | — | Sensitivity curve trim |
| **CH11–CH16** | Expansion | **CH11–16** | $172\dots 1811$ | $1000\dots 2000$ | Reserved / Aux | — | Diagnostics pass-through |
| **SBUS Failsafe** | Hardware Bit | Byte 23 Bit 3 | 0 / 1 | — | **Hardware Failsafe** | `0x001` | Full emergency stop |

---

## 2. Operating Principles & Safety Interlocks

### A. Throttle Engagement Interlocks
- Throttle commands are transmitted **only when**:
  1. RC signal is valid (no timeout and no failsafe flag).
  2. Ignition is **ON** (SWB down).
  3. Gear is in **Drive (D)** or **Reverse (R)**.
  4. Brake stroke is released ($\le 5.0\text{ mm}$).
- **Brake-Over-Throttle Interlock**: If the brake stick is pressed past $5.0\text{ mm}$ stroke ($> 1603\,\mu\text{s}$), motor speed is instantly zeroed ($0\text{ mm/s}$).

### B. Brake Caliper Transfer Formula
$$\text{Stroke (mm)} = \begin{cases} 
0.0\text{ mm} & \text{if } \text{Pulse} \le 1520\,\mu\text{s} \\
\operatorname{clamp}\left(\dfrac{\text{Pulse} - 1520\,\mu\text{s}}{450\,\mu\text{s}},\, 0.0,\, 1.0\right) \times 27.0\,\text{mm} & \text{if } \text{Pulse} > 1520\,\mu\text{s}
\end{cases}$$

Raw wire encoding for CAN `0x7B9`:
$$\text{stroke\_raw} = (\text{Stroke} + 30.0) \times 20 = \text{Stroke} \times 20 + 600$$

### C. Fail-Safe Emergency Stop & Recovery
- Trigger conditions:
  - RadioLink R16F asserts hardware failsafe bit (`flags & 0x08`).
  - RF pulse / frame drop exceeds $100\text{ ms}$ deadman timeout.
  - External `0x001 SAFETY_ESTOP` or `0x011 SYS_SAFETY_STS` (estop_active=1) received on CAN.
- Immediate Actions:
  - Steering snapped to $0.0^\circ$ rack center.
  - Mechanical brake clamped to **$27.0\text{ mm}$ (maximum emergency stroke)**.
  - Motor setpoint forced to $0\text{ mm/s}$.
  - Rate-limited `0x001 SAFETY_ESTOP` broadcast.
- **ESTOP Reset Sequence**:
  - Requires: Valid RC signal + Hardware failsafe cleared + Ignition switched OFF (SWB UP) + Gear in Neutral (SWC MID).
