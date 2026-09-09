# RM-ESP32-T12D — Technical Architecture & Specification

Modern **C++17** FreeRTOS application for the E-Trike **Receiver Module Gateway** interfacing with the **RadioLink T12D** transmitter and **RadioLink R16F V1.0** receiver over single-wire **SBUS**.

---

## 1. System Role & Architectural Boundaries

In the E-Trike distributed network, `rm-esp32-t12d` acts strictly as an **Operator Input Gateway & Telemetry Publisher**:
- **Direct Setpoints, Not Actuator Safety Arbiters**: The RM gateway converts operator inputs into clean vehicle setpoints (`0x169 VCU_SES_REQ`, `0x204 RT_DRIVE_CMD`, `0x7B9 VCU_SEB_REQ`, `0x110 SYS_MODE_CMD`, and `0x113 SYS_PWR_CMD`).
- **Vehicle Safety Arbitration**: Downstream controllers (`sys-esp32` System Safety Supervisor, `rt-esp32` Real-Time Dynamics Controller, `mtr-stm32` Motor Controller, and `seb`/`ses` smart actuators) handle all vehicle safety, contactor isolation, ESTOP latching, acceleration ramps, and limits.
- **Immediate Physical Authority**: Toggling switches (SWA Enable, SWB Park, SWC Gear, SWD Mode) commands immediate vehicle state changes without arbitrary temporal delays, artificial cluster lockouts, or arming dwell state machines.

---

## 2. End-to-End System Topology

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                             OPERATOR CONTROLS (RadioLink T12D)                             │
│   Right Stick: Steer (X) / Service Brake (Y)    │   Left Stick: Throttle (Y) / Aux (X)     │
│   SWA (Drive Enable: UP=OFF, DOWN=ON)           │   SWB (Park / Hold: UP=OFF, DOWN=HOLD)   │
│   SWC (Gear: UP=R, MID=N, DOWN=D)               │   SWD (Mode: UP=M, DOWN=A)               │
│   VRA (Aux Analog 1)                            │   VRB (Aux Analog 2)                     │
└─────────────────────────────────────────────┬───────────────────────────────────────────────┘
                                              │ 2.4 GHz FHSS V2.1 (12 Channels)
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                               RECEIVER (RadioLink R16F V1.0)                                │
│                   Physical CH16 configured for Inverted SBUS Serial Output                  │
└─────────────────────────────────────────────┬───────────────────────────────────────────────┘
                                              │ CH16 SBUS (100 kbps, 8E2, Inverted)
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                            GATEWAY FIRMWARE (rm-esp32-t12d)                                 │
│                                                                                             │
│  [task_rc_capture] (Core 1, Event-driven / 10 ms UART timeout)                              │
│    ├── Inverted UART1 Driver (GPIO 16 RX)                                                   │
│    ├── SbusParser: 16-channel bit extraction (11 bits/ch, 0x0F header, flags)               │
│    ├── Electrical Sanity Check: Verifies pulses within physical limits (800..2200 us)       │
│    ├── LinkState Evaluator (Normal <150ms, Degraded on frame_lost, Lost >150ms / failsafe)  │
│    └── Lock-Free Seqlock Publisher: Thread-safe atomic snapshot publication                 │
│                                                                                             │
│  [task_can_tx] (Core 0, 100 Hz / 10 ms)                                                     │
│    ├── Lock-Free Seqlock Consumer: Reads consistent, tear-free snapshot                      │
│    ├── 0x169 VCU_SES_REQ  (Smooth Steer Rack Angle +/-45.0 deg)                             │
│    ├── 0x204 RT_DRIVE_CMD (Signed Target Speed mm/s + Gear Selection)                       │
│    ├── 0x7B9 VCU_SEB_REQ  (Service Brake Stroke 0..27mm + Park Hold 15.0 mm)                │
│    ├── 0x110 SYS_MODE_CMD (10 Hz Heartbeat: Manual / Autonomous Request)                    │
│    ├── 0x113 SYS_PWR_CMD  (10 Hz Heartbeat: Powertrain Enable Request)                     │
│    └── TWAI 4-Slot TX Queue & Bus-Off Auto-Recovery                                        │
│                                                                                             │
│  [task_heartbeat] (Core 1, 10 Hz / 100 ms)                                                  │
│    └── 1 Hz Decimated Health & Status Diagnostic Logger                                     │
└─────────────────────────────────────────────┬───────────────────────────────────────────────┘
                                              │ Low CAN Bus (500 kbit/s, Classic CAN 2.0A)
                                              ▼
                ┌─────────────────────────────┴─────────────────────────────┐
                │                                                           │
                ▼                                                           ▼
