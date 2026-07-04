import cors from "@fastify/cors";
import fastifyStatic from "@fastify/static";
import websocket from "@fastify/websocket";
import Fastify from "fastify";
import { existsSync } from "node:fs";
import { resolve } from "node:path";
import { loadConfig, type AppConfig } from "./config";
import { registerCanRoutes } from "./api/can";
import { registerCommandRoutes } from "./api/cmd";
import { registerRecordingRoutes } from "./api/recordings";
import { registerSimRoutes } from "./api/sim";
import { registerSystemRoutes } from "./api/system";
import { CanalystBridge } from "./canalyst/bridge";
import { DebugStore } from "./db/queries";
import { MqttBridge } from "./mqtt/bridge";
import { SerialBridge } from "./serial/reader";
import { defaultStats } from "./types/can";
import { StreamHub } from "./ws/stream";

function makeShutdown(
  app: ReturnType<typeof Fastify>,
  bridge: CanalystBridge | SerialBridge | MqttBridge,
  hub: StreamHub,
  store: DebugStore
) {
  return async () => {
    app.log.info("Shutting down debug backend");
    // Clear sim timers (software injection)
    const timers = (app as any).__simTimers as Map<string, ReturnType<typeof setInterval>> | undefined;
    if (timers) { for (const t of timers.values()) clearInterval(t); timers.clear(); }
    hub.close();
    const timeout = setTimeout(() => {
      app.log.warn("bridge.close() timed out after 5s, forcing exit");
      process.exit(1);
    }, 5000).unref();
    try { await bridge.close(); } catch (error) { app.log.error(error, "bridge.close() failed"); }
    clearTimeout(timeout);
    store.close();
    await app.close();
  };
}

async function main(): Promise<void> {
  let config: AppConfig;
  try {
    config = loadConfig();
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
  const startedAt = Date.now() / 1000;
  const app = Fastify({ logger: true });
  const store = new DebugStore(config.dbPath, config.maxFrames);
  const hub = new StreamHub();
  store.setStats(defaultStats());

  await app.register(cors, { origin: true });
  await app.register(websocket);

  const uiDist = resolve(__dirname, "../../ui/dist");
  if (process.env.SERVE_UI === "true" || existsSync(uiDist)) {
    await app.register(fastifyStatic, { root: uiDist, prefix: "/" });
    app.setNotFoundHandler((request, reply) => {
      if (request.url.startsWith("/api/")) return reply.code(404).send({ error: "not found" });
      void reply.sendFile("index.html");
    });
    app.log.info(`Serving UI from ${uiDist}`);
  }

  // Resolve transport. Explicit CAN_TRANSPORT env var always wins.
  // On the default "serial" setting, the bridge script itself detects the
  // CANalyst-II hardware — no separate probe process, no USB race condition.
  let effectiveTransport = config.canTransport;
  let bridge: CanalystBridge | SerialBridge | MqttBridge;

  if (effectiveTransport === "serial") {
    const canalyst = new CanalystBridge(config, store, hub);
    canalyst.start();
    const detected = await canalyst.waitForConnection(3000);
    if (detected) {
      app.log.info("CANalyst-II auto-detected — using canalystii transport");
      bridge = canalyst;
      effectiveTransport = "canalystii";
    } else {
      app.log.info("No CANalyst-II found, using serial transport");
      await canalyst.abandon();
      bridge = new SerialBridge(config, store, hub);
      await bridge.start();
    }
  } else {
    bridge = effectiveTransport === "canalystii"
      ? new CanalystBridge(config, store, hub)
      : effectiveTransport === "mqtt"
        ? new MqttBridge(config, store, hub)
        : new SerialBridge(config, store, hub);
    await bridge.start();
  }

  // Expose hub for sim routes (software-only injection)
  (app as any).__hub = hub;
  (app as any).__simTimers = new Map<string, ReturnType<typeof setInterval>>();

  registerSimRoutes(app, store);

  const shutdown = makeShutdown(app, bridge, hub, store);

  registerSystemRoutes(app, store, bridge, hub, startedAt, shutdown);
  registerCanRoutes(app, store);
  registerCommandRoutes(app, store, bridge);
  registerRecordingRoutes(app, store);
  hub.registerRoutes(app);

  process.once("SIGINT", () => { shutdown().then(() => process.exit(0)).catch(() => process.exit(1)); });
  process.once("SIGTERM", () => { shutdown().then(() => process.exit(0)).catch(() => process.exit(1)); });

  await app.listen({ host: config.host, port: config.port });
}

void main().catch((error) => {
  console.error(error);
  process.exit(1);
});
