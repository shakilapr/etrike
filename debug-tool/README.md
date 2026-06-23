# E-Trike Diagnostic & Debug Tool

CAN bus monitor, analyzer, and command injector for the E-Trike vehicle control system. Connects to the **high-level CAN bus** (Jetson ↔ RT), bridges all traffic to a web UI over Wi-Fi/MQTT, and can inject commands to simulate the Jetson for bench testing — no logic analyzer required.

## Architecture

```
┌── Dev Machine ───────────────────────────────────────────────────────┐
│  ┌──────────────┐   ┌───────────────────────────────────────────┐    │
│  │  UI (Svelte) │◄──│  Backend (Fastify + Aedes MQTT + SQLite)  │    │
│  │  :5173       │   │  :3000 (REST/WS)   :1883 (MQTT)           │    │
│  └──────────────┘   └──────────────────┬────────────────────────┘    │
└────────────────────────────────────────┼────────────────────────────┘
                                         │ MQTT (Wi-Fi)
┌── ESP32-S3 ────────────────────────────┼────────────────────────────┐
│  ┌──────────────┐  ┌───────────────────┴──────────────────────┐     │
│  │ TWAI (CAN)   │  │ MQTT Client                              │     │
│  │ GPIO 5,4     │  │ Publish: all 12 CAN IDs                  │     │
│  │ 500 kbit/s   │  │ Subscribe: cmd/send, cmd/send/periodic   │     │
│  │ RX + TX*     │  └──────────────────────────────────────────┘     │
│  └──────┬───────┘                                                    │
└─────────┼────────────────────────────────────────────────────────────┘
          │
   High-Level CAN Bus (500 kbit/s)
          │
    ┌─────┴─────┐  ┌──────────┐
    │  Jetson   │  │ RT ESP32 │──(low bus)── SYS, MTR, SYNTREE...
    └───────────┘  └──────────┘

* TX only when commanded (CAN injection for testing)
```

## Why High-Level Bus Only?

The high-level bus already carries all system-level data — RT forwards key telemetry from the low bus (`0x011`, `0x120`, `0x600`) and ESTOP is bridged bidirectionally. This means **one CAN transceiver** (built-in TWAI, no MCP2515 needed) gives you:

- All Jetson commands (0x300, 0x301, 0x302, 0x400)
- Safety status (0x001, 0x011, 0x210)
- Actual speed (0x120)
- System diagnostics (0x600)
- All heartbeats (0x7FC, 0x7FD)

Actuator-level frames (SYNTREE 0x169/0x7B9, MTR 0x206, etc.) stay on the low bus and can be added as a v2 feature with a second TWAI or MCP2515.

## Components

| Folder | Tech | Purpose |
|--------|------|---------|
| `backend/` | Node.js + TypeScript + Fastify | REST API, WebSocket stream, embedded MQTT broker (Aedes), SQLite |
| `ui/` | Svelte + TypeScript + Vite | Dashboard, CAN monitor, injector, statistics |
| `debug-esp32/` | PlatformIO (C++17), ESP-IDF | Firmware: CAN RX/TX, MQTT bridge, command injection |
| `simulator/` | Node.js + TypeScript | Software device simulator for UI dev and E2E testing |
| `e2e/` | Playwright | Full-stack tests |

## Quick Start

### Backend + UI (no hardware needed)

```bash
cd debug-tool/backend && npm install && npm run dev    # :3000 + :1883
cd debug-tool/ui && npm install && npm run dev          # :5173
```

### With Simulator (no hardware)

```bash
cd debug-tool/simulator && npm install && npm run dev   # synthetic CAN traffic
```

### Firmware (ESP32-S3 + SN65HVD230)

```bash
cd debug-tool/debug-esp32
pio run -t upload
pio device monitor
```

### E2E Tests

```bash
cd debug-tool/e2e && npm install && npm test
```

## MQTT Topics

| Topic | Dir | Purpose |
|-------|-----|---------|
| `etrike/debug/status` | ← retained | Online/offline |
| `etrike/debug/can/rx/<id>` | ← | Decoded CAN frames (12 topics, one per ID) |
| `etrike/debug/can/stats` | ← | Per-ID counts, bus load (1 Hz) |
| `etrike/debug/cmd/send` | → | Inject single CAN frame |
| `etrike/debug/cmd/send/periodic` | → | Start/stop periodic injection |
| `etrike/debug/cmd/response` | ← | Command ack/error |

## Monitored CAN IDs

| ID | Name | Period |
|----|------|--------|
| `0x001` | SAFETY_ESTOP | Event |
| `0x011` | SYS_SAFETY_STS | 5 Hz |
| `0x120` | SYS_THROTTLE_STS | 100 Hz |
| `0x210` | RT_STATE_RPT | 10 Hz |
| `0x220` | RT_PID_RPT | (reserved) |
| `0x300` | HOST_DRIVE_CMD | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQ | Demand |
| `0x302` | HOST_LIGHT_CMD | Change |
| `0x400` | HOST_OBSTACLE_DIST | 10 Hz |
| `0x600` | SYS_DIAG_RPT | 1 Hz |
| `0x7FC` | JETSON_HEARTBEAT | 2 Hz |
| `0x7FD` | RT_HEARTBEAT | 2 Hz |

Full architecture: [`debug-tool-architecture.md`](debug-tool-architecture.md)
