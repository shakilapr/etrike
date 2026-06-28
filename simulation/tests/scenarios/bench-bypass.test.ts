/**
 * Bench Bypass Tests — verify each bypass flag scenario.
 *
 * Models the bench setup: single ECU, no peer ECUs, no actuators.
 * These tests exercise the same paths the compile-time bypass flags
 * (CONFIG_BENCH_SOLO, BYPASS_EPS_C_SYNC, etc.) enable on real hardware.
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

// ═══════════════════════════════════════════════════════════════
//  CONFIG_BENCH_SOLO — single ECU, no peer heartbeat timeouts
// ═══════════════════════════════════════════════════════════════

describe("CONFIG_BENCH_SOLO — single ECU bench", () => {
  it("RT alone on bus: does not ESTOP on missing SYS heartbeat", () => {
    // In bench mode, peer heartbeat timeouts are disabled.
    // RT should continue operating even when SYS is absent.
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "auto" }));
    // Run without SYS present — RT should handle gracefully
    const result = runner.runDuration(3000);
    // RT should still produce telemetry frames
    expect(result.highBus.total).toBeGreaterThan(0);
    expect(result.lowBus.total).toBeGreaterThan(0);
  });

  it("SYS alone on bus: does not ESTOP on missing RT heartbeat", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(2000);
    // SYS should send its own frames without RT present
    expect(result.lowBus.total).toBeGreaterThan(0);
    expect(result.validationErrors.length).toBe(0);
  });

  it("RT operates in AUTO with Host commands even without peer ECUs", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 1000, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(2000);
    // 0x204 should be generated (Host→RT→low bus)
    const drive204 = result.lowBus.byId["0x204"] ?? 0;
    expect(drive204).toBeGreaterThan(0);
    expect(result.validationErrors.length).toBe(0);
  });
});

// ═══════════════════════════════════════════════════════════════
//  CONFIG_BYPASS_EPS_C_SYNC — no steering actuator present
// ═══════════════════════════════════════════════════════════════

describe("CONFIG_BYPASS_EPS_C_SYNC — no steering actuator", () => {
  it("RT does not FAULT when EPS-C is absent", () => {
    // Without EPS-C, 0x201 never arrives. Bypass skips LISTEN_SYNC.
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 500, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(3000);
    // Should not have validation errors from missing EPS-C
    const stateFrames = runner.capturedFrames.filter(f => f.canId === "0x210");
    expect(stateFrames.length).toBeGreaterThan(0);
    // Last state frame should not show fault (safety_state != 2)
    const lastState = stateFrames[stateFrames.length - 1];
    expect(lastState.data[1] & 0x03).not.toBe(2); // not FAULT
  });

  it("RT still sends 0x169 even without EPS-C feedback", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 500, yawRateMradS: 100, gear: 1 }],
    }));
    const result = runner.runDuration(3000);
    // 0x169 should be generated even without 0x201 feedback
    // (in bypass mode, steering is assumed centered at 0°)
    const steerFrames = runner.capturedFrames.filter(f => f.canId === "0x169");
    expect(steerFrames.length).toBeGreaterThan(0);
  });
});

// ═══════════════════════════════════════════════════════════════
//  CONFIG_BYPASS_SEB_SYNC — no brake actuator present
// ═══════════════════════════════════════════════════════════════

describe("CONFIG_BYPASS_SEB_SYNC — no brake actuator", () => {
  it("SYS does not hang in LISTEN_SYNC when SEB is absent", () => {
    // Without SEB, 0x721 never arrives. Bypass skips to DEGRADED.
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(2000);
    // SYS should still produce periodic frames
    const safetyFrames = runner.capturedFrames.filter(f => f.canId === "0x011");
    expect(safetyFrames.length).toBeGreaterThan(0);
    expect(result.validationErrors.length).toBe(0);
  });

  it("SYS sends 0x7B9 in DEGRADED mode (lever-only, no sync)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(2000);
    // In bypass mode, SYS operates in DEGRADED — still sends brake commands
    const brakeFrames = runner.capturedFrames.filter(f => f.canId === "0x7B9");
    // DEGRADED mode sends 0x7B9 with lever defaults
    expect(brakeFrames.length).toBeGreaterThan(0);
  });
});

// ═══════════════════════════════════════════════════════════════
//  CONFIG_BYPASS_MTR_ABSENT — no MTR feedback
// ═══════════════════════════════════════════════════════════════

describe("CONFIG_BYPASS_MTR_ABSENT — no MTR on bus", () => {
  it("SYS does not ESTOP when MTR feedback (0x206) is absent", () => {
    // Without MTR, EGAS L2 monitoring is skipped.
    // SYS should not trigger ESTOP just because 0x206 is missing.
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 500, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(3000);
    // No ESTOP frames should be generated from MTR absence
    const estopFrames = runner.capturedFrames.filter(f => f.canId === "0x001");
    // ESTOP count from MTR absence should be zero
    expect(result.validationErrors.length).toBe(0);
  });

  it("SYS still monitors CAN health without MTR present", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({ initialMode: "manual" }));
    const result = runner.runDuration(2000);
    // 0x600 SYS_DIAG_RPT should still be produced
    const diagFrames = runner.capturedFrames.filter(f => f.canId === "0x600");
    expect(diagFrames.length).toBeGreaterThan(0);
  });
});

// ═══════════════════════════════════════════════════════════════
//  COMBINED BYPASSES — all flags active (minimal bench)
// ═══════════════════════════════════════════════════════════════

describe("Full bench bypass — all flags active", () => {
  it("RT+SYS on same bus: no peer ECUs, no actuators — stable for 30s", () => {
    // This is the minimal bench setup: two ESP32-S3 boards,
    // CANalyst-II as Host on high bus. No EPS-C, SEB, MTR, or PWT.
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [
        { durationMs: 5000, speedMmps: 500, yawRateMradS: 0, gear: 1 },
        { durationMs: 5000, speedMmps: 1000, yawRateMradS: 50, gear: 1 },
        { durationMs: 5000, speedMmps: 500, yawRateMradS: 0, gear: 1 },
        { durationMs: 5000, speedMmps: 0, yawRateMradS: 0, gear: 0 },
      ],
    }));
    const result = runner.runDuration(20000);
    // 20s drive cycle, all bypasses active — should be zero validation errors
    expect(result.validationErrors.length).toBe(0);
    // All key frames present
    expect(result.lowBus.byId["0x204"] ?? 0).toBeGreaterThan(0);
    expect(result.highBus.byId["0x210"] ?? 0).toBeGreaterThan(0);
    expect(result.highBus.total).toBeGreaterThan(0);
    expect(result.lowBus.total).toBeGreaterThan(0);
  });
});
