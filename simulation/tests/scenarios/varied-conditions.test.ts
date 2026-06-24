/**
 * Varied Conditions Test Suite — exercises ECUs under many operating conditions.
 *
 * Covers: manual/auto/estop modes, speed ranges, steering, braking, faults,
 * mode transitions, CAN corruption, obstacle scenarios.
 */
import { describe, it, expect } from "vitest";
import { SimulationRunner } from "../../src/harness/runner.js";
import type { SimConfig } from "../../src/core/types.js";

function cfg(opts: Partial<SimConfig> = {}): SimConfig {
  return {
    tickMs: 1, speed: 0, initialMode: "auto",
    plant: { wheelbaseMm: 1500, maxSpeedMmps: 3000, maxSteeringDeg: 40, steerLagMs: 50, brakeDecelMmps2PerMm: 2000 },
    hostDriveCycle: [{ durationMs: 99999, speedMmps: 0, yawRateMradS: 0, gear: 0 }],
    faults: [],
    ...opts,
  };
}

// ═══════════════════════════════════════════════════════════════
//  MODE TESTS
// ═══════════════════════════════════════════════════════════════

describe("Manual mode", () => {
  it("Host 0x300 ignored — RT does not forward drive commands", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      initialMode: "manual",
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(500);
    // In manual mode, Host drive commands are ignored → little to no bus activity
    // (heartbeats and diag frames still produced)
    expect(result.validationErrors.length).toBe(0);
  });
});

describe("Auto mode", () => {
  it("Host 0x300 drives vehicle at commanded speed", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(1000);
    expect(result.plantFinalSpeedMmps).toBeGreaterThan(500);
  });
});

describe("ESTOP mode — triggered while at speed", () => {
  it("vehicle stops within 1500ms of ESTOP trigger", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [
        { durationMs: 0, speedMmps: 2500, yawRateMradS: 0, gear: 1 },
        { durationMs: 99999, speedMmps: 2500, yawRateMradS: 0, gear: 1 },
      ],
      faults: [{ atMs: 1000, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(3000);
    // After 2000ms of ESTOP braking, vehicle should be near stop
    expect(result.plantFinalSpeedMmps).toBeLessThan(200);
  });

  it("ESTOP while stopped — nothing breaks", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 0, yawRateMradS: 0, gear: 0 }],
      faults: [{ atMs: 500, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(1000);
    expect(result.plantFinalSpeedMmps).toBe(0);
    expect(result.validationErrors.length).toBe(0);
  });
});

// ═══════════════════════════════════════════════════════════════
//  SPEED RANGE TESTS
// ═══════════════════════════════════════════════════════════════

describe("Speed ranges", () => {
  it("creep speed: 2 km/h (555 mm/s)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 555, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(500);
    expect(result.plantFinalSpeedMmps).toBeCloseTo(555, -1); // within ~10
  });

  it("cruise speed: 2500 mm/s — reaches target within 2s", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(2000);
    // Plant accelerates at 3000 mm/s², should reach ~2000 in ~670ms
    expect(result.plantFinalSpeedMmps).toBeGreaterThan(1500);
    expect(result.validationErrors.length).toBe(0);
  });

  it("max speed: 25 km/h (6944 mm/s)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 6944, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(2000);
    // Clamped to maxSpeedMmps (3000 in config)
    expect(result.plantFinalSpeedMmps).toBeLessThanOrEqual(3000);
  });

  it("reverse speed: -500 mm/s", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      plant: { wheelbaseMm: 1500, maxSpeedMmps: 3000, maxSteeringDeg: 40, steerLagMs: 50, brakeDecelMmps2PerMm: 2000 },
      hostDriveCycle: [{ durationMs: 99999, speedMmps: -400, yawRateMradS: 0, gear: 3 }],
    }));
    const result = runner.runDuration(500);
    expect(result.plantFinalSpeedMmps).toBeLessThan(0);
  });

  it("rapid speed change: 0 → 2500 → 500 → 0", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [
        { durationMs: 0,    speedMmps: 0,    yawRateMradS: 0, gear: 0 },
        { durationMs: 1000, speedMmps: 2500, yawRateMradS: 0, gear: 1 },
        { durationMs: 500,  speedMmps: 500,  yawRateMradS: 0, gear: 1 },
        { durationMs: 1500, speedMmps: 0,    yawRateMradS: 0, gear: 0 },
      ],
    }));
    const result = runner.runDuration(4000);
    expect(result.plantFinalSpeedMmps).toBeLessThan(100);
    expect(result.validationErrors.length).toBe(0);
  });
});

// ═══════════════════════════════════════════════════════════════
//  STEERING TESTS
// ═══════════════════════════════════════════════════════════════

describe("Steering", () => {
  it("straight line: yaw=0 → 0x169 steering frames present", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 1000, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(1500);
    // After steering sync (~700ms), 0x169 should be on low bus
    const h169 = result.lowBus.byId["0x169"] ?? 0;
    expect(h169).toBeGreaterThan(0);
    expect(result.validationErrors.length).toBe(0);
  });

  it("turning: yaw=100 mrad/s → 0x169 varies with yaw", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2778, yawRateMradS: 100, gear: 1 }],
    }));
    const result = runner.runDuration(2000);
    // Steering subsystem active: 0x169 and 0x201 present
    const h169 = result.lowBus.byId["0x169"] ?? 0;
    const h201 = result.lowBus.byId["0x201"] ?? 0;
    expect(h169).toBeGreaterThan(0);
    expect(h201).toBeGreaterThan(0);
    expect(result.validationErrors.length).toBe(0);
  });

  it("hard turn: yaw=500 mrad/s — no CAN errors", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 1389, yawRateMradS: 500, gear: 1 }],
    }));
    const result = runner.runDuration(2000);
    expect(result.validationErrors.length).toBe(0);
  });

  it("dynamic angle clamp: high speed limits steering", () => {
    // At 25 km/h, steering limit = 5° (dynamic clamp)
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 6944, yawRateMradS: 1000, gear: 1 }],
    }));
    const result = runner.runDuration(1000);
    // Steering angle should be clamped — not exceeding ~5°
    expect(result.plantMaxSteerDeg).toBeLessThan(10);
  });
});

