import { describe, it, expect, beforeEach } from "vitest";
import {
  TricycleKinematics,
  computeDynamicLimit,
  computeFollowingErrorThreshold,
} from "../../src/physics/tricycle.js";

describe("TricycleKinematics", () => {
  let model: TricycleKinematics;

  beforeEach(() => {
    model = new TricycleKinematics();
  });

  // ── Forward motion ───────────────────────────────────────────────

  it("resolves straight-ahead drive at 2 m/s", () => {
    const out = model.resolve({ speedMmps: 2000, yawRateMradS: 0 });
    expect(out.motorSpeedMmps).toBe(2000);
    expect(out.steerAngleMdeg).toBe(0);
    expect(out.steerValid).toBe(true);
    expect(out.reversing).toBe(false);
  });

  it("resolves a gentle turn at moderate speed", () => {
    // 2 m/s, 100 mrad/s yaw → small positive steer angle
    const out = model.resolve({ speedMmps: 2000, yawRateMradS: 100 });
    expect(out.motorSpeedMmps).toBe(2000);
    expect(out.steerAngleMdeg).toBeGreaterThan(0);
    // Formula: atan2(1.5 * 0.1, 2.0) = atan2(0.15, 2.0) ≈ 0.0749 rad ≈ 4.29°
    // In millidegrees: ~4290
    expect(out.steerAngleMdeg).toBeCloseTo(4290, -2); // within ~100 mdeg
    expect(out.steerValid).toBe(true);
    expect(out.reversing).toBe(false);
  });

  it("saturates steering at hard limit (~40°)", () => {
    // Large yaw rate combined with moderate speed to force saturation
    const out = model.resolve({ speedMmps: 1000, yawRateMradS: 1500 });
    expect(out.steerSaturated).toBe(true);
    // Angle should be clamped to ±40000 mdeg (±40°)
    expect(Math.abs(out.steerAngleMdeg)).toBeLessThanOrEqual(40000);
  });

  // ── Reverse ──────────────────────────────────────────────────────

  it("handles reversing at -0.5 m/s", () => {
    const out = model.resolve({ speedMmps: -500, yawRateMradS: 0 });
    expect(out.motorSpeedMmps).toBe(-500);
    expect(out.reversing).toBe(true);
  });

  it("rejects reverse below speed limit", () => {
    const out = model.resolve({ speedMmps: -1000, yawRateMradS: 0 });
    // Should be clamped to max reverse
    expect(out.motorSpeedMmps).toBeGreaterThanOrEqual(-500);
  });

  // ── Low-speed decay ──────────────────────────────────────────────

  it("decays steering toward zero at standstill", () => {
    // First turn to establish a steer hold
    model.resolve({ speedMmps: 2000, yawRateMradS: 100 });
    // Then come to a stop
    const out = model.resolve({ speedMmps: 0, yawRateMradS: 0 });
    // Steering should be partly decayed from previous hold
    expect(Math.abs(out.steerAngleMdeg)).toBeLessThan(5000); // less than 5°
    expect(out.reversing).toBe(false);
  });

  it("converts pure yaw at standstill to minimum-radius arc", () => {
    // At standstill with yaw rate, should produce a small forward arc
    const out = model.resolve({ speedMmps: 0, yawRateMradS: 50 });
    expect(out.motorSpeedMmps).toBeGreaterThan(0); // should be moving forward
    expect(Math.abs(out.steerAngleMdeg)).toBeGreaterThan(0);
  });

  // ── Obstacle limiter ─────────────────────────────────────────────

  it("obstacle_limit returns 0 at stop distance", () => {
    const result = TricycleKinematics.obstacleLimit(2000, 300);
    expect(result).toBe(0);
  });

  it("obstacle_limit returns full speed at clear distance", () => {
    const result = TricycleKinematics.obstacleLimit(2000, 3000);
    expect(result).toBe(2000);
  });

  it("obstacle_limit scales linearly in between", () => {
    // Halfway: 1650mm → 50% of target
    const result = TricycleKinematics.obstacleLimit(2000, 1650);
    expect(result).toBe(1000);
  });
});

describe("computeDynamicLimit", () => {
  it("returns 40° at 2 km/h", () => {
    const speedMmps = (2 * 1000) / 3.6; // 2 km/h in mm/s ≈ 556
    const limit = computeDynamicLimit(speedMmps);
    expect(limit).toBeCloseTo(40, 0);
  });

  it("returns ~5° at 25 km/h", () => {
    const speedMmps = (25 * 1000) / 3.6; // ≈ 6944 mm/s
    const limit = computeDynamicLimit(speedMmps);
    expect(limit).toBeCloseTo(5, 0);
  });

  it("is clamped to [5, 40]", () => {
    // Very slow
    expect(computeDynamicLimit(0)).toBeLessThanOrEqual(40);
    expect(computeDynamicLimit(0)).toBeGreaterThanOrEqual(5);
    // Very fast
    expect(computeDynamicLimit(10000)).toBeGreaterThanOrEqual(5);
  });
});

describe("computeFollowingErrorThreshold", () => {
  it("is at least 2° (min threshold at high speeds)", () => {
    // At very high speed, dynamic limit → 5°, so threshold = max(2, 0.25*5) = 2
    const veryFast = 25000 / 3.6; // 25 km/h in mm/s
    expect(computeFollowingErrorThreshold(veryFast)).toBe(2);
  });

  it("is larger at low speeds", () => {
    // At standstill, dynamic limit = 40°, threshold = max(2, 0.25*40) = 10
    expect(computeFollowingErrorThreshold(0)).toBe(10);
  });
});
