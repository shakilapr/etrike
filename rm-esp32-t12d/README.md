# RM-ESP32-T12D — Receiver Module Gateway (RadioLink T12D / R16F SBUS)

Gateway firmware for the **RadioLink T12D** transmitter and **RadioLink R16F V1.0** receiver running on the **ESP32** microcontroller over single-wire **SBUS**.

Designed specifically for the electric three-wheeler (`etrike`) operator controls and Low-CAN command broadcasting.

---

## 1. System Overview & Gateway Architecture

The `rm-esp32-t12d` node interfaces the RadioLink T12D transmitter (FHSS V2.1 protocol) with the vehicle's **Low CAN bus (500 kbit/s, Classic CAN 2.0A)**.

In the E-Trike distributed control system, `rm-esp32-t12d` functions strictly as an **Operator Input Gateway**:
- **Direct Setpoints**: Converts operator stick and switch inputs directly into clean CAN command frames (`0x169 VCU_SES_REQ`, `0x204 RT_DRIVE_CMD`, `0x7B9 VCU_SEB_REQ`, `0x110 SYS_MODE_CMD`, and `0x113 SYS_PWR_CMD`).
- **Downstream Safety Ownership**: Vehicle safety, contactor isolation, ESTOP latching, slew rates, and acceleration ramps are managed downstream by `sys-esp32`, `rt-esp32`, and `mtr-stm32`.
- **Immediate Authority**: Switch toggles command immediate vehicle setpoints without artificial arming delays or multi-second lockouts.

```
              [SWA] (3-pos)             [SWB] (3-pos)             [SWC] (3-pos)             [SWD] (2-pos)
              ┌───────────┐             ┌───────────┐             ┌───────────┐             ┌───────────┐
              │ OPERATING │             │   PARK /  │             │   GEAR    │             │   DRIVE   │
              │   MODE    │             │    HOLD   │             │  SELECT   │             │   ENABLE  │
              │  SELECT   │             │           │             │           │             │  REQUEST  │
              └───────────┘             └───────────┘             └───────────┘             └───────────┘
               UP:  BARE                 UP:  PARK HOLD (15mm)     UP:  REVERSE (R)          UP: DISABLE
               MID: SYS                  MID: RELEASED (0mm)       MID: NEUTRAL (N)          DN: ENABLE REQ
               DN:  RT                   DN:  RELEASED (0mm)       DN:  DRIVE   (D)

                     [VRA] (rotary)                            [VRB] (rotary)
                     ┌───────────┐                             ┌───────────┐
                     │   SPEED   │                             │    AUX    │
                     │  GOVERNOR │                             │  ANALOG 2 │
                     └───────────┘                             └───────────┘
                      (0.0..1.0)                                (0.0..1.0)

                     [VRC] (shoulder)                          [VRD] (shoulder)
                     ┌───────────┐                             ┌───────────┐
                     │ AUX BRAKE │                             │ AUX BRAKE │
                     │ PULL-DOWN │                             │ PULL-DOWN │
                     └───────────┘                             └───────────┘
                      (0 to -100%)                              (0 to -100%)

           LEFT GIMBAL (Ratcheted Throttle)               RIGHT GIMBAL (Spring Centered)
           ┌───────────────────────────┐              ┌───────────────────────────┐
           │             ▲             │              │             ▲             │
           │             │ THROTTLE    │              │             │ BRAKE (Y)   │ SERVICE BRAKE
           │     ◄───────┼───────►     │              │     ◄───────┼───────►     │ (0.0 to 27.0 mm)
           │             │ (0 to 100%) │              │   STEER (X) │ (Wide Center│ (Deadband: ±220us)
           │             ▼             │              │ (±45.0° Rack) Deadband)   │
           └───────────────────────────┘              └───────────────────────────┘
                   (Left Stick)                               (Right Stick)
```

---

## 2. Channel Mapping Table

