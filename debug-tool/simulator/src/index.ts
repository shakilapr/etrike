// CAN Bus Simulator - publishes synthetic dual-bus traffic via MQTT
// Usage: npx tsx src/index.ts [--broker mqtt://localhost:1883] [--mcp2515]

import { SimEngine } from "./sim-engine";

function parseArgs(): { broker: string; mcp2515: boolean } {
  const args = process.argv.slice(2);
  let broker = "mqtt://127.0.0.1:1883";
  let mcp2515 = false;

  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--broker" && args[i + 1]) {
      broker = args[++i];
    } else if (args[i] === "--mcp2515") {
      mcp2515 = true;
    }
  }

  return { broker, mcp2515 };
}

const { broker, mcp2515 } = parseArgs();

console.log(`[sim] Starting dual-bus CAN simulator`);
console.log(`[sim] Broker: ${broker}`);
if (mcp2515) console.log(`[sim] MCP2515 mode enabled (low bus via SPI)`);

const engine = new SimEngine(broker);
engine.start();

// Graceful shutdown
function shutdown(): void {
  console.log("\n[sim] Shutting down...");
  engine.stop();
  process.exit(0);
}

process.once("SIGINT", shutdown);
process.once("SIGTERM", shutdown);

console.log("[sim] Press Ctrl+C to stop");
