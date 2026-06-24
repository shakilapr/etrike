import { describe, it, expect } from "vitest";
import { driveForward } from "../../src/scenarios/drive-forward.js";
import { estopFlow } from "../../src/scenarios/estop-flow.js";
import { heartbeatTimeout } from "../../src/scenarios/heartbeat-timeout.js";
import { steeringSync } from "../../src/scenarios/steering-sync.js";

const scenarios = [driveForward, estopFlow, heartbeatTimeout, steeringSync];

describe("Scenarios", () => {
  for (const scenario of scenarios) {
    it(scenario.name, () => {
      const result = scenario.run();

      for (const assertion of scenario.assertions(result)) {
        expect(assertion.pass, `${assertion.name}: ${assertion.message}`).toBe(true);
      }
    });
  }
});
