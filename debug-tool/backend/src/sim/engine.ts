import type { DebugStore } from "../db/queries";
import type { CanFrame } from "../types/can";
import type { EcuModel, EcuConfig } from "./ecu-model";
import type { WorkModeConfig } from "./work-mode";
import { VirtualCanBus } from "./virtual-can";

export interface SimEngineState {
  running: boolean;
  tickMs: number;
  activeEcus: string[];
  physics: { speedMmps: number; steerAngleDeg: number; brakeKpa: number };
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
  private timer: ReturnType<typeof setInterval> | null = null;
  private tickMs = 10; // 100 Hz default
  private _state: SimEngineState = {
    running: false, tickMs: 10, activeEcus: [],
    physics: { speedMmps: 0, steerAngleDeg: 0, brakeKpa: 0 },
  };

  constructor(
    private store: DebugStore,
    private hub: { broadcast(event: { type: string; payload: unknown }): void },
  ) {}

  get state(): SimEngineState { return { ...this._state }; }

  /** Register an ECU model with the engine. */
  register(model: EcuModel): void {
    this.models.set(model.id, model);
    model.onFrame((frame) => {
      this.bus.send(frame);
      this.store.insertFrame(frame, "simulated");
      this.hub.broadcast({ type: "can_frame", payload: frame });
    });
  }

  /** Start the simulation with a given config. */
  async start(config: WorkModeConfig): Promise<void> {
    this.stop();
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
    this.timer = setInterval(() => this.tick(), this.tickMs);
  }

  /** Stop the simulation. */
  async stop(): Promise<void> {
    this._state.running = false;
    if (this.timer) { clearInterval(this.timer); this.timer = null; }
    for (const model of this.models.values()) {
      await model.stop();
    }
  }

  /** Inject a frame from an external source (physical bridge, controller, etc.). */
  injectExternal(frame: CanFrame): void {
    if (!this._state.running) return;
    this.bus.send(frame);
    // Also store physical frames so they appear in the UI
    this.store.insertFrame(frame, "physical");
    this.hub.broadcast({ type: "can_frame", payload: frame });
  }

  private tick(): void {
    // 1. Feed each model with frames from the virtual bus
    for (const model of this.models.values()) {
      // Feed frames addressed to this ECU on both buses
      const frames: CanFrame[] = [];
      for (const bus of ["high", "low"] as const) {
        frames.push(...this.bus.drain(bus));
      }
      for (const frame of frames) {
        model.ingest(frame);
      }
      // Tick the model
      model.tick(this.tickMs);
    }
  }
}
