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
import { FrameRouter, type FrameSource } from "./sim/router";
import { defaultStats, type CanFrame } from "./types/can";
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

  // Expose hub and router for sim routes + bridge frame handlers
  (app as any).__hub = hub;
  (app as any).__simTimers = new Map<string, ReturnType<typeof setInterval>>();
  const router = new FrameRouter();
  (app as any).__router = router;

  // Central routing helper — all frame sources call this instead of
  // store.insertFrame + hub.broadcast directly.
  function routeFrame(frame: CanFrame, source: FrameSource): void {
    const accepted = router.resolve(frame, source);
    if (accepted) {
      store.insertFrame(accepted);
      hub.broadcast({ type: "can_frame", payload: accepted });
    }
  }
  (app as any).__routeFrame = routeFrame;

  registerSimRoutes(app, store);
  registerCanRoutes(app, store);
  registerRecordingRoutes(app, store);

  // Mutable bridge reference so route handlers see the resolved transport.
  // Detection runs after the server is listening — routes use the wrapper.
  const bridgeRef: { current: CanalystBridge | SerialBridge | MqttBridge } = {
    current: new SerialBridge(config, store, hub)
  };
  const bridgeRefForHandlers = bridgeRef; // stable reference for closures

  const shutdown = makeShutdown(app, bridgeRef.current, hub, store);
  // Patch shutdown to use the current bridge
  const patchedShutdown = async () => {
    const b = bridgeRefForHandlers.current;
    app.log.info("Shutting down debug backend");
    const timers = (app as any).__simTimers as Map<string, ReturnType<typeof setInterval>> | undefined;
    if (timers) { for (const t of timers.values()) clearInterval(t); timers.clear(); }
    hub.close();
    const timeout = setTimeout(() => {
      app.log.warn("bridge.close() timed out after 5s, forcing exit");
      process.exit(1);
    }, 5000).unref();
    try { await b.close(); } catch (error) { app.log.error(error, "bridge.close() failed"); }
    clearTimeout(timeout);
    store.close();
    await app.close();
  };

  // Routes use bridgeRefForHandlers.current so they always see the active transport
  registerSystemRoutes(app, store, bridgeRefForHandlers.current, hub, startedAt, patchedShutdown);
  registerCommandRoutes(app, store, bridgeRefForHandlers.current);
  hub.registerRoutes(app);

  process.once("SIGINT", () => { patchedShutdown().then(() => process.exit(0)).catch(() => process.exit(1)); });
  process.once("SIGTERM", () => { patchedShutdown().then(() => process.exit(0)).catch(() => process.exit(1)); });

  // Start HTTP + WebSocket server immediately — transport detection runs async.
  await app.listen({ host: config.host, port: config.port });

  // Resolve transport (non-blocking — server is already listening).
  let effectiveTransport = config.canTransport;

  if (effectiveTransport === "serial") {
    const canalyst = new CanalystBridge(config, store, hub);
    canalyst.start();
    const detected = await canalyst.waitForConnection(3000);
    if (detected) {
      app.log.info("CANalyst-II auto-detected — using canalystii transport");
      await bridgeRefForHandlers.current.close();
      bridgeRefForHandlers.current = canalyst;
      effectiveTransport = "canalystii";
    } else {
      app.log.info("No CANalyst-II found, using serial transport");
      await canalyst.abandon();
      await bridgeRefForHandlers.current.start();
    }
    } else {
    await bridgeRefForHandlers.current.close();
    bridgeRefForHandlers.current = effectiveTransport === "canalystii"
      ? new CanalystBridge(config, store, hub)
      : effectiveTransport === "mqtt"
        ? new MqttBridge(config, store, hub)
        : new SerialBridge(config, store, hub);
    await bridgeRefForHandlers.current.start();
  }

  // Runtime transport switching endpoint
  app.post("/api/system/switch-transport", async (request, reply) => {
    const body = (request.body ?? {}) as Record<string, unknown>;
    const transport = String(body.transport ?? "");
    if (!["serial", "canalystii", "mqtt", "disabled"].includes(transport)) {
      return reply.code(400).send({ error: `invalid transport: ${transport}` });
    }
    try {
      await bridgeRefForHandlers.current.close();
      if (transport === "disabled") {
        bridgeRefForHandlers.current = new SerialBridge(config, store, hub);
        // don't start — leave disconnected
      } else if (transport === "canalystii") {
        bridgeRefForHandlers.current = new CanalystBridge(config, store, hub);
        await bridgeRefForHandlers.current.start();
      } else if (transport === "mqtt") {
        bridgeRefForHandlers.current = new MqttBridge(config, store, hub);
        await bridgeRefForHandlers.current.start();
      } else {
        // serial — run auto-detection for CANalyst-II first
        const canalyst = new CanalystBridge(config, store, hub);
        canalyst.start();
        const detected = await canalyst.waitForConnection(3000);
        if (detected) {
          bridgeRefForHandlers.current = canalyst;
        } else {
          await canalyst.abandon();
          bridgeRefForHandlers.current = new SerialBridge(config, store, hub);
          await bridgeRefForHandlers.current.start();
        }
      }
      return { ok: true, transport };
    } catch (error) {
      return reply.code(500).send({ error: error instanceof Error ? error.message : String(error) });
    }
  });
}

void main().catch((error) => {
  console.error(error);
  process.exit(1);
});
