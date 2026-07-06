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
import { FrameRouter } from "./sim/router";
import { MODE_DEFAULTS, workModeConfigSchema, type WorkModeConfig, type EcuId } from "./sim/work-mode";
import { defaultStats, type CanFrame } from "./types/can";
import { StreamHub } from "./ws/stream";

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

  // Wire router into the store — all insertFrame() calls from any source
  // (bridges, sim routes, etc.) automatically pass through the FrameRouter.
  store.router = router;

  registerSimRoutes(app, store);
  registerCanRoutes(app, store);
  registerRecordingRoutes(app, store);

  // Mutable bridge reference so route handlers always see the active transport.
  // Routes receive a Proxy that forwards all property access to the current bridge.
  const bridgeRef: { current: CanalystBridge | SerialBridge | MqttBridge } = {
    current: new SerialBridge(config, store, hub)
  };

  // Proxy that delegates to bridgeRef.current — route handlers always get the live bridge
  const bridgeProxy = new Proxy({} as CanalystBridge | SerialBridge | MqttBridge, {
    get(_target, prop) {
      const b = bridgeRef.current;
      return (b as any)[prop];
    },
    set(_target, prop, value) {
      const b = bridgeRef.current;
      (b as any)[prop] = value;
      return true;
    }
  });

  const patchedShutdown = async () => {
    const b = bridgeRef.current;
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

  // Routes receive the proxy — always delegates to bridgeRef.current
  registerSystemRoutes(app, store, bridgeProxy, hub, startedAt, patchedShutdown);
  registerCommandRoutes(app, store, bridgeProxy);
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
      await bridgeRef.current.close();
      bridgeRef.current = canalyst;
      effectiveTransport = "canalystii";
    } else {
      app.log.info("No CANalyst-II found, using serial transport");
      await canalyst.abandon();
      await bridgeRef.current.start();
    }
    } else {
    await bridgeRef.current.close();
    bridgeRef.current = effectiveTransport === "canalystii"
      ? new CanalystBridge(config, store, hub)
      : effectiveTransport === "mqtt"
        ? new MqttBridge(config, store, hub)
        : new SerialBridge(config, store, hub);
    await bridgeRef.current.start();
  }

  // Runtime transport switching endpoint
  app.post("/api/system/switch-transport", async (request, reply) => {
    const body = (request.body ?? {}) as Record<string, unknown>;
    const transport = String(body.transport ?? "");
    if (!["serial", "canalystii", "mqtt", "disabled"].includes(transport)) {
      return reply.code(400).send({ error: `invalid transport: ${transport}` });
    }
    try {
      await bridgeRef.current.close();
      if (transport === "disabled") {
        bridgeRef.current = new SerialBridge(config, store, hub);
        // don't start — leave disconnected
      } else if (transport === "canalystii") {
        bridgeRef.current = new CanalystBridge(config, store, hub);
        await bridgeRef.current.start();
      } else if (transport === "mqtt") {
        bridgeRef.current = new MqttBridge(config, store, hub);
        await bridgeRef.current.start();
      } else {
        // serial — run auto-detection for CANalyst-II first
        const canalyst = new CanalystBridge(config, store, hub);
        canalyst.start();
        const detected = await canalyst.waitForConnection(3000);
        if (detected) {
          bridgeRef.current = canalyst;
        } else {
          await canalyst.abandon();
          bridgeRef.current = new SerialBridge(config, store, hub);
          await bridgeRef.current.start();
        }
      }
      return { ok: true, transport };
    } catch (error) {
      return reply.code(500).send({ error: error instanceof Error ? error.message : String(error) });
    }
  });

  // ── Work mode configuration ──
  let currentConfig: WorkModeConfig = MODE_DEFAULTS.monitor;

  app.get("/api/mode", async () => currentConfig);

  app.post("/api/mode", async (request, reply) => {
    const parsed = workModeConfigSchema.safeParse(request.body ?? {});
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });

    const newConfig = parsed.data as WorkModeConfig;
    currentConfig = newConfig;

    // Apply ID sources to router
    router.clear();
    for (const [key, source] of Object.entries(newConfig.idSources)) {
      if (source === "*") continue;
      const [bus, id] = key.split(":");
      if ((bus === "high" || bus === "low") && id) {
        router.setSource(bus, id, source as "physical" | "emulated" | "simulated");
      }
    }

    // Start/stop ECU models based on config
    const simTimers = (app as any).__simTimers as Map<string, ReturnType<typeof setInterval>>;
    // Clear existing sim timers
    for (const t of simTimers.values()) clearInterval(t);
    simTimers.clear();

    app.log.info(`Work mode: ${newConfig.mode}, simulated ECUs: ${newConfig.simulatedEcus.join(", ") || "none"}`);
    return { ok: true, mode: newConfig.mode };
  });

  app.get("/api/mode/defaults", async () => MODE_DEFAULTS);
}

void main().catch((error) => {
  console.error(error);
  process.exit(1);
});
