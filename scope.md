# Autonomous E-Trike Project Scope & Hardware Limitations

This document provides a comprehensive, codebase-verified definition of the project vision, physical platform specifications, electrical signals, operational scope, and engineering limitations for the **Autonomous E-Trike Platform**.

---

## 1. Project Vision & Primary Goal

The primary goal of this project is to develop a low-speed, autonomous three-wheeled electric vehicle (E-Trike) platform retrofitted from a commercial **Bajaj delta-tricycle chassis**. 

The vehicle is designed for autonomous ground transport, closed-track mobility testing, driverless teleoperation, and robotics research. It integrates a high-level autonomy stack (Jetson Orin running ROS 2 Autoware) with a deterministic real-time microcontroller control network (ESP32-S3 and STM32 nodes).

---

## 2. Hardware & Physical Platform Specifications

### 2.1 Chassis & Geometry
- **Chassis Base:** Retrofitted commercial **Bajaj delta-tricycle** (1 front wheel, 2 rear wheels).
- **Wheelbase ($L$):** $1500\text{ mm}$ ($1.5\text{ m}$).
- **Rear Track Width ($w$):** $800\text{ mm}$ ($0.8\text{ m}$).
- **Rear Wheel Radius ($r_r$):** $200\text{ mm}$ ($0.2\text{ m}$).

### 2.2 Power & Propulsion System
- **Traction Battery:** **72V High-Voltage Lithium/Lead-Acid Pack** providing main DC traction power.
- **Control Power Supply:** **72V-to-12V DC-DC Converter** stepping traction voltage down to a stable 12V DC rail for ECUs, CAN transceivers, lighting, and low-voltage actuators. Controlled via direct extended CAN command `0x10262B27` (250 kbps powertrain bus).
- **Traction Motor:** **Single High-Voltage Central Electric Motor** driving a solid rear axle equipped with a **mechanical differential**.
- **Throttle Actuation:** **MCP4725 I2C DAC** (12-bit resolution, 0–5V analog output, address `0x60`) driving the motor controller's analog throttle input.
  - *MANUAL Mode:* Direct pass-through from physical throttle grip ADC (0–5V 12-bit) with 200 raw deadzone.
  - *AUTO Mode:* Voltage scaled linearly from commanded speed (`dac_value = |speed_mmps| / 3000 * 4095`).
  - *ESTOP Mode:* Instantly zeros DAC output ($0\text{V}$, 1 k$\Omega$ internal pulldown) within $<1\text{ ms}$.
- **Gear Relays:** **72V Optocoupled Relays (TLP281)** driving Drive (D), Sport (S), and Reverse (R) contactor lines on the motor controller.

### 2.3 Actuators & Steer-by-Wire
- **Front Steering Actuator (SES):** Commercial **EPS-C Steer-by-Wire unit** mounted on the single front steering column (`0x169 VCU_SES_REQ` @ 50 Hz). Position encoded with $30000$ raw offset ($0.1^\circ/\text{LSB}$). Dynamic slew rate limits: $125^\circ/\text{s} \to 525^\circ/\text{s}$.
- **Rear Braking Actuator (SEB):** Commercial **Smart Electronic Brake (SEB)** applying electromechanical/hydraulic pressure up to $5000\text{ kPa}$ (5 MPa) to the rear wheels (`0x7B9 VCU_SEB_REQ` @ 50 Hz).
  - *Pressure Scaling:* `seb_raw = brake_kpa * 0.02f` ($0.05\text{ MPa/bit}$, range $0-100\text{ raw}$).
  - *Pressure Mode:* Used in `AUTO` mode for software pressure requests (`0x205` / `0x7B9`).
  - *Stroke Mode:* Used in `MANUAL` mode (manual brake lever input) and `ESTOP` (maximum mechanical stroke).

