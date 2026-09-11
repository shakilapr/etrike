# RM-ESP32-T12D — Technical Architecture & Specification

Modern **C++17** FreeRTOS application for the E-Trike **Receiver Module Gateway** interfacing with the **RadioLink T12D** transmitter and **RadioLink R16F V1.0** receiver over single-wire **SBUS**.

---

## 1. System Role & Multi-Mode Gateway Philosophy

In the E-Trike distributed network, `rm-esp32-t12d` acts as a versatile **Operator Input Gateway & Testing Harness**. It provides three transparent operating modes, allowing operators and test engineers to connect directly to actuators, target `sys-esp32`, or target `rt-esp32`.

In each mode, the downstream connected nodes receive the exact canonical protocol frames they expect, making the gateway indistinguishable from the production subsystem it emulates:

1. **`BARE` Mode (Direct Actuator Testing)**:
   - **Environment**: Standalone bench or vehicle chassis testing without `sys-esp32`, `rt-esp32`, or `Host`.
   - **Bus**: Low-CAN (500 kbit/s).
   - **Role**: Emulates both the vehicle supervisor and real-time controller, sending direct actuator setpoints to SES (`0x169`), SEB (`0x7B9`), and MTR (`0x204`), alongside emulated supervisor mode (`0x110`) and power (`0x113`) frames.

2. **`SYS` Mode (Targeting `sys-esp32`)**:
   - **Environment**: Connected to `sys-esp32` on Low-CAN. No `rt-esp32` or `Host` connected.
   - **Bus**: Low-CAN (500 kbit/s).
   - **Role**: Emulates `rt-esp32` and HMI requests. Transmits operator demands via `0x111 HMI_MODE_REQ` and `0x112 HMI_PWR_REQ`, real-time drive setpoint `0x204 RT_DRIVE_CMD`, actuator commands `0x169` and `0x7B9`, and `0x7FD RT_HEARTBEAT`. Does not transmit `0x110` or `0x113` since `sys-esp32` generates them.

3. **`RT` Mode (Targeting `rt-esp32`)**:
   - **Environment**: Connected to `rt-esp32` on High-CAN. No Jetson/Host computer connected.
   - **Bus**: High-CAN (500 kbit/s).
   - **Role**: Emulates the autonomous Host system. Transmits high-level vehicle setpoints via `0x300 HOST_DRIVE_CMD`, `0x303 HOST_STEER_CMD`, `0x301 HOST_BRAKE_REQ`, `0x111 HMI_MODE_REQ`, `0x112 HMI_PWR_REQ`, and `0x7FC HOST_HEARTBEAT`.

Across all three modes:
- **Unified Operator Pipeline**: Gimbal deadbands, switch thresholds, failsafe fallback, and `RcSnapshot` are 100% identical.
- **Unified Serial Logging**: Telemetry output format remains identical regardless of active mode.
- **Programmatic Protocol Codecs**: All CAN frames are encoded directly via `protocol/compat/can.hpp` structs and functions, eliminating manual bit-shifting errors.

---

