# E-Trike Distributed Architecture & End-to-End Data Flow

## 1. Executive Summary & Distributed Network Topology

The E-Trike compute platform is partitioned across **distributed, specialized electronic control units (ECUs)** and smart drive-by-wire actuators. Real-time motion control, safety authority, and autonomous planning operate on physically separated microcontrollers connected via dual isolated **500 kbit/s Classical CAN (CAN 2.0A)** buses:

- **High CAN Bus (500 kbit/s)**: Point-to-point link connecting the high-level autonomy host (**Jetson Orin**) and the real-time kinematics gateway (**RT ESP32-S3**).
- **Low CAN Bus (500 kbit/s)**: Primary vehicle actuation and safety bus connecting **RT**, **SYS** (Safety & Mode Manager), **MTR** (STM32G431 Motor & Relay Controller), **RM** (ESP32 FlySky RC Gateway), **SES** (EPS-C Steer-by-Wire), **SEB** (Smart Electronic Brake), and auxiliary nodes (**PWT**).

```
 ┌────────────────────────────────────────────────────────┐
 │                   Jetson Orin NX                       │
 │  Perception, Path Planning, Localization, ROS 2 Stack  │
 └───────────────────────────┬────────────────────────────┘
                             │
                             │ High CAN (500 kbit/s)
                             │   [0x300, 0x301, 0x302, 0x7FC]
                             v
 ┌────────────────────────────────────────────────────────┐
 │                    RT ESP32-S3                         │
 │     Kinematics (Tricycle), PID Speed, Dual-Bus Gateway │
 └───────────────────────────┬────────────────────────────┘
                             │
                             │ Low CAN (500 kbit/s)
 ┌───────────────┬───────────┴───────────┬────────────────┬───────────────┐
 │               │                       │                │               │
 v               v                       v                v               v
┌─────────┐    ┌─────────────┐     ┌───────────┐    ┌───────────┐   ┌───────────┐
│SYS ESP32│    │  RM ESP32   │     │ MTR STM32 │    │ SES EPS-C │   │  SEB Brake│
│Safety & │    │  FlySky RC  │     │ Traction  │    │ Steer-by- │   │ Brake-by- │
│Mode Auth│    │  Gateway    │     │ & Relays  │    │ Wire      │   │ Wire      │
└─────────┘    └─────────────┘     └───────────┘    └───────────┘   └───────────┘
```

---

## 2. Core ECU Responsibilities & Technical Specs

| Node | Silicon & Clock | Operating System | Primary Functions & Hardware Authority | Non-Goals / Strict Boundaries |
| :--- | :--- | :--- | :--- | :--- |
| **Jetson** | Orin NX (Arm Cortex-A78AE + Ampere GPU) | Ubuntu Linux + ROS 2 Autoware | Camera/LiDAR perception, global path planning, high-level velocity (`0x300`) and steering requests | No hard real-time guarantees ($>10\,\text{ms}$ jitter). Cannot drive actuators or relays directly. |
| **RT** | ESP32-S3 Dual-Core @ 240 MHz | FreeRTOS (ESP-IDF 5.5) | Dual-CAN gateway, tricycle kinematics solver, PID closed-loop speed control, SES steering safety clamp | Does not switch battery relays, evaluate physical driver key switches, or read analog brake levers. |
| **SYS** | ESP32-S3 Dual-Core @ 240 MHz | FreeRTOS (ESP-IDF 5.5) | Master safety state machine, mode authority (`0x110`), physical ignition & E-Stop reading, PCR3 contactor cut | Does not solve vehicle kinematics, calculate steering geometry, or control traction motor PWM. |
| **MTR** | STM32G431CBU6 Cortex-M4F @ 16 MHz | Bare-Metal HAL (C++17) | Converts `0x204` speed to MCP4725 I2C DAC ($0.8\text{V}\dots 2.4\text{V}$), interlocking 72V relays (PA0/PA2/PA4) | Does not command steering angle or evaluate global obstacle distances. |
| **RM** | ESP32 Dual-Core @ 240 MHz | FreeRTOS (ESP-IDF 5.5) | Decodes 6-channel RMT pulses from FlySky FS-i6, converts SWC gear and stick inputs to canonical CAN | Does not execute closed-loop speed regulation or override SYS safety states. |
| **SES** | Smart Steer Actuator (EPS-C) | Internal ECU | Closed-loop steering column positioning based on `0x169 VCU_SES_REQ`, status feedback on `0x201` | Does not compute steering trajectory or dynamic rollover bounds. |
| **SEB** | Smart Brake Actuator | Internal ECU | Electromechanical brake cylinder pressure ($0\dots 27\,\text{mm}$) based on `0x7B9 VCU_SEB_REQ`, status on `0x721` | Does not arbitrate driver vs autonomous authority. |

