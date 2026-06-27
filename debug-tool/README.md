# E-Trike Diagnostic & Debug Tool

CAN bus monitor, analyzer, and command injector for the E-Trike vehicle control system. Connects via USB serial (ESP32-S3 bridge) or CANalyst-II USB analyzer. Streams decoded frames to a web UI, and can inject commands to simulate any node — Jetson, RT, SYS, MTR, or SYNTREE actuators.

## Architecture

```
ESP32-S3 ──Wi-Fi──► MQTT Broker ──► Backend ──WebSocket──► Browser
  │                     │                │
  │                     │                ├── REST API (:3000)
  │                     │                ├── In-memory frame store
  │                     │                └── Fans out to UI via WebSocket (:3000/ws)
  │                     │
  ├── Bus A: TWAI (GPIO 5/4, 500 kbit/s) — high or low bus
  └── Bus B: MCP2515 SPI (GPIO 36–40, 500 kbit/s) — optional second bus
```

The backend runs an **embedded MQTT broker** (aedes) — no external MQTT server required for local development. The ESP32 publishes CAN frames as JSON to MQTT topics; the backend subscribes, decodes, stores, and fans out to the browser.

## Single-bus or dual-bus?

ESP32-S3 has **one** TWAI controller. Single-bus (one SN65HVD230) covers most bench sessions — plug into whichever bus you're testing. For full pipeline visibility (watch 0x300→0x204→0x201 in real time), add the MCP2515 module for the second bus. Same code, same JSON format — the `bus` field tells the UI which bus each frame came from.

## Components

| Folder | Tech | Purpose |
|--------|------|---------|
| `backend/` | Node.js + TypeScript + Fastify | REST API, WebSocket, embedded MQTT broker, in-memory store |
| `ui/` | Svelte + TypeScript + Vite | Dual-bus dashboard, CAN monitor with bus tabs, injector, stats |
| `debug-esp32/` | PlatformIO (C++), ESP-IDF | Firmware: TWAI + optional MCP2515, 28-ID decoder, MQTT publisher |
| `simulator/` | Node.js + TypeScript | Dual-bus CAN simulator via MQTT for UI development and testing |
| `e2e/` | Playwright | Full-stack smoke tests |

## Quick Start

```bash
# Backend + UI (no hardware)
cd debug-tool/backend && npm install && npm run dev    # :3000 (includes embedded MQTT broker)
cd debug-tool/ui && npm install && npm run dev          # :5173

# Simulator (publishes synthetic CAN traffic via MQTT)
cd debug-tool/simulator && npm install && npx tsx src/index.ts

# CANalyst-II (real two-bus USB adapter, after Zadig/WinUSB driver binding)
cd debug-tool/backend && $env:CAN_TRANSPORT="canalystii"; npm run dev

# Firmware (ESP32-S3 + 1× SN65HVD230 for single-bus, add MCP2515 for dual-bus)
cd debug-tool/debug-esp32 && pio run -t upload

# Tests
cd debug-tool/e2e && npm install && npm test
```

## MQTT Topic Structure

| Topic | Direction | Description |
|-------|-----------|-------------|
| `etrike/debug/can/rx/<bus>/<id>` | ESP32 → Backend | CAN frame JSON |
| `etrike/debug/can/stats` | ESP32 → Backend | Per-bus stats (1 Hz) |
| `etrike/debug/status` | ESP32 → Backend | ESP32 online/offline heartbeat (5 s) |
| `etrike/debug/uptime` | ESP32 → Backend | ESP32 uptime seconds |
| `etrike/debug/cmd/send` | Backend → ESP32 | Single CAN frame injection |
| `etrike/debug/cmd/send/periodic` | Backend → ESP32 | Periodic injection start/stop |
| `etrike/debug/cmd/response` | ESP32 → Backend | Command acknowledgment |

## UI — Dual-Bus Tabs

The monitor has bus tabs for filtering:

- **All** — shows all frames from both buses
- **High** — shows only high-bus CAN IDs (Jetson↔RT traffic)
- **Low** — shows only low-bus CAN IDs (RT↔actuator traffic)

Each frame row shows a bus tag. The injector has a bus selector — you can't accidentally inject 0x169 on the high bus.

## Monitored CAN IDs

| Bus | Count | Key IDs |
|-----|-------|---------|
| High | 13 | 0x001, 0x011, 0x120, 0x206, 0x210, 0x220, 0x300, 0x301, 0x302, 0x400, 0x600, 0x7FC, 0x7FD |
| Low | 22 | 0x001, 0x011, 0x012, 0x110, 0x120, 0x169, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x302, 0x600, 0x6FA, 0x6FB, 0x721, 0x731, 0x741, 0x7B9, 0x7FD, 0x7FE |

Full architecture: [`debug-tool-architecture.md`](debug-tool-architecture.md)
