import { initCanDatabase } from "@etrike/debug-shared";
import cors from "@fastify/cors";
import fastifyStatic from "@fastify/static";
import websocket from "@fastify/websocket";
import Fastify from "fastify";
import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { loadConfig, type AppConfig } from "./config";
import { registerAppContext } from "./app-context";
import { OperationalStateMachine, type ExecutionMode } from "./state/machine";
import { registerCanRoutes } from "./api/can";
import { registerCommandRoutes } from "./api/cmd";
import { registerLeaseRoutes } from "./api/leases";
import { LeaseManager } from "./state/leases";
import { registerModeRoutes } from "./api/mode";
import { registerRecordingRoutes } from "./api/recordings";
import { registerSimRoutes } from "./api/sim";
import { registerSystemRoutes } from "./api/system";
import { CanalystBridge } from "./canalyst/bridge";
import { WorkerClient } from "./db/worker-client";
import { WriteQueue } from "./db/write-queue";
import { InjectionService } from "./api/injection";
import { ReplayEngine } from "./sim/replay";
import { ActiveTransportManager } from "./bridge/manager";

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

type AnyBridge = CanalystBridge | SerialBridge | ActiveTransportManager;
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
  const leaseManager = new LeaseManager();
  const router = new FrameRouter();

  const simEngine = new SimulationEngine(store);
  simEngine.onProducedFrame = (frame) => {
    const disp = router.route(frame, { producer: "simulation" });
    if (disp.accepted && disp.frame) {
      if (disp.ui) hub.broadcast({ type: "can_frame", payload: disp.frame });
      if (disp.recording) writeQueue.enqueue(disp.frame, "simulated");
      if (disp.sim_input) simEngine.injectExternal(disp.frame, { persist: false });
    }
  };

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
  
  const stateMachine = new OperationalStateMachine({
    onDisarm: async () => {
      // Future: revoke leases, physical arm drops
      // Currently, the physical transport ignores this, but it's recorded in state.
    },
    onModeSwitch: async (mode: ExecutionMode, profile?: string) => {
      // This is called sequentially by the state machine
      const currentConf = getCurrentConfig();
      
      for (const timer of simTimers.values()) clearInterval(timer);
      simTimers.clear();

      router.clear();
      for (const [key, source] of Object.entries(currentConf.idSources)) {
        if (source === "*") continue;
        const [bus, id] = key.split(":");
        if ((bus === "high" || bus === "low") && id) {
          router.setSource(bus as any, id, source as any);
        }
      }

      if (currentConf.mode === "full-sim" && currentConf.simulatedEcus.length > 0) {
        if (currentConf.modelBackend === "native" && rtModelNative) {
          simEngine.register(rtModelNative);
        }
        await simEngine.start(currentConf);
      } else if (simEngine.state.running) {
        await simEngine.stop();
      }
    }
  });

  const ctx = {
    store,
    stateMachine,
    writeQueue,
    leaseManager,
    hub,
    simTimers,
    router,
    simEngine,
    hostModel,
    rtModelTs,
    rtModelNative,
  } as any;
  ctx.injectionService = new InjectionService(ctx);
  ctx.replayEngine = new ReplayEngine(store, (frame) => {
    const disp = router.route(frame, { producer: "replay" });
    if (disp.accepted && disp.frame) {
      if (disp.ui) hub.broadcast({ type: "can_frame", payload: disp.frame });
    }
  });
  registerAppContext(app, ctx);


  // ── Bridge setup ──────────────────────────────────────────────────────
  const setupBridgeRouting = (bridge: FrameObservableBridge): void => {
    bridge.onFrame?.((frame) => {
      const disp = router.route(frame, { producer: "physical_rx" });
      if (disp.accepted && disp.frame) {
        if (disp.ui) hub.broadcast({ type: "can_frame", payload: disp.frame });
        if (disp.recording) writeQueue.enqueue(disp.frame, "physical");
        if (disp.sim_input) simEngine.injectExternal(disp.frame, { persist: false });
      }
    });
  };

  const transportManager = new ActiveTransportManager(config, store, hub, writeQueue);
  setupBridgeRouting(transportManager);

  // ── Current work mode (closure state, access via getModeConfig/setModeConfig) ──
  let currentConfig: WorkModeConfig = MODE_DEFAULTS.monitor;
  const getCurrentConfig = () => currentConfig;
  const setCurrentConfig = (c: WorkModeConfig) => { currentConfig = c; };

  // ── Route registration ────────────────────────────────────────────────
  registerSimRoutes(app, store, hub);
  registerCanRoutes(app, store, writeQueue);
  registerRecordingRoutes(app, store);
  registerSystemRoutes(app, store, transportManager, hub, startedAt, patchedShutdown);
  registerCommandRoutes(app, store, transportManager);
  registerModeRoutes(app, { config, store, bridgeRef: { current: transportManager as any }, feedPhysicalFramesToSim: setupBridgeRouting, getCurrentConfig, setCurrentConfig, writeQueue });
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
    try { await transportManager.close(); } catch (error) { app.log.error(error, "transportManager.close() failed"); }
    clearTimeout(timeout);
    await writeQueue.drain();
    await store.close();
    await app.close();
  }

  process.once("SIGINT", () => { patchedShutdown().then(() => process.exit(0)).catch(() => process.exit(1)); });
  process.once("SIGTERM", () => { patchedShutdown().then(() => process.exit(0)).catch(() => process.exit(1)); });

  // ── Start listening ───────────────────────────────────────────────────
  await app.listen({ host: config.host, port: config.port });

  // Start transport manager (handles auto-detection internally)
  await transportManager.start();
}

void main().catch((error) => {
  console.error(error);
  process.exit(1);
});
