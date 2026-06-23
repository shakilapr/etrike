# E-Trike Diagnostic & Debug Tool

CAN bus monitor, analyzer, and command injector for the E-Trike vehicle control system. Connects to **both CAN buses** (high + low) via the ESP32-S3's dual TWAI controllers. Streams all 28 CAN messages to a web UI over USB, and can inject commands on either bus.

## Architecture

```
ESP32-S3 ──USB──► Computer ──WebSocket──► Browser
  │                │
  │                ├── Backend reads COM port (JSON Lines)
  │                ├── Stores to SQLite
  │                └── Fans out to UI via WebSocket (:3000/ws)
  │
  ├── High CAN Bus (TWAI0, GPIO 5/4, 500 kbit/s)
  └── Low CAN Bus  (TWAI1, GPIO 17/16, 500 kbit/s)
```

## Why Dual-Bus

Single-bus monitoring leaves you blind to RT's outputs and actuator responses:

```
Inject 0x300 (high) → RT produces 0x204 + 0x169 (low) → EPS-C responds 0x201 (low)
                         ↑ these are invisible on high bus
```

Dual-bus gives full pipeline visibility. ESP32-S3 has two built-in TWAI controllers — just add a second SN65HVD230 transceiver.

## Components

| Folder | Tech | Purpose |
|--------|------|---------|
| `backend/` | Node.js + TypeScript + Fastify | REST API, WebSocket, serial port reader, SQLite |
| `ui/` | Svelte + TypeScript + Vite | Dashboard, CAN monitor (dual-bus color), injector + keyboard control, stats |
| `debug-esp32/` | PlatformIO (C++17), ESP-IDF | Firmware: dual TWAI, 28-ID decoder, JSON Lines over USB CDC |
| `simulator/` | Node.js + TypeScript | Dual-bus device simulator for UI dev and E2E testing |
| `e2e/` | Playwright | Full-stack tests |

## Quick Start

```bash
# Backend + UI (no hardware)
cd debug-tool/backend && npm install && npm run dev    # :3000
cd debug-tool/ui && npm install && npm run dev          # :5173

# Simulator (no hardware)
cd debug-tool/simulator && npm install && npm run dev

# Firmware (ESP32-S3 + 2× SN65HVD230)
cd debug-tool/debug-esp32 && pio run -t upload

# Tests
cd debug-tool/e2e && npm install && npm test
```

## Serial Protocol — JSON Lines

```json
// ESP32 → Computer (CAN frame with bus field)
{"ts":890123,"bus":"low","id":"0x204","name":"RT_DRIVE_CMD","dlc":5,"data":[0,0,7,208,1],"decoded":{"motor_speed_mmps":2000,"gear":1,"gear_name":"D"}}

// Computer → ESP32 (inject on specific bus)
{"cmd":"send","bus":"low","id":"0x204","dlc":5,"data":[0,0,7,208,1,0,0,0]}

// ESP32 → Computer (ack)
{"type":"cmd_ack","cmd":"send","status":"ok"}
```

## Keyboard Control

When the injector has focus, drive the vehicle directly from the keyboard:

| Key | Action |
|-----|--------|
| `W` `S` | Speed ±200 mm/s |
| `A` `D` | Steer left/right |
| `↑` `↓` `←` `→` | Fine speed/yaw adjust |
| `Space` (×2) | **ESTOP** |
| `B` / `R` | Brake / Release |
| `G` | Cycle gear N→D→S→R |
| `Esc` | Kill — zero everything |

Each keypress sends an immediate CAN frame. No form, no submit — real-time control.

## Monitored CAN IDs

| Bus | Count | Key IDs |
|-----|-------|---------|
| High | 13 | 0x001, 0x011, 0x120, 0x206, 0x210, 0x220, 0x300, 0x301, 0x302, 0x400, 0x600, 0x7FC, 0x7FD |
| Low | 22 | 0x001, 0x011, 0x012, 0x110, 0x120, 0x169, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x302, 0x600, 0x6FA, 0x6FB, 0x721, 0x731, 0x741, 0x7B9, 0x7FD, 0x7FE |

Full architecture: [`debug-tool-architecture.md`](debug-tool-architecture.md)
