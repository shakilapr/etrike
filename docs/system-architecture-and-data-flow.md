# E-Trike Distributed Architecture & End-to-End Data Flow

## 1. Executive Summary & Dual Operating Modes

The E-Trike compute platform is partitioned across **distributed, specialized electronic control units (ECUs)** and smart drive-by-wire actuators. Real-time motion control, safety authority, and autonomous planning operate on physically separated microcontrollers connected via dual isolated **500 kbit/s Classical CAN (CAN 2.0A)** buses:

- **High CAN Bus (500 kbit/s)**: Dedicated link connecting the high-level autonomy host (**Jetson Orin**) and the real-time kinematics gateway (**RT ESP32-S3**).
- **Low CAN Bus (500 kbit/s)**: Primary vehicle actuation and safety bus connecting **RT**, **SYS** (Safety & Mode Manager), **MTR** (STM32G431 Motor & Relay Controller), **RM** (ESP32 FlySky RC Gateway), **SES** (EPS-C Steer-by-Wire), **SEB** (Smart Electronic Brake), and auxiliary nodes (**PWT**).

The vehicle supports **two distinct, mutually independent operational topologies**:

1. **Topology 1: Autonomous / Host-Supervised Control Hierarchy (Host $\rightarrow$ RT $\rightarrow$ SYS $\rightarrow$ Actuators)**
   - Used during full autonomous navigation and software drive-by-wire.
   - The Jetson Orin plans trajectories, RT solves kinematics and closed-loop speed regulation, SYS governs high-voltage safety interlocks, contactor state, and brake supervision, commanding **SES**, **SEB**, and **MTR**.
2. **Topology 2: Direct Remote Manual Control (RM Standalone Bypass on Low CAN)**
   - Used when operating without the Host, RT, and SYS (e.g. manual vehicle positioning, staging, field recovery, or bench testing).
   - RM connects directly to the Low CAN bus (500 kbps) and directly drives the low-level actuator ECUs (**SES**, **SEB**, and **MTR**), with fail-safe ESTOP (`0x001`) asserted if the RF transmitter link drops.

═══════════════════════════════════════════════════════════════════════════════════
TOPOLOGY 1: AUTONOMOUS / HOST-SUPERVISED HIERARCHY
═══════════════════════════════════════════════════════════════════════════════════

  ┌────────────────────────────────────────────────────────┐
  │                   Jetson Orin NX                       │
  │  Perception, Path Planning, Localization, ROS 2 Stack  │
  └───────────────────────────┬────────────────────────────┘
                              │
                              │ High CAN (500 kbit/s)
                              │   [0x300, 0x301, 0x302, 0x7FC]
                              ▼
  ┌────────────────────────────────────────────────────────┐
  │                    RT ESP32-S3                         │
  │     Kinematics (Tricycle), PID Speed, Dual-Bus Gateway │
  └───────────────────────────┬────────────────────────────┘
                              │
                              │ Low CAN (500 kbit/s)
  ┌───────────────────────────┼────────────────────────────┐
  │                           │                            │
  ▼                           ▼                            ▼
┌──────────────────┐  ┌──────────────────┐       ┌──────────────────┐
│    SYS ESP32     │  │   Actuators:     │       │    MTR STM32     │
│ Safety Authority │  │ SES Steer (0x169)│       │ Traction & Relay │
│ 72V Contactor Cut│  │ SEB Brake (0x7B9)│       │ Drive (0x204)    │
└──────────────────┘  └──────────────────┘       └──────────────────┘

═══════════════════════════════════════════════════════════════════════════════════
TOPOLOGY 2: DIRECT REMOTE MANUAL CONTROL (RM STANDALONE BYPASS ON LOW CAN)
[Host, RT, and SYS Disconnected or Powered OFF]
═══════════════════════════════════════════════════════════════════════════════════

   [FlySky FS-i6 Transmitter]
               │ 2.4 GHz AFHDS 2A RF
               ▼
   [FlySky FS-iA6 Receiver]
               │ 6-Channel PWM Pulses (GPIO 18, 19, 14, 32, 13, 4)
               ▼
   ┌───────────────────────────────────────────────────────┐
   │                     RM ESP32                          │
   │           Direct Actuator Master Gateway              │
   └───────────────────────────┬───────────────────────────┘
                               │
                               │ Low CAN Bus (500 kbit/s)
         ┌─────────────────────┼─────────────────────┐
         │                     │                     │
         ▼                     ▼                     ▼
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│   SES (EPS-C)    │  │    SEB Brake     │  │    MTR STM32     │
│ Steer-by-Wire    │  │  Brake-by-Wire   │  │ Motor Controller │
│  0x169 Target    │  │   0x7B9 Stroke   │  │ 0x204 / 0x0AA/0x0BB
└──────────────────┘  └──────────────────┘  └──────────────────┘

