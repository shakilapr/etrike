# E-Trike — Drive-by-Wire Control System

Autonomous electric tricycle with distributed CAN bus architecture, ROS 2
perception/planning, and EGAS 3-level motor safety.

**Version:** v0.4.0-alpha

## What It Does

- Steer-by-wire and brake-by-wire via CAN-connected actuator modules
- **Autonomous mode** — Jetson Orin ROS 2 + Autoware.Auto → CAN commands → RT kinematics → actuators
- **Manual mode** — rider brake lever pass-through; motor actuation remains blocked pending MTR hardware completion
- **Emergency stop** — hardwired GPIO + CAN 0x001 with steering ramp-to-zero
- **EGAS motor safety target** — MTR hardware implementation is incomplete; no vehicle motor-actuation path is approved

## Responsibility Split

| Concern | Jetson | RT | SYS | MTR | PWT |
|---------|--------|-----|-----|-----|-----|
| Perception / planning | ✓ | | | | |
| ROS 2 → CAN bridge | ✓ | | | | |
| CAN gateway (low ↔ high) | | ✓ | | | |
| CAN gateway (low ↔ powertrain) | | | | | planned |
| Tricycle kinematics | | ✓ | | | |
| Steering angle compute + CAN TX | | ✓ | | | |
| Steering boot sync | | ✓ | | | |
| Steering safety (clamp, hard-stops, following error) | | ✓ | | | |
| Obstacle speed limit | | ✓ | | | |
| Command staleness watchdog | | ✓ | | | |
| E-stop GPIO + button | | | ✓ | planned | |
| Brake lever → CAN | | | ✓ | | |
| Brake boot sync + rolling counter | | | ✓ | | |
| DC-DC converter CAN control | | | planned | | ✓ |
| Heartbeat monitoring | | ✓ | ✓ | | planned |
| Mode switch reading | | | ✓ | | |
| Throttle ADC / DAC / gear I/O | | | retired | planned | |
| Motor feedback CAN TX | | | | planned | |
| 12V accessory / lights / indicators | | | ✓ | | |
| System diagnostics | | | ✓ | | |

> Motor I/O is blocked until MTR GPIO, ADC, I2C, CAN, and ESTOP hardware are implemented and validated.

Three CAN buses: high-level (500k), low-level (500k), powertrain (250k).
Full architecture: [`architecture.md`](architecture.md) · CAN IDs: [`can-dictionary.md`](can-dictionary.md)

## Project Structure

```
rt-esp32/       Realtime physics + steering firmware (PlatformIO)
sys-esp32/      Safety + body control firmware (PlatformIO)
mtr-stm32/      Planned motor-control firmware (STM32 hardware layer incomplete)
pwt-esp32/      Standalone 250 kbit/s powertrain node firmware (PlatformIO)
jetson/         ROS 2 Autoware.Auto bridge (autoware_vehicle_bridge)
shared/         CAN protocol, endian helpers, shared config
simulation/     TypeScript dual-bus simulation (Vitest, 332 tests)
native-test/    C++ host tests with FreeRTOS kernel (CTest)
debug-tool/     Web UI + REST API for CAN monitoring/injection
docs/           Architecture, wiring, bench test plans, safety docs
tex/            LaTeX documentation generator
```

## Quick Start

```bash
# Build firmware
cd rt-esp32 && pio run
cd sys-esp32 && pio run

# Run tests
cd simulation && npm test          # 332 TypeScript simulation tests
cd native-test/build && ctest      # C++ native tests
cd debug-tool/backend && npm test  # Backend API tests
cd debug-tool/ui && npm test       # Frontend decoder tests

# Debug tool (no hardware needed)
cd debug-tool/simulator && npm start
cd debug-tool/backend && npm run dev
cd debug-tool/ui && npm run dev     # → http://127.0.0.1:5173
```

## Status

Alpha-stage prototype. Bench-tested with CANalyst-II analyzer. No motor
actuation path is approved: SYS direct motor I/O is disabled and MTR hardware
is incomplete. HIL testing and on-vehicle validation have not been performed.
