# E-Trike System Unit Scopes: Hardware, Active Software, Unimplemented & Justifications

This document provides a strict, comprehensive architectural scope defining:
- **Part 1: Hardware Units** (Physical ECUs, actuators, sensors, relays, adapters, buses)
- **Part 2: Active Software Units** (Production algorithms, state machines, drivers, tasks, bridge nodes, active control-toolkit)
- **Part 3: Unimplemented Units** (Planned architecture migrations, future algorithms, desktop wrappers)
- **Part 4: Architectural & Kinematic Model Justifications** (Unicycle vs. Bicycle vs. Tricycle derivations and design rationales)

Every entry is strictly mapped to its authoritative codebase files, C++ symbols, or hardware specifications.

---

# Part 1: Hardware Units (Physical Subsystems)

## Hardware Overview Table

| Hardware Unit | Location / Hardware Spec | Voltage / Interface | Primary Physical Function | Main Non-Goal / Boundary |
| :--- | :--- | :--- | :--- | :--- |
| **Jetson Orin ECU** | High-level Autonomy Compute | 12V / CAN High (SPI) | Runs ROS 2 autonomy, perception, and High CAN command output | Does NOT directly control low-level motor PWM, steering pins, or PCR3 relays |
| **RT ESP32-S3 ECU** | Real-Time Kinematics & Gateway | 12V / CAN High & Low | Executes 100 Hz kinematics, PID speed, 50 Hz steering logic, and gateway bridging | Does NOT execute ROS 2 perception or hardwire power relay isolation |
| **SYS ESP32-S3 ECU** | Safety, Mode & Body Gateway | 12V / CAN Low, I/O | Monitors physical mode switches, brake lever ADC, PCR3 relay output, and lighting | Does NOT execute vehicle kinematics or speed PID control loops |
| **MTR STM32 ECU** | Motor Actuation Controller | High-Voltage Traction / CAN Low | Converts `0x204` speed setpoints into physical motor drive PWM and reads gear inputs | Does NOT compute vehicle kinematics, steer angles, or obstacle distances |
| **PWT ESP32-S3 ECU** | Powertrain CAN Node | 12V / CAN Low | Standalone CAN monitor node for powertrain telemetry | Does NOT command vehicle motion or arbitrate safety states |
| **CANalyst-II USB Adapter** | USB-to-CAN Interface | USB 2.0 / Dual CAN | Physical adapter connecting operator host workstation to High/Low CAN buses | Does NOT execute internal ECU state machines or safety watchdogs |
| **EPS-C Steer Actuator (SES)** | Steer-by-Wire Actuator Unit | 12V / CAN Low (`0x169`, `0x201`) | Applies motor torque to turn physical steering column based on CAN position targets | Does NOT calculate vehicle trajectory or evaluate dynamic rollover limits |
| **Smart Electronic Brake (SEB)** | Electromechanical Brake Actuator | 12V / CAN Low (`0x7B9`, `0x721`) | Applies hydraulic/mechanical brake pressure to rear wheels based on CAN pressure targets | Does NOT modulate individual wheel ABS or steer angle stability |
| **Traction Motor & Encoders** | Rear Axle Drive & PCNT Encoders | High-Voltage Motor / Quadrature GPIO | Provides physical propulsion and emits quadrature speed pulses to ESP32 PCNT pins | Does NOT perform dead-reckoning $x,y,\theta$ odometry integration |
| **PCR3 Safety Relay Circuit** | Physical Emergency Power Cut | Hardware Coil / Hardwired E-Stop | Physically cuts battery power to traction motor controller upon E-Stop trigger | Does NOT perform controlled electrical slowing or software trajectory planning |
| **Dual Physical CAN Buses** | High Bus (Host) & Low Bus (Actuators) | 500 kbps Differential (TWAI/MCP2515) | Transmits physical differential CAN signals across distributed ECU nodes | Does NOT inspect, filter, or modify CAN frame payload contents |

---

## 1.1 Jetson Orin ECU (Host Compute)
- **Hardware Spec:** Nvidia Jetson Orin compute board running Linux OS and ROS 2 Autoware framework. Connected to High CAN bus via USB/SPI CAN transceiver.
- **What It DOES:** Runs perception (LiDAR/cameras), global localization, ROS 2 path planning, and emits High CAN command frames (`0x300 HOST_DRIVE_CMD`, `0x301`, `0x302`, `0x7FC`).
- **What It DOES NOT Do:** Does NOT execute deterministic real-time control loops ($<10\text{ ms}$), drive GPIO pins directly, or interface with hardware PCR3 safety relays.

---

