# RadioLink T12D Controller Specification (RM-ESP32-T12D)

Automotive & Ground-Vehicle HMI Specification for the **RadioLink T12D transmitter** and **R16F V1.0 receiver** over single-wire **SBUS** on the electric three-wheeler (`etrike`). Aligned with **NHTSA, SAE, FHWA, ISO 2575, UN Regulation 121, and ISO 13850**.

---

## 1. Controller Layout & Physical Mapping

```
              [SWA] (2-pos)             [SWB] (2-pos)             [SWC] (3-pos)             [SWD] (2-pos)
              ┌───────────┐             ┌───────────┐             ┌───────────┐             ┌───────────┐
              │   DRIVE   │             │   PARK /  │             │ PRECISION │             │  MANUAL / │
              │   ENABLE  │             │    HOLD   │             │  NORMAL / │             │    AUTO   │
              │  REQUEST  │             │           │             │   FAST    │             │  REQUEST  │
              └───────────┘             └───────────┘             └───────────┘             └───────────┘
               UP: DISABLE               UP: PARK/HOLD             UP:  PRECISION (0.75 m/s) UP: MANUAL
               DN: ENABLE REQ            DN: DRIVE                 MID: NORMAL    (1.8 m/s)  DN: AUTO REQ
                                                                   DN:  FAST     (3.0 m/s)

                     [VRA] (rotary)                            [VRB] (rotary)
                     ┌───────────┐                             ┌───────────┐
                     │   SPARE / │                             │  SPARE /  │
                     │ AUXILIARY │                             │ AUXILIARY │
                     └───────────┘                             └───────────┘
                       (Reserved)                                (Reserved)

           LEFT GIMBAL (Spring Centered)              RIGHT GIMBAL (Spring Centered)
           ┌───────────────────────────┐              ┌───────────────────────────┐
           │             ▲             │              │             ▲             │
           │             │             │              │             │ FORWARD (+) │
           │     ◄───────┼───────►     │ IMPLEMENT    │     ◄───────┼───────►     │ STEERING (X)
           │     AUX X   │   AUX Y     │ (Pan / Tilt) │    LEFT     │    RIGHT    │ (±45.0° Rack)
           │             ▼             │              │             ▼ REVERSE (-) │
           └───────────────────────────┘              └───────────────────────────┘
                   (Auxiliary)                                (Vehicle Drive)
```

---

## 2. 12-Channel Automotive HMI Mapping Table

| Channel | T12D Control | Physical Type | Function | MCU Interpretation & Range | Telemetry / CAN Target |
| :---: | :--- | :--- | :--- | :--- | :--- |
| **CH1** | **Right Stick X** | 2-Axis Gimbal (Spring Centered) | **Steering** | Proportional: $\pm 45.0^\circ$ rack angle ($29550\dots 30450$ raw) | `0x169 VCU_SES_REQ` |
| **CH2** | **Right Stick Y** | 2-Axis Gimbal (**Spring Centered**) | **Signed Longitudinal Velocity** | Proportional: $-1000\dots +3000\text{ mm/s}$ (Forward / Reverse with neutral dwell) | `0x204 RT_DRIVE_CMD` |
| **CH3** | **Left Stick X** | 2-Axis Gimbal (Spring Centered) | **Implement / Aux X** | Proportional: $0.0\dots 1.0$ (Pan / lateral implement) | Telemetry / Aux |
| **CH4** | **Left Stick Y** | 2-Axis Gimbal (Spring Centered) | **Implement / Aux Y** | Proportional: $0.0\dots 1.0$ (Tilt / vertical implement) | Telemetry / Aux |
| **CH5** | **SWA Switch** | Top-Left Outer (2-Position) | **Drive Enable Request** | Edge-Qualified: UP = Disabled, DOWN = Arming Request | Safety Arbiter |
| **CH6** | **SWB Switch** | Top-Left Inner (2-Position) | **Park / Brake Hold** | Semantic Request: UP = Park/Hold, DOWN = Drive | SYS / SEB |
| **CH7** | **SWC Switch** | Top-Right Inner (3-Position) | **Drive Envelope** | 3-Tier: UP = Precision ($0.75\text{ m/s}$), MID = Normal ($1.8\text{ m/s}$), DOWN = Fast ($3.0\text{ m/s}$) | Speed Arbiter |
| **CH8** | **SWD Switch** | Top-Right Outer (2-Position) | **Manual / Auto Request** | Asymmetric: UP = Manual, DOWN = Auto Request | `0x110 SYS_MODE_CMD` |
| **CH9** | **VRA Knob** | Top Center-Left Rotary | **Spare / Auxiliary** | Removed from driving path (auxiliary / implement speed) | Aux / Telemetry |
| **CH10**| **VRB Knob** | Top Center-Right Rotary | **Spare / Auxiliary** | Removed from steering dynamics (deterministic steering) | Aux / Telemetry |
| **CH11**| *Unassigned* | Software NULL | **Spare / Expansion** | Reserved (Lights / Horn) | — |
| **CH12**| *Unassigned* | Software NULL | **Spare / Expansion** | Reserved | — |