---

## 2. Core ECU Responsibilities & Technical Specs

| Node | Silicon & Clock | Operating System | Primary Functions & Hardware Authority | Non-Goals / Strict Boundaries |
| :--- | :--- | :--- | :--- | :--- |
| **Jetson** | Orin NX (Arm Cortex-A78AE + Ampere GPU) | Ubuntu Linux + ROS 2 Autoware | Camera/LiDAR perception, global path planning, velocity (`0x300`) and steering requests | No hard real-time guarantees ($>10\,\text{ms}$ jitter). Cannot drive actuators or relays directly. |
| **RT** | ESP32-S3 Dual-Core @ 240 MHz | FreeRTOS (ESP-IDF 5.5) | Dual-CAN gateway, tricycle kinematics solver, PID closed-loop speed control, SES steering safety clamp | Does not switch battery relays, evaluate physical driver key switches, or read analog brake levers. |
| **SYS** | ESP32-S3 Dual-Core @ 240 MHz | FreeRTOS (ESP-IDF 5.5) | Master safety state machine, mode authority (`0x110`), physical ignition & E-Stop reading, PCR3 contactor cut | Does not solve vehicle kinematics, calculate steering geometry, or control traction motor PWM. |
| **MTR** | STM32G431CBU6 Cortex-M4F @ 16 MHz | Bare-Metal HAL (C++17) | Converts `0x204` speed (or `0x0BB`/`0x0AA`) to MCP4725 I2C DAC ($0.8\text{V}\dots 2.4\text{V}$), interlocking 72V relays (PA0/PA2/PA4) | Does not command steering angle or evaluate global obstacle distances. |
| **RM** | ESP32 Dual-Core @ 240 MHz | FreeRTOS (ESP-IDF 5.5) | Decodes 6-channel RMT pulses from FlySky FS-i6. In Standalone Mode, directly drives SES (`0x169`), SEB (`0x7B9`), and MTR (`0x204`/`0x0BB`/`0x0AA`) over Low CAN | Does not execute autonomous trajectory planning or closed-loop speed regulation. |
| **SES** | Smart Steer Actuator (EPS-C) | Internal ECU | Closed-loop steering column positioning based on `0x169 VCU_SES_REQ`, status feedback on `0x201` | Does not compute steering trajectory or dynamic rollover bounds. |
| **SEB** | Smart Brake Actuator | Internal ECU | Electromechanical brake cylinder displacement ($0\dots 27\,\text{mm}$) based on `0x7B9 VCU_SEB_REQ`, status on `0x721` | Does not arbitrate driver vs autonomous authority. |

---

## 3. Detailed Operational Modes & Data Flow Pipelines

### Topology 1: Autonomous Mode (Host $\rightarrow$ RT $\rightarrow$ SYS $\rightarrow$ Actuators)

In Autonomous Mode, the vehicle operates under full electronic authority:

```
[Jetson Orin]
   │  0x300 HOST_DRIVE_CMD (target speed mm/s, curvature rad/m) [High CAN, 50 Hz]
   │  0x301 HOST_BRAKE_REQ (target pressure kPa)                [High CAN, 50 Hz]
   ▼
[RT ESP32-S3]
   ├─► Validates command freshness (< 200 ms timeout)
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

[SYS ESP32]
   ├─► Monitors physical safety interlocks & PCR3 contactor
   ├─► In AUTO mode, suppresses its own 0x7B9 transmission to avoid bus collision with RT
   └─► If RT crashes or heartbeat (0x7FD) is lost: SYS takes over brake authority and trips contactor
```

