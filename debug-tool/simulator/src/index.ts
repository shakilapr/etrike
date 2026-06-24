/**
 * CAN Bus Simulator — publishes synthetic CAN traffic via MQTT
 *
 * Simulates realistic frame rates for testing the debug tool UI
 * without hardware. Publishes to the same MQTT topics the real
 * ESP32 firmware uses.
 *
 * Usage: npx tsx src/index.ts [--broker mqtt://localhost:1883]
 */

import mqtt from "mqtt";

const BROKER_URL = process.argv.find((a) => a.startsWith("--broker="))?.split("=")[1] ?? "mqtt://127.0.0.1:1883";

// ── Simulated CAN messages ──
interface SimMessage {
  bus: "high" | "low";
  id: string;
  name: string;
  dlc: number;
  intervalMs: number;
  generator: (t: number) => number[];
}

const messages: SimMessage[] = [
  // High bus
  { bus: "high", id: "0x011", name: "SYS_SAFETY_STS", dlc: 2, intervalMs: 200, generator: () => [0, 1] },
  { bus: "high", id: "0x120", name: "SYS_THROTTLE_STS", dlc: 2, intervalMs: 10, generator: (t) => {
    const speed = Math.round(Math.sin(t * 0.5) * 2000);
    return [(speed >> 8) & 0xff, speed & 0xff];
  }},
  { bus: "high", id: "0x210", name: "RT_STATE_RPT", dlc: 3, intervalMs: 100, generator: () => [1, 1, 0] },
  { bus: "high", id: "0x600", name: "SYS_DIAG_RPT", dlc: 8, intervalMs: 1000, generator: () => [1, 0, 1, 0, 0x01, 0xF4, 0, 0] },
  { bus: "high", id: "0x7FC", name: "HOST_HEARTBEAT", dlc: 1, intervalMs: 500, generator: (t) => [Math.floor(t) % 256] },
  { bus: "high", id: "0x7FD", name: "RT_HEARTBEAT", dlc: 1, intervalMs: 500, generator: (t) => [Math.floor(t) % 256] },
  // Low bus
  { bus: "low", id: "0x011", name: "SYS_SAFETY_STS", dlc: 2, intervalMs: 200, generator: () => [0, 1] },
  { bus: "low", id: "0x120", name: "SYS_THROTTLE_STS", dlc: 2, intervalMs: 10, generator: (t) => {
    const speed = Math.round(Math.cos(t * 0.3) * 1500);
    return [(speed >> 8) & 0xff, speed & 0xff];
  }},
  { bus: "low", id: "0x201", name: "SES_STATUS", dlc: 8, intervalMs: 10, generator: (t) => {
    const angle = Math.round(Math.sin(t * 0.2) * 3000);
    return [1, angle & 0xff, (angle >> 8) & 0xff, 0, 0, 0, 0, 0];
  }},
  { bus: "low", id: "0x600", name: "SYS_DIAG_RPT", dlc: 8, intervalMs: 1000, generator: () => [0, 0, 1, 0, 0x00, 0xC8, 0, 0] },
  { bus: "low", id: "0x721", name: "SEB_STATUS", dlc: 8, intervalMs: 10, generator: (t) => {
    const stroke = Math.round((Math.sin(t * 0.5) + 1) * 500);
    return [1, stroke & 0xff, (stroke >> 8) & 0xff, 50, 0, 0, 0, 0];
  }},
  { bus: "low", id: "0x7FD", name: "RT_HEARTBEAT", dlc: 1, intervalMs: 500, generator: (t) => [Math.floor(t) % 256] },
  { bus: "low", id: "0x7FE", name: "SYS_HEARTBEAT", dlc: 1, intervalMs: 100, generator: (t) => [Math.floor(t) % 256] },
];

// ── Stats ──
const stats: Record<string, { total: number; by_id: Record<string, number>; tec: number; rec: number }> = {
  high: { total: 0, by_id: {}, tec: 0, rec: 0 },
  low: { total: 0, by_id: {}, tec: 0, rec: 0 },
};

function publishStats(client: mqtt.MqttClient, uptimeS: number) {
  const payload = {
    ts: Date.now() / 1000,
    uptime_s: uptimeS,
    buses: {
      high: {
        active: true,
        total: stats.high.total,
        fps: 0,  // calculated by backend
        load_pct: Math.min(100, stats.high.total / (uptimeS * 500000)) * 100,
        tec: stats.high.tec,
        rec: stats.high.rec,
        by_id: stats.high.by_id,
      },
      low: {
        active: true,
        total: stats.low.total,
        fps: 0,
        load_pct: Math.min(100, stats.low.total / (uptimeS * 500000)) * 100,
        tec: stats.low.tec,
        rec: stats.low.rec,
        by_id: stats.low.by_id,
      },
    },
  };
  client.publish("etrike/debug/can/stats", JSON.stringify(payload), { qos: 1 });
}

// ── Main ──
async function main() {
  console.log(`Simulator connecting to ${BROKER_URL}...`);

  const client = mqtt.connect(BROKER_URL, {
    clientId: `etrike-simulator-${Date.now()}`,
    clean: true,
  });

  await new Promise<void>((resolve, reject) => {
    client.once("connect", () => resolve());
    client.once("error", reject);
    setTimeout(() => reject(new Error("connect timeout")), 5000);
  });

  console.log("Connected. Publishing frames...");

  const startTime = Date.now();
  const sendCounts: Record<string, number> = {};

  // Publish status heartbeat
  setInterval(() => {
    client.publish("etrike/debug/status", JSON.stringify({ online: true }), { qos: 1 });
    client.publish("etrike/debug/uptime", JSON.stringify({ uptime_s: Math.round((Date.now() - startTime) / 1000) }), { qos: 1 });
  }, 5000);

  // Publish stats every second
  setInterval(() => {
    publishStats(client, Math.round((Date.now() - startTime) / 1000));
  }, 1000);

  // Publish simulated CAN frames
  for (const msg of messages) {
    const key = `${msg.bus}:${msg.id}`;
    sendCounts[key] = 0;
    setInterval(() => {
      const t = (Date.now() - startTime) / 1000;
      const data = msg.generator(t);
      const payload = {
        ts: Date.now() / 1000,
        bus: msg.bus,
        id: msg.id,
        dlc: msg.dlc,
        data,
        name: msg.name,
      };
      const topic = `etrike/debug/can/rx/${msg.bus}/${msg.id}`;
      client.publish(topic, JSON.stringify(payload), { qos: 0 });

      sendCounts[key]++;
      stats[msg.bus].total++;
      stats[msg.bus].by_id[msg.id] = (stats[msg.bus].by_id[msg.id] ?? 0) + 1;
    }, msg.intervalMs);
  }

  // Status log every 5s
  setInterval(() => {
    const elapsed = Math.round((Date.now() - startTime) / 1000);
    const totalFrames = Object.values(sendCounts).reduce((a, b) => a + b, 0);
    console.log(`[${elapsed}s] ${totalFrames} frames sent across ${Object.keys(sendCounts).length} message types`);
  }, 5000);
}

main().catch((err) => {
  console.error("Simulator error:", err);
  process.exit(1);
});