| Channel | Physical Control | Function | Signal Interpretation & Range | CAN Target |
| :---: | :--- | :--- | :--- | :--- |
| **CH1** | **Right Stick X** | **Steering** | Proportional: $\pm 45.0^\circ$ rack angle ($29550\dots 30450$ raw, $\pm 30\,\mu\text{s}$ center deadband) | `0x169 VCU_SES_REQ` / `0x303 HOST_STEER_CMD` |
| **CH2** | **Right Stick Y** | **Service Brake** | Proportional: $0.0\dots 27.0\text{ mm}$ stroke (Push/Pull outside $1280\dots 1720\,\mu\text{s}$ deadband) | `0x7B9 VCU_SEB_REQ` / `0x301 HOST_BRAKE_REQ` |
| **CH3** | **Left Stick Y** | **Motor Throttle** | Proportional: $0\dots 100\%$ ($1160\dots 1900\,\mu\text{s}$, rest $\le 1160\,\mu\text{s} = 0\%$) | `0x204 RT_DRIVE_CMD` / `0x300 HOST_DRIVE_CMD` |
| **CH4** | **Left Stick X** | **Aux Implement X** | Proportional: $0.0\dots 1.0$ | Aux / Telemetry |
| **CH5** | **SWA Switch** | **Operating Mode** | 3-Position: UP = `BARE`, MID = `SYS`, DOWN = `RT` | Target CAN Cluster Selection |
| **CH6** | **SWB Switch** | **Park / Brake Hold** | 3-Position: UP = Park Hold ($15\text{ mm}$, Neutral), MID/DOWN = Released ($0\text{ mm}$) | `0x7B9` / `0x301` |
| **CH7** | **SWC Switch** | **Gear Selector** | 3-Position: UP = Reverse (`R`), MID = Neutral (`N`), DOWN = Drive (`D`) | `0x204 RT_DRIVE_CMD` / `0x300 HOST_DRIVE_CMD` |
| **CH8** | **SWD Switch** | **Drive Enable** | 2-Position: UP = Disabled (OFF), DOWN = Enable Request (ON) | `0x113 SYS_PWR_CMD` / `0x112 HMI_PWR_REQ` |
| **CH9** | **VRA Knob** | **Speed Governor** | Rotary Potentiometer: $0.0\dots 1.0$ (Scales max ceiling for D and R from 0% to 100%) | Motor Command Governor |
| **CH10**| **VRB Knob** | **Aux Analog 2** | Rotary Potentiometer: $0.0\dots 1.0$ | Telemetry / Aux |
| **CH11**| **VRC Knob** | **Aux Brake Pull** | Shoulder control: Rest at 0 (1500us); 0 to -10% deadband; -10% to -100% applies 0 to 27mm brake | `0x7B9` / `0x301` Service Brake |
| **CH12**| **VRD Knob** | **Aux Brake Pull** | Shoulder control: Rest at 0 (1500us); 0 to -10% deadband; -10% to -100% applies 0 to 27mm brake | `0x7B9` / `0x301` Service Brake |

---

## 3. Operating Principles

### A. Steering Control (CH1 Right Stick X)
- **Deadband**: $\pm 30\,\mu\text{s}$ around center ($1500\,\mu\text{s}$). Within deadband, rack angle is $0.0^\circ$.
- **Smooth Ramp**: Angle ramps continuously from $0.0^\circ$ beyond the deadband up to $\pm 45.0^\circ$ rack mechanical limit.
- **Steer While Parked**: Steering remains functional while parked to allow tire pre-alignment.

### B. Service Brake & Park Brake (CH2, CH11, CH12 & CH6)
- **Right Stick Service Brake (CH2)**: Extra-wide center deadband ($\pm 220\,\mu\text{s}$, active only outside $1280\dots 1720\,\mu\text{s}$) prevents accidental braking during hard steering. Beyond deadband, applies progressive $0.0\dots 27.0\text{ mm}$ stroke.
- **Aux Pull-Down Brakes (VRC CH11 & VRD CH12)**: At rest ($0 / 1500\,\mu\text{s}$), brake is $0.0\text{ mm}$. First $0\dots -10\%$ is deadband (no brake). Pulling down past $-10\%$ to $-100\%$ progressively applies $0.0\dots 27.0\text{ mm}$ brake.
- **Park / Brake Hold (SWB CH6)**: Flipping SWB UP engages $15.0\text{ mm}$ holding stroke and forces transmission gear to Neutral (`can::Gear::N`) and target speed to $0\text{ mm/s}$.
- **Arbitration**: Caliper stroke commands $\max(\text{stick\_brake}, \text{vrc\_brake}, \text{vrd\_brake}, \text{park\_hold\_brake})$.

### C. Throttle & VRA Speed Governor (CH3, CH9 & CH7)
- **Drive Active Condition**: Drive requires `drive_enable_req && !park_hold_req && signal_valid`.
- **Idle Deadband**: Pulses $\le 1160\,\mu\text{s}$ are strictly $0\%$ to provide a solid mechanical rest zone.
- **VRA Dynamic Speed Governor (CH9)**: Scales maximum speed ceiling from $0\%$ to $100\%$ of parameter limits for both Drive and Reverse. Full Left Stick travel maps smoothly across the active ceiling:
  - Gear **D**: $0\dots +(\text{VRA} \times 3000)\text{ mm/s}$ (up to $10.8\text{ km/h}$).
  - Gear **R**: $0\dots -(\text{VRA} \times 500)\text{ mm/s}$ (up to $1.8\text{ km/h}$).
  - Gear **N** or Park: $0\text{ mm/s}$.
- **Brake-Over-Throttle Interlock**: Total brake stroke $> 5.0\text{ mm}$ immediately cuts throttle demand to $0$.

