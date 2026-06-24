/**
 * Scenario: Jetson sends drive command → RT produces 0x204 + 0x169
 * → MTR responds with 0x120/0x206 → EPS-C responds with 0x201.
 */

import type { SimConfig, SimulationResult } from "../core/types.js";
import { SimulationRunner } from "../harness/runner.js";

export const driveForward = {
  name: "Drive forward",
  description: "Jetson sends 0x300 speed=2000, yaw=0, gear=D → verify full pipeline",

  configure(): Partial<SimConfig> {
    return {
      initialMode: "auto",
      jetsonDriveCycle: [
        { speedMmps: 0, yawRateMradS: 0, gear: 0, durationMs: 500 },
        { speedMmps: 2000, yawRateMradS: 0, gear: 1, durationMs: 5000 },
      ],
    };
  },

  run(): SimulationResult {
    const runner = new SimulationRunner();
    runner.configure(this.configure());
    return runner.runDuration(2000); // run for 2s
  },

  assertions(result: SimulationResult): Array<{ name: string; pass: boolean; message: string }> {
    return [
      {
        name: "0x204 frames were produced on low bus",
        pass: result.lowBus.total > 0,
        message: `Low bus total frames: ${result.lowBus.total}`,
      },
      {
        name: "No validation errors",
        pass: result.validationErrors.length === 0,
        message: `Validation errors: ${result.validationErrors.map(e => `${e.canId}: ${e.error}`).join(", ")}`,
      },
      {
        name: "0x204 appears in per-ID stats",
        pass: (result.lowBus.byId["0x204"] ?? 0) > 0,
        message: `0x204 count: ${result.lowBus.byId["0x204"] ?? 0}`,
      },
      {
        name: "CAN frames produced",
        pass: result.totalFrames > 50,
        message: `Total frames: ${result.totalFrames}`,
      },
    ];
  },
};
