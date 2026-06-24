/**
 * Scenario: Freeze Jetson heartbeat after startup — RT should detect timeout
 * and apply assisted stop (2000 kPa brake).
 */
import type { SimConfig, SimulationResult } from "../core/types.js";
import { SimulationRunner } from "../harness/runner.js";

export const heartbeatTimeout = {
  name: "Heartbeat timeout",
  description: "Freeze Jetson 0x7FC at t=500ms, verify RT continues producing frames",

  configure(): Partial<SimConfig> {
    return {
      initialMode: "auto",
      jetsonDriveCycle: [
        { speedMmps: 1000, yawRateMradS: 0, gear: 1, durationMs: 10000 },
      ],
      faults: [
        { atMs: 500, type: "freezeHeartbeat", target: "jetson" },
      ],
    };
  },

  run(): SimulationResult {
    const runner = new SimulationRunner();
    runner.configure(this.configure());
    return runner.runDuration(4000);
  },

  assertions(result: SimulationResult): Array<{ name: string; pass: boolean; message: string }> {
    return [
      {
        name: "No validation errors",
        pass: result.validationErrors.length === 0,
        message: `Errors: ${result.validationErrors.map(e => e.error).join(", ")}`,
      },
      {
        name: "0x204 continues on low bus (RT still running)",
        pass: (result.lowBus.byId["0x204"] ?? 0) > 20,
        message: `0x204 count: ${result.lowBus.byId["0x204"] ?? 0}`,
      },
      {
        name: "CAN frames produced",
        pass: result.totalFrames > 100,
        message: `Total frames: ${result.totalFrames}`,
      },
    ];
  },
};
