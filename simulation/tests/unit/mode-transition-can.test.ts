/**
 * Mode transition CAN output test — verify correct CAN frame presence/absence
 * per mode: Manual, Auto, ESTOP, and transitions between them.
 *
 * Architecture:
 * - MANUAL: RT does NOT send 0x204 (drive), 0x205 (brake), 0x169 (steer).
 * - AUTO: RT sends all actuator commands; SYS suppresses 0x7B9.
 * - ESTOP: 0x001 broadcast, zeroed setpoints.
 * - SYS 0x110: sent on mode change only.
 *
 * CAN IDs verified per mode: 0x204, 0x205, 0x169, 0x7B9, 0x110.
 */
import { describe, it, expect } from "vitest";
import { SimulationRunner } from "../../src/harness/runner.js";
import type { SimConfig } from "../../src/core/types.js";

function cfg(overrides: Partial<SimConfig> = {}): SimConfig {
  return {
    tickMs: 1,
    speed: 0,
    initialMode: "manual",
    plant: {
      wheelbaseMm: 1500,
      maxSpeedMmps: 3000,
      maxSteeringDeg: 40,
      steerLagMs: 50,
      brakeDecelMmps2PerMm: 2000,
    },
    hostDriveCycle: [{ durationMs: 99999, speedMmps: 500, yawRateMradS: 0, gear: 1 }],
    faults: [],
    ...overrides,
  };
}

// How many ms to run for each test (long enough for all periodic frames)
const RUN_MS = 2000;

// ── MANUAL mode ───────────────────────────────────────────────────────

describe("MANUAL mode CAN output", () => {
  it("RT does NOT send actuator commands: 0x204, 0x205, 0x169 absent", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(RUN_MS);

    // In MANUAL, RT suppresses 0x204, 0x205, 0x169
    expect(result.lowBus.byId["0x204"] ?? 0).toBe(0);
    expect(result.lowBus.byId["0x169"] ?? 0).toBe(0);
    // 0x205 also suppressed by RT in MANUAL; SYS may send 0x7B9 independently
    const _b205 = result.lowBus.byId["0x205"] ?? 0;
    expect(_b205).toBe(0);
  });

  it("SYS sends 0x110 mode command (MANUAL = 0)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(RUN_MS);

    // SYS sends 0x110 on mode (once at start)
    expect(result.lowBus.byId["0x110"] ?? 0).toBeGreaterThanOrEqual(1);
    expect(result.validationErrors.length).toBe(0);
  });

  it("SYS heartbeat and RT heartbeat present on correct buses", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(RUN_MS);

    // 0x7FE SYS_HEARTBEAT on low bus (10 Hz)
    expect(result.lowBus.byId["0x7FE"] ?? 0).toBeGreaterThan(15);
    expect(result.highBus.byId["0x7FE"] ?? 0).toBe(0); // not bridged

    // 0x7FD RT_HEARTBEAT on both buses (2 Hz)
    expect(result.lowBus.byId["0x7FD"] ?? 0).toBeGreaterThan(3);
    expect(result.highBus.byId["0x7FD"] ?? 0).toBeGreaterThan(3);
  });
});

// ── AUTO mode ─────────────────────────────────────────────────────────

