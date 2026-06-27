# E-Trike — Drive-by-Wire Control System

Autonomous electric tricycle with five-node distributed CAN bus architecture, ROS 2
perception/planning, and ISO 26262 EGAS 3-level motor safety.

**Version:** v0.0.5-alpha

## What It Does

- **Steer-by-wire** (SYNTREE EPS-C) and **brake-by-wire** (SYNTREE SEB) via CAN
- **Autonomous mode** — Jetson Orin ROS 2 + Autoware.Auto → CAN commands → RT kinematics → actuators
- **Manual mode** — rider throttle, gear selector, and brake lever pass-through
- **Emergency stop** — hardwired GPIO + CAN 0x001 with steering ramp-to-zero
- **EGAS 3-level motor safety** — dedicated STM32 isolates motor actuation from body control

## Architecture

| Node | Hardware | Role |
|------|----------|------|
| Jetson Orin | NVIDIA | ROS 2 perception, planning, Autoware.Auto bridge |
| RT ESP32-S3 | ESP32-S3 | Realtime physics, steering control, CAN gateway (high↔low) |
| SYS ESP32-S3 | ESP32-S3 | Safety monitor, brake control, body/lights, diagnostics |
| MTR STM32 | STM32 | Motor actuation (DAC 0–5V, gear relays, throttle ADC) |
| PWT ESP32-S3 | ESP32-S3 | Powertrain CAN gateway (low↔powertrain 250k) |

Three CAN buses: high-level (500k), low-level (500k), powertrain (250k).
Full architecture: [`architecture.md`](architecture.md) · CAN IDs: [`can-dictionary.md`](can-dictionary.md)

## Project Structure

```
rt-esp32/       Realtime physics + steering firmware (PlatformIO)
sys-esp32/      Safety + body control firmware (PlatformIO)
mtr-stm32/      Motor control firmware (PlatformIO, STM32 HAL stubs)
pwt-esp32/      Powertrain gateway firmware (PlatformIO)
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

Alpha-stage prototype. Bench-tested with CANalyst-II analyzer. Motor actuation
currently on SYS ESP32-S3 (MTR STM32 migration pending hardware). HIL testing
and on-vehicle validation not yet performed.