---

## 3. End-to-End Data Flow Pipelines

### Flow A: Autonomous Mode (Jetson $\rightarrow$ RT $\rightarrow$ Actuators)

```
[Jetson Orin]
   │  0x300 HOST_DRIVE_CMD (target speed mm/s, curvature rad/m) [High CAN, 50 Hz]
   │  0x301 HOST_BRAKE_REQ (target pressure kPa)                [High CAN, 50 Hz]
   ▼
[RT ESP32-S3]
   ├─► Validates command age (< 200 ms timeout)
   ├─► Evaluates ultrasonic obstacle speed clamp (0.5 m → stop, 2.0 m → 30% clamp)
   ├─► Tricycle Kinematics:
   │     δ_steer = atan(L * curvature / (1 - 0.5 * W * curvature))
   │     v_motor = target_speed * (1 + 0.5 * W * curvature)
   ├─► Speed PID closed-loop against encoder feedback (PCNT)
   ├─► Safety clamping: Steer rate limit (180 deg/s), angle bounds (±450°)
   │
   ├─► Low CAN 0x169 VCU_SES_REQ  ──► [SES Steer Actuator] (Turns front fork)
   ├─► Low CAN 0x204 RT_DRIVE_CMD ──► [MTR STM32] (Sets MCP4725 DAC + Relays)
   └─► Low CAN 0x7B9 VCU_SEB_REQ  ──► [SEB Brake Actuator] (Clamps brake disks)
```

1. **Host Generation**: Jetson calculates path curvature $\kappa$ and target velocity $v$. It broadcasts `0x300` on High CAN.
2. **Gateway Reception & Validation**: RT's MCP2515 SPI controller receives `0x300`. RT checks heartbeat `0x7FC` and command staleness ($<200\,\text{ms}$).
3. **Safety Clamping**: RT evaluates front ultrasonic sensors. If obstacle $<0.5\,\text{m}$, forward target speed is clamped to $0\,\text{mm/s}$.
4. **Kinematic Translation**:
   - $\delta_{\text{steer}} = \arctan\left(\frac{L \cdot \kappa}{1 - 0.5 \cdot W \cdot \kappa}\right)$ where $L = 1.35\,\text{m}$, $W = 0.88\,\text{m}$.
   - Target steering angle is rate-limited to $180^\circ/\text{s}$ and clamped within $[-450^\circ, +450^\circ]$.
5. **Actuator Dispatch**:
   - RT transmits `0x169` (`VCU_SES_REQ`) at 50 Hz to the steering actuator (SES).
   - RT transmits `0x204` (`RT_DRIVE_CMD`) at 50 Hz to the motor node (MTR).
   - RT transmits `0x7B9` (`VCU_SEB_REQ`) at 50 Hz to the electronic brake (SEB).

---

### Flow B: Remote Control Mode (FlySky FS-i6 $\rightarrow$ RM $\rightarrow$ Vehicle)

