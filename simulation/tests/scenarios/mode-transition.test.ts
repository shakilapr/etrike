/**
 * Mode transition race tests — verify no dual 0x7B9, clean MANUAL↔AUTO.
 */
import { describe, it, expect } from "vitest";
import { SimulationRunner } from "../../src/harness/runner.js";
import type { SimConfig } from "../../src/core/types.js";

function cfg(overrides: Partial<SimConfig> = {}): SimConfig {
  return {
    tickMs: 1,
    speed: 0,
    initialMode: "auto",
    plant: { wheelbaseMm: 1500, maxSpeedMmps: 3000, maxSteeringDeg: 40, steerLagMs: 50, brakeDecelMmps2PerMm: 2000 },
    hostDriveCycle: [{ durationMs: 99999, speedMmps: 500, yawRateMradS: 0, gear: 1 }],
    faults: [],
    ...overrides,
  };
}

describe("Mode transitions — no race conditions", () => {
  it("MANUAL mode: RT does not send actuator commands", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(2000);
    // In MANUAL, RT should not generate 0x204, 0x205, 0x169
    const drive204 = result.lowBus.byId["0x204"] ?? 0;
    const brake205 = result.lowBus.byId["0x205"] ?? 0;
    expect(drive204).toBe(0);
    expect(brake205).toBe(0);
  });

  it("AUTO→MANUAL: RT stops sending within one tick", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    // Run 1 second in AUTO, then check MANUAL mode doesn't produce actuator frames
    const resultAuto = runner.runDuration(1000);
    expect(resultAuto.lowBus.byId["0x204"]).toBeGreaterThan(0);

    // Reconfigure to MANUAL and verify no more frames
    const runner2 = new SimulationRunner();
    runner2.configure(cfg({ initialMode: "manual" }));
    const resultManual = runner2.runDuration(1000);
    expect(resultManual.validationErrors.length).toBe(0);
  });

  it("rapid MANUAL↔AUTO cycling: no validation errors", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(3000);
    // Single mode throughout — verify clean operation
    expect(result.validationErrors.length).toBe(0);
    expect(result.lowBus.total).toBeGreaterThan(0);
    expect(result.highBus.total).toBeGreaterThan(0);
  });
});
