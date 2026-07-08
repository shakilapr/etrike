/**
 * Mode, sim-controller, and transport-switching routes.
 * Extracted from index.ts to eliminate inline route handlers.
 */
import type { FastifyInstance } from "fastify";
import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import type { CanalystBridge } from "../canalyst/bridge";
import type { SerialBridge } from "../serial/reader";
import type { MqttBridge } from "../mqtt/bridge";
import type { CanFrame } from "../types/can";
import { CanalystBridge as CanalystBridgeClass } from "../canalyst/bridge";
import { SerialBridge as SerialBridgeClass } from "../serial/reader";
import { MqttBridge as MqttBridgeClass } from "../mqtt/bridge";
import { MODE_DEFAULTS, workModeConfigSchema, type WorkModeConfig } from "../sim/work-mode";
import type { WriteQueue } from "../db/write-queue";

type AnyBridge = CanalystBridge | SerialBridge | MqttBridge;
type FrameObservableBridge = AnyBridge & {
  onFrame?: (callback: (frame: CanFrame) => void) => void;
};

export interface ModeRouteOptions {
  config: AppConfig;
  store: DebugStore;
  bridgeRef: { current: AnyBridge };
  feedPhysicalFramesToSim: (bridge: FrameObservableBridge) => void;
  getCurrentConfig: () => WorkModeConfig;
  setCurrentConfig: (c: WorkModeConfig) => void;
  writeQueue: WriteQueue;
}

export function registerModeRoutes(app: FastifyInstance, opts: ModeRouteOptions): void {
  const { config, store, bridgeRef, feedPhysicalFramesToSim, getCurrentConfig, setCurrentConfig, writeQueue } = opts;

  // ── GET /api/mode ──────────────────────────────────────────────────────
  app.get("/api/mode", async () => getCurrentConfig());

  // ── GET /api/mode/defaults ─────────────────────────────────────────────
  app.get("/api/mode/defaults", async () => MODE_DEFAULTS);

  // ── POST /api/mode ─────────────────────────────────────────────────────
  let modeSwitching = false;
  app.post("/api/mode", async (request, reply) => {
    if (modeSwitching) return reply.code(409).send({ error: "mode switch already in progress" });
    modeSwitching = true;
    try {
      const parsed = workModeConfigSchema.safeParse(request.body ?? {});
      if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });
      const newConfig = parsed.data as WorkModeConfig;
      setCurrentConfig(newConfig);

      const { simTimers, router, simEngine, rtModelNative } = app.ctx;

      // Clear all backend periodic sim timers
      for (const timer of simTimers.values()) clearInterval(timer);
      simTimers.clear();

      // Reset router and re-populate from config
      router.clear();
      for (const [key, source] of Object.entries(newConfig.idSources)) {
        if (source === "*") continue;
        const [bus, id] = key.split(":");
        if ((bus === "high" || bus === "low") && id) {
          router.setSource(bus, id, source as "physical" | "emulated" | "simulated");
        }
      }

      // Start/stop simulation engine
      if (newConfig.mode === "full-sim" && newConfig.simulatedEcus.length > 0) {
        if (newConfig.modelBackend === "native" && rtModelNative) {
          simEngine.register(rtModelNative);
        }
        await simEngine.start(newConfig);
      } else if (simEngine.state.running) {
        await simEngine.stop();
      }

      return { ok: true, mode: newConfig.mode };
    } finally {
      modeSwitching = false;
    }
  });

  // ── POST /api/sim/controller ───────────────────────────────────────────
  app.post("/api/sim/controller", async (request, reply) => {
    const { hostModel, simEngine } = app.ctx;
    if (!simEngine.state.running) {
      return reply.code(400).send({ error: "HOST model not active — switch to Full Simulation mode" });
    }
    const body = (request.body ?? {}) as Record<string, unknown>;
    if (typeof body.speed_mmps === "number") hostModel.speedMmps = body.speed_mmps;
    if (typeof body.yaw_mrad_s === "number") hostModel.yawMradS = body.yaw_mrad_s;
    if (typeof body.gear === "number") hostModel.gear = body.gear;
    if (typeof body.brake_kpa === "number") hostModel.brakeKpa = body.brake_kpa;
    return { ok: true, speed: hostModel.speedMmps, gear: hostModel.gear, brake: hostModel.brakeKpa };
  });

  // ── GET /api/sim/state ─────────────────────────────────────────────────
  app.get("/api/sim/state", async () => {
    const { simEngine } = app.ctx;
    return {
      running: simEngine.state.running,
      activeEcus: simEngine.state.activeEcus,
      physics: simEngine.state.physics,
    };
  });

  // ── POST /api/system/switch-transport ──────────────────────────────────
  app.post("/api/system/switch-transport", async (request, reply) => {
    const body = (request.body ?? {}) as Record<string, unknown>;
    const transport = String(body.transport ?? "");
    if (!["serial", "canalystii", "mqtt", "disabled"].includes(transport)) {
      return reply.code(400).send({ error: `invalid transport: ${transport}` });
    }
    try {
      await bridgeRef.current.close();

      if (transport === "disabled") {
        const b = new SerialBridgeClass(config, store, app.ctx.hub, writeQueue);
        feedPhysicalFramesToSim(b as FrameObservableBridge);
        bridgeRef.current = b;
      } else if (transport === "canalystii") {
        const b = new CanalystBridgeClass(config, store, app.ctx.hub, writeQueue);
        feedPhysicalFramesToSim(b as FrameObservableBridge);
        await b.start();
        bridgeRef.current = b;
      } else if (transport === "mqtt") {
        const b = new MqttBridgeClass(config, store, app.ctx.hub, writeQueue);
        feedPhysicalFramesToSim(b as FrameObservableBridge);
        await b.start();
        bridgeRef.current = b;
      } else {
        // "serial" — auto-detect canalyst first, fall back to serial
        const canalyst = new CanalystBridgeClass(config, store, app.ctx.hub, writeQueue);
        feedPhysicalFramesToSim(canalyst as FrameObservableBridge);
        await canalyst.start();
        const detected = await canalyst.waitForConnection(3000);
        if (detected) {
          bridgeRef.current = canalyst;
        } else {
          await canalyst.abandon();
          const serial = new SerialBridgeClass(config, store, app.ctx.hub, writeQueue);
          feedPhysicalFramesToSim(serial as FrameObservableBridge);
          await serial.start();
          bridgeRef.current = serial;
        }
      }
      return { ok: true, transport };
    } catch (error) {
      return reply.code(500).send({ error: error instanceof Error ? error.message : String(error) });
    }
  });
}