1. **Host Generation**: Jetson calculates path curvature $\kappa$ and target velocity $v$. It broadcasts `0x300` on High CAN.
2. **Gateway Reception & Validation**: RT receives `0x300` over High CAN, checking host heartbeat `0x7FC` and command staleness ($<200\,\text{ms}$).
3. **Safety Clamping**: RT evaluates front ultrasonic distance. If obstacle $<0.5\,\text{m}$, speed target is clamped to $0\,\text{mm/s}$.
4. **Kinematic Translation**:
   - $\delta_{\text{steer}} = \arctan\left(\frac{L \cdot \kappa}{1 - 0.5 \cdot W \cdot \kappa}\right)$ where $L = 1.35\,\text{m}$, $W = 0.88\,\text{m}$.
   - Front fork steering angle is rate-limited to $180^\circ/\text{s}$ and clamped within $[-450^\circ, +450^\circ]$.
5. **Actuator Dispatch**:
   - RT transmits `0x169` (`VCU_SES_REQ`) at 50 Hz to SES.
   - RT transmits `0x204` (`RT_DRIVE_CMD`) at 50 Hz to MTR.
   - RT transmits `0x7B9` (`VCU_SEB_REQ`) at 50 Hz to SEB.
   - SYS supervises contactor state and safety integrity; upon any fatal error or RT timeout, SYS drops 72V traction power.

---

### Topology 2: Direct Remote Manual Control (RM Standalone Bypass on Low CAN)

In Direct Remote Manual Control, **Host, RT, and SYS are completely omitted or powered down**. The RM ESP32 connects directly to the Low CAN bus (500 kbps) and acts as the direct manual master for all three actuator ECUs:

```
[FlySky FS-i6 Transmitter]
   │ 2.4 GHz AFHDS 2A RF link
   ▼
[FS-iA6 Receiver]
   │ 6-Channel PWM pulses (1000 µs – 2000 µs)
   ▼
[RM ESP32 (Direct Standalone Master)]
   ├─ CH0 (GPIO 18): Steering Stick  ──► ±450.0° angle setpoint
   ├─ CH1 (GPIO 19): Brake Stick     ──► 0 – 27 mm stroke setpoint
   ├─ CH2 (GPIO 14): VRA Dial        ──► 0 – 100% speed trim / throttle
   ├─ CH4 (GPIO 13): SWB 2-Pos Switch ─► Ignition ON / OFF
   └─ CH5 (GPIO 4):  SWC 3-Pos Switch ─► Gear Selector (UP=Rev, MID=Neutral, DOWN=Drive)
   │
   ├─► Low CAN 0x169 VCU_SES_REQ  ──► [SES Steer] (Direct front wheel steering)
   ├─► Low CAN 0x7B9 VCU_SEB_REQ  ──► [SEB Brake] (Direct mechanical brake pull)
   ├─► Low CAN 0x204 RT_DRIVE_CMD  ──► [MTR STM32] (Canonical speed & gear target)
   ├─► Low CAN 0x0BB RELAY_STATE  ──► [MTR STM32] (Fallback relay state: 0x05=D, 0x09=R, 0x03=P)
   ├─► Low CAN 0x0AA THROTTLE_RAW ──► [MTR STM32] (Fallback 16-bit throttle to DAC)
   └─► Low CAN 0x001 SAFETY_ESTOP ──► ALL NODES   (Asserted if RC RF signal is lost)
```

1. **RF Input Capture**: The ESP-IDF RMT peripheral samples PWM pulses from the FS-iA6 receiver:
   - `CH0`: Steering right gimbal ($\pm 450.0^\circ$).
   - `CH1`: Brake left gimbal ($0\dots 27\,\text{mm}$ stroke).
   - `CH2`: Speed trim / throttle rotary potentiometer ($0\dots 100\%$).
   - `CH4`: Ignition toggle switch (`SWB`: UP = Off, DOWN = On).
   - `CH5`: Gear selector (`SWC`: UP = Reverse, MID = Neutral/Park, DOWN = Drive).
2. **Direct Actuator Dispatch over Low CAN**:
   - **SES Steering (`0x169`)**: Transmitted at 50 Hz. Angle setpoint $\theta_{\text{target}} \in [-450^\circ, +450^\circ]$. Active only when Ignition is ON and Gear is D or R.
   - **SEB Braking (`0x7B9`)**: Transmitted at 50 Hz. Stroke $0\dots 27\,\text{mm}$. When brake stick is released, stroke is $0\,\text{mm}$.
   - **MTR Motor Traction (`0x204`, `0x0BB`, `0x0AA`)**: Transmitted at 50 Hz.
     - Canonical `0x204`: Motor speed = $\text{trim} \times 3000\,\text{mm/s}$ (Drive) or $\text{trim} \times 500\,\text{mm/s}$ (Reverse), with commanded gear state.
     - Fallback `0x0BB`: Discrete relay mode byte (`0x03` = Park, `0x05` = Drive, `0x09` = Reverse).
     - Fallback `0x0AA`: 16-bit linear throttle word mapped to the MCP4725 DAC.
