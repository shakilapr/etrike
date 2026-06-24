import { describe, it, expect, beforeEach } from "vitest";
import { RtKinematicsController } from "../../src/controllers/rt-kinematics.js";

describe("RtKinematicsController", () => {
  let ctrl: RtKinematicsController;

  beforeEach(() => {
    ctrl = new RtKinematicsController();
  });

  it("resolves a simple drive command", () => {
    const out = ctrl.resolve({ speedMmps: 2000, yawRateMradS: 0, gear: 1 });
    expect(out.motorSpeedMmps).toBe(2000);
    expect(out.steerAngleDeg).toBe(0);
    expect(out.gear).toBe(1);
  });

  it("computes steer angle for a turn", () => {
    const out = ctrl.resolve({ speedMmps: 2000, yawRateMradS: 100, gear: 1 });
    expect(out.steerAngleDeg).toBeGreaterThan(0);
    expect(out.steerAngleDeg).toBeLessThan(40);
  });

  it("applies dynamic angle clamp", () => {
    // At 3m/s, dynamic limit is ~27°. With yaw=2000 mrad/s, atan2 produces
    // ~45° which the hard limit clamps to 40°, then dynamic clamps to ~27°.
    const out = ctrl.resolve({ speedMmps: 3000, yawRateMradS: 2000, gear: 1 });
    // Should be clamped below hard limit of 40°
    expect(Math.abs(out.steerAngleDeg)).toBeLessThan(30);
    // But should produce some steering
    expect(Math.abs(out.steerAngleDeg)).toBeGreaterThan(10);
  });

  it("obstacle_limit returns 0 at 300mm", () => {
    expect(ctrl.obstacleLimit(2000, 300)).toBe(0);
  });

  it("obstacle_limit returns full at 3000mm", () => {
    expect(ctrl.obstacleLimit(2000, 3000)).toBe(2000);
  });

  it("computeSteerRate returns min at low speed", () => {
    expect(RtKinematicsController.computeSteerRate(0)).toBe(125);
  });

  it("computeSteerRate increases with speed", () => {
    const low = RtKinematicsController.computeSteerRate(0);
    const high = RtKinematicsController.computeSteerRate(7000);
    expect(high).toBeGreaterThan(low);
  });

  it("getDynamicLimit returns 40° at low speed", () => {
    expect(ctrl.getDynamicLimit(0)).toBeCloseTo(40, -1);
  });

  it("getFollowingErrorThreshold is at least 2°", () => {
    expect(ctrl.getFollowingErrorThreshold(0)).toBeGreaterThanOrEqual(2);
  });
});