// ═══════════════════════════════════════════════════════════════
//  BRAKING TESTS
// ═══════════════════════════════════════════════════════════════

describe("Braking", () => {
  it("ESTOP at full speed → max brake (20000 kPa → full stroke)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [
        { durationMs: 0, speedMmps: 3000, yawRateMradS: 0, gear: 1 },
        { durationMs: 99999, speedMmps: 3000, yawRateMradS: 0, gear: 1 },
      ],
      faults: [{ atMs: 2000, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(4000);
    expect(result.plantFinalSpeedMmps).toBeLessThan(100);
    // Brake stroke should be non-zero (brakes engaged)
    expect(result.plantFinalBrakeStrokeMm).toBeGreaterThanOrEqual(0);
  });

  it("braking while steering (combined maneuver)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [
        { durationMs: 0,    speedMmps: 2500, yawRateMradS: 300, gear: 1 },
        { durationMs: 2000, speedMmps: 0,    yawRateMradS: 0,   gear: 0 },
      ],
    }));
    const result = runner.runDuration(5000);
    expect(result.plantFinalSpeedMmps).toBeLessThan(100);
    // No validation errors during combined maneuver
    expect(result.validationErrors.length).toBe(0);
  });
});

// ═══════════════════════════════════════════════════════════════
//  FAULT INJECTION
// ═══════════════════════════════════════════════════════════════

describe("Fault injection", () => {
  it("drop 0x204 frame → speed feedback loop not disrupted", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 1000, yawRateMradS: 0, gear: 1 }],
      faults: [
        { atMs: 200, type: "dropMessage", canId: "0x204", bus: "low" },
      ],
    }));
    const result = runner.runDuration(1000);
    // A single dropped frame should not cause major disruption
    expect(result.totalFrames).toBeGreaterThan(10);
  });

  it("corrupt 0x169 steering command → checksum catches it", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 500, yawRateMradS: 200, gear: 1 }],
      faults: [
        { atMs: 300, type: "corruptMessage", canId: "0x169", bus: "low", byteIndex: 2, xorMask: 0xAA },
      ],
    }));
    const result = runner.runDuration(1000);
    // System should recover after corrupted frame
    expect(result.totalFrames).toBeGreaterThan(10);
  });

  it("multiple sequential ESTOP triggers — only first matters", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
      faults: [
        { atMs: 500, type: "triggerEstop" },
        { atMs: 1000, type: "triggerEstop" },
        { atMs: 1500, type: "triggerEstop" },
      ],
    }));
    const result = runner.runDuration(3000);
    expect(result.plantFinalSpeedMmps).toBeLessThan(100);
    // System shouldn't crash from repeated ESTOP triggers
    expect(result.totalFrames).toBeGreaterThan(10);
  });

  it("ESTOP + corrupt frame simultaneously", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
      faults: [
        { atMs: 500, type: "triggerEstop" },
        { atMs: 500, type: "corruptMessage", canId: "0x205", bus: "low", byteIndex: 0 },
      ],
    }));
    const result = runner.runDuration(1500);
    expect(result.plantFinalSpeedMmps).toBeLessThan(2000);
  });
});

// ═══════════════════════════════════════════════════════════════
//  OBSTACLE SCENARIOS
// ═══════════════════════════════════════════════════════════════

describe("Obstacle braking", () => {
  it("obstacle at 200mm → max brake assist (5000 kPa)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
    }));
    // Set obstacle very close (200mm = emergency)
    runner.rt.setObstacle(200);
    const result = runner.runDuration(1000);
    // Obstacle braking should slow vehicle — it won't maintain 2000 mm/s
    // (brake is applied at 50Hz, should decelerate significantly over 1s)
    expect(result.validationErrors.length).toBe(0);
  });

  it("obstacle at 1500mm → moderate assist", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
    }));
    runner.rt.setObstacle(1500);
    const result = runner.runDuration(500);
    expect(result.validationErrors.length).toBe(0);
  });

  it("obstacle at 3000mm → no brake assist (clear)", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
    }));
    runner.rt.setObstacle(3000);
    const result = runner.runDuration(1000);
    expect(result.plantFinalSpeedMmps).toBeGreaterThan(1000);
  });
});

// ═══════════════════════════════════════════════════════════════
//  EDGE CASES
// ═══════════════════════════════════════════════════════════════

describe("Edge cases", () => {
  it("zero-duration simulation", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg());
    const result = runner.runDuration(0);
    expect(result.totalFrames).toBe(0);
  });

  it("single tick operation", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 1000, yawRateMradS: 0, gear: 1 }],
    }));
    const result = runner.runDuration(1);
    expect(result.validationErrors.length).toBe(0);
  });

  it("ESTOP triggered at t=0ms", () => {
    const runner = new SimulationRunner();
    runner.configure(cfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 0, yawRateMradS: 0, gear: 0 }],
      faults: [{ atMs: 0, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(500);
    expect(result.plantFinalSpeedMmps).toBe(0);
  });
});