3. **Deadman Fail-Safe**:
   - If RC signal is lost for $>100\,\text{ms}$, RM immediately asserts `0x001 SAFETY_ESTOP` (DLC 0), clamps steering to $0^\circ$, applies maximum emergency braking ($27\,\text{mm}$ stroke), and zeroes all throttle commands.

---

### MTR Motor Node Dual-Compatibility Handling

The MTR STM32 node accommodates both operational topologies seamlessly:

```
[Low CAN Bus]
   │
   ├─► Canonical Frame: 0x204 RT_DRIVE_CMD (speed_mmps, gear) ──► Used by RT (Mode 1) & RM (Mode 2)
   ├─► Fallback Frames:  0x0BB (Relay State) + 0x0AA (Throttle) ──► Direct legacy RM bypass
   ├─► Safety Frame:    0x001 SAFETY_ESTOP                     ──► Immediate relay open & DAC to 0V
   ▼
[MTR STM32G431]
   ├─ Watchdog Monitor: Command age > 500 ms ──► CUT SPEED TO 0, GEAR TO NEUTRAL
   ├─ Interlocking Relay FSM:
   │    Switching between D (PA2) and R (PA0) enforces:
   │    1. Command speed = 0 mm/s
   │    2. De-energize current relay
   │    3. Enforce 200 ms Deadtime Delay
   │    4. Energize target relay
   │
   ├─ Throttle Calculation:
   │    DAC_Code = Map(speed_mmps, 0..3000 mmps) ──► 0.8 V .. 2.4 V (MCP4725)
   │
   ├─ 100 Hz Telemetry: 0x120 SYS_THROTTLE_STS (speed_mmps)
   └─ 50 Hz Feedback:   0x206 MTR_MOTOR_FBK (actual speed, gear, fault flags)
```

---

## 4. Complete Canonical CAN Message Matrix