```
[FlySky FS-i6 Transmitter]
   │ 2.4 GHz AFHDS 2A RF link
   ▼
[FS-iA6 Receiver]
   │ 6-Channel PWM pulses (1000 µs – 2000 µs)
   ▼
[RM ESP32 (Remote Control Gateway)]
   ├─ CH0 (GPIO 18): Steering Stick  ──► ±450.0° angle setpoint
   ├─ CH1 (GPIO 19): Brake Stick     ──► 0 – 27 mm stroke setpoint
   ├─ CH2 (GPIO 14): VRA Potentiometer─► 0 – 100% speed trim clamp
   ├─ CH4 (GPIO 13): SWB 2-Pos Switch ─► Ignition ON / OFF
   └─ CH5 (GPIO 4):  SWC 3-Pos Switch ─► Gear Selector (UP=Rev, MID=Neutral, DOWN=Drive)
   │
   ├─► Low CAN 0x111 HMI_MODE_REQ (Gear D/N/R, Target Mode)
   ├─► Low CAN 0x112 HMI_PWR_REQ  (Ignition Relay Request)
   ├─► Low CAN 0x169 VCU_SES_REQ  (Manual Steer Angle Target) ──► [SES Steer]
   ├─► Low CAN 0x204 RT_DRIVE_CMD (Direct Speed Target)      ──► [MTR STM32]
   └─► Low CAN 0x7B9 VCU_SEB_REQ  (Direct Brake Target)      ──► [SEB Brake]
```

1. **Driver Input**: The operator toggles FlySky switches and moves control gimbals.
2. **Pulse Decoding**: The ESP-IDF RMT hardware peripheral samples pulse widths with $1\,\mu\text{s}$ precision:
   - Center deadband: $\pm 30\,\mu\text{s}$ suppresses analog jitter around $1500\,\mu\text{s}$.
   - Hysteresis filter prevents rapid bouncing on switch thresholds.
3. **Control Arbitration**:
   - `SWB` controls ignition state via `0x112`.
   - `SWC` (3-position switch) selects Reverse ($1000\,\mu\text{s}$), Neutral ($1500\,\mu\text{s}$), or Drive ($2000\,\mu\text{s}$) via `0x111`.
   - Right stick X-axis directly commands steering angle `0x169` to SES.
   - Left stick Y-axis commands brake cylinder displacement `0x7B9` to SEB.
   - Forward speed target (`0x204`) is modulated by the `VRA` potentiometer dial ($0\%\dots 100\%$).

---

### Flow C: Motor Actuation & Relay Control (MTR STM32 Node)

```
[Low CAN Bus]
   │  0x204 RT_DRIVE_CMD (speed_mmps, gear: N/D/S/R)
   │  0x110 SYS_MODE_CMD (MANUAL, AUTO, ESTOP)
   │  0x001 SAFETY_ESTOP (DLC 0)
   ▼
[MTR STM32G431]
   ├─ Watchdog Monitor: Command age > 200 ms ──► CUT SPEED TO 0
   ├─ Interlocking Relay FSM:
   │    Switching between D (PA2) and R (PA0) enforces:
   │    1. Command speed = 0 mm/s
   │    2. De-energize current relay
   │    3. Enforce 200 ms Deadtime Delay
   │    4. Energize target relay
   │
   ├─ Throttle Calculation:
   │    DAC_Code = Map(speed_mmps, 0..3000 mmps) ──► 0.8 V .. 2.4 V
   │    Bit-bang SW-I2C ──► MCP4725 DAC Output
   │
   ├─ 100 Hz Telemetry: 0x120 SYS_THROTTLE_STS (speed_mmps)
   └─ 50 Hz Feedback:   0x206 MTR_MOTOR_FBK (actual speed, gear, fault flags)
```

1. **CAN Reception**: STM32 FDCAN1 receives `0x204`, `0x110`, and `0x001`.
2. **Safety Watchdog**: If `0x204` is not received within $200\,\text{ms}$, speed target collapses to $0\,\text{mm/s}$.
3. **Relay Mutual Exclusion**:
   - Drive Relay (PA2) and Reverse Relay (PA0) are strictly mutually exclusive.
   - A directional change triggers a $200\,\text{ms}$ neutral deadtime to prevent arcing 72V contacts.
