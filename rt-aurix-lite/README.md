# RT-AURIX-Lite — Consolidated E-Trike Controller

Single AURIX TC3xx variant that combines RT (realtime physics, steering, CAN gateway) and SYS (safety, brake, body control, motor actuation) into one microcontroller on a single CAN bus.

## What this is

A cost-reduced variant of the [distributed E-Trike architecture](../architecture.md) that runs all realtime control on one AURIX TC3xx instead of two ESP32-S3s. The MTR STM32 (EGAS Level 1 motor controller) and Jetson Orin (ROS 2 perception) remain separate.

## Architecture

See [`rt-aurix-lite-architecture.md`](rt-aurix-lite-architecture.md) for the full specification.

## Key Differences from Distributed

| | Distributed | AURIX Lite |
|---|---|---|
| MCUs | 2× ESP32-S3 | 1× AURIX TC3xx |
| CAN buses | 2 (high + low) | 1 |
| CAN gateway | Yes (RT bridges) | No (single bus) |
| Heartbeats | 3 nodes per bus | 2 nodes (AURIX + Jetson) |
| Task count | 8 (RT) + 15 (SYS) = 23 | 16 (merged) |

## CAN Protocol

Uses the **same CAN IDs, signal layouts, and protocol definitions** as the distributed architecture. See [`can-dictionary.md`](../can-dictionary.md). All 25 CAN IDs coexist on one bus — no ID conflicts.

## Hardware

Target: **AURIX TC3xx Lite Kit** (KIT_A2G_TC387_LITE)
- MCMCAN for single CAN bus
- I2C0 for IMU (optional)
- GPIO for lights, buttons, relays, watchdog

## Build

Placeholder — implementation files in `src/` to follow.

```
rt-aurix-lite/
├── rt-aurix-lite-architecture.md   ← Architecture spec
├── README.md                       ← This file
└── src/                            ← Source files (TBD)
    └── config.h                    ← Merged pin/config
```

## Status

Architecture documented. Source implementation pending.