### D. RF Link Supervision & Failsafe Fallback
- **Timeout**: Frames not received for $> 150\text{ ms}$ or receiver failsafe flag asserts $\implies$ `LinkState::Lost`, `signal_valid = false`.
- **Safe Fallback**: On signal loss, target motor speed is clamped to $0\text{ mm/s}$, gear forces to Neutral, and $15.0\text{ mm}$ park holding stroke engages.
- **Recovery**: Normal command transmission resumes immediately when valid frames are restored.

---

## 4. Hardware Wiring & Level Divider

Only **three connections** are required between the R16F receiver and the ESP32:

| R16F Pin | ESP32 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **CH16 Signal** (Top) | **GPIO 16** | SBUS Serial RX | Inverted UART1 RX (100 kbps, 8E2) |
| **CH16 +5V** (Middle) | **5V Logic Rail** | Receiver Power | 3–12 V supply (~50 mA @ 5V). Do NOT use EXT port to power receiver! |
| **CH16 GND** (Bottom) | **GND** | Ground | Common reference ground |

### CAN Bus Wiring:
| Net | ESP32 Pin | Direction | Connected To |
| :--- | :--- | :--- | :--- |
| **CAN TX** | **GPIO 5** | Output | Low CAN Transceiver TXD |
| **CAN RX** | **GPIO 4** | Input | Low CAN Transceiver RXD |

---

## 5. Receiver & Transmitter Setup

### 5.1 RadioLink T12D Transmitter Configuration
1. Power on T12D.
2. In the System/Model menu, ensure protocol is set to **FHSS V2.1** (required for 12 proportional channels).
3. Assign physical controls in **BASIC MENU $\to$ AUX-CH**:
   - CH1: Right Stick X (Steering)
   - CH2: Right Stick Y (Service Brake)
   - CH3: Left Stick Y (Motor Throttle)
   - CH4: Left Stick X (Aux Implement X)
   - CH5: SWA (Operating Mode: UP=BARE, MID=SYS, DOWN=RT)
   - CH6: SWB (Park Hold: UP=HOLD, MID/DOWN=OFF)
   - CH7: SWC (Transmission Gear: UP=R, MID=N, DOWN=D)
   - CH8: SWD (Drive Enable: UP=OFF, DOWN=ON)
   - CH9: VRA (Speed Governor: 0% to 100%)
   - CH10: VRB (Aux Analog 2)
   - CH11: VRC (Aux Brake Pull: 0 to -100%)
   - CH12: VRD (Aux Brake Pull: 0 to -100%)

### 5.2 Binding R16F Receiver
1. Place transmitter and receiver ~60 cm apart.
2. Power on the R16F receiver.
3. Press and hold the **ID SET** button on the R16F for $>1$ second.
4. The receiver LED will flash rapidly. Release the button.
5. Once bound, the LED remains solid.

### 5.3 Enable SBUS Output Mode on R16F
1. With receiver powered, **short press** the ID SET button once.
2. Verify LEDs:
   - **RED LED**: ON
   - **BLUE LED**: ON
   - **Interpretation**: Red + Blue ON indicates **PWM + SBUS mode**. CH16 is now the SBUS serial output.

---

## 6. Serial Telemetry Logging

To maintain optimal serial throughput at 100 Hz CAN transmit rate, `rm-esp32-t12d` uses a **dual-rate change-driven + 2 Hz decimated logger**:
- Dynamic changes (steering $\ge 1^\circ$, brake $\ge 0.5\text{ mm}$, throttle $\ge 5\%$, speed $\ge 50\text{ mm/s}$, governor $\ge 5\%$, gear, enable, park, mode) are logged immediately:
  ```text
  I (14502) tx: STR:+0.0 BRK: 0.0  THR: 50% GOV: 80% MTR:+1200[D]  ARM:ON  PRK:OFF  MOD:BARE RF:OK
  ```

---

## 7. Building, Flashing & Testing

### Building with PlatformIO
```powershell
cd rm-esp32-t12d

# Build vehicle firmware
pio run -e vehicle

# Build bench firmware
pio run -e bench

# Flash to ESP32
pio run -e vehicle -t upload

# Monitor telemetry serial log (115200 baud)
pio run -e vehicle -t upload -t monitor
```

### Running Native Unit Tests
```powershell
C:\TDM-GCC-64\bin\g++.exe -std=c++17 -Wall -Wextra -Irm-esp32-t12d/src -I. -Ishared rm-esp32-t12d/src/sbus_parser.cpp rm-esp32-t12d/test/test_rm_t12d_suite.cpp -o rm-esp32-t12d/test/test_rm_t12d_suite.exe
.\rm-esp32-t12d\test\test_rm_t12d_suite.exe
```
All 158 test assertions validate SBUS parsing, microsecond calibration, steering deadband curves, gear selection, park hold logic, brake-over-throttle interlock, 3 runtime operating modes (BARE, SYS, RT), link loss failsafe, and CAN encoding.
