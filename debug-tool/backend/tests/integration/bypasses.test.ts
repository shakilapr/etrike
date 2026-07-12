import { normalizeFrame } from "@etrike/debug-shared";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { SimulationEngine } from "../../src/sim/engine";
import type { CanFrame } from "../../types/can";
import type { DebugStore } from "../../src/db/store";
import type { WriteQueue } from "../../src/db/write-queue";
import { RtModel } from "../../src/sim/ecus/rt-model";
import { SysModel } from "../../src/sim/ecus/sys-model";
import { HostModel } from "../../src/sim/ecus/host-model";

describe("Safety Bypasses (Bench Mode)", () => {
  let engine: SimulationEngine;
  let hubEvents: { bus: "high"|"low", payload: CanFrame }[] = [];

  let hub: any;
  let store: any;
  let writeQueue: any;

  beforeEach(() => {
    vi.useFakeTimers();
    hubEvents = [];
    store = { insertFrame: vi.fn().mockImplementation(f => ({ ...f, row_id: 1 })) } as unknown as DebugStore;
    hub = { broadcast: (ev: any) => hubEvents.push(ev) };
    writeQueue = { enqueue: vi.fn((f) => store.insertFrame(f)), flush: vi.fn(), drain: vi.fn() } as unknown as WriteQueue;
  });

  afterEach(() => {
    if (engine) engine.stop();
    vi.useRealTimers();
  });

  it("reports steer_state=4 during ESTOP without bypasses", async () => {
    engine = new SimulationEngine(store);
    engine.onProducedFrame = (frame) => { hubEvents.push({ type: "can_frame", payload: frame }); };
    engine.register(new HostModel());
    engine.register(new RtModel());
    engine.register(new SysModel());
    await engine.start({
      mode: "full-sim",
      simulatedEcus: ["rt", "sys"],
      idSources: {},
      injectEmulatedToPhysical: false,
      bypasses: { sesSync: false, sebSync: false, mtrAbsent: false, benchSolo: false }
    });

    // Let it boot
    vi.advanceTimersByTime(100);
    
    // Trigger ESTOP
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "high", id: "0x001", name: "SAFETY_ESTOP", dlc: 0, data: [], decoded: {} }));
    vi.advanceTimersByTime(200);

    // Look for RT_STATE_RPT (0x210) on high bus
    const stateRpt = hubEvents.filter(e => e.payload.frame.id === "0x210").pop();
    expect(stateRpt).toBeDefined();
    
    // steer_state is in the decoded payload
    expect(stateRpt!.payload.decoded.signals.steer_state).toBe(4); 
    // safety_state is 1 (InternalEstop)
    expect(stateRpt!.payload.decoded.signals.safety_state).toBe(1);
  });

  it("reports steer_state=1 during ESTOP with sesSync bypass (Bench Mode)", async () => {
    engine = new SimulationEngine(store);
    engine.onProducedFrame = (frame) => { hubEvents.push({ type: "can_frame", payload: frame }); };
    engine.register(new HostModel());
    engine.register(new RtModel());
    engine.register(new SysModel());
    await engine.start({
      mode: "bench",
      simulatedEcus: ["rt", "sys"],
      idSources: {},
      injectEmulatedToPhysical: false,
      bypasses: { sesSync: true, sebSync: true, mtrAbsent: true, benchSolo: true }
    });

    // Let it boot
    vi.advanceTimersByTime(100);
    
    // Trigger ESTOP
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "high", id: "0x001", name: "SAFETY_ESTOP", dlc: 0, data: [], decoded: {} }));
    vi.advanceTimersByTime(200);

    // Look for RT_STATE_RPT (0x210) on high bus
    const stateRpt = hubEvents.filter(e => e.payload.frame.id === "0x210").pop();
    expect(stateRpt).toBeDefined();
    
    // steer_state remains 1 due to bypass!
    expect(stateRpt!.payload.decoded.signals.steer_state).toBe(1); 
    // safety_state is still 1 (InternalEstop)
    expect(stateRpt!.payload.decoded.signals.safety_state).toBe(1);
  });
});
