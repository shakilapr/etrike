/**
 * Scenario: RT boot sync with EPS-C — verify BOOT_WAIT→LISTEN_SYNC→ACTIVE
 * and that 0x169 transmission begins.
 */
import type { SimConfig, SimulationResult } from "../core/types.js";
import { SimulationRunner } from "../harness/runner.js";

export const steeringSync = {
  name: "Steering sync",
  description: "RT boots, waits for EPS-C alignment at t=600ms, transitions to ACTIVE",

  configure(): Partial<SimConfig> {
    return {
      initialMode: "auto",
      // Gentle drive command so RT has a target to steer to
      hostDriveCycle: [
        { speedMmps: 1000, yawRateMradS: 50, gear: 1, durationMs: 5000 },
      ],
    };
  },

  run(): SimulationResult {
    const runner = new SimulationRunner();
    runner.configure(this.configure());
    return runner.runDuration(3000);
  },

  assertions(result: SimulationResult): Array<{ name: string; pass: boolean; message: string }> {
    return [
      {
        name: "No validation errors",
        pass: result.validationErrors.length === 0,
        message: `Errors: ${result.validationErrors.map(e => e.error).join(", ")}`,
      },
      {
        name: "0x169 steering commands appear on low bus",
        pass: (result.lowBus.byId["0x169"] ?? 0) > 5,
        message: `0x169 count: ${result.lowBus.byId["0x169"] ?? 0} (expected steering commands after sync)`,
      },
      {
        name: "0x204 motor commands appear on low bus",
        pass: (result.lowBus.byId["0x204"] ?? 0) > 20,
        message: `0x204 count: ${result.lowBus.byId["0x204"] ?? 0}`,
      },
    ];
  },
};