---

## 3. Core Safety Rules & Operating Principles

### A. Right Gimbal Driving Dynamics
- **Deterministic Steering (CH1)**: Steering response is fixed and calibrated ($\pm 45.0^\circ$ rack limit, $\pm 30\,\mu\text{s}$ center deadband). VRB does **not** dynamically change steering curves; vehicle steering feels identical on every drive.
- **Signed Velocity & Spring Neutral (CH2)**: Right Stick Y springs to neutral. Releasing the stick snaps velocity to $0\text{ mm/s}$ (regenerative braking to stop).
- **Direction Reversal Guard**: Moving from forward to reverse requires:
  $$\text{measured\_speed} \le 50\text{ mm/s} \quad \mathbf{AND} \quad \text{neutral\_dwell\_timer} \ge 200\text{ ms}$$
  A timer alone never authorizes reverse torque while the vehicle is moving forward.

### B. Edge-Qualified SWA Arming Sequence
- On boot or after RF link recovery, drive starts **Disarmed**.
- Arming requires:
  1. RF link observed healthy for $\ge 500\text{ ms}$.
  2. SWA first observed in the **DISABLED** position (`UP`).
  3. Right Stick Y confirmed in neutral.
  4. Deliberate operator transition `DISABLED (UP) -> ENABLED (DOWN)`.
- Toggling SWA `UP` disarms drive **immediately and unconditionally**.

### C. Semantic Park / Brake Hold (SWB)
- `SWB = UP`: Driver requests `PARK_HOLD`. SYS/RT commands hydraulic brake hold ($15.0\text{ mm}$ stroke) and neutral motor torque.
- `SWB = DOWN`: Driver requests `DRIVE`. Transitioning to Drive requires stick in neutral.

### D. Single-Control Speed Envelope (SWC)
Speed limits are unambiguous and controlled solely by Switch C:
- **PRECISION (UP)**: $0\dots 0.75\text{ m/s}$ ($2.7\text{ km/h}$).
- **NORMAL (MID)**: $0\dots 1.8\text{ m/s}$ ($6.5\text{ km/h}$).
- **FAST (DOWN)**: $0\dots 3.0\text{ m/s}$ ($10.8\text{ km/h}$) — conditionally granted by SYS based on vehicle health.

### E. Asymmetric Authority Transition (SWD)
- **Manual $\to$ Auto**: Requires vehicle stationary ($|\text{speed}| < 50\text{ mm/s}$), stick in neutral, and system health checks.
- **Auto $\to$ Manual**: Flipping SWD `UP` immediately and unconditionally revokes autonomous authority.

### F. Separation of Physical ESTOP from RF Loss (ISO 13850)
- **Physical ESTOP**: Managed exclusively by the vehicle's hardware safety chain and mushroom buttons. Radio switches **never** reset physical ESTOP. Resetting the emergency stop device must not restart the machine.
- **RF Loss**:
  - $0\dots 50\text{ ms}$: Normal operation.
  - $50\dots 100\text{ ms}$: Stale frames, hold previous safe state, log warning.
  - $> 100\text{ ms}$: Link lost. Motor setpoint forced to $0\text{ mm/s}$, controlled safe stop commanded, drive disarmed. Re-arm sequence required upon RF recovery.
