import { ID_MTR_MOTOR_FBK, ID_SES_STATUS, ID_SEB_STATUS } from "@etrike/debug-shared";
import type { DebugStore } from "../db/queries";
import type { CanFrame } from "../types/can";
import type { EcuModel, EcuConfig } from "./ecu-model";
import type { FrameSource } from "./router";
import type { WorkModeConfig } from "./work-mode";
import { VirtualCanBus } from "./virtual-can";
import { SessionTimebase, defaultTimebase } from "@etrike/debug-shared";
import type { WriteQueue } from "../db/write-queue";

export interface SimEngineState {
  running: boolean;
  tickMs: number;
  activeEcus: string[];
  physics: { speedMmps: number; steerAngleDeg: number; brakeKpa: number; odometer_m: number };
}

/**
 * Lightweight simulation engine.
 * Ticks all active ECU models, routes frames between them on a virtual CAN bus,
 * and feeds output frames into the DebugStore + WebSocket hub.
 *
 * Can mix IPC native models (C++) and TypeScript models behind the same
 * EcuModel interface.
 */
export class SimulationEngine {
  readonly bus = new VirtualCanBus();
  private models = new Map<string, EcuModel>();
  private timer: NodeJS.Timeout | null = null;
  private lastTickMs: number = 0;
  private tickMs = 10; // 100 Hz default
  private _state: SimEngineState = {
    running: false, tickMs: 10, activeEcus: [],
    physics: { speedMmps: 0, steerAngleDeg: 0, brakeKpa: 0, odometer_m: 0 },
  };

  constructor(
    private store: DebugStore,
    private hub: { broadcast(event: { type: string; payload: unknown }): void },
    private writeQueue: WriteQueue,
    private timebase: SessionTimebase = defaultTimebase
  ) {}

  get state(): SimEngineState { return { ...this._state }; }

  /** Register an ECU model with the engine. */
  register(model: EcuModel): void {
    this.models.set(model.id, model);
      model.onFrame((frame) => {
        const canonical = this.timebase.now();
        const routed = { ...frame, ts_us: canonical.ts_us, seq: canonical.seq };
        this.bus.send(routed);
        this.writeQueue.enqueue(routed, "simulated");
        this.hub.broadcast({ type: "can_frame", payload: routed });
      });
  }

  /** Start the simulation with a given config. */
  async start(config: WorkModeConfig): Promise<void> {
    await this.stop();
    this.bus.reset();

    const ecuIds = config.simulatedEcus;
    this._state.activeEcus = [...ecuIds];

    for (const id of ecuIds) {
      const model = this.models.get(id);
      if (!model) continue;
      const ecuCfg: EcuConfig = { bypasses: config.bypasses };
      model.config(ecuCfg);
      await model.start();
    }

    this._state.running = true;
    this.lastTickMs = performance.now();

    const loop = () => {
      if (!this._state.running) return;
      
      const now = performance.now();
      let dt = now - this.lastTickMs;
      if (dt > 1000) dt = 1000; // Cap at 1s to prevent death spiral

      while (dt >= this.tickMs) {
        this.tick();
        dt -= this.tickMs;
        this.lastTickMs += this.tickMs;
      }
      
      if (!this._state.running) return;

      const timeToNextTick = this.tickMs - (performance.now() - this.lastTickMs);
      
      if (timeToNextTick > 5) {
        // Sleep most of the way, waking up a little early
        this.timer = setTimeout(loop, Math.max(1, timeToNextTick - 2));
      } else {
        // Yield to the event loop for the last few milliseconds
        this.timer = setImmediate(loop) as unknown as NodeJS.Timeout;
      }
    };
    loop();
  }

  async stop(): Promise<void> {
    this._state.running = false;
    if (this.timer) { 
      clearTimeout(this.timer);
      clearImmediate(this.timer as any);
      this.timer = null; 
    }
    for (const model of this.models.values()) {
      await model.stop();
    }
    this._state.activeEcus = [];
    this._state.physics = { speedMmps: 0, steerAngleDeg: 0, brakeKpa: 0, odometer_m: 0 };
  }

  /** Inject a frame from an external source (physical bridge, controller, etc.). */
  injectExternal(frame: CanFrame, options: { persist?: boolean; source?: FrameSource } = {}): void {
    if (!this._state.running) return;
    this.bus.send(frame);
    if (options.persist !== false) {
      this.writeQueue.enqueue(frame, options.source ?? "emulated");
      this.hub.broadcast({ type: "can_frame", payload: frame });
    }
  }

  private tick(): void {
    // Drain all pending frames once, then feed to all models
    const allFrames: CanFrame[] = [];
    for (const bus of ["high", "low"] as const) {
      allFrames.push(...this.bus.drain(bus));
    }

    // Snoop frames to update telemetry state
    for (const frame of allFrames) {
      if (frame.decoded?.signals) {
        if (frame.frame.id === ID_MTR_MOTOR_FBK && typeof frame.decoded?.signals.motor_speed_mmps === "number") {
          this._state.physics.speedMmps = frame.decoded?.signals.motor_speed_mmps;
        } else if (frame.frame.id === ID_SES_STATUS && typeof frame.decoded?.signals.str_angle === "number") {
          this._state.physics.steerAngleDeg = frame.decoded?.signals.str_angle / 10;
        } else if (frame.frame.id === ID_SEB_STATUS && typeof frame.decoded?.signals.act_pressure_kpa === "number") {
          this._state.physics.brakeKpa = frame.decoded?.signals.act_pressure_kpa;
        }
      }
    }
    
    // Integrate speed for odometer
    this._state.physics.odometer_m += (this._state.physics.speedMmps / 1000) * (this.tickMs / 1000);

    // Only active ECU models participate in the simulation tick.
    for (const id of this._state.activeEcus) {
      const model = this.models.get(id);
      if (!model) continue;
      for (const frame of allFrames) {
        model.ingest(frame);
      }
      model.tick(this.tickMs);
    }
  }
}