### 2.4 ECU Network Topology
- **Jetson Orin (Host Compute):** High-level ROS 2 autonomy, LiDAR/camera perception, global path planning, emitting `0x300 HOST_DRIVE_CMD`.
- **RT ESP32-S3 (Real-Time Kinematics & Gateway):** 100 Hz inverse bicycle solver, PID speed controller, dynamic angle clamping, CAN router.
- **SYS ESP32-S3 (Safety & Body Gateway):** Mode management (`MANUAL`, `AUTO`, `ESTOP`), physical key switch / brake lever ADC inputs, lighting control (`0x302`), hardware PCR3 battery power isolation relay driver.
- **MTR STM32 (Motor Controller):** Converts digital CAN speed setpoints (`0x204`) into physical motor PWM drive and monitors physical gear inputs (D, S, R).
- **PWT ESP32-S3 (Powertrain Monitor):** Standalone CAN node monitoring battery voltage, current, and temperature telemetry.

---

## 3. Project Scope (What IS Included)

1. **Autonomous Velocity & Curvature Tracking:** Host autonomy issues linear velocity ($v$) and yaw rate ($\omega$) commands via CAN (`0x300 HOST_DRIVE_CMD`), which RT ESP32 converts into front steering angle ($\delta$) and rear motor speed setpoints.
2. **Deterministic Real-Time Safety Supervision:** 100 Hz continuous safety checks monitoring command staleness ($>500\text{ ms}$), heartbeat loss ($>1500\text{ ms}$), obstacle proximity, and following error. Immediately broadcasts `0x001 SAFETY_ESTOP` and trips hardware PCR3 battery isolation relays.
3. **Dual-Mode Governance (ISO 26262 Alignment):** Gated operational modes (`MANUAL` vs `AUTO`). In `MANUAL` mode, RT software actuation is strictly muted on CAN (`0x169`, `0x204`), giving full control to the human driver.
4. **Dynamic Rollover Protection & Obstacle Limiting:** Speed-dependent dynamic angle clamping ($40^\circ \to 5^\circ$ up to $25\text{ km/h}$) to prevent cornering tip-over, and automatic speed reduction based on obstacle distance ($300\text{ mm} \to 3000\text{ mm}$).
5. **Software-in-the-Loop (SIL) & Diagnostic Tooling:** Native C++ SIL engine (`sim_engine_native`), TypeScript multi-ECU simulation harness (`simulation/`), and `control-toolkit` FastAPI diagnostic backend.

---

## 4. Physical Limitations & Engineering Constraints (What IS NOT Included)

### 4.1 Single Front Wheel Steering & Rollover Instability (Delta Trike Geometry)
- **Physical Issue:** Delta trikes (1 front wheel, 2 rear wheels) have a narrow triangular support polygon. High lateral acceleration ($a_y = \frac{v^2}{L}\tan\delta$) during sharp cornering creates severe rollover risk compared to 4-wheel vehicles or tadpole trikes.
- **Engineering Constraint:** Maximum forward speed is strictly clamped ($25\text{ km/h}$ maximum), and allowable front steering angle is dynamically restricted at speed ($40^\circ$ at $\le 2\text{ km/h}$ down to $5^\circ$ at $\ge 25\text{ km/h}$).

### 4.2 Single Traction Motor & Rear Mechanical Differential
- **Physical Issue:** The vehicle relies on a single central motor driving a solid rear axle with a mechanical differential. There is **no independent dual-motor torque vectoring** or active differential control.
- **Engineering Constraint:** Longitudinal speed control is strictly single-input single-output (SISO). Wheel speed differences during cornering ($v_{\text{left}}$ vs $v_{\text{right}}$) are handled purely mechanically by the rear differential.

### 4.3 No Spin-In-Place / Non-Holonomic Steering Limit
- **Physical Issue:** Unlike differential-drive or skid-steer mobile robots, a single front wheel steerable tricycle cannot spin in place ($v=0, \omega \ne 0$).
- **Engineering Constraint:** Near-zero speed turning ($|v| \le 50\text{ mm/s}$) sets steering to full lock ($\pm 40^\circ$) to prepare steer geometry before forward speed is applied, preventing uncommanded forward lurching.

### 4.4 Open-Loop RT Steering (No Local ECU Path Drift Correction)
- **Physical Issue:** The RT ESP32 kinematics resolver converts host yaw rate into steer angle open-loop. It does **not** perform local closed-loop steering drift correction for road slope, side winds, or tire slip.
- **Engineering Constraint:** Global path correction and drift compensation must be handled upstream by ROS 2 perception and localization on the Jetson Orin host.

