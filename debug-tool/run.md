# Running the Debug Tool

## Quick Start

Start both servers from the repo root:

```bash
# Terminal 1 — Backend (Fastify API + embedded MQTT broker, :3000)
cd debug-tool/backend && npm run dev

# Terminal 2 — UI dev server (Svelte + Vite, :5173)
cd debug-tool/ui && npm run dev
```

The UI is available at **http://localhost:5173**.

## Optional: Simulator

If you don't have ESP32 hardware connected, run the simulator to generate synthetic CAN traffic:

```bash
cd debug-tool/simulator && npx tsx src/index.ts
```

## Optional: ESP32 Firmware

For real CAN hardware:

```bash
cd debug-tool/debug-esp32 && pio run -t upload
```

## Optional: CANalyst-II Hardware

Bind the CANalyst-II to WinUSB with Zadig first. Then start the backend with
the CANalyst-II transport:

```powershell
cd debug-tool/backend
$env:CAN_TRANSPORT = "canalystii"
$env:CANALYST_BITRATE = "500000"
npm run dev
```

Default channel mapping:

| CANalyst-II channel | Debug tool bus |
|---------------------|----------------|
| Channel 0 | High |
| Channel 1 | Low |

Override the mapping if your cables are swapped:

```powershell
$env:CANALYST_CH0_BUS = "low"
$env:CANALYST_CH1_BUS = "high"
```

## Dependencies

| Component | Tech | Port |
|-----------|------|------|
| `backend/` | Node.js + TypeScript + Fastify + aedes MQTT | 3000 |
| `ui/` | Svelte + TypeScript + Vite | 5173 |
| `simulator/` | Node.js + TypeScript | — |
| `debug-esp32/` | PlatformIO (C++), ESP-IDF | — |

Run `npm install` in each directory before first use.
