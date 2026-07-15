import { describe, it } from 'vitest';
import { SimulationRunner } from "../src/harness/runner.js";
import { exportFramesToJsonl } from "../src/harness/exporter.js";
import { driveForward } from "../src/scenarios/drive-forward.js";
import { estopFlow } from "../src/scenarios/estop-flow.js";
import { heartbeatTimeout } from "../src/scenarios/heartbeat-timeout.js";
import { steeringSync } from "../src/scenarios/steering-sync.js";

describe('Export Traces', () => {
  it('exports all scenarios to JSONL', async () => {
    const scenarios = [
      { name: "drive_forward", run: () => driveForward.run() },
      { name: "estop_flow", run: () => estopFlow.run() },
      { name: "heartbeat_timeout", run: () => heartbeatTimeout.run() },
      { name: "steering_sync", run: () => steeringSync.run() }
    ];

    for (const s of scenarios) {
      console.log(`Running scenario: ${s.name}`);
      const runner = new SimulationRunner();
      
      const scenarioDef = s.name === "drive_forward" ? driveForward :
                          s.name === "estop_flow" ? estopFlow :
                          s.name === "heartbeat_timeout" ? heartbeatTimeout : steeringSync;
                          
      runner.configure(scenarioDef.configure());
      
      let durationMs = 2000;
      if (s.name === "heartbeat_timeout") durationMs = 1500;
      if (s.name === "estop_flow") durationMs = 2000;
      
      runner.runDuration(durationMs);
      
      exportFramesToJsonl(runner.capturedFrames, `./traces/${s.name}.jsonl`);
    }
  });
});
