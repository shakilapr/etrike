# E-Trike — Autonomous Drive-by-Wire Vehicle Platform

A distributed, drive-by-wire **autonomous vehicle (AV)** platform built on an
electric tricycle chassis. The system coordinates five ECUs across three CAN
buses, with a ROS 2 / Autoware perception and planning stack and an EGAS
3-level motor safety architecture.

> **Scope of this repository:** This repo contains the **low-level** embedded
> control layer (RT, SYS, MTR, PWT firmware, CAN protocol, simulation, and
> tooling). The **high-level** autonomy stack (ROS 2 / Autoware Universe on
> Jetson) lives in a separate repository:
> [shakilapr/etrike-av](https://github.com/shakilapr/etrike-av).

**Version:** v0.8.0-alpha

## Features

- **Steer-by-wire and brake-by-wire** via CAN-connected actuator modules (EPS-C, SEB).
- **Autonomous mode** — the AV stack: Jetson Orin running ROS 2 / Autoware
  Universe issues planning commands over CAN; RT performs kinematics and drives
  the actuators.
- **Manual mode** — rider brake lever is passed through SYS to the brake
  actuator; motor actuation remains blocked pending MTR hardware completion.
- **Emergency stop** — hardwired GPIO plus CAN `0x001`, with steering
  ramp-to-zero and maximum brake.
- **EGAS 3-level motor safety** — function controller (MTR), monitor (SYS), and
  hardwired ESTOP.
- **Dual-bus gateway** — RT bridges the high-level and low-level CAN buses.
- **Hardware-in-the-loop tooling** — CAN simulation, monitoring, and injection
  via a web debug tool, plus a native (host-side) test harness.

## System Architecture

The platform is split into a **high-level** autonomy stack and a **low-level**
embedded control layer, connected over three CAN buses.

**High level** — the autonomy compute node:

| Node | Platform | Role |
|------|----------|------|
| Jetson | NVIDIA Jetson Orin | ROS 2 / Autoware Universe perception, planning, and ROS→CAN bridge |

**Low level** — the embedded ECUs that actuate and safeguard the vehicle:

| ECU | Platform | Role |
|-----|----------|------|
| RT | ESP32-S3 | Realtime kinematics, steering control, high↔low CAN gateway |
| SYS | ESP32-S3 | Safety and mode authority, body control, EGAS L2 monitor |
| MTR | STM32 | Planned motor actuation (hardware layer incomplete) |
| PWT | ESP32-S3 | Standalone 250 kbit/s powertrain node |

**CAN buses:**

| Bus | Speed | Nodes |
|-----|-------|-------|
| High-level | 500 kbit/s | Jetson, RT |
| Low-level | 500 kbit/s | RT, SYS, MTR, EPS-C (steering), SEB (brake) |
| Powertrain | 250 kbit/s | PWT, DC-DC converter, motor controller (telemetry) |

> **Deployment status:** RT high-to-low CAN is implemented. MTR hardware
> initialization and ESTOP are incomplete, so no vehicle motor-actuation path
> is approved. PWT has a single 250 kbit/s CAN interface and is not a
> low-to-powertrain gateway.

Detailed architecture, CAN catalog, and design rationale are documented in
[`architecture.md`](architecture.md). The complete bit-level CAN catalog is in
[`can-dictionary.md`](can-dictionary.md).

## Responsibility Split

| Concern | Jetson | RT | SYS | MTR | PWT |
|---------|--------|-----|-----|-----|-----|
| Perception / planning | ✓ | | | | |
| ROS 2 → CAN bridge | ✓ | | | | |
| CAN gateway (high ↔ low) | | ✓ | | | |
| CAN gateway (low ↔ powertrain) | | | | | planned |
| Tricycle kinematics | | ✓ | | | |
| Steering compute + CAN TX | | ✓ | | | |
| Steering safety (clamp, following error) | | ✓ | | | |
| Obstacle speed limit | | ✓ | | | |
| Command staleness watchdog | | ✓ | | | |
| ESTOP GPIO + button | | | ✓ | planned | |
| Brake lever → CAN | | | ✓ | | |
| DC-DC converter control | | | planned | | ✓ |
| Heartbeat monitoring | | ✓ | ✓ | | planned |
| Mode switch | | | ✓ | | |
| Throttle ADC / DAC / gear I/O | | | retired | planned | |
| Motor feedback CAN TX | | | | planned | |
| Lights / indicators / 12V relay | | | ✓ | | |
| System diagnostics | | | ✓ | | |

> Motor I/O is not approved on SYS or MTR until the MTR HAL, CAN, ADC, DAC, and
> direct ESTOP hardware have been implemented and tested.

## Repository Structure

```
rt-esp32/         Realtime physics, steering, and CAN gateway firmware (PlatformIO)
sys-esp32/        Safety and body control firmware (PlatformIO)
mtr-stm32/        Planned motor-control firmware (STM32; hardware layer incomplete)
pwt-esp32/        Standalone 250 kbit/s powertrain node firmware (PlatformIO)
jetson/           ROS 2 / Autoware Universe bridge
shared/           CAN protocol, endian helpers, shared configuration
protocol/         CAN wire-contract definitions, generated codecs, and tooling
simulation/       TypeScript dual-bus simulation (Vitest)
native-test/      C++ host tests with FreeRTOS kernel (CTest)
debug-tool/       Web UI and REST API for CAN monitoring and injection
control-toolkit/  GUI toolbox for debugging, testing, and kinematics simulation
docs/             Architecture, wiring, bench test plans, and safety docs
tex/              LaTeX documentation generator
```

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) for firmware builds.
- Node.js and npm for the simulation, debug tool, and control toolkit.
- A C++ toolchain (CMake/CTest) for the native test harness.
- ESP-IDF 5.0 or later for the ESP32 firmware.

### Build Firmware

```bash
cd rt-esp32  && pio run
cd sys-esp32 && pio run
cd pwt-esp32 && pio run
```

### Run Tests

```bash
cd simulation       && npm test          # TypeScript dual-bus simulation (Vitest)
cd native-test/build && ctest            # C++ host tests
cd debug-tool/backend && npm test        # Debug tool backend API tests
cd debug-tool/ui      && npm test        # Debug tool frontend decoder tests
```

### Run the Debug Tool (no hardware required)

```bash
cd debug-tool/simulator && npm start
cd debug-tool/backend   && npm run dev
cd debug-tool/ui        && npm run dev    # → http://127.0.0.1:5173
```

## Documentation

- [`architecture.md`](architecture.md) — topology, CAN catalog, mode/ESTOP state,
  design principles, and per-ECU detail.
- [`can-dictionary.md`](can-dictionary.md) — bit-level CAN message catalog.
- [`docs/`](docs/) — wiring harness, bench test plans, safety documentation, and
  hardware manuals.
- [`protocol/README.md`](protocol/README.md) — CAN wire-contract model and
  codegen tooling.

## Status

Alpha-stage prototype. Bench-tested with a CANalyst-II analyzer. No motor
actuation path is approved: SYS direct motor I/O is disabled and the MTR
hardware layer is incomplete. Hardware-in-the-loop testing and on-vehicle
validation have not been performed.