## 2. End-to-End System Topology

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                             OPERATOR CONTROLS (RadioLink T12D)                             │
│   Right Stick: Steer (X) / Service Brake (Y)    │   Left Stick: Throttle (Y)                │
│   SWA (Target Mode: UP=BARE, MID=SYS, DOWN=RT)  │   SWB (Park / Hold: UP=HOLD, MID/DN=REL)  │
│   SWC (Gear: UP=R, MID=N, DOWN=D)               │   SWD (Drive Enable: UP=OFF, DOWN=ON)     │
│   VRA (Dynamic Speed Governor 0..100%)          │   VRB (Aux Analog Knob 2)                 │
│   VRC (Aux Pull-Down Brake 1)                   │   VRD (Aux Pull-Down Brake 2)             │
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
│    ├── Sanity Check: Verifies pulses within electrical limits (800..2200 us)                │
│    ├── LinkState Evaluator (Normal <150ms, Degraded on frame_lost, Lost >150ms / failsafe)  │
│    └── Lock-Free Seqlock Publisher: Thread-safe atomic snapshot publication                 │
│                                                                                             │
│  [task_can_tx] (Core 0, 100 Hz / 10 ms)                                                     │
│    ├── Lock-Free Seqlock Consumer: Reads consistent, tear-free snapshot                      │
│    ├── Mode Dispatcher: Programmatic Frame Generation via protocol/compat/can.hpp           │
│    │     ├── MODE BARE: 0x169, 0x7B9, 0x204, 0x110 (10Hz), 0x113 (10Hz)                    │
│    │     ├── MODE SYS:  0x169, 0x7B9, 0x204, 0x111 (10Hz), 0x112 (10Hz), 0x7FD (2Hz)        │
│    │     └── MODE RT:   0x303, 0x301, 0x300, 0x111 (10Hz), 0x112 (10Hz), 0x7FC (2Hz)        │
│    ├── TWAI 4-Slot TX Queue & Bus-Off Auto-Recovery                                        │
│    └── Delta-Driven Serial Telemetry Logger                                                 │
│                                                                                             │
│  [task_heartbeat] (Core 1, 10 Hz / 100 ms)                                                  │
│    └── 1 Hz Decimated Health & Status Diagnostic Logger                                     │
└─────────────────────────────────────────────┬───────────────────────────────────────────────┘
                                              │ CAN Bus (500 kbit/s Classic CAN 2.0A)
                                              │
                      ┌───────────────────────┼───────────────────────┐
                      ▼                       ▼                       ▼
            ┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐
            │   MODE 1: BARE   │    │   MODE 2: SYS    │    │   MODE 3: RT     │
            │ Direct Actuators │    │ Target sys-esp32 │    │ Target rt-esp32  │
            │    (Low-CAN)     │    │    (Low-CAN)     │    │   (High-CAN)     │
            └──────────────────┘    └──────────────────┘    └──────────────────┘