### 4.5 72V Inductive Load & Contactor Interlocks
- **Physical Issue:** Switching 72V gear relays (Drive, Sport, Reverse) under heavy motor load causes electrical arcing and contactor degradation.
- **Engineering Constraint:** Gear switching in `AUTO` mode is software-interlocked to standstill ($|v| < 50\text{ mm/s}$).

### 4.6 Classical CAN 2.0B Bandwidth Limits
- **Physical Issue:** The network operates on Classical CAN 2.0B at 500 kbps (8-byte max DLC, no CAN FD).
- **Engineering Constraint:** High-frequency host chatter is isolated on High CAN, while safety-critical actuator frames run on a separate Low CAN bus at 50 Hz.

---

## 5. Safety Timeouts, Grace Periods & Operating Boundaries

| Safety Parameter | Value / Threshold | Codebase Constant / Source | Hardware Action upon Violation |
| :--- | :--- | :--- | :--- |
| **Startup Grace Period** | $3000\text{ ms}$ (3 s) | `kStartupGracePeriodMs` ([shared_config.h](file:///e:/work/etrike/shared/shared_config.h#L25)) | Suppresses stale command faults during initial boot sensor sync |
| **Host Command Timeout** | $500\text{ ms}$ | `kHostCmdStaleTimeoutMs` ([shared_config.h](file:///e:/work/etrike/shared/shared_config.h#L23)) | Broadcasts `0x001 SAFETY_ESTOP`, trips PCR3 relay |
| **CAN Heartbeat Timeout** | $1500\text{ ms}$ | `kHeartbeatTimeoutMsHost` ([shared_config.h](file:///e:/work/etrike/shared/shared_config.h#L24)) | Broadcasts `0x001 SAFETY_ESTOP`, trips PCR3 relay |
| **MTR Command Staleness** | $200\text{ ms}$ | `kMtrFaultCmdTimeout` ([shared_config.h](file:///e:/work/etrike/shared/shared_config.h#L46)) | Zeros MCP4725 DAC output, sets neutral gear (controlled stop) |
| **E-Stop Broadcast Rate Limit** | $\ge 250\text{ ms}$ | `kEstopBroadcastMinIntervalUs` ([shared_config.h](file:///e:/work/etrike/shared/shared_config.h#L26)) | Rate-limits `0x001` frames to prevent CAN bus flooding |
| **ESTOP Steering Centering** | $20^\circ/\text{s}$ | `kSteerEstopRampDegS` ([config.h](file:///e:/work/etrike/rt-esp32/src/config.h#L40)) | Ramps front wheel to straight ($0^\circ$) before mode handoff |
| **Max Forward Speed** | $10.8\text{ km/h}$ ($3000\text{ mm/s}$) | `kMaxSpeedFwdMmps` ([shared_config.h](file:///e:/work/etrike/shared/shared_config.h#L18)) | Clamps motor speed setpoint in solver |
| **Max Reverse Speed** | $1.8\text{ km/h}$ ($500\text{ mm/s}$) | `kMaxSpeedRevMmps` ([shared_config.h](file:///e:/work/etrike/shared/shared_config.h#L19)) | Clamps reverse motor speed setpoint in solver |
| **Steering Angle Limits** | $\pm 40.0^\circ \to \pm 5.0^\circ$ | `compute_dynamic_limit()` ([physics_model.cpp](file:///e:/work/etrike/rt-esp32/src/physics_model.cpp#L23)) | Clamps front steer angle based on current vehicle speed |

---

## 6. Verification & Test Strategy

- **Software Track (Virtual / Hosted):** Automated CTest suite (`native-test/`), TypeScript virtual CAN harness (`simulation/`), and `control-toolkit` FastAPI REST/WebSocket tests. Allows testing full kinematics, codecs, and safety state machines without physical hardware risk.
- **Hardware Track (Bench / Vehicle):** CANalyst-II adapter testing, bench ECU hardware loopback, low-speed closed-track vehicle testing, and physical E-Stop relay isolation verification.
