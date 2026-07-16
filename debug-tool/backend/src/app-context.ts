/**
 * Typed application context — replaces the `(app as any).__xxx` anti-pattern.
 * Register via `registerAppContext(app, ctx)` before route registration.
 * Access in any route handler as `request.server.ctx.xxx`.
 */
import type { FastifyInstance } from "fastify";
import type { SimulationEngine } from "./sim/engine";
import type { HostModel } from "./sim/ecus/host-model";
import type { RtModel } from "./sim/ecus/rt-model";
import type { IpcEngineAdapter } from "./sim/ipc-adapter";
import type { FrameRouter } from "./sim/router";
import type { StreamHub } from "./ws/stream";
import type { OperationalStateMachine } from "./state/machine";
import type { WriteQueue } from "./db/write-queue";
import type { LeaseManager } from "./state/leases";
import type { InjectionService } from "./api/injection";
import type { ReplayEngine } from "./sim/replay";
import type { DebugStore } from "./db/queries";

export interface AppContext {
  store: DebugStore;
  hub: StreamHub;
  stateMachine: OperationalStateMachine;
  writeQueue: WriteQueue;
  leaseManager: LeaseManager;
  injectionService: InjectionService;
  replayEngine: ReplayEngine;
  /** Backend-managed periodic sim timers (keyed by `sim:bus:id`). */
  simTimers: Map<string, ReturnType<typeof setInterval>>;
  router: FrameRouter;
  simEngine: SimulationEngine;
  hostModel: HostModel;
  rtModelTs: RtModel;
  rtModelNative: IpcEngineAdapter | null;
}

declare module "fastify" {
  interface FastifyInstance {
    ctx: AppContext;
  }
}

export function registerAppContext(app: FastifyInstance, ctx: AppContext): void {
  app.decorate("ctx", ctx);
}
