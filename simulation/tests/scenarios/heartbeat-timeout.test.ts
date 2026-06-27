import { describe, it, expect } from "vitest";
import { SimulationRunner } from "../../src/harness/runner.js";

describe("Architecture Gaps", () => {
  it("gap #10: Host heartbeat loss triggers assisted stop brake (2000 kPa)", () => {
    const runner = new SimulationRunner();
    runner.configure({
      initialMode: "auto",
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 1000, yawRateMradS: 0, gear: 1 }],
      faults: [
        { atMs: 500, type: "freezeHeartbeat", target: "host" },
      ],
    });
    const result = runner.runDuration(5000);

    // After 1500ms timeout + one frame, 0x205 should carry assist-stop brake (2000 kPa)
    const brakeFrames = runner.capturedFrames.filter(f => f.canId === "0x205");
    const assistBrakeFound = brakeFrames.some((f: any) => {
      const kpa = (f.data[0] << 24) | (f.data[1] << 16) | (f.data[2] << 8) | f.data[3];
      return kpa >= 2000;
    });
    expect(assistBrakeFound).toBe(true);

    // Vehicle should decelerate — motor zeroed + 2000 kPa assist stop brake
    expect(result.plantFinalSpeedMmps).toBeLessThan(500);
  });
});
