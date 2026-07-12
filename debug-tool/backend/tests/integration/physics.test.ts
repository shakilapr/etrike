import { normalizeFrame } from "@etrike/debug-shared";
import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { SimulationEngine } from "../../src/sim/engine";
import { HostModel } from "../../src/sim/ecus/host-model";
import { RtModel } from "../../src/sim/ecus/rt-model";
import { MtrModel } from "../../src/sim/ecus/mtr-model";
import { SesModel } from "../../src/sim/ecus/ses-model";
import type { DebugStore } from "../../src/db/queries";
import type { WriteQueue } from "../../src/db/write-queue";

describe("SIL Physics & Dynamics", () => {
  let engine: SimulationEngine;
  let hubEvents: any[] = [];
  let host: HostModel;
  
  beforeEach(async () => {
    vi.useFakeTimers();
    hubEvents = [];
    const store = { 
      insertFrame: vi.fn().mockImplementation(f => ({ ...f, row_id: 1 })) 
    } as unknown as DebugStore;
    const hub = { broadcast: (ev: any) => hubEvents.push(ev) };
    
    host = new HostModel();
    const writeQueue = { enqueue: vi.fn((f) => store.insertFrame(f)), flush: vi.fn(), drain: vi.fn() } as unknown as WriteQueue;
    engine = new SimulationEngine(store, hub, writeQueue);
    engine.register(host);
    engine.register(new RtModel());
    engine.register(new MtrModel());
    engine.register(new SesModel());
    
    await engine.start({
      mode: "full-sim",
      simulatedEcus: ["host", "rt", "mtr", "ses"],
      idSources: {},
      injectEmulatedToPhysical: false,
      bypasses: {}
    });
  });

  afterEach(async () => {
    await engine.stop();
    vi.useRealTimers();
  });

  it("gradually accelerates motor speed according to physics model (not instantaneous)", async () => {
    // Inject AUTO mode
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "low", id: "0x110", name: "SYS_MODE_CMD", dlc: 1, data: [1], decoded: { mode: 1 } }));
    
    // Command Host to drive
    host.speedMmps = 2000;
    
    vi.advanceTimersByTime(200);
    
    const mtrFbs = hubEvents.filter(e => e.payload.frame.id === "0x206");
    let mtrFb = mtrFbs.pop();
    expect(mtrFb).toBeDefined();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBeGreaterThan(0);
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBeLessThan(1500);
    
    // Fast forward 2 seconds (time to reach steady state)
    vi.advanceTimersByTime(2000);
    
    mtrFb = hubEvents.filter(e => e.payload.frame.id === "0x206").pop();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBeGreaterThan(1990); // Approaching 2000
  });

  it("translates yaw rate into steering angle target dynamically", async () => {
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "low", id: "0x110", name: "SYS_MODE_CMD", dlc: 1, data: [1], decoded: { mode: 1 } }));
    
    host.yawMradS = 1000;
    
    vi.advanceTimersByTime(100);
    
    const sesReq = hubEvents.filter(e => e.payload.frame.id === "0x169").pop();
    expect(sesReq).toBeDefined();
    // 1000 mrad/s * 0.05 = 50
    expect(sesReq.payload.decoded.signals.target_angle).toBe(50);
  });

  it("brakes sharply to 0 when ESTOP is triggered during motion", async () => {
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "low", id: "0x110", name: "SYS_MODE_CMD", dlc: 1, data: [1], decoded: { mode: 1 } }));
    
    host.speedMmps = 2000;
    vi.advanceTimersByTime(2000);
    
    let mtrFb = hubEvents.filter(e => e.payload.frame.id === "0x206").pop();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBeGreaterThan(1990);
    
    // Trigger ESTOP!
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "high", id: "0x001", name: "SAFETY_ESTOP", dlc: 0, data: [], decoded: {} }));
    
    vi.advanceTimersByTime(100);
    mtrFb = hubEvents.filter(e => e.payload.frame.id === "0x206").pop();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBe(0);
  });

  it("handles high-level braking (0x301) at 30km/h and decays speed over ~380ms", async () => {
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "low", id: "0x110", name: "SYS_MODE_CMD", dlc: 1, data: [1], decoded: { mode: 1 } }));
    
    // 30 km/h = 8333 mm/s, turning 20 deg/min = ~5.8 mrad/s
    host.speedMmps = 8333;
    host.yawMradS = 6;
    
    // Reach steady state
    vi.advanceTimersByTime(2000);
    
    let mtrFb = hubEvents.filter(e => e.payload.frame.id === "0x206").pop();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBeGreaterThan(8300);
    
    // Trigger High-Level Brake (5000 kPa)
    host.brakeKpa = 5000;
    
    // Wait 300ms for 0x301 (10Hz) -> 0x205 (10Hz) propagation and some decay
    vi.advanceTimersByTime(300);
    mtrFb = hubEvents.filter(e => e.payload.frame.id === "0x206").pop();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBeGreaterThan(0);
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBeLessThan(8300);

    // Fast forward enough for decay to finish (380ms decay + propagation)
    vi.advanceTimersByTime(500);
    mtrFb = hubEvents.filter(e => e.payload.frame.id === "0x206").pop();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBe(0);
  });

  it("handles ESTOP braking at 30km/h and stops instantly", async () => {
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "low", id: "0x110", name: "SYS_MODE_CMD", dlc: 1, data: [1], decoded: { mode: 1 } }));
    
    // 30 km/h = 8333 mm/s, turning 20 deg/min = ~5.8 mrad/s
    host.speedMmps = 8333;
    host.yawMradS = 6;
    
    // Reach steady state
    vi.advanceTimersByTime(2000);
    
    let mtrFb = hubEvents.filter(e => e.payload.frame.id === "0x206").pop();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBeGreaterThan(8300);
    
    // Trigger ESTOP!
    engine.injectExternal(normalizeFrame({ ts: 0, bus: "high", id: "0x001", name: "SAFETY_ESTOP", dlc: 0, data: [], decoded: {} }));
    
    // 100ms wait to allow RT to process ESTOP and send targetSpeed=0 to MTR, avoiding stale 0x204 race condition
    vi.advanceTimersByTime(100);
    mtrFb = hubEvents.filter(e => e.payload.frame.id === "0x206").pop();
    expect(mtrFb.payload.decoded.signals.actual_speed_mmps).toBe(0);
  });
});

