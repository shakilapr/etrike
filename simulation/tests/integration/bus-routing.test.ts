/**
 * Bus Routing Test (I8) — verify frames appear on correct buses.
 *
 * - 0x204 RT_DRIVE_CMD: low bus only (never high bus)
 * - 0x300 HOST_DRIVE_CMD: high bus only (never low bus)
 * - Forwarded frames (0x001, 0x011, 0x120, 0x206, 0x600): BOTH buses
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
    faults: [{ atMs: 200, type: "triggerEstop" }],  // ESTOP to test 0x001 on both buses
    ...overrides,
  };
}

describe("Bus routing — I8", () => {
  it("0x204 RT_DRIVE_CMD only on low bus, never on high bus", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg());
    const result = runner.runDuration(200);
    // Before ESTOP at 200ms, RT should have sent 0x204 on low bus
    expect(result.lowBus.byId["0x204"] ?? 0).toBeGreaterThan(0);
    // 0x204 must never appear on high bus
    expect(result.highBus.byId["0x204"] ?? 0).toBe(0);
  });

  it("0x300 HOST_DRIVE_CMD only on high bus, never on low bus", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg());
    const result = runner.runDuration(200);
    expect(result.highBus.byId["0x300"] ?? 0).toBeGreaterThan(0);
    expect(result.lowBus.byId["0x300"] ?? 0).toBe(0);
  });

  it("0x001 ESTOP on both buses after trigger", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg());
    const result = runner.runDuration(500);
    // ESTOP triggered at 200ms, RT originates 0x001 on both buses at 50Hz
    expect(result.lowBus.byId["0x001"] ?? 0).toBeGreaterThan(0);
    expect(result.highBus.byId["0x001"] ?? 0).toBeGreaterThan(0);
  });

  it("0x011 SYS_SAFETY_STS on both buses (forwarded)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg());
    const result = runner.runDuration(1200);
    // SYS sends 0x011 at 5 Hz = ~6 times in 1200ms on low bus
    // RT forwards each to high bus
    expect(result.lowBus.byId["0x011"] ?? 0).toBeGreaterThanOrEqual(1);
    expect(result.highBus.byId["0x011"] ?? 0).toBeGreaterThanOrEqual(1);
  });

  it("0x120 SYS_THROTTLE_STS on both buses (forwarded)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg());
    const result = runner.runDuration(200);
    // MTR sends 0x120 at 100 Hz; RT forwards low→high
    expect(result.lowBus.byId["0x120"] ?? 0).toBeGreaterThan(0);
    expect(result.highBus.byId["0x120"] ?? 0).toBeGreaterThan(0);
  });

  it("0x206 MTR_MOTOR_FBK on both buses (forwarded)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg());
    const result = runner.runDuration(200);
    expect(result.lowBus.byId["0x206"] ?? 0).toBeGreaterThan(0);
    expect(result.highBus.byId["0x206"] ?? 0).toBeGreaterThan(0);
  });

  it("0x600 SYS_DIAG_RPT on both buses (forwarded)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg());
    const result = runner.runDuration(1200);
    // SYS sends 0x600 at 1 Hz; RT forwards low→high
    expect(result.lowBus.byId["0x600"] ?? 0).toBeGreaterThanOrEqual(1);
    expect(result.highBus.byId["0x600"] ?? 0).toBeGreaterThanOrEqual(1);
  });
});