## 1.2 RT ESP32-S3 ECU (Real-Time Kinematics & Gateway)
- **Hardware Spec:** ESP32-S3 dual-core microcontroller. Built-in TWAI transceiver (Low CAN @ 500 kbps), SPI MCP2515 controller (High CAN @ 500 kbps), PCNT quadrature pulse inputs.
- **What It DOES:** Runs primary 100 Hz kinematics and control loop, resolves tricycle kinematics, and drives Low CAN actuators (`0x169` Steering @ 50 Hz, `0x204` Motor @ 50 Hz, `0x7B9` Brake @ 50 Hz).
- **What It DOES NOT Do:** Does NOT process heavy vision point-clouds, switch high-current battery relays directly, or read manual brake lever ADCs.

---

## 1.3 SYS ESP32-S3 ECU (Safety, Mode Management & Body Controller)
- **Hardware Spec:** ESP32-S3 dual-core microcontroller. Low CAN TWAI, ADC manual brake inputs, key switch GPIOs, PCR3 relay driver GPIO.
- **What It DOES:** Reads physical mode switches, manual brake lever ADC inputs, and lighting switches; drives the physical hardware PCR3 safety relay output pin during E-Stops; sends `0x7B9` manual brake commands.
- **What It DOES NOT Do:** Does NOT solve inverse kinematics, execute PID speed loops, or interface with High CAN bus directly.

---

## 1.4 MTR STM32 ECU (Motor Controller & Actuation Board)
- **Hardware Spec:** STM32 microcontroller integrated into traction motor drive controller. Low CAN, physical gear inputs (D, S, R), motor MOSFET H-bridge driver.
- **What It DOES:** Converts digital `0x204 RT_DRIVE_CMD` speed setpoints into high-current motor PWM; monitors physical gear inputs; cuts motor output within $200\text{ ms}$ if commands stall or E-Stop occurs.
- **What It DOES NOT Do:** Does NOT compute steer angles, solve kinematics, or communicate directly with Jetson.

---

## 1.5 PWT ESP32-S3 ECU (Powertrain CAN Node)
- **Hardware Spec:** ESP32-S3 microcontroller on Low CAN bus.
- **What It DOES:** Monitors powertrain sensors (battery pack voltage, current, temperature) and broadcasts standalone CAN telemetry.
- **What It DOES NOT Do:** Does NOT issue vehicle motion setpoints or arbitrate safety mode states.

---

