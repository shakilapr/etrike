# Control Toolkit (Control Toolbox) — Ultimate Master User & Technical Manual

The **E-Trike Control Toolkit** (also known as **Control Toolbox**) is the central engineering station, bench testing harness, vehicle simulator, diagnostic suite, and fault injection engine for the E-Trike vehicle platform. It integrates a high-performance **FastAPI** Python backend (`8001`) with a modern **React/Vite** web UI (`5173`), providing complete control, observation, recording, playback, and analysis capabilities across physical hardware and virtual environments.

---

## Table of Contents
1. [System Architecture & Protocol Parity](#1-system-architecture--protocol-parity)
2. [Global Controls, Topbar & Safety Gates](#2-global-controls-topbar--safety-gates)
3. [Exhaustive UI Workspace & Tab Guide](#3-exhaustive-ui-workspace--tab-guide)
   - [3.1 Overview Tab (Dashboard & System Health)](#31-overview-tab-dashboard--system-health)
   - [3.2 Drive Console Tab (Vehicle Motion & 3D Kinematics)](#32-drive-console-tab-vehicle-motion--3d-kinematics)
   - [3.3 Direct Control Tab (Actuator Engineering)](#33-direct-control-tab-actuator-engineering)
   - [3.4 Fault Injector Tab (Chaos & Safety Testing)](#34-fault-injector-tab-chaos--safety-testing)
   - [3.5 Live CAN Monitor Tab (Real-Time Packet Inspector)](#35-live-can-monitor-tab-real-time-packet-inspector)
   - [3.6 CAN Dictionary Tab (Contract Specification Viewer)](#36-can-dictionary-tab-contract-specification-viewer)
   - [3.7 Bench & Synthetic Peers Tab (ECU Simulation & HMI)](#37-bench--synthetic-peers-tab-ecu-simulation--hmi)
   - [3.8 Diagnostics Tab (Bus Jitter, Stale Signals & Episodes)](#38-diagnostics-tab-bus-jitter-stale-signals--episodes)
   - [3.9 Recordings & Vector Export Tab (Traffic Logging & BLF Export)](#39-recordings--vector-export-tab-traffic-logging--blf-export)
   - [3.10 Network Topology Tab (Dual-Bus Load & Traffic)](#310-network-topology-tab-dual-bus-load--traffic)
   - [3.11 System Logs Tab (Audit Trail & Error Tracebacks)](#311-system-logs-tab-audit-trail--error-tracebacks)
   - [3.12 System Settings Tab (Hardware & Software Configuration)](#312-system-settings-tab-hardware--software-configuration)
   - [3.13 Active TX Rail (Global Transmission Manager)](#313-active-tx-rail-global-transmission-manager)
4. [Pre-Run Vehicle Commissioning & Safety Checklist](#4-pre-run-vehicle-commissioning--safety-checklist)
5. [Complete 9-ECU Signal-by-Signal Testing Plan](#5-complete-9-ecu-signal-by-signal-testing-plan)
   - [5.1 ECU Node 1: Steering ECU (SES / ses.yaml / 0x100 Series on Low Bus)](#51-ecu-node-1-steering-ecu-ses--sesyaml--0x100-series-on-low-bus)
   - [5.2 ECU Node 2: Motor Control Unit (MCU / mtr.yaml / 0x300 Series on High Bus)](#52-ecu-node-2-motor-control-unit-mcu--mtryaml--0x300-series-on-high-bus)
   - [5.3 ECU Node 3: Smart Electronic Braking (SEB / seb.yaml / 0x101 Series on Low Bus)](#53-ecu-node-3-smart-electronic-braking-seb--sebyaml--0x101-series-on-low-bus)
   - [5.4 ECU Node 4: Battery Management & Powertrain (BMS/PWT / pwtyaml / 0x200 Series)](#54-ecu-node-4-battery-management--powertrain-bmspwt--pwtyaml--0x200-series)
   - [5.5 ECU Node 5: Vehicle Control Unit & Host Controller (VCU / host.yaml / 0x300 Series)](#55-ecu-node-5-vehicle-control-unit--host-controller-vcu--hostyaml--0x300-series)
   - [5.6 ECU Node 6: HMI & Body Controls (HMI / hmi.yaml / 0x102 Series on Low Bus)](#56-ecu-node-6-hmi--body-controls-hmi--hmiyaml--0x102-series-on-low-bus)
   - [5.7 ECU Node 7: Real-Time ECU Node (RT / rtyaml / 0x200 Series on High Bus)](#57-ecu-node-7-real-time-ecu-node-rt--rtyaml--0x200-series-on-high-bus)
   - [5.8 ECU Node 8: System ECU Node (SYS / sys.yaml / 0x201 Series on Low Bus)](#58-ecu-node-8-system-ecu-node-sys--sysyaml--0x201-series-on-low-bus)
   - [5.9 ECU Node 9: Network Diagnostics & Sync (NET / network.yaml / 0x010 & 0x700 Series)](#59-ecu-node-9-network-diagnostics--sync-net--networkyaml--0x010--0x700-series)
6. [Complete Backend REST & WebSocket API Reference](#6-complete-backend-rest--websocket-api-reference)
7. [Command-Line Utilities & Automated QA Script Suite](#7-command-line-utilities--automated-qa-script-suite)
8. [Step-by-Step Task Workflows](#8-step-by-step-task-workflows)
   - [Workflow 1: System Startup & Operational Readiness Check](#workflow-1-system-startup--operational-readiness-check)
   - [Workflow 2: Manual Driving via Keyboard & Speed/Yaw Sliders](#workflow-2-manual-driving-via-keyboard--speedyaw-sliders)
   - [Workflow 3: Switching to Real Hardware Mode (CANalyst-II)](#workflow-3-switching-to-real-hardware-mode-canalyst-ii)
   - [Workflow 4: Direct SES Steering Control with 4-bit Rolling Counters](#workflow-4-direct-ses-steering-control-with-4-bit-rolling-counters)
   - [Workflow 5: Running Automated Analysis Drive Profiles (Sine/Trapezoid)](#workflow-5-running-automated-analysis-drive-profiles-sinetrapezoid)
   - [Workflow 6: Executing a Chaos Fault Injection Campaign](#workflow-6-executing-a-chaos-fault-injection-campaign)
   - [Workflow 7: Hardware-in-the-Loop (HIL) Simulation with Synthetic Peers](#workflow-7-hardware-in-the-loop-hil-simulation-with-synthetic-peers)
   - [Workflow 8: Recording CAN Traffic & Exporting to Vector CANalyzer (BLF/DBC)](#workflow-8-recording-can-traffic--exporting-to-vector-canalyzer-blfdbc)
   - [Workflow 9: Headless Automated Testing via CLI Scripts](#workflow-9-headless-automated-testing-via-cli-scripts)
9. [Comprehensive Troubleshooting & Diagnostics Matrix](#9-comprehensive-troubleshooting--diagnostics-matrix)

---

## 1. System Architecture & Protocol Parity

### 1.1 Dual CAN Bus Topology
The E-Trike vehicle architecture runs two physical or virtual CAN channels at **500 kbit/s**:
- **CAN High (Drive & Dynamics Bus)**: Connects primary powertrain units including Motor Control Unit (`VCU_MCU_REQ` / `0x300`), host drive intent (`HOST_DRIVE_CMD` / `0x300`), battery management telemetry, and vehicle kinematics state.
- **CAN Low (Body & Safety Actuators Bus)**: Connects steer-by-wire actuator (`VCU_SES_REQ` / `0x100`), electronic braking (`VCU_BRAKE_REQ`), lighting controls (`VCU_LIGHT_REQ`), HMI mode updates, and safety telemetry.

```
       +-------------------------------------------------------------+
       |               Control Toolkit Web UI (Vite)                 |
       |                    http://127.0.0.1:5173                    |
       +------------------------------+------------------------------+
                                      | WebSocket & REST API
                                      v
       +-------------------------------------------------------------+
       |               Control Toolkit API (FastAPI)                 |
       |                    http://127.0.0.1:8001                    |
       +------------------------------+------------------------------+
                                      |
       +------------------------------+------------------------------+
       |                     Protocol Bridge                         |
       |     (Single Source of Truth generated from protocol/YAML)   |
       +--------------+-------------------------------+--------------+
                      |                               |
                      v                               v
         +--------------------------+    +--------------------------+
         |     CAN High (500k)      |    |      CAN Low (500k)      |
         |  Drive & Motor Control   |    | Steering, Brake & Lights |
         +--------------------------+    +--------------------------+
```

### 1.2 Bit-Exact Protocol Parity
The backend imports the generated `protocol` Python package directly compiled from system YAML contracts in `protocol/contracts/`. The API exposes validation hashes (`wire_hash`, `semantic_hash`, `network_hash`) ensuring 100% bit-exact parity between physical ECU firmware, native software simulation, and the Control Toolkit.

---

## 2. Global Controls, Topbar & Safety Gates

The **Topbar** remains fixed at the top of the interface across all screens:

1. **Transport Profile Switcher ("Computer" vs "Real")**:
   - **Computer Mode (`pure_software`)**: Dual software virtual CAN buses. Manages local Native SIL binary processes (`rt-aurix-lite.exe`).
   - **Real Mode (`bench_test` / `full_vehicle`)**: Interfaced via dual-channel **CANalyst-II USB** hardware adapter (`CH0=High`, `CH1=Low` @ 500k).
   - **Fail-Safe Switch Logic**: When switching to Real mode, the backend probes hardware presence. If missing, it immediately rolls back to Computer mode, forces Bench TX to `DISABLED`, and returns `503 Service Unavailable` with diagnostic detail.
2. **Bench TX Arming Gate ("Arm Bench TX")**:
   - **Hard Safety Gate**: Outbound CAN transmission is **`DISABLED`** by default. You must click **"Arm Bench TX"** before any drive command, direct actuator test, or fault injection can send frames onto virtual or physical buses.
3. **Emergency Stop Button ("STOP ALL")**:
   - Prominent red button. Instantly cancels all active periodic scheduler jobs, clears direct actuator outputs, halts fault injections, releases ownership leases, and neutralizes both buses.
4. **Live Stream Indicator**:
   - Real-time indicator displaying WebSocket state (**Live** in green with ping latency, or **Offline** in red).
5. **Session & Revision Counter**:
   - Displays active session ID and optimistic concurrency revision counter.

---

## 3. Exhaustive UI Workspace & Tab Guide

### 3.1 Overview Tab (Dashboard & System Health)
- **Purpose**: System summary, active node health grid, and live message frequencies.
- **Key UI Elements**:
  - **ECU Node Grid**: Cards for VCU, MCU, SES, BMS, and Host Controller showing status (**Active**, **Quiet**, **Offline**).
  - **Bus Rate Meters**: Live frames/second gauges for CAN High and Low channels.
  - **System Banner**: Current profile, TX arming state, running jobs count, and system alert notifications.
- **How to Use**:
  1. Click **Overview** in the sidebar.
  2. Confirm backend health status is `RUNNING` and observe baseline frame rates (> 50 fps).

---

### 3.2 Drive Console Tab (Vehicle Motion & 3D Kinematics)
- **Purpose**: Interactive vehicle motion control, manual driving, gear selection, and kinematics feedback.
- **Key UI Elements**:
  - **3D Vehicle Visualizer**: Interactive canvas showing real-time wheel angle, speed vector, and direction.
  - **Keyboard Drive Controls**: Press and hold **W** (Throttle), **S** (Brake/Reverse), **A** (Steer Left), **D** (Steer Right).
  - **Manual Target Sliders**: Speed Target (`0 - 3000 mm/s`) and Yaw Target (`-3000 to +3000 mrad/s`).
  - **Gear Selector Buttons**: **P** (Park), **R** (Reverse), **N** (Neutral), **D** (Drive).
  - **Drive Profile Buttons**: **Eco**, **Normal**, **Sport** (Sport applies a 20% speed boost).
- **How to Use**:
  1. Arm **Bench TX** in the topbar.
  2. Select **D** gear or hold **W** (pressing **W** automatically promotes gear from **N** to **D**).
  3. Use **A** and **D** to steer. Watch the 3D wheel angle update.
  4. Release keys: Observe the **Host Stale Watchdog** automatically zero speed and stop periodic transmission after **500ms**.

---

### 3.3 Direct Control Tab (Actuator Engineering)
- **Purpose**: Directly command low-level actuators, overriding drive intent.
- **Key UI Elements**:
  - **Motor Control Channel (`VCU_MCU_REQ`)**: RPM Target (`0 - 6000 RPM`), Torque Limit (`0 - 100 Nm`), Inverter Enable checkbox.
  - **Steering Actuator Channel (`VCU_SES_REQ`)**: Target Angle Raw (`-450 to +450`), Alignment Enable, Control Enable, Target Speed Raw (`125 - 525`). Automatic 0–15 rolling counter.
  - **Brake Actuator Channel (`VCU_BRAKE_REQ`)**: Target Brake Pressure, Motor Brake Request, Electronic Brake Enable.
- **How to Use**:
  1. Click **Control** on the sidebar.
  2. Toggle **Steering Channel** to ON. Adjust Target Angle Raw slider to `150`.
  3. Verify periodic 20ms transmission of `VCU_SES_REQ` in the Active TX Rail.

> [!IMPORTANT]
> **Mutual Exclusion**: Direct actuator mode and Drive Console kinematics cannot run simultaneously. Activating direct control automatically cancels drive intent jobs.

---

### 3.4 Fault Injector Tab (Chaos & Safety Testing)
- **Purpose**: Inject artificial faults, packet corruptions, drops, overrides, and bus floods.
- **Key UI Elements**:
  - **Preset Library**: Pre-built scenarios ("Steering Counter Freeze", "Motor Overspeed", "CRC Error").
  - **Custom Builder**: Select Target Bus (High/Low), Target Message ID, Mode (Single-Shot, Periodic, Override), and Fault Type (Payload Corruption, Bit Flip, DLC Tampering, Packet Drop, Bus Flood).
  - **Active Injections Table**: List of active fault injection jobs with individual cancellation controls.
- **How to Use**:
  1. Click **Inject** on the sidebar.
  2. Select **Low Bus**, **VCU_SES_REQ**, **Packet Drop (50% loss)**, click **Start Injection**.
  3. Inspect diagnostic logs and stop the injection when validation is complete.

---

### 3.5 Live CAN Monitor Tab (Real-Time Packet Inspector)
- **Purpose**: High-speed CAN frame inspector with signal decoding.
- **Key UI Elements**:
  - **Packet Table**: Timestamp, Bus, Frame ID (Hex/Dec), Message Name, DLC, Payload (Hex), Decoded Signals.
  - **Stream Controls**: **Pause Stream**, **Resume Stream**, **Clear Buffer**, **Freeze Frame**.
  - **Filters**: Bus filter (`High`/`Low`/`All`), Search query filter.
  - **Expanded Inspector Drawer**: Click any row to expand a full signal bitfield matrix and engineering values.
- **How to Use**:
  1. Click **Live CAN** on the sidebar.
  2. Type `0x300` in the search box to isolate motor messages.
  3. Click a row to view raw bit patterns and decoded engineering parameters.

---

### 3.6 CAN Dictionary Tab (Contract Specification Viewer)
- **Purpose**: Browse full system CAN contract specifications.
- **Key UI Elements**:
  - **Search & Filter**: Search by signal name, ID, or sender node.
  - **Bit Layout Diagram**: Graphical bit matrix showing exact byte (0–7) and bit (0–63) mappings.
  - **Signal Specs**: Data type, Scale, Offset, Min, Max, Units (`mm/s`, `mrad/s`, `RPM`, `mV`).
- **How to Use**:
  1. Click **CAN Dictionary** on the sidebar.
  2. Look up `VCU_SES_REQ` to verify byte layout and cycle timing (20ms).

---

### 3.7 Bench & Synthetic Peers Tab (ECU Simulation & HMI)
- **Purpose**: Simulate missing ECUs (MCU, SES, BMS) and override HMI parameters.
- **Key UI Elements**:
  - **Synthetic MCU Peer Toggle**: Simulates motor RPM, inverter status, and temperature telemetry.
  - **Synthetic SES Peer Toggle**: Simulates steering angle feedback and rolling health counter.
  - **Synthetic BMS Peer Toggle**: Simulates battery % SOC, voltage, and cell temperatures.
  - **HMI Mode Overrides**: Drive profile switches (Eco/Normal/Sport), turn signals, horn, hazard lights.
- **How to Use**:
  1. Click **Bench** on the sidebar.
  2. Enable **Synthetic MCU** and **Synthetic SES**.
  3. Verify in **Overview** that MCU and SES nodes show **Active** (green).

---

### 3.8 Diagnostics Tab (Bus Jitter, Stale Signals & Episodes)
- **Purpose**: Track signal freshness, cycle time jitter, packet loss, and diagnostic episodes.
- **Key UI Elements**:
  - **Stale Signal Table**: Highlights signals exceeding 3x expected period.
  - **Jitter & Drift Matrix**: Cycle time variance (min, max, average delta ms).
  - **Episode History Log**: Diagnostic incident logs captured during safety stops or network drops.
- **How to Use**:
  1. Click **Diagnostics** on the sidebar.
  2. Inspect jitter variance to ensure timing drift remains below ±2ms.

---

### 3.9 Recordings & Vector Export Tab (Traffic Logging & BLF Export)
- **Purpose**: Record live CAN sessions and export traffic into industry-standard **Vector BLF / ASC** formats.
- **Key UI Elements**:
  - **Recording Controls**: **Start Recording**, **Stop Recording**, Name & Description fields.
  - **Saved Recordings List**: Saved sessions with frame counts and duration.
  - **Export Buttons**:
    - **Export JSON**: Raw JSON frame export.
    - **Export Vector Package (ZIP)**: Generates a complete ZIP archive containing **Vector `.blf` / `.asc` log files** bundled with generated **`.dbc` signal database files** for instant import into Vector CANalyzer / CANoe!
  - **Playback Engine**: Replay recorded sessions back onto virtual or physical buses at 1x, 2x, or 0.5x speed.
- **How to Use**:
  1. Click **Recordings** (or access via **Live CAN**).
  2. Click **Start Recording**, run your drive test sequence, then click **Stop Recording**.
  3. Click **Export Vector Package** to download the BLF + DBC bundle for Vector CANalyzer analysis.

---

### 3.10 Network Topology Tab (Dual-Bus Load & Traffic)
- **Purpose**: Bus bandwidth monitoring and network diagram visualization.
- **Key UI Elements**:
  - **Dual-Bus Topology Diagram**: Visual map of interconnected ECU nodes.
  - **Bus Utilization Meters**: Percentage bus load gauges for CAN High and Low.
  - **PyUSB Polling Health**: Display active hardware polling interval (default 2ms).
- **How to Use**:
  1. Click **Network** in the sidebar.
  2. Verify bus utilization stays below 80% under peak load.

---

### 3.11 System Logs Tab (Audit Trail & Error Tracebacks)
- **Purpose**: Review backend execution logs, API events, and tracebacks.
- **Key UI Elements**:
  - **Log Stream Table**: Level (`INFO`, `WARN`, `ERROR`, `DEBUG`), Timestamp, Module, Text.
  - **Export Tools**: **Copy Logs** to clipboard, **Download Log File**.
- **How to Use**:
  1. Click **Logs** on the sidebar.
  2. Filter by `ERROR` to diagnose backend failures.

---

### 3.12 System Settings Tab (Hardware & Software Configuration)
- **Purpose**: Configure API endpoints, hardware bitrates, and SIL paths.
- **Key UI Elements**:
  - **API Endpoint Config**: Backend host/port (`127.0.0.1:8001`).
  - **CANalyst-II Config**: Bitrate (`500000`), Device Index (`0`), Poll Delay (`2` ms).
  - **Native SIL Path**: File path to compiled `rt-aurix-lite.exe`.
- **How to Use**:
  1. Click **Settings** on the sidebar.
  2. Enter the executable path for SIL testing and click **Save Settings**.

---

### 3.13 Active TX Rail (Global Transmission Manager)
- **Purpose**: Persistent drawer on the right side of the screen tracking all outbound jobs.
- **Key UI Elements**:
  - **Job Cards**: Active periodic jobs, direct actuator commands, and background fault injections.
  - **Quick Controls**: Individual job cancel buttons, **Stop All Jobs** button.
- **How to Use**:
  1. Click the **Active TX** drawer handle on the right edge of the window.
  2. Monitor active background transmissions while navigating between tabs.

---

## 4. Pre-Run Vehicle Commissioning & Safety Checklist

> [!CAUTION]
> **MANDATORY SAFETY PROCEDURES**: Perform every step of this 5-phase checklist in exact order before powering high-voltage traction packs or attempting vehicle road/bench motion.

```
       +-------------------------------------------------------------+
       |   PHASE 1: Mechanical Isolation & 60-Ohm Termination Check   |
       +------------------------------+------------------------------+
                                      | PASS
                                      v
       +-------------------------------------------------------------+
       |   PHASE 2: PC USB Driver Preflight (0 Transmitted Packets)  |
       +------------------------------+------------------------------+
                                      | PASS
                                      v
       +-------------------------------------------------------------+
       |   PHASE 3: ESP32 Hardware Verification (`hw_verify` Build)   |
       +------------------------------+------------------------------+
                                      | PASS
                                      v
       +-------------------------------------------------------------+
       |   PHASE 4: Passive Traffic Observation (Bench TX DISABLED)  |
       +------------------------------+------------------------------+
                                      | PASS
                                      v
       +-------------------------------------------------------------+
       |   PHASE 5: Controlled Arming & Neutral Zero-Command Verification|
       +-------------------------------------------------------------+
```

### Phase 1: Mechanical & Electrical Pre-Power Isolation
- [ ] **Vehicle Elevated**: Rear drive wheels and front steering wheel suspended off the ground on heavy-duty jack stands.
- [ ] **Emergency Stop Button**: Mechanical E-Stop mushroom button wired to main contactor coils tested and verified functional.
- [ ] **High Voltage (HV) Pack Disconnected**: High voltage traction battery service plug pulled. Only **12V auxiliary logic power** connected.
- [ ] **Bus Termination Resistor Verification**:
  - Measure unpowered resistance across CAN-H and CAN-L pins on CAN High bus: Must measure **$60\,\Omega \pm 5\,\Omega$** ($120\,\Omega$ terminators at each physical bus end).
  - Measure unpowered resistance across CAN-H and CAN-L pins on CAN Low bus: Must measure **$60\,\Omega \pm 5\,\Omega$**.
- [ ] **Ground Reference**: Common logic reference ground connected between CANalyst-II GND and vehicle 12V ground rail.

### Phase 2: PC USB Driver & Preflight Probe
- [ ] **USB Driver Assignment**: Windows Device Manager verifies USB device `04D8:0053` bound to `WinUSB` or `libusbK` via Zadig.
- [ ] **Passive Preflight Test**:
  - Run passive adapter probe:
    ```powershell
    python control-toolkit/backend/scripts/canalyst_preflight.py --open --listen-seconds 3
    ```
  - **Required Result**: `dual_channel_open`, `ready_for_unpowered_can_wiring`, `transmitted: 0`.

### Phase 3: ESP32 Firmware Hardware Verification (`hw_verify`)
- [ ] **Flash Hardware Verification Firmware**:
  ```powershell
  cd e:\work\etrike\can-test
  pio run -e hw_verify -t upload --upload-port COM9   # RT Node
  pio run -e hw_verify -t upload --upload-port COM5   # SYS Node
  ```
- [ ] **Verify Self-Test Output**:
  - PSRAM: **PASS** (both nodes).
  - TWAI NO_ACK self-test: **PASS** (both nodes).
  - MCP2515 SPI interface: **PASS** (RT node for High bus).
- [ ] **Flash Vehicle Firmware**: Flash `rt-esp32` and `sys-esp32` production firmware only after `hw_verify` passes.

### Phase 4: Passive Traffic Observation (No Transmission)
- [ ] Power ECUs with 12V auxiliary power.
- [ ] In Control Toolkit topbar, select profile **Real → Full Vehicle**.
- [ ] Verify **Bench TX is `DISABLED`**.
- [ ] Confirm:
  - Overview tab shows ECU node heartbeats.
  - High and Low RX packet counters increase without frame drop errors.
  - Zero bus-off or TWAI re-initialization log warnings.

### Phase 5: Controlled Arming & Zero-Command Verification
- [ ] Safety operator stationed at main E-Stop power switch.
- [ ] Click **"Arm Bench TX"** in topbar.
- [ ] Verify vehicle remains completely stationary with zero motor torque command.
- [ ] Proceed to ECU signal-by-signal testing.

---

## 5. Complete 9-ECU Signal-by-Signal Testing Plan

Perform signal verification systematically ECU node by ECU node across all 9 YAML system contracts.

---

### 5.1 ECU Node 1: Steering ECU (SES / `ses.yaml` / `0x100` Series on Low Bus)
- **Target ECU**: SES (Steering Actuator Controller)
- **Wire Message**: `VCU_SES_REQ` (`0x100`, 20ms period, Low Bus)

#### Signal-by-Signal Test Steps:
1. **Signal: `alignment_enable` (Bit 0, 1-bit boolean)**:
   - Go to **Direct Control** tab -> **Steering Panel**.
   - Set `alignment_enable = True`. Observe byte 0 payload bit 0 in Live CAN (`0x01`).
   - *Verification*: Confirm SES ECU executes internal steering center alignment procedure.
2. **Signal: `control_enable` (Bit 1, 1-bit boolean)**:
   - Toggle `control_enable = True`. Observe byte 0 payload bit 1 (`0x02`).
   - *Verification*: Confirm SES actuator state transitions from Standby to Active.
3. **Signal: `target_angle_raw` (Bits 8-23, 16-bit signed integer, scale 0.1 deg, range -450 to +450)**:
   - **Step A (`0` center)**: Command `target_angle_raw = 0`. Inspect feedback signal `SES_ANGLE_ACTUAL` in Live CAN. Verify wheel is centered (0.0°).
   - **Step B (`+100` right)**: Command `target_angle_raw = 100` (+10.0° right). Verify physical wheel turns 10.0° right and `SES_ANGLE_ACTUAL` reaches 100 within 200ms.
   - **Step C (`-100` left)**: Command `target_angle_raw = -100` (-10.0° left). Verify physical wheel turns 10.0° left.
   - **Step D (Limits `+450` / `-450`)**: Step to extreme bounds (`+450` and `-450`). Confirm software limits prevent mechanical over-travel.
4. **Signal: `target_speed_raw` (Bits 24-31, 8-bit unsigned integer, range 125-525)**:
   - Command a 20° angle step with `target_speed_raw = 125` (slow slew). Measure rotation time (~1.5s).
   - Command a 20° angle step with `target_speed_raw = 525` (fast slew). Measure rotation time (~0.4s).
5. **Signal: `rolling_counter` (Bits 4-7 of Byte 4, 4-bit unsigned, 0-15)**:
   - Observe `VCU_SES_REQ` in Live CAN. Verify rolling counter increments `0 -> 1 -> 2 ... -> 15 -> 0` on every 20ms frame.
   - Go to **Fault Injector** tab: Inject a rolling counter freeze fault (freeze counter at `5`).
   - *Verification*: Confirm SES ECU detects stale counter within 60ms (3 frames) and safely mutes steering torque.

---

### 5.2 ECU Node 2: Motor Control Unit (MCU / `mtr.yaml` / `0x300` Series on High Bus)
- **Target ECU**: MCU (Motor Controller)
- **Wire Message**: `VCU_MCU_REQ` (`0x300`, 10ms period, High Bus)

#### Signal-by-Signal Test Steps:
1. **Signal: `inverter_enable` (Bit 0, 1-bit boolean)**:
   - Go to **Direct Control** tab -> **Motor Panel**.
   - Set `inverter_enable = False`. Command 500 RPM target.
   - *Verification*: Confirm MCU status remains Inactive and no motor phase current flows.
   - Set `inverter_enable = True`. Verify MCU status transitions to Inverter Ready.
2. **Signal: `direction` (Bit 1, 1-bit boolean, 0=Forward, 1=Reverse)**:
   - Set `direction = 0` (Forward). Command 100 RPM. Verify rear wheel rotates forward.
   - Set `direction = 1` (Reverse). Command 100 RPM. Verify rear wheel rotates reverse.
3. **Signal: `target_rpm` (Bits 8-23, 16-bit unsigned, range 0-6000 RPM)**:
   - **Step A (`0 RPM`)**: Command `target_rpm = 0`. Confirm motor is stationary.
   - **Step B (`500 RPM`)**: Command `target_rpm = 500`. Observe feedback signal `MCU_RPM_ACTUAL` reaching 500 RPM in Live CAN.
   - **Step C (`1500 RPM` & `3000 RPM`)**: Step to 1500 RPM and 3000 RPM. Verify smooth acceleration ramp without velocity overshoots.
4. **Signal: `torque_limit_raw` (Bits 24-31, 8-bit unsigned, 0-100 Nm)**:
   - Step `torque_limit_raw` from 10 Nm to 50 Nm while observing inverter DC bus current. Verify phase torque clamping matches commanded limit.

---

### 5.3 ECU Node 3: Smart Electronic Braking (SEB / `seb.yaml` / `0x101` Series on Low Bus)
- **Target ECU**: SEB (Smart Electronic Brake Actuator)
- **Wire Message**: `VCU_BRAKE_REQ` (`0x101`, 20ms period, Low Bus)

#### Signal-by-Signal Test Steps:
1. **Signal: `brake_enable` (Bit 0, 1-bit boolean)**:
   - Toggle `brake_enable = True`. Verify brake actuator pressurization pump energizes.
2. **Signal: `motor_brake_req` (Bits 8-15, 8-bit unsigned, 0-100% regen braking)**:
   - Step `motor_brake_req` from 0% to 50% to 100%. Verify regenerative brake command frame is forwarded to MCU.
3. **Signal: `hydraulic_pressure_target` (Bits 16-31, 16-bit unsigned, 0-100 BAR)**:
   - Step target pressure: `0 BAR` -> `10 BAR` -> `30 BAR` -> `50 BAR`.
   - Observe feedback signal `BRAKE_PRESS_ACTUAL` in Live CAN. Confirm caliper pressure reaches target without fluid leakage or thermal overload.

---

### 5.4 ECU Node 4: Battery Management & Powertrain (BMS/PWT / `pwt.yaml` / `0x200` Series)
- **Target ECU**: BMS & High-Voltage Powertrain
- **Telemetry Signal Verification**:
1. **Signal: `BMS_PACK_VOLTAGE` (16-bit unsigned integer, mV)**:
   - Observe Live CAN: Verify pack voltage reports nominal battery voltage (`48000 mV` to `72000 mV`).
   - Use **Fault Injector** to override voltage to `36000 mV` (undervoltage). Verify VCU generates Low Battery warning.
2. **Signal: `BMS_PACK_CURRENT` (16-bit signed integer, mA)**:
   - Apply acceleration load. Verify discharge current registers positive (+mA).
   - Apply regenerative braking. Verify charge current registers negative (-mA).
3. **Signal: `BMS_SOC` (8-bit unsigned integer, 0-100%)**:
   - Verify Overview tab battery gauge matches `BMS_SOC` telemetry.
4. **Signal: `BMS_FAULT_FLAGS` (16-bit bitmask)**:
   - Inject simulated BMS cell overtemperature fault bit.
   - *Verification*: Confirm VCU sheds load and opens traction contactor safety relay.

---

### 5.5 ECU Node 5: Vehicle Control Unit & Host Controller (VCU / `host.yaml` / `0x300` Series)
- **Target Unit**: VCU Controller / Host Kinematics Engine
- **Wire Message**: `HOST_DRIVE_CMD` (`0x300`, 10ms period, High Bus)

#### Signal-by-Signal Test Steps:
1. **Signal: `gear` (Bits 0-3, 4-bit enum: 0=Park, 1=Reverse, 2=Neutral, 3=Drive)**:
   - Set `gear = Neutral (2)`. Command throttle. Confirm VCU suppresses motor commands.
   - Set `gear = Drive (3)`. Confirm VCU forwards drive commands to MCU.
2. **Signal: `shaped_speed` (Bits 8-23, 16-bit signed integer, mm/s)**:
   - Step speed command in 500 mm/s increments (`500`, `1000`, `2000`, `3000 mm/s`).
   - Verify VCU converts kinematics speed into target MCU RPM.
3. **Signal: `shaped_yaw` (Bits 24-39, 16-bit signed integer, mrad/s)**:
   - Step yaw command (`-1000`, `0`, `+1000 mrad/s`).
   - Verify VCU computes differential wheel speeds and commands SES steering angle according to tricycle kinematics model:
     $$\delta = \arctan\left(\frac{L \cdot \omega_{\text{yaw}}}{v}\right)$$
4. **Signal: `host_stale_watchdog` (Safety Timeout Test)**:
   - Send continuous drive commands. Pause client transmission.
   - *Verification*: Confirm VCU host watchdog zero-commands speed after **500ms** and returns vehicle to safe stop.

---

### 5.6 ECU Node 6: HMI & Body Controls (HMI / `hmi.yaml` / `0x102` Series on Low Bus)
- **Target ECU**: Dashboard HMI & Lighting Body Controller
- **Wire Message**: `VCU_LIGHT_REQ` & `VCU_HMI_STAT` (`0x102`, 50ms period, Low Bus)

#### Signal-by-Signal Test Steps:
1. **Signal: `headlight_beam` (Bits 0-1, Enum: 0=Off, 1=Low Beam, 2=High Beam)**:
   - Go to **Bench** tab -> **HMI Panel**.
   - Toggle Headlight to **Low Beam**. Observe byte 0 payload in Live CAN (`0x01`). Verify physical low beam LED activates.
   - Toggle Headlight to **High Beam**. Verify high beam relay engages.
2. **Signal: `turn_signal` (Bits 2-3, Enum: 0=Off, 1=Left, 2=Right, 3=Hazard)**:
   - Select **Turn Left (1)**: Verify left turn indicator flashes at 1.5 Hz on instrument cluster.
   - Select **Turn Right (2)**: Verify right turn indicator flashes at 1.5 Hz.
   - Select **Hazard (3)**: Verify both indicators flash synchronously.
3. **Signal: `horn_req` (Bit 4, 1-bit boolean)**:
   - Toggle Horn ON: Verify 12V horn relay energizes immediately.
4. **Signal: `drive_mode_stat` (Bits 8-11, Enum: 0=Eco, 1=Normal, 2=Sport)**:
   - Switch mode to **Sport**. Verify dashboard display changes to Sport theme and speed limits adjust accordingly.

---

### 5.7 ECU Node 7: Real-Time ECU Node (RT / `rt.yaml` / `0x200` Series on High Bus)
- **Target ECU**: RT Node (AURIX / ESP32-S3 Real-Time Powertrain Controller)
- **Telemetry Message**: `RT_STATUS` (`0x200`, 50ms period, High Bus)

#### Signal-by-Signal Test Steps:
1. **Signal: `rtos_stack_watermark` (Bits 0-15, 16-bit uint, Bytes)**:
   - Inspect Live CAN for `RT_STATUS`. Verify FreeRTOS stack high-water mark remains > 512 bytes across all tasks.
2. **Signal: `cpu_load_pct` (Bits 16-23, 8-bit uint, 0-100%)**:
   - Verify CPU utilization stays below 70% during peak drive command transmission.
3. **Signal: `twai_tec_rec` (Bits 24-39, Bytes 3-4, TEC & REC Error Counters)**:
   - Verify Transmit Error Counter (TEC) and Receive Error Counter (REC) remain at `0`.
   - Disconnect CAN High bus terminator: Observe TEC rise. Confirm soft recovery logic triggers before bus-off lockup occurs.

---

### 5.8 ECU Node 8: System ECU Node (SYS / `sys.yaml` / `0x201` Series on Low Bus)
- **Target ECU**: SYS Node (ESP32-S3 Body & Auxiliary System Controller)
- **Telemetry Message**: `SYS_STATUS` (`0x201`, 50ms period, Low Bus)

#### Signal-by-Signal Test Steps:
1. **Signal: `gpio_input_bitmask` (Bits 0-15, 16-bit bitmask)**:
   - Press brake pedal physical switch: Observe bit 0 change `0 -> 1`.
   - Press side-stand kill switch: Observe bit 1 change `0 -> 1`. Confirm VCU forces Neutral gear.
2. **Signal: `adc_pedal_raw` (Bits 16-31, 16-bit uint, mV)**:
   - Depress accelerator pedal slowly from 0% to 100%.
   - Verify raw ADC voltage scales smoothly from 800 mV (idle) to 4200 mV (full throttle) without signal dropout.
3. **Signal: `temp_ambient_raw` (Bits 32-47, 16-bit signed, 0.1 °C)**:
   - Verify ambient temperature sensor reports realistic values (e.g. `250` for 25.0 °C).

---

### 5.9 ECU Node 9: Network Diagnostics & Sync (NET / `network.yaml` / `0x010` & `0x700` Series)
- **Target Module**: Network Diagnostic & Time Sync Subsystem
- **Messages**: `NET_SYNC` (`0x010`), `NET_DIAG_REQ` (`0x700`, OBD/UDS Diagnostic Gate)

#### Signal-by-Signal Test Steps:
1. **Signal: `epoch_timestamp_ms` (Message `NET_SYNC` / `0x010`, 64-bit uint)**:
   - Observe `NET_SYNC` on High and Low buses. Verify 1000ms periodic epoch timestamp broadcast matches PC time.
2. **Diagnostic Service: `read_dtc_fault_memory` (Message `NET_DIAG_REQ` / `0x700`)**:
   - Send diagnostic read request via REST API:
     ```powershell
     Invoke-RestMethod -Uri "http://127.0.0.1:8001/api/v1/events" -Method Get
     ```
   - Verify active Diagnostic Trouble Codes (DTCs) are retrieved and parsed with human-readable fault descriptions.

---

## 6. Complete Backend REST & WebSocket API Reference

The backend API listens at `http://127.0.0.1:8001/api/v1` (OpenAPI docs at `http://127.0.0.1:8001/docs`).

| Group | Method | Path | Summary / Description |
|---|---|---|---|
| **Health** | `GET` | `/api/v1/status` | System health, adapter status, protocol hashes, session snapshot. |
| **Observation** | `GET` | `/api/v1/state` | Latest state store for all CAN signals. |
| | `GET` | `/api/v1/history` | Historical signal trace buffer. |
| | `GET` | `/api/v1/topology` | Bus topology and frame rate statistics. |
| | `WS` | `/api/v1/stream` | Real-time WebSocket frame stream. |
| **Session** | `GET` | `/api/v1/sessions` | Get current active session details. |
| | `POST` | `/api/v1/sessions` | Create or reset session. |
| | `POST` | `/api/v1/sessions/{id}/profile` | Switch transport profile (`pure_software`, `bench_test`, `full_vehicle`). |
| | `POST` | `/api/v1/sessions/{id}/bench-tx` | Arm or disarm Bench TX safety gate. |
| | `POST` | `/api/v1/sessions/{id}/stop-all` | **Emergency Stop**: Cancel all background transmissions immediately. |
| **Control** | `POST` | `/api/v1/control/intent` | Send drive intent (`throttle`, `steer`, `gear`, `mode`). Schedules `HOST_DRIVE_CMD`. |
| | `POST` | `/api/v1/control/release` | Release active drive intent manually. |
| | `POST` | `/api/v1/control/direct` | Command direct actuator channel (`motor`, `steering`, `brake`). |
| **Analysis** | `POST` | `/api/v1/analysis/host-drive` | Start automated drive profile generator (Sine wave / Trapezoid speed tests). |
| | `POST` | `/api/v1/analysis/stop` | Stop automated analysis driver. |
| **HMI** | `POST` | `/api/v1/hmi/mode` | Set vehicle drive mode (Eco, Normal, Sport, Reverse). |
| | `POST` | `/api/v1/hmi/power` | Set ignition / power state (Off, Accessory, On, Ready). |
| **Inject** | `POST` | `/api/v1/injections/preview` | Preview fault injection frame payload before sending. |
| | `POST` | `/api/v1/injections` | Start a single-shot, periodic, or override fault injection job. |
| | `DELETE`| `/api/v1/injections/{job_id}` | Stop specific fault injection job. |
| **Synthetic**| `GET` | `/api/v1/synthetic-peers` | List active synthetic peer simulations. |
| | `POST` | `/api/v1/synthetic-peers/start` | Start synthetic MCU, SES, or BMS peer simulation. |
| | `POST` | `/api/v1/synthetic-peers/stop` | Stop synthetic peer simulation. |
| **Recordings**|`POST` | `/api/v1/recordings` | Start recording live CAN traffic. |
| | `POST` | `/api/v1/recordings/{id}/stop` | Stop active recording. |
| | `GET` | `/api/v1/recordings/{id}/export/vector` | Export recording as **Vector BLF + DBC ZIP package**. |
| | `POST` | `/api/v1/recordings/{id}/playback` | Replay recorded CAN session onto active transport. |
| **Diagnostics**|`GET`| `/api/v1/events` | Diagnostic event history. |
| | `GET` | `/api/v1/episodes` | Fault episode log history. |
| | `GET` | `/api/v1/evidence/{id}` | Download incident evidence snapshot package. |
| **Protocol** | `GET` | `/api/v1/protocol/dictionary` | Get complete system CAN dictionary spec. |
| | `POST` | `/api/v1/protocol/encode` | Encode JSON signal dict into raw 8-byte CAN payload. |
| | `POST` | `/api/v1/protocol/decode` | Decode raw 8-byte CAN payload into JSON signals. |

---

## 7. Command-Line Utilities & Automated QA Script Suite

The Control Toolkit includes a suite of command-line tools and test scripts located in `control-toolkit/backend/scripts/`:

### 7.1 `control_drive_probe.py`
- **Purpose**: Autonomous live API drive probe script for headless integration testing.
- **Usage**:
  ```powershell
  python control-toolkit/backend/scripts/control_drive_probe.py
  ```
- **Function**: Automatically arms Bench TX via REST, sends a sequence of forward, reverse, and steering intents, verifies `HOST_DRIVE_CMD` periodic transmission, and tests watchdog timeout.

### 7.2 `canalyst_preflight.py`
- **Purpose**: Passive hardware preflight check for physical bench setup.
- **Usage**:
  ```powershell
  python control-toolkit/backend/scripts/canalyst_preflight.py
  ```
- **Function**: Probes USB devices, tests PyUSB bindings, checks CANalyst-II adapter channels 0 and 1 without transmitting any frames.

### 7.3 `dual_bus_api_qa.py`
- **Purpose**: Comprehensive API test harness for dual-bus validation.
- **Usage**:
  ```powershell
  python control-toolkit/backend/scripts/dual_bus_api_qa.py
  ```
- **Function**: Validates REST endpoints, WebSocket message frames, session concurrency locks, and message encoding/decoding parity.

### 7.4 `combination_matrix_qa.py`
- **Purpose**: QA test matrix runner testing all permutations of modes, profiles, and transport configurations.
- **Usage**:
  ```powershell
  python control-toolkit/backend/scripts/combination_matrix_qa.py
  ```

### 7.5 `software_only_recipe_qa.py`
- **Purpose**: Executes software-only automated recipe tests for CI/CD pipelines.
- **Usage**:
  ```powershell
  python control-toolkit/backend/scripts/software_only_recipe_qa.py
  ```

---

## 8. Step-by-Step Task Workflows

---

### Workflow 1: System Startup & Operational Readiness Check
1. Open Terminal 1: Run `npm run toolkit:api` (API starts at `http://127.0.0.1:8001`).
2. Open Terminal 2: Run `npm run toolkit:ui` (Vite UI starts at `http://127.0.0.1:5173`).
3. Open browser to **`http://127.0.0.1:5173/`**.
4. Verify topbar shows **Stream Live** (green).
5. Click **Overview** tab: Confirm backend status is `RUNNING`.

---

### Workflow 2: Manual Driving via Keyboard & Speed/Yaw Sliders
1. Click **Arm Bench TX** in the Topbar.
2. Click **Drive Console** in the sidebar.
3. Select **D** (Drive) gear button.
4. Press and hold **W** key (throttle forward). Use **A** / **D** keys to steer left/right.
5. Watch the 3D vehicle graphic turn and accelerate in real-time.
6. Release all keys: Observe host stale watchdog zeroing speed after 500ms.

---

### Workflow 3: Switching to Real Hardware Mode (CANalyst-II)
1. Plug in CANalyst-II USB adapter (`CH0=High`, `CH1=Low` @ 500k).
2. Click **Real** toggle in topbar.
3. Observe mode switch to Real (amber).
4. *If USB disconnected*: System displays `503 Service Unavailable`, rolls back to **Computer** mode safely, and disarms Bench TX.

---

### Workflow 4: Direct SES Steering Control with 4-bit Rolling Counters
1. Ensure **Bench TX** is armed.
2. Click **Control** on sidebar.
3. Locate **Steering Channel (`VCU_SES_REQ`)** card, toggle switch to ON.
4. Set Target Angle Raw to `200` (20.0° turn).
5. Open **Live CAN** tab: Observe `VCU_SES_REQ` (`0x100`) transmitting every 20ms with `rolling_counter` incrementing 0 to 15 continuously.

---

### Workflow 5: Running Automated Analysis Drive Profiles (Sine/Trapezoid)
1. Arm **Bench TX**.
2. Trigger the automated analysis driver via API:
   ```powershell
   Invoke-RestMethod -Uri "http://127.0.0.1:8001/api/v1/analysis/host-drive" -Method Post -Body '{"profile": "sine", "duration_sec": 10}' -ContentType "application/json"
   ```
3. Click **Drive Console**: Watch the vehicle smoothly execute a 10-second sinusoidal steering sweep automatically.

---

### Workflow 6: Executing a Chaos Fault Injection Campaign
1. Click **Inject** on sidebar.
2. Select **Low Bus**, **Target Message: VCU_SES_REQ**, **Fault Type: Packet Drop**, **Drop Pattern: 50% loss**.
3. Click **Start Injection**.
4. Open **Diagnostics** tab: Observe steering signal jitter increase.
5. Return to **Inject** tab and click **Stop Injection**.

---

### Workflow 7: Hardware-in-the-Loop (HIL) Simulation with Synthetic Peers
1. Connect physical VCU board to CANalyst-II hardware adapter.
2. Click **Bench** on sidebar.
3. Toggle ON **Simulated MCU** and **Simulated BMS**.
4. Power on physical VCU: VCU receives realistic motor RPM and battery telemetry, enabling standalone VCU testing.

---

### Workflow 8: Recording CAN Traffic & Exporting to Vector CANalyzer (BLF/DBC)
1. Click **Live CAN** or **Recordings** tab.
2. Click **Start Recording**.
3. Run your drive test sequence.
4. Click **Stop Recording**.
5. Click **Export Vector Package (ZIP)**: Download the package containing `.blf` log files and `.dbc` signal database files for direct analysis in Vector CANalyzer / CANoe.

---

### Workflow 9: Headless Automated Testing via CLI Scripts
1. Ensure API backend is running at `127.0.0.1:8001`.
2. Open terminal and run:
   ```powershell
   python control-toolkit/backend/scripts/control_drive_probe.py
   ```
3. The probe script automatically exercises drive intent, verifies scheduler execution, tests watchdog safety timeouts, and reports PASS/FAIL status.

---

## 9. Comprehensive Troubleshooting & Diagnostics Matrix

| Symptom | Root Cause | Exact Resolution Steps |
|---|---|---|
| **UI Topbar shows "Offline"** | API backend on `8001` is not running. | Run `npm run toolkit:api` in Terminal 1. Verify `http://127.0.0.1:8001/api/v1/status`. |
| **Real Mode toggle returns HTTP 503** | CANalyst-II USB device missing or WinUSB driver not assigned. | Plug USB in. Run Zadig to assign `WinUSB` or `libusbK` driver to CANalyst device. |
| **Drive intent sends but no frames transmit** | Bench TX safety interlock is disarmed. | Click **"Arm Bench TX"** in topbar. |
| **Direct Steering command returns HTTP 409** | Drive Console Kinematics mode active (mutual exclusion). | Stop drive intent in Drive Console before activating direct actuator channels. |
| **Keyboard driving unresponsive** | Drive Console window not focused or gear in Neutral (`N`). | Click inside Drive Console canvas and select **D** (Drive) gear. |
| **Port 5173 / 8001 in use** | Stray background node/uvicorn process. | Run `Stop-Process` script or kill existing processes via Task Manager. |
| **Vector BLF export fails** | Recording ID not found or empty recording. | Ensure recording was started and received CAN frames before stopping. |
| **CAN Bus-Off on physical nodes** | Missing termination resistor or bad ground. | Verify $60\,\Omega$ termination resistance across CAN-H/L and verify ground reference. |