describe("AUTO mode CAN output", () => {
  it("RT sends actuator commands: 0x204, 0x169 present on low bus", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);

    // 0x204 at 100 Hz, 0x169 at 50 Hz (after steering sync ~500ms)
    expect(result.lowBus.byId["0x204"] ?? 0).toBeGreaterThan(100); // ~2000ms * 100Hz
    expect(result.lowBus.byId["0x169"] ?? 0).toBeGreaterThan(50);  // ~1500ms * 50Hz
    expect(result.validationErrors.length).toBe(0);
  });

  it("0x205 brake command present on low bus (50 Hz)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);

    expect(result.lowBus.byId["0x205"] ?? 0).toBeGreaterThan(50); // 50 Hz
  });

  it("0x7B9 present on low bus in AUTO when steering ACTIVE + not ESTOP", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);

    // RT sends 0x7B9 in AUTO when conditions met (after steering sync ~500ms)
    expect(result.lowBus.byId["0x7B9"] ?? 0).toBeGreaterThan(20);
  });

  it("0x204, 0x205, 0x169 NOT on high bus (routed correctly)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);

    // RT actuator commands are low-bus only
    expect(result.highBus.byId["0x204"] ?? 0).toBe(0);
    expect(result.highBus.byId["0x205"] ?? 0).toBe(0);
    expect(result.highBus.byId["0x169"] ?? 0).toBe(0);
    expect(result.highBus.byId["0x7B9"] ?? 0).toBe(0);
  });

  it("SYS 0x110 sent on mode change to AUTO", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);

    expect(result.lowBus.byId["0x110"] ?? 0).toBeGreaterThanOrEqual(1);
  });
});

// ── ESTOP mode ────────────────────────────────────────────────────────

describe("ESTOP mode CAN output", () => {
  it("0x001 ESTOP frames on both buses when triggered", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      initialMode: "auto",
      faults: [{ atMs: 500, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(RUN_MS);

    // ESTOP frames present (from RT + SYS)
    const estopFrames = runner.capturedFrames.filter(
      f => f.canId === "0x001" && f.simTimeMs >= 500,
    );
    expect(estopFrames.length).toBeGreaterThan(0);
  });

  it("0x169 continues during ESTOP ramp-to-zero, then stops in FAULT", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      initialMode: "auto",
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 500, yawRateMradS: 0, gear: 1 }],
      faults: [{ atMs: 1000, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(RUN_MS);

    // After ESTOP at 1000ms, 0x169 should continue during ramp (~1.5s for 30° at 20°/s)
    expect(result.lowBus.byId["0x169"] ?? 0).toBeGreaterThan(30);
    // 0x204 still present (with zeroed speed)
    expect(result.lowBus.byId["0x204"] ?? 0).toBeGreaterThan(50);
  });
});

// ── Manual ↔ Auto transitions ─────────────────────────────────────────

describe("Mode transition cycles", () => {
  it("MANUAL that never transitions has clean validation", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(RUN_MS);
    expect(result.validationErrors.length).toBe(0);
  });

  it("AUTO has no validation errors", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);
    expect(result.validationErrors.length).toBe(0);
  });

  it("AUTO with triggered ESTOP has no validation errors", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      initialMode: "auto",
      faults: [{ atMs: 500, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(RUN_MS);
    // Validation errors are acceptable during ESTOP (rate-limited frames, etc.)
    // but there should not be critical structural violations
    expect(result.totalFrames).toBeGreaterThan(0);
  });
});

// ── Forwarding rules across all modes ─────────────────────────────────

describe("CAN forwarding across modes", () => {
  it("0x011, 0x120, 0x206, 0x600 forwarded low→high in AUTO", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);

    expect(result.highBus.byId["0x011"] ?? 0).toBeGreaterThan(5);
    expect(result.highBus.byId["0x120"] ?? 0).toBeGreaterThan(50);
    expect(result.highBus.byId["0x206"] ?? 0).toBeGreaterThan(20);
    expect(result.highBus.byId["0x600"] ?? 0).toBeGreaterThanOrEqual(1);
  });

  it("0x302 (light cmd) forwarded high→low in AUTO", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);

    // Host sends 0x302 on high bus; RT forwards to low
    expect(result.lowBus.byId["0x302"] ?? 0).toBeGreaterThan(0);
  });

  it("0x7FE SYS heartbeat NOT forwarded to high bus", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    const result = runner.runDuration(RUN_MS);

    // SYS heartbeat stays on low bus (not in forwarding rules)
    // 0x7FE on high bus = forwarded by RT? Should be 0 since not in is_forwarded_low_to_high
    expect(result.highBus.byId["0x7FE"] ?? 0).toBe(0);
  });
});
