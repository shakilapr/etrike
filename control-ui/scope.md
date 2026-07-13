# CAN Controller — Scope & Requirements

This document defines the strict, non-negotiable requirements for the CAN Controller software. 

## 1. Primary Objectives
- **Monitor:** Real-time visibility into all CAN traffic on the vehicle.
- **Control & Mimic:** Ability to inject specific frames to spoof ECUs or control actuators.
- **Simplicity:** Absolute minimal lines of code. No legacy simulators, no complex recording pipelines, no routing "spaghetti."

## 2. Supported Work Modes
- **Mode 1: Full Vehicle (Monitor & Inject):** When connected to the fully assembled E-Trike, the tool passively monitors all traffic for the dashboard and only transmits when an operator explicitly injects a command (e.g., an ESTOP override).
- **Mode 2: Bench Test (Synthetic Peer):** When connected to an isolated physical ECU (e.g., testing just the RT module on a desk), the tool actively acts as a "Virtual Vehicle" by automatically broadcasting all missing heartbeats and synthetic statuses to prevent the physical ECU from entering a fault state.
- **Mode 3: Hardware-Free (Pure Software):** When the USB adapter is unplugged, the tool falls back to an internal RAM-based CAN bus interface, allowing full simulation and UI development without requiring any physical hardware.

## 3. Hardware Requirements
- **Primary Interface:** Must interface directly with the **CANalyst-II Dual-Channel USB Analyzer**.
- **Dual-Bus Support:** Must connect simultaneously to:
  - **Channel 0:** High-Level CAN Bus (500 kbit/s).
  - **Channel 1:** Low-Level CAN Bus (500 kbit/s).
- **Hardware-Free Virtual Mode:** If the CANalyst-II is unplugged, the tool must fall back to a purely software-based `virtual` CAN interface. This allows developers to boot the UI, run the dashboards, and simulate the entire vehicle network entirely in RAM without needing physical hardware.

## 3. Data Processing Requirements
- **Single Source of Truth:** Must use the existing YAML files (`can_high.yaml`, `can_low.yaml`) through generated runtime codecs, validators, constants, and UI metadata. DBC may be generated for third-party tools but is not an application runtime dependency.
- **Real-Time Decoding:** Must translate raw hexadecimal payloads into human-readable engineering values (e.g., `mm/s`, `kPa`, boolean flags) immediately upon reception.
- **Mixed-Endianness Support:** Must seamlessly handle both `motorola` (Big-Endian, for custom protocols) and `intel` (Little-Endian, for `sbw_unit` and `bbw_unit` actuators) concurrently.
- **Dynamic Checksums & Counters:** When spoofing actuator commands (e.g., `VCU_SES_REQ`, `VCU_SEB_REQ`), the controller must automatically compute and append rolling counters (0-15) and XOR checksums (e.g., `Checksum = XOR(bytes 0-6) ^ 0xFF`) dynamically, as required by the actuator specifications.
- **Mandatory Enable Flags:** When injecting spoofed actuator frames, the tool must enforce mandatory safety bits (e.g., ensuring `SES_RollCntEnable` and `SES_ChecksumEnable` are locked to `1`).
- **Complex Bit Overlaps:** The decoding/encoding logic must correctly handle the overlapping byte structures defined in the Intel sub-protocols (e.g., shared nibbles between speed, torque, and security counters in Byte 5 of `VCU_SES_REQ`).
- **Empty Payloads (DLC=0):** Must fully support sending and receiving zero-byte event frames (e.g., `0x001 SAFETY_ESTOP`).
- **Safety Limits Enforcement:** Must restrict user-injected commands to the maximum bounds defined in the YAML `constants` block (e.g., limiting forward speed requests to `max_speed_fwd_mmps`).

## 4. User Interface Requirements
- **Hardware Connection Status:** Must display unambiguous connection indicators showing:
  - CANalyst-II USB adapter status (Connected/Disconnected).
  - High-Level Bus status (Active/Offline).
  - Low-Level Bus status (Active/Offline).
- **Network Topology Map (Connected Items):** The UI must include a visual map showing which hardware items are currently connected and talking to the network, updating in real-time as components are plugged in or unplugged.
- **Node Heartbeat Indicators:** Must visually indicate the liveness of all key ECUs by monitoring their specific heartbeat frames:
  - **Host (Orin NX):** High Bus `0x7FC`
  - **RT (Real-Time Gateway):** High & Low Bus `0x7FD`
  - **SYS (Safety/Body):** Low Bus `0x7FE`
  - **PWT (Powertrain):** Low Bus `0x7FB`
