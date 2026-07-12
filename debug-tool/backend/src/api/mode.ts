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
  app.post("/api/mode", async (request, reply) => {
    const parsed = workModeConfigSchema.safeParse(request.body ?? {});
    if (!parsed.success) return reply.code(400).send({ error: parsed.error.flatten() });
    
    const newConfig = parsed.data as WorkModeConfig;
    setCurrentConfig(newConfig);

    const { stateMachine } = app.ctx;
    
    // Map legacy WorkMode string to Operational ExecutionMode
    let execMode: "offline" | "monitor" | "simulation" | "replay" = "monitor";
    if (newConfig.mode === "full-sim" || newConfig.mode === "hybrid" || newConfig.mode === "bench" || newConfig.mode === "emulator") {
      execMode = "simulation";
    }

    try {
      await stateMachine.transitionMode(execMode, newConfig.mode);
      return { ok: true, mode: newConfig.mode };
    } catch (err) {
      return reply.code(500).send({ error: err instanceof Error ? err.message : String(err) });
    }
  });

  // ── GET /api/state ──────────────────────────────────────────────────────
  app.get("/api/state", async () => {
    const { stateMachine } = app.ctx;
    const state = stateMachine.state;
    return {
      mode: state.mode,
      arm: state.arm,
      profile: state.profile,
      revision: state.revision.toString(),
    };
  });

  // ── POST /api/state/arm ─────────────────────────────────────────────────
  app.post("/api/state/arm", async (request, reply) => {
    const { stateMachine } = app.ctx;
    try {
      const state = await stateMachine.arm();
      return { ok: true, arm: state.arm };
    } catch (err) {
      return reply.code(400).send({ error: err instanceof Error ? err.message : String(err) });
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