## 1.6 CANalyst-II USB-to-CAN Hardware Adapter
- **Hardware Spec:** Commercial dual-channel USB-to-CAN adapter connected to operator host workstation ([canalyst.py](file:///e:/work/etrike/control-toolkit/backend/control_toolkit/transport/canalyst.py)).
- **What It DOES:** Bridges physical High/Low CAN buses to host software via native VCI USB drivers.
- **What It DOES NOT Do:** Does NOT run internal vehicle state machines or execute safety watchdogs.

---

## 1.7 EPS-C Steer-by-Wire Actuator (SES)
- **Hardware Spec:** Commercial steer-by-wire motor unit integrated into steering column. Low CAN (`0x169 VCU_SES_REQ` @ 50 Hz, `0x201 SES_STATUS` @ 100 Hz).
- **What It DOES:** Measures steering column position and outputs motor torque to achieve CAN target angles (`0x169`); reports diagnostic status on `0x201`.
- **What It DOES NOT Do:** Does NOT evaluate vehicle speed stability or accept Jetson commands without RT ECU mode gating.

---

## 1.8 Smart Electronic Brake (SEB) Actuator
- **Hardware Spec:** Electromechanical/hydraulic brake unit on rear axle. Low CAN (`0x7B9 VCU_SEB_REQ` @ 50 Hz, `0x721 SEB_STATUS` @ 10 Hz).
- **What It DOES:** Applies physical hydraulic/mechanical braking force up to $5000\text{ kPa}$ based on CAN setpoints (`0x7B9`); reports line pressure on `0x721`.
- **What It DOES NOT Do:** Does NOT modulate individual wheel ABS or calculate deceleration curves independently.

---

## 1.9 Traction Motor & Quadrature Encoders
- **Hardware Spec:** Rear axle electric drive motor equipped with dual-channel quadrature optical/Hall encoders connected to ESP32 PCNT pins.
- **What It DOES:** Provides physical propulsion and emits digital quadrature pulses proportional to wheel rotational speed.
- **What It DOES NOT Do:** Does NOT convert raw pulse counts into metric speed ($\text{mm/s}$) or calculate global vehicle coordinates.

---

## 1.10 PCR3 Safety Relay Circuit
- **Hardware Spec:** Electromechanical isolation relay between battery pack and motor controller, driven by SYS ESP32 GPIO and physical E-Stop buttons.
- **What It DOES:** Physically disconnects battery power from traction motor electronics upon E-Stop trigger independent of software state.
- **What It DOES NOT Do:** Does NOT perform gradual software speed ramping or allow software to re-energize relay while physical E-Stop button is depressed.

---

## 1.11 Dual Physical CAN Buses (High Bus & Low Bus)
- **Hardware Spec:** 500 kbps differential twisted-pair networks (High CAN for Host/RT, Low CAN for RT/SYS/Actuators).
- **What It DOES:** Provides physical differential CAN signaling and hardware bitwise arbitration for priority message delivery (`0x001` over `0x300`).
- **What It DOES NOT Do:** Does NOT inspect or modify payload contents at the physical bus layer.

---
---

# Part 2: Active Production Software Units

## Active Software Overview Table

| Software Unit | Implementation File / Symbol | Runtime Context | Output Artifact | Main Non-Goal / Boundary |
| :--- | :--- | :--- | :--- | :--- |
| **Control-Toolkit Backend** | `control-toolkit/backend/...` | Host Python App | Production Operator API & Telemetry | Does NOT execute autonomous vehicle driving algorithms |
| **Autoware Vehicle Bridge** | `jetson/.../vehicle_bridge_node.cpp` | ROS 2 C++ Node | `0x300 HOST_DRIVE_CMD` | Does NOT run real-time hardware safety checks (<10 ms) |
| **Kinematics Resolver** | `rt-esp32/src/physics_model.cpp` | RT ESP32 Core 1 | `rt::ResolvedSetpoint` | Does NOT model tire dynamic slip angles ($\alpha_f, \alpha_r$) |
| **PID Speed Controller** | `rt-esp32/src/pid_controller.h` | RT ESP32 Core 1 | Speed trim effort (mm/s) | Does NOT control steering angle or vehicle yaw rate |
| **Speed Estimator** | `rt-esp32/src/encoder_pcnt.cpp`, `calculated_speed.h` | RT ESP32 Core 1 | `g_measured_speed_mmps` | Does NOT compute global 2D $(x, y, \theta)$ odometry pose |
| **Steering Control State Machine** | `rt-esp32/src/steering_control.h` | RT ESP32 Core 1 | `0x169 VCU_SES_REQ` | Does NOT determine target steer angles independently |
| **SEB Brake Arbitrator** | `rt-esp32/src/brake_arbitration.h` | RT & SYS ESP32 | `0x7B9 VCU_SEB_REQ` | Does NOT perform per-wheel ABS modulation |
| **Safety Monitor Logic** | `rt-esp32/src/safety_monitor.h` | RT & SYS ESP32 | `0x001 SAFETY_ESTOP` | Does NOT execute gentle trajectory slowing on hard E-Stops |
| **Mode Manager & Router** | `sys-esp32/.../mode_manager.cpp`, `can_dispatch.h` | SYS & RT ESP32 | Mode state (`0x012`) | Does NOT alter payload signal values during frame forwarding |
| **FreeRTOS Queues & Atomics** | `rt-esp32/src/main.cpp`, `rt_state.h` | RT & SYS ESP32 | `xQueue`, `std::atomic` | Does NOT use blocking mutex locks inside 100 Hz control loop |
| **CAN Drivers & Codecs** | `can_driver_twai.cpp`, `can_driver_mcp2515.cpp` | RT & SYS ESP32 | Encoded CAN Frames | Does NOT execute higher-level vehicle mode state machine |
| **Protocol Codec Generator** | `protocol/` (`gen_can.py`) | Build-time Tool | `protocol.h`, Python codecs | Does NOT generate dynamic runtime memory allocations |
| **TypeScript Simulation Engine** | `simulation/src/` | Vitest / Node.js | Simulated Virtual CAN | Does NOT replace physical ECU hardware-in-the-loop testing |
| **SIL Simulation Engine** | `native-test/sim-engine/main_native.cpp` | Host Executable | JSONL Telemetry Stream | Does NOT simulate hardware electrical noise or SPI pin latency |

---

## 2.1 Control Toolkit Backend & Operator Suite (`control-toolkit`)
- **Source:** `control-toolkit/backend/control_toolkit/`
- **What It DOES:** Active production diagnostic backend and operator suite. Provides FastAPI web API, CANalyst-II adapter transport (`canalyst.py`), packet injection scheduler (`scheduler.py`), ownership leases, and SIL lifecycle management (`lifecycle.py`).
- **What It DOES NOT Do:** Does NOT run autonomous vehicle driving algorithms; does NOT allow unauthenticated direct CAN injections during AUTO mode.

---

## 2.2 Autoware Vehicle Bridge (`autoware_vehicle_bridge`)
- **Source:** [vehicle_bridge_node.cpp](file:///e:/work/etrike/jetson/src/autoware_vehicle_bridge/src/vehicle_bridge_node.cpp) & [vehicle_bridge_node.hpp](file:///e:/work/etrike/jetson/src/autoware_vehicle_bridge/include/autoware_vehicle_bridge/vehicle_bridge_node.hpp)
- **What It DOES:** ROS 2 Lifecycle C++ node subscribing to Autoware control commands; sends CAN frames `0x300 HOST_DRIVE_CMD` (`speed_mmps`, `yaw_rate_mrad_s`, `gear`), `0x301`, `0x302`, `0x7FC`; publishes `VehicleKinematicState` & `VelocityReport` topics.
- **What It DOES NOT Do:** Does NOT execute ROS 2 path planning inside this node; does NOT run real-time hardware safety watchdogs.

---

## 2.3 Kinematics Resolver & Physics Model (`rt::PhysicsModel` / `rt::DirectResolver`)
- **Source:** [physics_model.cpp](file:///e:/work/etrike/rt-esp32/src/physics_model.cpp), [direct_resolver.cpp](file:///e:/work/etrike/rt-esp32/src/direct_resolver.cpp), [resolver_config.h](file:///e:/work/etrike/rt-esp32/src/resolver_config.h)
- **What It DOES:** Solves inverse bicycle kinematics $\delta = \arctan\left(\frac{L \omega}{v}\right)$ (`PhysicsModel`); handles near-zero speeds ($|v| \le 50\text{ mm/s}$) with lock turn-in; applies dynamic angle clamping ($40^\circ \to 5^\circ$) up to $25\text{ km/h}$; scales speed for obstacle distance ($300\text{ mm} \to 3000\text{ mm}$). Supports direct yaw-to-steer scaling (`DirectResolver`).
- **What It DOES NOT Do:** Does NOT model tire dynamic slip angles ($\alpha_f, \alpha_r$); does NOT auto-correct lateral track drift.

---

## 2.4 PID Speed Controller (`rt::PidController` & `rt::SpeedController`)
- **Source:** [pid_controller.h](file:///e:/work/etrike/rt-esp32/src/pid_controller.h) & [speed_controller.h](file:///e:/work/etrike/rt-esp32/src/speed_controller.h)
- **What It DOES:** Executes 100 Hz SISO speed error correction on RT ESP32; uses derivative-on-measurement with D-term filtering (`d_filter_alpha`); applies anti-windup and integral resets on step changes $>500\text{ mm/s}$; resets on 0 speed feedback.
- **What It DOES NOT Do:** Does NOT control steering angle or vehicle yaw; does NOT run when `ETRIKE_RT_PID_MODE == 0`.

---

## 2.5 Speed Measurement & Estimator (`encoder_pcnt` & `rt::CalculatedSpeedEstimator`)
- **Source:** [encoder_pcnt.cpp](file:///e:/work/etrike/rt-esp32/src/encoder_pcnt.cpp) & [calculated_speed.h](file:///e:/work/etrike/rt-esp32/src/calculated_speed.h)
- **What It DOES:** Reads ESP32 PCNT hardware pulses (`read_rear_encoder_speed_mmps()`); provides `rt::CalculatedSpeedEstimator` fallback when configured to `Calculated`; updates atomic `g_measured_speed_mmps`.
- **What It DOES NOT Do:** Does NOT compute global 2D $(x, y, \theta)$ odometry pose; does NOT process steering position feedback (read on `0x201`).

---

## 2.6 Steering Control State Machine (`rt::SteeringControl`)
- **Source:** [steering_control.h](file:///e:/work/etrike/rt-esp32/src/steering_control.h)
- **What It DOES:** Manages EPS-C steer-by-wire state machine on RT ESP32; transmits continuous `0x169 VCU_SES_REQ` at strictly 50 Hz; applies speed-dependent slew rate limits ($125^\circ/\text{s} \to 525^\circ/\text{s}$); executes smooth centering ramps; encodes raw offset ($30000 + \text{decideg}$).
- **What It DOES NOT Do:** Does NOT calculate target steer angles independently; does NOT transmit `0x169` in MANUAL mode.

---

## 2.7 Smart Electronic Brake (SEB) Arbitrator (`brake_arbitrate`)
- **Source:** [brake_arbitration.h](file:///e:/work/etrike/rt-esp32/src/brake_arbitration.h), [seb_request.h](file:///e:/work/etrike/rt-esp32/src/seb_request.h), [brake_control.h (SYS)](file:///e:/work/etrike/sys-esp32/src/brake_control.h)
- **What It DOES:** Arbitrates between obstacle brake demand, host requests (`0x301`), and manual lever inputs using maximum priority selection; sends `0x7B9 VCU_SEB_REQ` at 50 Hz clamped to $5000\text{ kPa}$.
- **What It DOES NOT Do:** Does NOT modulate individual wheel ABS; does NOT allow software to command lower brake pressure than physical manual lever input.

---

## 2.8 Safety Monitor Logic (`rt::SafetyMonitor` & `sys::SafetyMonitor`)
- **Source:** [safety_monitor.h (RT)](file:///e:/work/etrike/rt-esp32/src/safety_monitor.h), [safety_monitor.cpp (SYS)](file:///e:/work/etrike/sys-esp32/src/safety_monitor.cpp)
- **What It DOES:** Evaluates safety rules at 100 Hz: Host staleness ($>500\text{ ms}$), heartbeats ($>1500\text{ ms}$), obstacle proximity ($d_{\text{obs}} \le 300\text{ mm}$), following error, bus-off; broadcasts `0x001 SAFETY_ESTOP` (`SendToFront`); commands SYS GPIO for PCR3 relay cutoff; latches E-Stop state.
- **What It DOES NOT Do:** Does NOT execute controlled trajectory slowing on hard E-Stops; does NOT allow software to clear active PCR3 relay latches.

---

## 2.9 Mode Manager & CAN Gateway Router (`ModeManager` & `can_dispatch`)
- **Source:** [mode_manager.cpp (SYS)](file:///e:/work/etrike/sys-esp32/src/mode_manager.cpp), [can_dispatch.h (RT)](file:///e:/work/etrike/rt-esp32/src/can_dispatch.h)
- **What It DOES:** Governs state transitions (`MANUAL`, `AUTO`, `ESTOP`) based on key switch and CAN `0x012`; gates actuator frame output (`0x169`, `0x204`) during MANUAL mode; forwards authorized cross-bus CAN frames.
- **What It DOES NOT Do:** Does NOT alter payload signal values during frame forwarding; does NOT permit AUTO mode entry if faults exist.

---

## 2.10 FreeRTOS Architecture & Atomic Storage
- **Source:** [main.cpp](file:///e:/work/etrike/rt-esp32/src/main.cpp), [rt_state.h](file:///e:/work/etrike/rt-esp32/src/rt_state.h)
- **What It DOES:** Separates execution across dual ESP32 cores (Core 1: 100 Hz control loop; Core 0: TWAI & MCP2515 tasks); uses FreeRTOS queues (`g_gw_tx_low_q`, `g_cmd_q`) and lock-free `std::atomic` variables.
- **What It DOES NOT Do:** Does NOT use blocking mutex locks inside the 100 Hz control loop; does NOT allocate dynamic heap memory during periodic loops.

---

## 2.11 CAN Drivers & Protocol Codecs (`can_driver_twai`, `can_driver_mcp2515`)
- **Source:** [can_driver_twai.cpp](file:///e:/work/etrike/rt-esp32/src/can_driver_twai.cpp), [can_driver_mcp2515.cpp](file:///e:/work/etrike/rt-esp32/src/can_driver_mcp2515.cpp), `protocol/`
- **What It DOES:** Manages Low CAN TWAI (500 kbps) and High CAN SPI MCP2515 (500 kbps); encodes/decodes C++ structs; computes XOR byte checksums ($\text{XOR}[0..6] \oplus \text{0xFF}$) and rolling counters ($0..15$); restarts TWAI on bus-off.
- **What It DOES NOT Do:** Does NOT alter vehicle state machine mode transitions.

---

## 2.12 Protocol Contract Codec Generator (`protocol/`)
- **Source:** `protocol/gen_can.py`, `protocol/contracts/`
- **What It DOES:** Compiles YAML CAN contracts (`can_high.yaml`, `can_low.yaml`) into zero-allocation C++ (`protocol.h`) and Python/TypeScript codec definitions with static bit layouts and XOR checksum contracts.
- **What It DOES NOT Do:** Does NOT generate dynamic memory allocation code.

---

## 2.13 TypeScript Multi-ECU Simulation Engine (`simulation/`)
- **Source:** `simulation/src/` ([simulation/src/index.ts](file:///e:/work/etrike/simulation/src/index.ts))
- **What It DOES:** Headless multi-node virtual CAN simulation environment in TypeScript/Vitest that emulates RT ESP32, SYS ESP32, EPS-C, SEB, MTR, and physics plant behavior for automated safety scenario testing.
- **What It DOES NOT Do:** Does NOT replace physical ECU hardware-in-the-loop (HIL) testing.

---

## 2.14 Software-in-the-Loop (SIL) Engine (`sim_engine_native`)
- **Source:** [main_native.cpp](file:///e:/work/etrike/native-test/sim-engine/main_native.cpp)
- **What It DOES:** Compiles production C++ headers (`physics_model.h`, codecs, safety checks) into a native host OS executable; reads/writes JSON lines on stdin/stdout to emulate RT ESP32 behavior for Control Toolkit testing.
- **What It DOES NOT Do:** Does NOT simulate physical electrical noise, SPI propagation delays, or transceiver bus-off events.

---
---

# Part 3: Unimplemented Units (Planned Architecture & Future Work)

These hardware and software units are identified in architectural specifications, future workplans, or design notes, but are **NOT implemented** in the active codebase binaries.

## Unimplemented Units Overview Table

| Unimplemented Unit | Category | Planned Target Node / Context | Architectural Reference | Intended Function | Current Codebase Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Big ESP32-S3 Consolidated ECU** | Architecture / Firmware | Unified Single ESP32-S3 Node | `big-esp32/big-architecture.md` | Merging RT ESP32 and SYS ESP32 onto a single dual-core ESP32-S3 microcontroller | NOT IMPLEMENTED. Architecture plan exists (`big-architecture.md`), but no firmware source files exist under `big-esp32` |
| **Tauri Desktop App Packaging** | Software / UI | Operator Workstation (`control-toolkit`) | `control-toolkit/.../architecture-control-toolkit.md` | Native cross-platform desktop application wrapper (Rust + Webview) for Control Toolkit | NOT IMPLEMENTED. Dashboard operates as a web app served via Python FastAPI backend |
| **Closed-Loop Path Correction** | Software / Kinematics | RT ESP32 Steering (`rt-esp32/src/`) | `docs/architecture/pid_controller.md` §5 | Auto-correcting front wheel steering angle to compensate for lateral tire slip or wind drift | NOT IMPLEMENTED. RT ESP32 executes kinematics commands blindly; lateral drift correction requires Jetson host setpoints |
| **Model Predictive Control (MPC)** | Software / Autonomy | Jetson Orin Motion Planner | `notes/pid-control.md` | Constrained multi-variable optimal trajectory solver (simultaneous speed + steering optimization) | NOT IMPLEMENTED IN REPOSITORY. Bridge processes incoming ROS 2 velocity & curvature setpoints directly |
| **High-Fidelity Dynamic Plant Model** | Software / Physics | Host Plant Simulator / SIL | `debug-tool/debug-tool-architecture.md` | Full 3D rigid-body tire slip force ($F_y = C_\alpha \alpha$) and suspension roll/load transfer model | NOT IMPLEMENTED. Current simulators use rigid-body 2D planar bicycle kinematics without dynamic slip modeling |
| **CAN FD (Flexible Data-Rate) Upgrade** | Hardware / Protocol | Physical CAN Bus Transceivers | `debug-tool/work-plan.md` | Upgrading physical CAN bus from classical $8\text{-byte}$ / $500\text{ kbps}$ to CAN FD ($64\text{-byte}$ / $2\text{-}5\text{ Mbps}$) | NOT IMPLEMENTED. System operates strictly on classical CAN 2.0B at 500 kbps |

---

## 3.1 Big ESP32-S3 Consolidated ECU Architecture (`big-esp32`)
- **Category:** Architecture & Firmware Refactoring.
- **Planned Context:** Consolidated Single-Node Controller Architecture ([big-architecture.md](file:///e:/work/etrike/big-esp32/big-architecture.md)).
- **Intended Function:** Merge the real-time kinematics gateway (RT ESP32) and safety/body manager (SYS ESP32) onto a single dual-core ESP32-S3 microcontroller board to eliminate inter-ECU CAN gateway latency ($10-20\text{ ms}$).
- **Current Status:** **NOT IMPLEMENTED.** Detailed architecture specifications exist in `big-esp32/big-architecture.md`, but no compiled C++ firmware source files exist under the `big-esp32/` directory. System runs on dual physical ESP32 nodes (`rt-esp32` and `sys-esp32`).

---

## 3.2 Tauri Desktop App Packaging
- **Category:** Software Packaging & User Interface.
- **Planned Context:** Operator Workstation User Interface (`control-toolkit`).
- **Intended Function:** Wrap the web dashboard into a standalone, native desktop bundle using Tauri (Rust + Webview) with desktop tray management and offline execution.
- **Current Status:** **NOT IMPLEMENTED.** The Control Toolkit frontend operates as a web application served by Python FastAPI backend (`control-toolkit/backend`) and viewed inside a web browser. No `tauri.conf.json` or Rust desktop wrapper files exist in the repository.

---

## 3.3 Closed-Loop Path Correction (RT Steering Auto-Correction)
- **Category:** Control Loop Algorithm.
- **Planned Context:** Real-Time Control Loop on RT ESP32 ([pid_controller.md](file:///e:/work/etrike/docs/architecture/pid_controller.md#L52)).
- **Intended Function:** Automatically adjust physical front wheel steering angle $\delta$ based on onboard IMU yaw rate or lateral acceleration feedback to compensate for real-world lateral vehicle drift (e.g., road crown, crosswinds, uneven tire grip).
- **Current Status:** **NOT IMPLEMENTED.** As documented in system architecture:
  - PID speed control operates on longitudinal velocity error only.
  - Kinematics resolver (`PhysicsModel`) converts `0x300` `yaw_rate_mrad_s` into physical steer angles open-loop (without lateral path feedback correction).
  - Any lateral path correction must be detected by higher-level perception/localization on the Jetson Orin host, which then issues an updated `yaw_rate` command to the RT node.

---

## 3.4 Model Predictive Control (MPC)
- **Category:** High-Level Motion Control Algorithm.
- **Planned Context:** Jetson Orin Host Autonomy Motion Planner.
- **Intended Function:** Solve constrained multi-variable optimization problems in real-time ($10-20\text{ Hz}$), simultaneously optimizing speed profiles, steering angles, lateral accelerations, and actuator effort limits under hard mechanical vehicle constraints.
- **Current Status:** **NOT IMPLEMENTED IN THIS REPOSITORY.** The vehicle bridge node ([vehicle_bridge_node.cpp](file:///e:/work/etrike/jetson/src/autoware_vehicle_bridge/src/vehicle_bridge_node.cpp)) processes incoming high-level ROS 2 velocity and curvature commands and encodes them directly into CAN `0x300 HOST_DRIVE_CMD`. No internal MPC optimization solver or matrix solver library exists in `rt-esp32`, `sys-esp32`, or `autoware_vehicle_bridge`.

---

## 3.5 High-Fidelity Dynamic Vehicle Plant Model (Tire Slip & Load Transfer)
- **Category:** Physics Plant Simulation.
- **Planned Context:** Host Diagnostic Simulator & SIL Plant Model ([debug-tool-architecture.md](file:///e:/work/etrike/debug-tool/debug-tool-architecture.md)).
- **Intended Function:** Simulate full 3D rigid-body vehicle dynamics including non-linear lateral tire slip forces ($F_yf = C_{\alpha f} \alpha_f$), longitudinal slip ratio, wheel load transfer, and suspension roll dynamics.
- **Current Status:** **NOT IMPLEMENTED.** Production firmware and simulation engines (`PhysicsModel`, `sim_engine_native`, `simulation/src/`) rely exclusively on 2D planar single-track kinematic bicycle models without dynamic slip or load transfer modeling.

---

## 3.6 CAN FD (Flexible Data-Rate) Protocol Upgrade
- **Category:** Hardware & Network Protocol.
- **Planned Context:** Vehicle CAN Bus Physical Layer & Codecs ([debug-tool/work-plan.md](file:///e:/work/etrike/debug-tool/work-plan.md)).
- **Intended Function:** Upgrade hardware CAN transceivers and microcontrollers to CAN FD to support payload sizes up to $64\text{ bytes}$ and data bitrates up to $2-5\text{ Mbps}$.
- **Current Status:** **NOT IMPLEMENTED.** All vehicle microcontrollers, CAN drivers (`can_driver_twai.cpp`, `can_driver_mcp2515.cpp`), CANalyst-II transports, and generated codecs operate strictly on Classical CAN 2.0B ($8\text{-byte}$ max DLC, $500\text{ kbps}$ bitrate).

---
---

# Part 4: Architectural & Kinematic Model Justifications

This section provides explicit engineering justifications, mathematical transformations, and safety design rationales for the model choices and unit boundaries across the system.

---

## 4.1 Kinematic Model Conversions: Unicycle $\to$ Bicycle $\to$ Delta-Tricycle

### 1. Unicycle Model (Host Autonomy Layer)
- **Mathematical Form:**
  $$\mathbf{u}_{\text{host}} = \begin{bmatrix} v \\ \omega \end{bmatrix} = \begin{bmatrix} \text{speed\_mmps} / 1000 \\ \text{yaw\_rate\_mrad\_s} / 1000 \end{bmatrix}$$
- **Engineering Justification:**
  High-level autonomy planners (ROS 2 Autoware / Navigation2) operate holonomically using body-frame linear velocity $v$ and yaw rate $\omega$. This unicycle abstraction is vehicle-agnostic, parameter-free, and decouples path planning algorithms from underlying wheel geometry, track width, or steering mechanical linkages.

---

### 2. Single-Track Inverse Bicycle Model (RT ESP32 Resolver Layer)
- **Mathematical Transformation ([physics_model.cpp:L52](file:///e:/work/etrike/rt-esp32/src/physics_model.cpp#L52)):**
  $$\delta = \arctan\left(\frac{L \omega}{v}\right) \quad \text{where } L = 1.5\text{ m (wheelbase)}$$
- **Engineering Justification:**
  The RT ESP32 microcontroller must convert body-level motion setpoints ($v, \omega$) into physical steering wheel angles ($\delta$) and longitudinal motor target speeds. 
  The single-track bicycle model simplifies the vehicle by collapsing the two rear wheels into a single virtual wheel on the centerline at the rear axle midpoint $(x, y)$. Under the non-holonomic no-slip constraint ($\dot{x}\sin\theta - \dot{y}\cos\theta = 0$), the vehicle heading derivative is $\dot{\theta} = \frac{v}{L} \tan(\delta)$, which directly maps yaw rate $\omega$ to steer angle $\delta$.

---

### 3. Physical Delta-Tricycle Model & Rear Mechanical Differential
- **Wheel-Level Kinematic Relations:**
  For turn radius $R = \frac{L}{\tan(\delta)}$ and rear track width $w = 0.8\text{ m}$, the inner and outer rear wheel path speeds are:
  $$v_{\text{left}} = v \left(1 - \frac{w}{2L}\tan(\delta)\right), \quad v_{\text{right}} = v \left(1 + \frac{w}{2L}\tan(\delta)\right)$$
- **Engineering Justification for Single Motor Output:**
  Rather than commanding separate dual-motor electronic differential speeds ($v_{\text{left}}, v_{\text{right}}$), the E-Trike utilizes a single traction motor driving a solid rear axle equipped with a **mechanical differential**. 
  The physical mechanical differential naturally splits torque and automatically accommodates the kinematic speed ratio between the left and right rear wheels during cornering. This allows the RT ESP32 firmware to output a single average motor speed target ($v$), eliminating complex dual-motor cross-coupling software control at low speeds.

---

## 4.2 PID Speed Controller Separation Rationale

- **Decoupling Speed PID from Steering:**
  The PID speed controller ([pid_controller.h](file:///e:/work/etrike/rt-esp32/src/pid_controller.h)) operates exclusively on longitudinal velocity error ($v_{\text{target}} - v_{\text{measured}}$) and does **NOT** attempt yaw rate or steering error correction.
- **Engineering Justification:**
  Combining longitudinal speed PID with steering torque vectoring on a narrow-track delta trike creates unstable cross-coupling loops. If tire slip occurs, active speed differential torque can alter lateral yaw dynamics and risk tip-over. Keeping longitudinal speed PID strictly SISO guarantees deterministic control stability.

---

## 4.3 Open-Loop Steering vs. Host Path Correction Rationale

- **Open-Loop RT Steering Execution:**
  The RT ESP32 resolver calculates front steering angle $\delta$ strictly from the Host's requested yaw rate and speed without executing local closed-loop path drift correction ([pid_controller.md](file:///e:/work/etrike/docs/architecture/pid_controller.md#L52)).
- **Engineering Justification:**
  Running a local steering correction loop on the microcontroller would compete against the Jetson host's global ROS 2 trajectory follower, leading to hunting, oscillation, and control fighting. The host autonomy stack owns global localization and path tracking, while the RT ECU owns high-speed command execution and dynamic safety limits.

---

## 4.4 Hardware PCR3 Relay Cutoff vs. Software Deceleration Rationale

- **Hardwired Electrical Isolation:**
  Emergency stop conditions trigger the SYS ESP32 to command an immediate hardware GPIO low output, de-energizing the main PCR3 battery relay coil ([safety_monitor.cpp](file:///e:/work/etrike/sys-esp32/src/safety_monitor.cpp)).
- **Engineering Justification:**
  Relying solely on software controlled deceleration is unsafe during critical faults because software tasks may freeze (watchdog trip, stack overflow, memory corruption) or CAN transceivers may fail. Hardwiring the PCR3 relay coil to the SYS ECU and physical hardwired E-Stop switches guarantees immediate battery isolation per ISO 26262 functional safety standards.

---

## 4.5 Dual-Bus CAN Architecture (High vs. Low) Rationale

- **Electrically Isolated Bus Topology:**
  High CAN (Host Jetson $\leftrightarrow$ RT ESP32 @ 100 Hz) is separated from Low CAN (RT ESP32 $\rightarrow$ EPS-C, SEB, MTR @ 50 Hz).
- **Engineering Justification:**
  High-throughput host autonomy frames (perception diagnostic telemetry, path vectors) can saturate CAN bandwidth. Isolating High CAN from Low CAN prevents high-frequency host chatter from delaying safety-critical actuator command frames (`0x169` steering, `0x204` motor, `0x7B9` brake) on the physical actuator bus.