- **Live Status Dashboards:** Must provide a visual dashboard displaying live status of components, vehicle speeds, and sensor readings.
- **Injection Controls:** Must provide a clean interface allowing the operator to select a message, input human-readable values, and inject it onto the physical bus.
- **Zero-Latency Feel:** The UI must update fast enough to be useful for real-time actuator debugging.

## 5. Control & Interaction Requirements
- **Game-Pad/Keyboard Control:** Must support real-time teleoperation using standard keyboard controls (e.g., WASD for drive/steer) for intuitive, game-like vehicle control.
- **Dedicated Hotkeys:** Must support immediate hotkey bindings for critical actions like Hard Brake and ESTOP.
- **Isolated Unit Testing:** Must provide the ability to target and control specific individual ECUs (e.g., commanding just the steer-by-wire unit, or just the DC-DC converter) without interfering with the rest of the network.
- **Message Verification:** Must include a mechanism to trigger and verify the behavior of individual CAN messages sequentially to confirm each frame functions as defined in the YAML.

## 6. System Emulation & Behaviors
- **Heartbeat Emulation:** When spoofing a node, the tool must automatically transmit that node's required heartbeat at the correct frequency (e.g., sending `0x7FC` at 2 Hz when spoofing the Host) to prevent the RT/SYS watchdogs from triggering an immediate ESTOP.
- **Synthetic Peer Injection:** To support 'PROTOTYPE' bench testing mode (Mode 1), the controller must be able to act as a synthetic peer for absent hardware by broadcasting mandatory status frames at precise rates to the correct bus:
  - `0x201 SES_STATUS` @ 100 ms → **Low Bus** (Fakes EPS-C). *Startup Constraint: Must boot with `angle=0` and `angle_status=1` (Aligned), otherwise the Gateway will trigger an implausibility fault.*
  - `0x721 SEB_STATUS` @ 100 ms → **Low Bus** (Fakes SEB)
  - `0x206 MTR_MOTOR_FBK` @ 50 ms → **Low Bus** (Fakes MTR)
  - `0x7FE SYS_HEARTBEAT` @ 100 ms → **Low Bus** (Fakes SYS)
  - `0x7FD RT_HEARTBEAT` @ 500 ms → **Both Buses** (Fakes RT Gateway)
  - `0x300 HOST_DRIVE_CMD` @ 100 ms → **High Bus** (Fakes Host)
  - `0x7FC HOST_HEARTBEAT` @ 500 ms → **High Bus** (Fakes Host)
- **Mode-Aware Injection:** Must support two distinct control modalities:
  - **Kinematics Mode (High Bus):** Injecting `0x300` (Drive Cmd) to mimic the Jetson Host, allowing the physical RT ECU to compute the inverse bicycle kinematics and safety limits.
  - **Direct Actuator Mode (Low Bus):** Injecting `0x204` (Motor) and `0x169`/`0x7B9` (Steer/Brake) directly on the Low bus to test actuators in isolation, bypassing RT kinematics.

- **HMI & Virtual Hardware Overrides:** To eliminate reliance on physical GPIO buttons (which are often disconnected on test benches), the controller UI natively acts as the vehicle's **HMI (Human-Machine Interface)** node. 
  - **Mode & Power:** The UI natively commands the vehicle's state machine by broadcasting `0x111 HMI_MODE_REQ` and `0x112 HMI_PWR_REQ`, bypassing the need for physical switches.
  - **Emergencies:** Software ESTOPs are injected directly via `0x001 SAFETY_ESTOP`.
  - **Virtual Encoders:** The tool spoofs `0x206 MTR_MOTOR_FBK` to simulate rolling wheels, satisfying the EGAS L2 safety monitor even if physical encoders are absent.

## 7. Diagnostic & Logging Requirements
- **Diagnostic Message Identification:** The tool must automatically identify diagnostic and telemetry frames (e.g., `SYS_DIAG_RPT`, `STEER_DIAG`, `BRAKE_DIAG`) as distinct from critical command frames.
- **Persistent Logging:** Must provide a mechanism to log these identified diagnostic messages to a file for post-test analysis and fault tracing, without bogging down the live UI.

## 8. Architectural Requirements
- **Absolute Minimum Code:** The architecture must prioritize brevity. 
- **Strict Separation:** The hardware bridging logic must be entirely separate from the UI visualization layer.
- **Stateless Bridge:** The backend must act as a transparent, stateless pipe between the CANalyst-II and the UI. It must not attempt to maintain simulated vehicle state or gatekeep messages.
