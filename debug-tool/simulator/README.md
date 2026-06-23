# CAN Bus Simulator

Publishes synthetic CAN traffic via MQTT for testing the debug tool UI without hardware.

## Usage

```bash
npm install
npx tsx src/index.ts
```

The simulator publishes 12 CAN messages across both buses at realistic rates
(10 Hz — 100 Hz), plus stats every second and status heartbeats every 5 seconds.

Default broker: `mqtt://127.0.0.1:1883`. Override with `--broker=mqtt://other:1883`.