```

---

## 3. Mode CAN Matrix & Protocol Codec Specification

All messages and signal formats are bound directly to canonical generated definitions from `protocol/compat/can.hpp`:

| Signal / Demand | `BARE` Mode (Actuator Direct) | `SYS` Mode (Target `sys-esp32`) | `RT` Mode (Target `rt-esp32`) |
| :--- | :--- | :--- | :--- |
| **Steering Demand** | `0x169 VCU_SES_REQ`<br>`can::custom::ses::encode_command` | `0x169 VCU_SES_REQ`<br>`can::custom::ses::encode_command` | `0x303 HOST_STEER_CMD`<br>`can::gen::encode_host_steer_cmd` |
| **Brake Demand** | `0x7B9 VCU_SEB_REQ`<br>`can::custom::seb::encode_command` | `0x205 RT_BRAKE_CMD`<br>`can::gen::encode_rt_brake_cmd` | `0x301 HOST_BRAKE_REQ`<br>`can::gen::encode_host_brake_req` |
| **Speed & Gear** | `0x204 RT_DRIVE_CMD`<br>`can::gen::encode_rt_drive_cmd` | `0x204 RT_DRIVE_CMD`<br>`can::gen::encode_rt_drive_cmd` | `0x300 HOST_DRIVE_CMD`<br>`can::gen::encode_host_drive_cmd` |
| **Mode Request** | `0x110 SYS_MODE_CMD`<br>`can::gen::encode_sys_mode_cmd` | `0x111 HMI_MODE_REQ`<br>`can::gen::encode_hmi_mode_req` | `0x111 HMI_MODE_REQ`<br>`can::gen::encode_hmi_mode_req` |
| **Power Request** | `0x113 SYS_PWR_CMD`<br>`can::gen::encode_sys_pwr_cmd` | `0x112 HMI_PWR_REQ`<br>`can::gen::encode_hmi_pwr_req` | `0x112 HMI_PWR_REQ`<br>`can::gen::encode_hmi_pwr_req` |
| **Heartbeat** | *Emulated via 0x110/0x113* | `0x7FD RT_HEARTBEAT`<br>`can::gen::encode_rt_heartbeat` | `0x7FC HOST_HEARTBEAT`<br>`can::gen::encode_host_heartbeat` |

### Detailed Mode Descriptions

#### 3.1 `BARE` Mode
- Direct control of `SES` (steering), `SEB` (braking), and `MTR` (traction).
- Sends `VCU_SES_REQ` (0x169) with raw rack angle counts ($29550\dots 30450$).
- Sends `VCU_SEB_REQ` (0x7B9) with raw stroke request ($600\dots 1140$).
- Sends `RT_DRIVE_CMD` (0x204) with signed target velocity in mm/s and gear (`N`, `D`, `R`).
- Periodically emits `SYS_MODE_CMD` (0x110) and `SYS_PWR_CMD` (0x113) so actuators detect an active system supervisor.
- **Emulates the SYS safety authority by emitting `SYS_SAFETY_STS` (0x011, `estop_active=0`, valid AUTOSAR E2E CRC) at 10 Hz.** MTR gates ignition on `safety_state_valid_`, which is set only by `0x011` (`mtr-stm32/src/motor_manager.h:313/:194-212`); without it the motor never ignites. (Not emitted in SYS mode — the real `sys-esp32` owns `0x011` there.)

#### 3.2 `SYS` Mode
- Direct connection to `sys-esp32` on Low-CAN.
- Emulates the presence of `rt-esp32` and HMI switches.
- Emits `HMI_MODE_REQ` (0x111) and `HMI_PWR_REQ` (0x112) reflecting operator toggles.
- Emits `RT_DRIVE_CMD` (0x204) and `VCU_SES_REQ` (0x169), and expresses brake demand as `RT_BRAKE_CMD` (0x205, kPa). **`sys-esp32` is the sole `VCU_SEB_REQ` (0x7B9) producer and applies the `0x205` intent to SEB** (`sys-esp32/src/main.cpp:892-906`); rm must NOT emit `0x7B9` in SYS mode (it would collide with `sys-esp32`).
- Emits `RT_HEARTBEAT` (0x7FD) at 2 Hz to prevent `sys-esp32` heartbeat timeout faults.
- Suppresses `SYS_MODE_CMD` (0x110) and `SYS_PWR_CMD` (0x113) to avoid CAN arbitration conflict with `sys-esp32`.

#### 3.3 `RT` Mode
- Direct connection to `rt-esp32` on High-CAN.
- Emulates the autonomous `Host` vehicle computer.
- Emits `HOST_DRIVE_CMD` (0x300) with target speed in mm/s, yaw rate in mrad/s, and gear.
- Emits `HOST_STEER_CMD` (0x303) with steer angle in $0.1^\circ$ units and rolling counter.
- Emits `HOST_BRAKE_REQ` (0x301) with brake pressure in kPa (converted from millimeter demand).
- Emits `HMI_MODE_REQ` (0x111) and `HMI_PWR_REQ` (0x112) for mode and power arbitration by RT and SYS.
- Emits `HOST_HEARTBEAT` (0x7FC) at 2 Hz to keep RT host watchdog fresh.
- **Host-only: does NOT emit SYS-owned frames.** `rt-esp32` requires `READY_BIT_SAFETY(0x011) | READY_BIT_MODE(0x110) | READY_BIT_HOST(0x300)` for motion (`rt-esp32/src/safety_stream_loss.h:50-51`), but `0x011 SYS_SAFETY_STS` and `0x110 SYS_MODE_CMD` are **owned by `sys-esp32`** (`protocol/contracts/sys.yaml`). rm therefore emits only those three Host frames it owns (`0x300/0x301/0x303`) plus HMI/Host heartbeat, and **no `0x011`/`0x110`**. This also matches the real bus topology: rt consumes SYS authority only from its LOW bus (`can_rx_router.h:64-84`) and Host commands only from HIGH (`can_rx_router.h:28/36/44`); a single-bus rm on HIGH could not deliver low-bus authority anyway. **Motion authority therefore requires a real `sys-esp32` on rt's Low bus** (as the system testbench models with `SysNode`). A prior revision emitted `0x011`/`0x110` in RT mode as a bench workaround; that has been removed (Host/SYS role separation).

---

## 4. Core Software Modules & Input Pipeline

### 4.1 SBUS Protocol Parser (`sbus_parser.h`)
- **Format**: 25-byte frame received every $14\dots 20\text{ ms}$ at 100,000 baud, 8 data bits, even parity, 2 stop bits (`8E2`), active-low inverted logic.
- **Bit Unpacking**: 16 channels packed into 22 bytes ($16 \times 11\text{ bits} = 176\text{ bits}$).
- **Microsecond Mapping**:
  $$\text{Pulse } (\mu\text{s}) = 988 + \frac{\text{raw} - 172}{1811 - 172} \times (2012 - 988)$$
  Center position: $992 \approx 1500\,\mu\text{s}$.
- **Electrical Plausibility Check**: Pulse validity ($800\dots 2200\,\mu\text{s}$) is scoped across active channels 0..11 (`kChSteering` through `kChAuxVrd`).
- **Hardware Failsafe Bits**: Byte 23 bits indicate Frame Lost (`0x04`) and Receiver Failsafe (`0x08`).

---

### 4.2 Link Health & Loss Supervisor
- **Timeout**: Frames not received for $> 150\text{ ms}$ or hardware failsafe flag $\implies$ `LinkState::Lost`, `signal_valid = false`.
- **Safe Fallback**: On signal loss, target motor speed is clamped to $0\text{ mm/s}$, transmission gear shifts to Neutral (`can::Gear::N`), and park brake holding stroke ($15.0\text{ mm}$) is commanded.
- **Immediate Recovery**: As soon as valid packets resume, control is immediately restored.

---

### 4.3 Direct Switch Mapping
- **SWA Operating Mode Selector (CH5, 3-Position)**:
  - `UP` ($\le 1300\,\mu\text{s}$): `BARE` Mode (Direct Actuators on Low-CAN).
  - `MID` ($1300\dots 1700\,\mu\text{s}$): `SYS` Mode (Targeting `sys-esp32` on Low-CAN).
  - `DOWN` ($\ge 1700\,\mu\text{s}$): `RT` Mode (Targeting `rt-esp32` on High-CAN).
  - **Dynamic Runtime Switching**: Mode transitions take effect immediately in `task_can_tx`, re-routing the output cluster while preserving snapshot continuity and identical telemetry logging.
- **SWB Park / Brake Hold (CH6, 3-Position)**:
  - `UP` ($\le 1300\,\mu\text{s}$): Park Engaged (`park_hold_req = true`, $15.0\text{ mm}$ holding stroke, Neutral gear, $0\text{ mm/s}$).
  - `MID` / `DOWN` ($> 1300\,\mu\text{s}$): Park Released (`park_hold_req = false`, $0\text{ mm}$ stroke).
- **SWC Gear Selector (CH7, 3-Position)**:
  - `UP` ($\le 1300\,\mu\text{s}$): Reverse (`Gear::R`).
  - `MID` ($1300\dots 1700\,\mu\text{s}$): Neutral (`Gear::N`).
  - `DOWN` ($\ge 1700\,\mu\text{s}$): Drive (`Gear::D`).
  - **State Preservation**: Physical switch selection is preserved in `snap.gear` regardless of park hold status. `drive_cmd.gear` emits `snap.gear` whenever drive is active (`ARM:ON`), defaulting to `Gear::N` when disarmed or parked.
- **SWD Drive Enable (CH8, 2-Position)**:
  - `UP` ($< 1500\,\mu\text{s}$): Disabled (`drive_enable_req = false`).
  - `DOWN` ($\ge 1500\,\mu\text{s}$): Enabled (`drive_enable_req = true`).
  - Drive is active whenever `drive_enable_req && !park_hold_req && signal_valid`.

---

### 4.4 Steering Dynamics & Continuous Deadband
- **Deadband**: $\pm 30\,\mu\text{s}$ around center ($1500\,\mu\text{s}$). Within deadband, rack angle is strictly $0.0^\circ$.
- **Smooth Normalization**: Beyond the deadband, offset is subtracted so the angle ramps continuously from $0.0^\circ$:
  $$\text{norm} = \frac{|\text{steer\_offset}| - 30}{450 - 30}, \quad \theta = \text{sgn}(\text{steer\_offset}) \times \text{norm} \times 45.0^\circ$$
- **Limits**: Clamped strictly to $\pm 45.0^\circ$ ($29550\dots 30450$ raw CAN counts in BARE/SYS; $-450\dots +450$ in $0.1^\circ$ in RT mode).
- **Steer While Parked**: Steer setpoints remain active while parked to allow tire pre-alignment.

---

### 4.5 Throttle, Governor & Braking Pipeline
- **Motor Throttle (CH3 Left Stick Vertical)**:
  - Bottom idle deadband up to $1160\,\mu\text{s}$ ($0\%$ demand).
  - Linear from $1160\,\mu\text{s}$ to $1950\,\mu\text{s}$ ($0\dots 100\%$).
  - Scaled proportionally by **Dynamic Speed Governor (VRA, CH9)**:
    - Drive: $0\dots (+3000\text{ mm/s} \times \text{GovScale})$.
    - Reverse: $0\dots (-500\text{ mm/s} \times \text{GovScale})$.
    - Neutral: strictly $0\text{ mm/s}$.
- **Service Brake (CH2 Right Stick Vertical - Wide Deadband)**:
  - Center neutral deadband: $1280 \dots 1720\,\mu\text{s}$ ($\pm 220\,\mu\text{s}$ around center) preventing accidental braking while steering.
  - Forward push ($1720 \dots 1980\,\mu\text{s}$): commands $0.0 \dots 27.0\text{ mm}$ stroke.
- **Auxiliary Pull-Down Service Brake (CH11 VRC & CH12 VRD)**:
  - Pull-down deadband: $0 \dots -10\%$ ($1500 \dots 1450\,\mu\text{s}$) commands $0.0\,\text{mm}$.
  - Active pull-down: $-10\% \dots -100\%$ ($1450 \dots 1000\,\mu\text{s}$) commands progressive $0.0 \dots 27.0\,\text{mm}$ service brake.
  - Overall brake stroke is arbitrated by taking the maximum among Right Stick (Y), VRC, VRD, and SWB Park Hold.
- **Brake-Over-Throttle Interlock**: Total brake stroke $> 5.0\text{ mm}$ instantly clamps motor throttle to $0$.

---

## 5. Tasks & FreeRTOS Architecture

| Task Name | Priority | Core | Period | Purpose |
| :--- | :---: | :---: | :---: | :--- |
| `task_rc_capture` | 8 | Core 1 | Event-driven | Blocks on UART1 FIFO with 10ms timeout, decodes SBUS immediately upon frame arrival |
| `task_can_tx` | 4 | Core 0 | $10\text{ ms}$ (100 Hz) | Reads snapshot, encodes active mode CAN command cluster, runs TWAI recovery, logs telemetry |
| `task_heartbeat` | 1 | Core 1 | $100\text{ ms}$ (10 Hz) | 1 Hz health summary & TWAI status diagnostic logger |

### Telemetry Logging & Serial Feedback
- **Delta-Triggered Logging**: Immediate serial output on change in steer ($\ge 1.0^\circ$), brake ($\ge 0.5\,\text{mm}$), throttle ($\ge 5\%$), governor ($\ge 5\%$), speed ($\ge 50\,\text{mm/s}$), gear (`[R]/[N]/[D]`), drive enable (`ARM`), park hold (`PRK`), or mode (`MOD`).
- **Periodic Decimated Telemetry**: 2 Hz summary logging (`kCanLogDecimation = 50` ticks = 500 ms) ensures continuous feedback on stationary controls.
- **Single-Line Serial Format**: `STR:%+5.1f BRK:%4.1f  THR:%3.0f%% GOV:%3.0f%% MTR:%+5ld[%s]  ARM:%-3s PRK:%-4s  MOD:%-4s RF:%s`

---

## 6. Verification & Automated Test Suite

A standalone native C++17 unit test suite (`test/test_rm_t12d_suite.cpp`) validates:
1. SBUS frame parsing and timing checks.
2. Steering deadband and smooth curve normalization.
3. Throttle, speed, gear selection, and brake-over-throttle interlock.
4. CAN frame encoding for all three modes (`BARE`, `SYS`, `RT`) directly against generated protocol codecs.
5. Failsafe safe stops and park brake hold across all modes.

```powershell
C:\TDM-GCC-64\bin\g++.exe -std=c++17 -Wall -Wextra -Irm-esp32-t12d/src -I. -Ishared rm-esp32-t12d/src/sbus_parser.cpp rm-esp32-t12d/test/test_rm_t12d_suite.cpp -o rm-esp32-t12d/test/test_rm_t12d_suite.exe
.\rm-esp32-t12d\test\test_rm_t12d_suite.exe
```