4. **Throttle Output**: Commanded speed is scaled to the motor controller's analog input range ($0.8\,\text{V} \dots 2.4\,\text{V}$) and sent to the MCP4725 12-bit DAC over software I2C (PA5/PA7).

---

### Flow D: Emergency Stop & Safety Contactor Trip (Global ESTOP)

```
[Trigger Source]
  (Physical E-Stop / Software Fault / Jetson Crash / Watchdog Timeout)
   │
   ├─► Hardwired E-Stop Button ──► Physically opens PCR3 Main Contactor (Cuts 72V Traction)
   │
   └─► CAN Frame 0x001 SAFETY_ESTOP (DLC 0 wire contract)
         │
         ├──► [SYS ESP32]: Asserts PCR3 trip GPIO, broadcasts 0x110 (ESTOP mode)
         ├──► [RT ESP32]:  Freezes kinematics, commands 0x169 steer to 0°, sets speed to 0
         ├──► [MTR STM32]: Immediate relay de-energization (PA0=SET, PA2=SET, PA4=SET),
         │                 forces DAC to 0.0 V, asserts Gap #15 ESTOP confirmation on 0x206
         ├──► [SES Steer]: Internal torque de-rate to safe limp state
         └──► [SEB Brake]: Clamps brakes to maximum hold pressure (5000 kPa)
```

1. **Detection**: Initiated by hardware switch, watchdog timeout, CAN bus-off, or host disconnection.
2. **CAN Broadcast**: `0x001 SAFETY_ESTOP` is injected onto both High and Low buses.
3. **Hardware Contactor Trip**: SYS drives the PCR3 relay control pin, cutting 72V traction power.
4. **Actuator Reaction**:
   - MTR cuts relay outputs and sets DAC to $0.0\,\text{V}$.
   - SEB engages full mechanical braking pressure to bring the vehicle to an immediate halt.
   - RT centers steering and holds the zero velocity setpoint.

---

## 4. Complete Canonical CAN Message Matrix