┌───────────────────────────────┐                           ┌───────────────────────────────┐
│           SYS-ESP32           │                           │           RT-ESP32            │
│ (System & Safety Supervisor)  │                           │ (Real-Time Dynamics & Drive)  │
│  - Contactors & hardwired trip│                           │  - Torque & speed closed-loop │
│  - Emergency stop handling    │                           │  - Steer rate limit & slew    │
│  - Mode state machine         │                           │  - Electro-hydraulic SEB cal  │
└───────────────────────────────┘                           └───────────────────────────────┘
```

---

## 3. Core Software Modules & Input Pipeline

### 3.1 SBUS Protocol Parser (`sbus_parser.h`)
- **Format**: 25-byte frame received every $14\dots 20\text{ ms}$ at 100,000 baud, 8 data bits, even parity, 2 stop bits (`8E2`), active-low inverted logic.
- **Bit Unpacking**: 16 channels packed into 22 bytes ($16 \times 11\text{ bits} = 176\text{ bits}$).
- **Microsecond Mapping**:
  $$\text{Pulse } (\mu\text{s}) = 988 + \frac{\text{raw} - 172}{1811 - 172} \times (2012 - 988)$$
  Center position: $992 \approx 1500\,\mu\text{s}$.
- **Hardware Failsafe Bits**: Byte 23 bits indicate Frame Lost (`0x04`) and Receiver Failsafe (`0x08`).

---

### 3.2 Link Health & Loss Supervisor
- **Timeout**: Frames not received for $> 150\text{ ms}$ or hardware failsafe flag $\implies$ `LinkState::Lost`, `signal_valid = false`.
- **Safe Fallback**: On signal loss, target motor speed is clamped to $0\text{ mm/s}$, transmission gear shifts to Neutral (`can::Gear::N`), and park brake holding stroke ($15.0\text{ mm}$) is commanded.
- **Immediate Recovery**: As soon as valid packets resume, control is immediately restored.

---

### 3.3 Direct Switch Mapping
- **SWA Drive Enable**:
  - `UP` ($< 1500\,\mu\text{s}$): Disabled (`drive_enable_req = false`).
  - `DOWN` ($\ge 1500\,\mu\text{s}$): Enabled (`drive_enable_req = true`).
  - Drive is active whenever `drive_enable_req && !park_hold_req && signal_valid`.
- **SWB Park / Brake Hold**:
  - `UP` ($< 1500\,\mu\text{s}$): Park Released (`park_hold_req = false`, $0\text{ mm}$ stroke).
  - `DOWN` ($\ge 1500\,\mu\text{s}$): Park Engaged (`park_hold_req = true`, $15.0\text{ mm}$ stroke, Neutral gear, $0\text{ mm/s}$).
- **SWC Gear Selector**:
  - `UP` ($\le 1300\,\mu\text{s}$): Reverse (`Gear::R`).
  - `MID` ($1300\dots 1700\,\mu\text{s}$): Neutral (`Gear::N`).
  - `DOWN` ($\ge 1700\,\mu\text{s}$): Drive (`Gear::D`).
- **SWD Autonomous Mode Request**:
  - `UP` ($< 1500\,\mu\text{s}$): Manual (`auto_mode_req = false`).
  - `DOWN` ($\ge 1500\,\mu\text{s}$): Autonomous Request (`auto_mode_req = true`).

---

### 3.4 Steering Dynamics & Continuous Deadband
- **Deadband**: $\pm 30\,\mu\text{s}$ around center ($1500\,\mu\text{s}$). Within deadband, rack angle is strictly $0.0^\circ$.
- **Smooth Normalization**: Beyond the deadband, offset is subtracted so the angle ramps continuously from $0.0^\circ$:
  $$\text{norm} = \frac{|\text{steer\_offset}| - 30}{450 - 30}, \quad \theta = \text{sgn}(\text{steer\_offset}) \times \text{norm} \times 45.0^\circ$$
- **Limits**: Clamped strictly to $\pm 45.0^\circ$ ($29550\dots 30450$ raw CAN counts).
- **Steer While Parked**: Steer setpoints remain active while parked to allow tire pre-alignment.

---

### 3.5 Throttle & Brake Pipeline
- **Motor Throttle (CH3 Left Stick Vertical)**:
  - Linear from $0\%$ at $1050\,\mu\text{s}$ to $100\%$ at $1950\,\mu\text{s}$.
  - Scaled by transmission gear: Drive ($0\dots +3000\text{ mm/s}$), Reverse ($0\dots -500\text{ mm/s}$), Neutral ($0\text{ mm/s}$).
- **Service Brake (CH2 Right Stick Vertical)**:
  - Spring-centered stick: pushed forward past $1520\,\mu\text{s}$ commands $0.0\dots 27.0\text{ mm}$ stroke.
  - **Brake-Over-Throttle Interlock**: Total brake $> 5.0\text{ mm}$ cuts motor throttle to $0$.
- **Auxiliary Dials (VRA / VRB)**:
  - Mapped directly to auxiliary outputs (`aux_vra`, `aux_vrb` $0.0\dots 1.0$) for implements; does not interfere with braking or throttle.

---

## 4. Tasks & FreeRTOS Architecture

| Task Name | Priority | Core | Period | Purpose |
| :--- | :---: | :---: | :---: | :--- |
| `task_rc_capture` | 8 | Core 1 | Event-driven | Blocks on UART1 FIFO with 10ms timeout, decodes SBUS immediately upon frame arrival |
| `task_can_tx` | 4 | Core 0 | $10\text{ ms}$ (100 Hz) | Reads snapshot, encodes and transmits CAN command cluster, runs TWAI recovery |
| `task_heartbeat` | 1 | Core 1 | $100\text{ ms}$ (10 Hz) | 1 Hz periodic serial diagnostic logger and TWAI health monitor |

---

## 5. CAN Frame Encoding Specification

| CAN ID | Name | Period | Signal Layout |
| :--- | :--- | :---: | :--- |
| `0x169` | `VCU_SES_REQ` | 10 ms | `d[0]`: Enable flags (bit 0 = align, bit 1 = control)<br>`d[2..3]`: Target angle raw i16 ($29550\dots 30450$)<br>`d[4..5]`: Target speed raw ($328$) + rolling counter (0..15)<br>`d[6]`: Vehicle speed raw<br>`d[7]`: XOR checksum |
| `0x204` | `RT_DRIVE_CMD` | 10 ms | `d[0..3]`: Motor speed int32 ($-500\dots +3000\text{ mm/s}$)<br>`d[4]`: Transmission Gear (`0`=Neutral, `1`=Drive, `3`=Reverse) |
| `0x7B9` | `VCU_SEB_REQ` | 10 ms | `d[0]`: Control mode & enable flags<br>`d[2..3]`: Brake stroke request raw uint16 ($600\dots 1140 \implies 0\dots 27\text{ mm}$)<br>`d[6]`: Rolling counter (0..15)<br>`d[7]`: XOR checksum |
| `0x110` | `SYS_MODE_CMD` | 100 ms | `d[0]`: Operator Mode Request (`0`=Manual, `1`=Auto)<br>`d[1]`: Rolling counter (0..255) |
| `0x113` | `SYS_PWR_CMD` | 100 ms | `d[0]`: Powertrain Enable (`0`=Off, `1`=On)<br>`d[1]`: Rolling counter (0..255) |

---

## 6. Verification & Automated Test Suite

A standalone native C++17 unit test suite (`test/test_rm_t12d_suite.cpp`) validates parsing and frame codecs:
```powershell
C:\TDM-GCC-64\bin\g++.exe -std=c++17 -Wall -Wextra -Irm-esp32-t12d/src -I. -Ishared rm-esp32-t12d/src/sbus_parser.cpp rm-esp32-t12d/test/test_rm_t12d_suite.cpp -o rm-esp32-t12d/test/test_rm_t12d_suite.exe
.\rm-esp32-t12d\test\test_rm_t12d_suite.exe
```
