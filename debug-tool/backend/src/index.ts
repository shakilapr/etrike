import { initCanDatabase } from "@etrike/debug-shared";
import cors from "@fastify/cors";
import fastifyStatic from "@fastify/static";
import websocket from "@fastify/websocket";
import Fastify from "fastify";
import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { loadConfig, type AppConfig } from "./config";
import { registerAppContext } from "./app-context";
import { registerCanRoutes } from "./api/can";
import { registerCommandRoutes } from "./api/cmd";
import { registerModeRoutes } from "./api/mode";
import { registerRecordingRoutes } from "./api/recordings";
import { registerSimRoutes } from "./api/sim";
import { registerSystemRoutes } from "./api/system";
import { CanalystBridge } from "./canalyst/bridge";
import { WorkerClient } from "./db/worker-client";
import { WriteQueue } from "./db/write-queue";
import { MqttBridge } from "./mqtt/bridge";
import { SerialBridge } from "./serial/reader";
import { FrameRouter } from "./sim/router";
import { MODE_DEFAULTS, type WorkModeConfig } from "./sim/work-mode";
import { SimulationEngine } from "./sim/engine";
import { HostModel } from "./sim/ecus/host-model";
import { RtModel } from "./sim/ecus/rt-model";
import { MtrModel } from "./sim/ecus/mtr-model";
import { SysModel } from "./sim/ecus/sys-model";
import { SesModel } from "./sim/ecus/ses-model";
import { SebModel } from "./sim/ecus/seb-model";
import { IpcEngineAdapter } from "./sim/ipc-adapter";
import { defaultStats, type CanFrame } from "./types/can";
import { StreamHub } from "./ws/stream";

type AnyBridge = CanalystBridge | SerialBridge | MqttBridge;
type FrameObservableBridge = AnyBridge & {
  onFrame?: (callback: (frame: CanFrame) => void) => void;
};

async function main(): Promise<void> {
  try {
    initCanDatabase();
  } catch (err) {
    console.error("Failed to initialize CAN database:", err);
    process.exit(1);
  }

  let config: AppConfig;
  try {
    config = loadConfig();
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }

  const startedAt = Date.now() / 1000;
  const app = Fastify({ logger: true });
  const store = new WorkerClient(config.dbPath, config.maxFrames);
  const writeQueue = new WriteQueue(store);
  const hub = new StreamHub();
  store.init().catch(console.error);
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

  // ── Simulation engine & ECU models ────────────────────────────────────
  const router = new FrameRouter();
  writeQueue.router = router;

  const simEngine = new SimulationEngine(store, hub, writeQueue);

  const hostModel = new HostModel();
  const rtModelTs = new RtModel();
  const mtrModel = new MtrModel();
  const sysModel = new SysModel();
  const sesModel = new SesModel();
  const sebModel = new SebModel();

  let rtModelNative: IpcEngineAdapter | null = null;
  try {
    rtModelNative = new IpcEngineAdapter("rt");
    app.log.info("Native RT model available via IPC");
  } catch {
    app.log.info("Native RT model not available — using TypeScript fallback");
  }

  // Register TypeScript models
  simEngine.register(hostModel);
  simEngine.register(rtModelTs);
  simEngine.register(mtrModel);
  simEngine.register(sysModel);
  simEngine.register(sesModel);
  simEngine.register(sebModel);

  // ── Typed app context — replaces all (app as any).__xxx ───────────────
  const simTimers = new Map<string, ReturnType<typeof setInterval>>();
  registerAppContext(app, {
    hub,
    simTimers,
    router,
    simEngine,
    hostModel,
    rtModelTs,
    rtModelNative,
  });

  // ── Bridge setup ──────────────────────────────────────────────────────
  const feedPhysicalFramesToSim = (bridge: FrameObservableBridge): void => {
    bridge.onFrame?.((frame) => simEngine.injectExternal(frame, { persist: false }));
  };

  const bridgeRef: { current: AnyBridge } = {
    current: new SerialBridge(config, store, hub, writeQueue),
  };
  feedPhysicalFramesToSim(bridgeRef.current as FrameObservableBridge);

  // Proxy that delegates all property access to bridgeRef.current so route
  // handlers always see the live transport without needing to be re-registered.
  const bridgeProxy = new Proxy({} as AnyBridge, {
    get(_target, prop) { return (bridgeRef.current as any)[prop]; },
    set(_target, prop, value) { (bridgeRef.current as any)[prop] = value; return true; },
  });

  // ── Current work mode (closure state, access via getModeConfig/setModeConfig) ──
  let currentConfig: WorkModeConfig = MODE_DEFAULTS.monitor;
  const getCurrentConfig = () => currentConfig;
  const setCurrentConfig = (c: WorkModeConfig) => { currentConfig = c; };

  // ── Route registration ────────────────────────────────────────────────
  registerSimRoutes(app, store, hub);
  registerCanRoutes(app, store, writeQueue);
  registerRecordingRoutes(app, store);
  registerSystemRoutes(app, store, bridgeProxy, hub, startedAt, patchedShutdown);
  registerCommandRoutes(app, store, bridgeProxy);
  registerModeRoutes(app, { config, store, bridgeRef, feedPhysicalFramesToSim, getCurrentConfig, setCurrentConfig, writeQueue });
  hub.registerRoutes(app);

  // ── Graceful shutdown ─────────────────────────────────────────────────
  async function patchedShutdown() {
    app.log.info("Shutting down debug backend");
    for (const t of simTimers.values()) clearInterval(t);
    simTimers.clear();
    hub.close();
    const timeout = setTimeout(() => {
      app.log.warn("bridge.close() timed out after 5s, forcing exit");
      process.exit(1);
    }, 5000).unref();
    try { await bridgeRef.current.close(); } catch (error) { app.log.error(error, "bridge.close() failed"); }
    clearTimeout(timeout);
    await writeQueue.drain();
    await store.close();
    await app.close();
  }

  process.once("SIGINT", () => { patchedShutdown().then(() => process.exit(0)).catch(() => process.exit(1)); });
  process.once("SIGTERM", () => { patchedShutdown().then(() => process.exit(0)).catch(() => process.exit(1)); });

  // ── Start listening ───────────────────────────────────────────────────
  await app.listen({ host: config.host, port: config.port });

  // ── Transport auto-detection (non-blocking — server already listening) ──
  let effectiveTransport = config.canTransport;

  if (effectiveTransport === "serial") {
    const canalyst = new CanalystBridge(config, store, hub, writeQueue);
    feedPhysicalFramesToSim(canalyst as FrameObservableBridge);
    await canalyst.start();
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
    if (effectiveTransport === "canalystii") {
      bridgeRef.current = new CanalystBridge(config, store, hub, writeQueue);
    } else if (effectiveTransport === "mqtt") {
      bridgeRef.current = new MqttBridge(config, store, hub, writeQueue);
    } else {
      bridgeRef.current = new SerialBridge(config, store, hub, writeQueue);
    }
    feedPhysicalFramesToSim(bridgeRef.current as FrameObservableBridge);
    await bridgeRef.current.start();
  }
}

void main().catch((error) => {
  console.error(error);
  process.exit(1);
});
