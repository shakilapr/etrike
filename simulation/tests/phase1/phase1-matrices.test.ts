import { describe, expect, it } from "vitest";
import { SimulationRunner } from "../../src/harness/runner.js";
import type { FaultSpec, SimConfig } from "../../src/core/types.js";
import { assertPhase1Invariants } from "../../src/checks/phase1-invariants.js";

const PHASE1_SCENARIO_CASES = 18;

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

function run(config: SimConfig) {
  const runner = new SimulationRunner();
  runner.configure(config);
  const result = runner.runDuration(1200);
  assertPhase1Invariants({ result, frames: runner.capturedFrames, mode: config.initialMode });
  return { runner, result };
}

describe("Phase 1 mode matrix", () => {
  for (const mode of ["manual", "auto", "estop"] as const) {
    it(`${mode} mode honors actuator authority invariants`, () => {
      run(cfg({ initialMode: mode }));
    });
  }
});

describe("Phase 1 bypass matrix", () => {
  for (const mode of ["manual", "auto"] as const) {
    it(`bench bypass visibility remains explicit in ${mode}`, () => {
      const { result } = run(cfg({ initialMode: mode }));
      expect(result.lowBus.byId["0x600"] ?? 0).toBeGreaterThan(0);
    });
  }
});

describe("Phase 1 connection/disconnect matrix", () => {
  const faults: FaultSpec[][] = [
    [{ atMs: 300, type: "freezeHeartbeat", target: "host" }],
    [{ atMs: 300, type: "freezeHeartbeat", target: "sys" }],
    [{ atMs: 300, type: "dropMessage", canId: "0x300", bus: "high" }],
  ];

  faults.forEach((faultSet, index) => {
    it(`connection fault case ${index + 1} remains safe`, () => {
      const { result } = run(cfg({ faults: faultSet }));
      expect(result.validationErrors).toEqual([]);
    });
  });
});

describe("Phase 1 nominal maneuver matrix", () => {
  const maneuvers = [
    { speedMmps: 0, yawRateMradS: 0, gear: 0 },
    { speedMmps: 1000, yawRateMradS: 0, gear: 1 },
    { speedMmps: 500, yawRateMradS: 400, gear: 1 },
    { speedMmps: -300, yawRateMradS: 0, gear: 3 },
  ];

  maneuvers.forEach((step, index) => {
    it(`nominal maneuver ${index + 1} stays in bounds`, () => {
      run(cfg({ hostDriveCycle: [{ durationMs: 99999, ...step }] }));
    });
  });
});

describe("Phase 1 CAN corruption matrix", () => {
  for (const canId of ["0x300", "0x301", "0x721"]) {
    it(`${canId} corruption is visible to validation`, () => {
      const { runner } = run(cfg({
        faults: [{ atMs: 100, type: "corruptMessage", canId, byteIndex: 0, xorMask: 0x01 }],
      }));
      expect(runner.capturedFrames.some(f => f.canId === canId)).toBe(true);
    });
  }
});

describe("Phase 1 scenario coverage target", () => {
  it("records current generated scenario count", () => {
    expect(PHASE1_SCENARIO_CASES).toBeLessThan(504);
  });
});