| CAN ID | Hex | Name | Source | Destination | Bus | Rate | DLC | Payload & Key Signals |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | `0x001` | `SAFETY_ESTOP` | ANY | ALL | Both | Event | 0 | **0-byte wire contract**. Immediate vehicle shutdown. |
| **17** | `0x011` | `SYS_SAFETY_STATUS` | SYS | RT, Jetson | Low $\rightarrow$ High | 20 Hz | 2 | Byte 0: State (`BOOT`, `READY`, `ESTOP`), Byte 1: Fault bits |
| **18** | `0x012` | `SYS_DCDC_CMD` | SYS | DC-DC | Low | 10 Hz | 1 | `0x01` = Enable 48V/12V DC-DC converter |
| **272**| `0x110` | `SYS_MODE_CMD` | SYS | ALL | Low | 20 Hz | 1 | `0 = MANUAL`, `1 = AUTO`, `2 = ESTOP` |
| **273**| `0x111` | `HMI_MODE_REQ` | RM / HMI | SYS | Low | 20 Hz | 2 | Byte 0: Target mode, Byte 1: Requested gear |
| **274**| `0x112` | `HMI_PWR_REQ` | RM / HMI | SYS | Low | 10 Hz | 1 | `0x01` = Ignition power request |
| **288**| `0x120` | `SYS_THROTTLE_STS` | MTR | RT, SYS | Low $\rightarrow$ High | 100 Hz | 2 | Commanded motor speed (int16 mm/s) |
| **361**| `0x169` | `VCU_SES_REQ` | RT / RM | SES | Low | 50 Hz | 8 | Target steering angle ($\pm 450.0^\circ$, 0.1°/LSB), enable bit |
| **513**| `0x201` | `SES_STATUS` | SES | RT | Low | 100 Hz | 8 | Actual steering angle, torque feedback, motor status |
| **516**| `0x204` | `RT_DRIVE_CMD` | RT / RM | MTR | Low | 50 Hz | 5 | Speed setpoint (int32, mm/s), Gear (uint8: N/D/S/R) |
| **517**| `0x205` | `RT_BRAKE_CMD` | RT | SEB, SYS | Low | 50 Hz | 4 | Commanded brake pressure / stroke setpoint |
| **518**| `0x206` | `MTR_MOTOR_FBK` | MTR | SYS, RT | Low | 50 Hz | 4 | Actual velocity (int16), Gear state (uint8), Faults |
| **528**| `0x210` | `RT_STATE_REPORT` | RT | Jetson | High | 50 Hz | 8 | Vehicle telemetry, odometry speed, steering angle |
| **544**| `0x220` | `RT_PID_FEEDBACK` | RT | Jetson | High | 50 Hz | 8 | PID setpoint, measured speed, error, integrator term |
| **768**| `0x300` | `HOST_DRIVE_CMD` | Jetson | RT | High | 50 Hz | 8 | Target linear velocity (int16 mm/s), curvature (int16) |
| **769**| `0x301` | `HOST_BRAKE_REQ` | Jetson | RT | High | 50 Hz | 2 | Requested braking pressure (uint16 kPa) |
| **770**| `0x302` | `HOST_LIGHT_CMD` | Jetson | SYS | High $\rightarrow$ Low | 10 Hz | 2 | Turn signals, headlights, hazard light flags |
| **1024**| `0x400`| `RT_OBSTACLE_DIST`| RT | Jetson | High | 20 Hz | 4 | Ultrasonic distance sensors (cm, front left/right) |
| **1825**| `0x721`| `SEB_STATUS` | SEB | SYS, RT | Low | 10 Hz | 8 | Brake cylinder pressure, error status, limit switch |
| **1977**| `0x7B9`| `VCU_SEB_REQ` | SYS / RM | SEB | Low | 50 Hz | 8 | Target displacement (mm, 0.1mm/LSB), rolling counter, CRC |
| **2044**| `0x7FC`| `HOST_HEARTBEAT` | Jetson | RT | High | 10 Hz | 1 | Host liveness counter |
| **2045**| `0x7FD`| `RT_HEARTBEAT` | RT | SYS, Jetson| Both | 10 Hz | 1 | RT gateway and kinematics liveness counter |
| **2046**| `0x7FE`| `SYS_HEARTBEAT` | SYS | RT, MTR | Low | 10 Hz | 1 | Master safety authority liveness counter |

---

## 5. Failure Mode & Effects Analysis (FMEA)

| Failure Scenario | Detecting Node | Detection Mechanism | Immediate Action | Final Vehicle State |
| :--- | :--- | :--- | :--- | :--- |
| **Jetson Autonomy Crash** | RT ESP32 | `0x7FC` heartbeat timeout ($>200\,\text{ms}$) | RT cancels speed target ($0\,\text{mm/s}$), applies gentle deceleration | Controlled stop; RT holds steering steady |
| **RT Gateway Failure** | SYS & MTR | `0x7FD` heartbeat timeout ($>200\,\text{ms}$) | SYS trips PCR3 relay; MTR drops throttle DAC to $0\,\text{V}$ | 72V traction cut; mechanical brakes engage |
| **CAN Bus-Off on Low Bus** | Any Node | TEC counter $\ge 256$, hardware Bus-Off ISR | Automatic slot reclamation; reset peripheral; assert safe I/O | Relays open; motor de-energized |
| **Steering Following Error** | RT ESP32 | $|\delta_{\text{cmd}} - \delta_{\text{actual}}| > 15^\circ$ for $>100\,\text{ms}$ | RT commands immediate vehicle stop; broadcasts `0x001` | Controlled emergency stop |
| **Remote Control Signal Loss** | RM ESP32 | No RMT pulses for $>500\,\text{ms}$ | RM outputs zero speed and neutral gear command | Vehicle coasts down or holds neutral |
| **Motor Stall / Throttle Short** | MTR STM32 | Loss of `0x204` CAN frames ($>200\,\text{ms}$) | Internal hardware timeout resets DAC code to 0 | Motor unpowered |
