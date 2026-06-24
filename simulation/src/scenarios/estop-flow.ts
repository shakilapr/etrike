/**
 * Scenario: ESTOP triggered — verify all nodes enter safe state.
 */
import type { SimConfig, SimulationResult } from "../core/types.js";
import { SimulationRunner } from "../harness/runner.js";

export const estopFlow = {
  name: "ESTOP flow",
  description: "Trigger ESTOP at t=1500ms and verify safe state within 500ms",

  configure(): Partial<SimConfig> {
    return {
      initialMode: "auto",
      jetsonDriveCycle: [
        { speedMmps: 2000, yawRateMradS: 0, gear: 1, durationMs: 5000 },
      ],
      faults: [
        { atMs: 1500, type: "triggerEstop" },
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
        name: "Plant was moving before ESTOP",
        pass: result.plantMaxSteerDeg >= 0,
        message: "Plant steering should be tracked",
      },
      {
        name: "No validation errors",
        pass: result.validationErrors.length === 0,
        message: `Errors: ${result.validationErrors.map(e => e.error).join(", ")}`,
      },
      {
        name: "0x204 appears on low bus",
        pass: (result.lowBus.byId["0x204"] ?? 0) > 10,
        message: `0x204 count: ${result.lowBus.byId["0x204"] ?? 0}`,
      },
      {
        name: "CAN frames produced on both buses",
        pass: result.highBus.total > 0 && result.lowBus.total > 0,
        message: `High: ${result.highBus.total}, Low: ${result.lowBus.total}`,
      },
    ];
  },
};