| CAN ID | Hex | Name | Source | Destination | Bus | Rate | DLC | Payload & Key Signals |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | `0x001` | `SAFETY_ESTOP` | ANY / RM | ALL | Both | Event | 0 | **0-byte wire contract**. Immediate vehicle shutdown. |
| **17** | `0x011` | `SYS_SAFETY_STATUS` | SYS | RT, Jetson | Low $\rightarrow$ High | 20 Hz | 3 | Byte 0: State (`BOOT`, `READY`, `ESTOP`), Byte 1-2: Fault bits |
| **170**| `0x0AA` | `RM_THROTTLE_RAW` | RM | MTR | Low | 50 Hz | 8 | Bytes 0-1: 16-bit raw analog throttle (legacy standalone bypass) |
| **187**| `0x0BB` | `RM_RELAY_STATE` | RM | MTR | Low | 50 Hz | 8 | Byte 0: Digital gear/relay state (`0x03`=P, `0x05`=D, `0x09`=R) |
| **272**| `0x110` | `SYS_MODE_CMD` | SYS | ALL | Low | 20 Hz | 1 | `0 = MANUAL`, `1 = AUTO`, `2 = ESTOP` |
| **273**| `0x111` | `HMI_MODE_REQ` | RM / HMI | SYS | Low | 20 Hz | 2 | Byte 0: Target mode, Byte 1: Requested gear |
| **274**| `0x112` | `HMI_PWR_REQ` | RM / HMI | SYS | Low | 10 Hz | 2 | `0x01` = Ignition power request, rolling counter |
| **288**| `0x120` | `SYS_THROTTLE_STS` | MTR | RT, SYS | Low $\rightarrow$ High | 100 Hz | 2 | Commanded motor speed (int16 mm/s) |
| **361**| `0x169` | `VCU_SES_REQ` | RT / RM | SES | Low | 50 Hz | 8 | Target steering angle ($\pm 450.0^\circ$, 0.1°/LSB), XOR8 checksum |
| **513**| `0x201` | `SES_STATUS` | SES | RT | Low | 100 Hz | 8 | Actual steering angle, torque feedback, motor status |
| **516**| `0x204` | `RT_DRIVE_CMD` | RT / RM | MTR | Low | 50 Hz | 5 | Speed setpoint (int32, mm/s), Gear (uint8: N/D/S/R) |
| **517**| `0x205` | `RT_BRAKE_CMD` | RT | SEB, SYS | Low | 50 Hz | 4 | Commanded brake pressure (int32 kPa) |
| **518**| `0x206` | `MTR_MOTOR_FBK` | MTR | SYS, RT | Low | 50 Hz | 4 | Actual velocity (int16), Gear state (uint8), Faults (Gap #15 ACK) |
| **528**| `0x210` | `RT_STATE_REPORT` | RT | Jetson | High | 50 Hz | 6 | Vehicle telemetry, odometry speed, steering angle |
| **544**| `0x220` | `RT_PID_FEEDBACK` | RT | Jetson | High | 50 Hz | 6 | PID setpoint, measured speed, error, integrator term |
| **768**| `0x300` | `HOST_DRIVE_CMD` | Jetson | RT | High | 50 Hz | 8 | Target linear velocity (int16 mm/s), curvature (int16) |
| **769**| `0x301` | `HOST_BRAKE_REQ` | Jetson | RT | High | 50 Hz | 4 | Requested braking pressure (uint32 kPa) |
| **770**| `0x302` | `HOST_LIGHT_CMD` | Jetson | SYS | High $\rightarrow$ Low | 10 Hz | 1 | Turn signals, headlights, hazard light flags |
| **1024**| `0x400`| `RT_OBSTACLE_DIST`| RT | Jetson | High | 20 Hz | 4 | Ultrasonic distance sensors (mm, front left/right) |
| **1825**| `0x721`| `SEB_STATUS` | SEB | SYS, RT | Low | 10 Hz | 8 | Brake cylinder pressure, error status, limit switch |
| **1977**| `0x7B9`| `VCU_SEB_REQ` | SYS / RT / RM | SEB | Low | 50 Hz | 8 | Target displacement (mm, 0.05mm/LSB), rolling counter, XOR8 checksum |
| **2044**| `0x7FC`| `HOST_HEARTBEAT` | Jetson | RT | High | 10 Hz | 2 | Host liveness counter |
| **2045**| `0x7FD`| `RT_HEARTBEAT` | RT | SYS, Jetson| Both | 10 Hz | 2 | RT gateway and kinematics liveness counter |
| **2046**| `0x7FE`| `SYS_HEARTBEAT` | SYS | RT, MTR | Low | 10 Hz | 2 | Master safety authority liveness counter |

---

## 5. Failure Mode & Effects Analysis (FMEA)

| Failure Scenario | Active Mode | Detecting Node | Detection Mechanism | Immediate Action | Final Vehicle State |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Jetson Autonomy Crash** | Mode 1 (Auto) | RT ESP32 | `0x7FC` heartbeat timeout ($>200\,\text{ms}$) | RT cancels speed target ($0\,\text{mm/s}$), applies gentle deceleration | Controlled stop; RT holds steering steady |
| **RT Gateway Failure** | Mode 1 (Auto) | SYS & MTR | `0x7FD` heartbeat timeout ($>200\,\text{ms}$) | SYS trips PCR3 relay; MTR drops throttle DAC to $0\,\text{V}$ | 72V traction cut; mechanical brakes engage |
| **CAN Bus-Off on Low Bus** | Either | Any Node | TEC counter $\ge 256$, hardware Bus-Off ISR | Automatic slot reclamation; reset peripheral; assert safe I/O | Relays open; motor de-energized |
| **Steering Following Error** | Mode 1 (Auto) | RT ESP32 | $|\delta_{\text{cmd}} - \delta_{\text{actual}}| > 15^\circ$ for $>100\,\text{ms}$ | RT commands immediate vehicle stop; broadcasts `0x001` | Controlled emergency stop |
| **Remote Control Signal Loss** | Mode 2 (RM Standalone) | RM ESP32 | Loss of RMT pulses for $>100\,\text{ms}$ | RM broadcasts `0x001 SAFETY_ESTOP`, zeroes speed, applies full brake ($27\,\text{mm}$) | Immediate emergency stop; all actuators locked |
| **Motor Comms Timeout** | Either | MTR STM32 | Loss of CAN drive frames ($>500\,\text{ms}$) | Internal hardware timeout resets DAC code to 0 and gear to Neutral | Motor unpowered |

