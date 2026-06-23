# E-Trike Diagnostic & Debug Tool

CAN bus monitor, analyzer, and command injector for the E-Trike vehicle control system. Connects to one or both CAN buses over USB. Streams decoded frames to a web UI, and can inject commands to simulate any node — Jetson, RT, SYS, MTR, or SYNTREE actuators.

## Architecture

```
ESP32-S3 ──USB──► Computer ──WebSocket──► Browser
  │                │
  │                ├── Backend reads COM port (JSON Lines)
  │                ├── Stores to SQLite
  │                └── Fans out to UI via WebSocket (:3000/ws)
  │
  ├── Bus A: TWAI (GPIO 5/4, 500 kbit/s) — high or low bus
  └── Bus B: MCP2515 SPI (GPIO 36–40, 500 kbit/s) — optional second bus
```

## Single-bus or dual-bus?

ESP32-S3 has **one** TWAI controller. Single-bus (one SN65HVD230) covers most bench sessions — plug into whichever bus you're testing. For full pipeline visibility (watch 0x300→0x204→0x201 in real time), add the MCP2515 module for the second bus. Same code, same JSON format — the `bus` field tells the UI which bus each frame came from.

## Components

| Folder | Tech | Purpose |
|--------|------|---------|
| `backend/` | Node.js + TypeScript + Fastify | REST API, WebSocket, serial port reader, SQLite |
| `ui/` | Svelte + TypeScript + Vite | Dashboard, CAN monitor (dual-bus color), injector + keyboard control, stats |
| `debug-esp32/` | PlatformIO (C++17), ESP-IDF | Firmware: TWAI + optional MCP2515, 28-ID decoder, JSON Lines over USB CDC |
| `simulator/` | Node.js + TypeScript | Dual-bus device simulator for UI dev and E2E testing |
| `e2e/` | Playwright | Full-stack tests |

## Quick Start

```bash
# Backend + UI (no hardware)
cd debug-tool/backend && npm install && npm run dev    # :3000
cd debug-tool/ui && npm install && npm run dev          # :5173

# Simulator (no hardware)
cd debug-tool/simulator && npm install && npm run dev

# Firmware (ESP32-S3 + 1× SN65HVD230 for single-bus, add MCP2515 for dual-bus)
cd debug-tool/debug-esp32 && pio run -t upload

# Tests
cd debug-tool/e2e && npm install && npm test
```

## Serial Protocol — JSON Lines

```json
// ESP32 → Computer (CAN frame with bus field)
{"ts":890123,"bus":"low","id":"0x204","name":"RT_DRIVE_CMD","dlc":5,"data":[0,0,7,208,1],"decoded":{"motor_speed_mmps":2000,"gear":1,"gear_name":"D"}}

// ESP32 → Computer (bus status — active/inactive per controller)
{"type":"stats","buses":{"high":{"active":true,"fps":247},"low":{"active":false,"fps":0}}}

// Computer → ESP32 (inject on specific bus)
{"cmd":"send","bus":"low","id":"0x204","dlc":5,"data":[0,0,7,208,1,0,0,0]}

// ESP32 → Computer (ack)
{"type":"cmd_ack","cmd":"send","status":"ok"}
```

## UI — Two Bus Tabs

The monitor has separate tabs for each bus — no mixing, no confusion:

- **[High Bus 🟢]** — shows only high-bus CAN IDs (Jetson↔RT traffic)
- **[Low Bus 🔴]** — shows only low-bus CAN IDs (RT↔actuator traffic)

A disconnected bus shows 🔴 and a "plug cable here" diagram. The injector's CAN ID dropdown is filtered to the selected bus — you can't accidentally inject 0x169 on the high bus.

## Keyboard Control

Inject on the currently selected bus. `Tab` switches buses. Keys adapt:

| Key | High Bus | Low Bus |
|-----|----------|---------|
| `W` `S` | 0x300 speed ±200 | 0x204 speed ±200 |
| `A` `D` | 0x300 yaw ±87 | 0x169 angle ±5° |
| `Space` (×2) | 0x001 ESTOP | 0x001 ESTOP |
| `B` / `R` | 0x301 brake/release | 0x205 brake kPa set/release |
| `Esc` | Zero 0x300+0x301 | Zero 0x204+0x205+0x169 |

## Monitored CAN IDs

| Bus | Count | Key IDs |
|-----|-------|---------|
| High | 13 | 0x001, 0x011, 0x120, 0x206, 0x210, 0x220, 0x300, 0x301, 0x302, 0x400, 0x600, 0x7FC, 0x7FD |
| Low | 22 | 0x001, 0x011, 0x012, 0x110, 0x120, 0x169, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x302, 0x600, 0x6FA, 0x6FB, 0x721, 0x731, 0x741, 0x7B9, 0x7FD, 0x7FE |

Full architecture: [`debug-tool-architecture.md`](debug-tool-architecture.md)
