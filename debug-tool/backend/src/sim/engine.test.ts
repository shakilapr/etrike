import { ID_HOST_DRIVE_CMD } from "@etrike/debug-shared";
import { afterEach, describe, expect, it, vi } from "vitest";
import { SimulationEngine } from "./engine";
import type { DebugStore, StoredCanFrame } from "../db/queries";
import type { CanFrame } from "../types/can";
import type { WriteQueue } from "../db/write-queue";
import type { EcuConfig, EcuModel, EcuState } from "./ecu-model";
import type { WorkModeConfig } from "./work-mode";

class TestModel implements EcuModel {
  configCalls = 0;
  startCalls = 0;
  stopCalls = 0;
  ingestCalls = 0;
  tickCalls = 0;
  callbacks: Array<(frame: CanFrame) => void> = [];

  constructor(readonly id: string) {}

  config(_params: EcuConfig): void {
    this.configCalls += 1;
  }

  start(): void {
    this.startCalls += 1;
  }

  ingest(_frame: CanFrame): void {
    this.ingestCalls += 1;
  }

  tick(_dtMs: number): CanFrame[] {
    this.tickCalls += 1;
    return [];
  }

  onFrame(callback: (frame: CanFrame) => void): void {
    this.callbacks.push(callback);
  }

  state(): EcuState {
    return { ecu: this.id, healthy: true, uptimeMs: 0 };
  }

  stop(): void {
    this.stopCalls += 1;
  }
}

function makeFrame(): CanFrame {
  return {
    ts: Date.now() / 1000,
    bus: "high",
    id: ID_HOST_DRIVE_CMD,
    name: "HOST_DRIVE_CMD",
    dlc: 8,
    data: [0, 0, 0, 0, 0, 0, 0, 1],
    decoded: { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 },
  };
}

function makeConfig(activeEcus: WorkModeConfig["simulatedEcus"]): WorkModeConfig {
  return {
    mode: "full-sim",
    simulatedEcus: activeEcus,
    idSources: {},
    injectEmulatedToPhysical: false,
    bypasses: { sesSync: false, sebSync: false, mtrAbsent: false, benchSolo: false },
  };
}

describe("SimulationEngine", () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it("ticks and ingests only active ECU models", async () => {
    vi.useFakeTimers();
    const store = {
      insertFrame: (frame: CanFrame): StoredCanFrame => ({
        ...frame,
        row_id: 1,
        ts_real: Date.now() / 1000,
        ts_device: frame.ts,
        ts_us: frame.ts_us ?? "0",
        seq: frame.seq ?? 0,
      }),
    } as unknown as DebugStore;
    const hub = { broadcast: vi.fn() };
    const writeQueue = { enqueue: vi.fn(), flush: vi.fn(), drain: vi.fn() } as unknown as WriteQueue;
    const engine = new SimulationEngine(store, hub, writeQueue);
    const active = new TestModel("host");
    const inactive = new TestModel("rt");

    engine.register(active);
    engine.register(inactive);
    await engine.start(makeConfig(["host"]));
    engine.injectExternal(makeFrame(), { persist: false });
    vi.advanceTimersByTime(10);
    await engine.stop();

    expect(active.startCalls).toBe(1);
    expect(inactive.startCalls).toBe(0);
    expect(active.ingestCalls).toBe(1);
    expect(inactive.ingestCalls).toBe(0);
    expect(active.tickCalls).toBe(1);
    expect(inactive.tickCalls).toBe(0);
  });
});
