/**
 * E-Trike Simulation — CLI entry point.
 *
 * Usage:
 *   npx tsx src/index.ts                            # run default drive-forward scenario
 *   npx tsx src/index.ts --scenario estop-flow       # run a specific scenario
 *   npx tsx src/index.ts --list                      # list available scenarios
 *   npx tsx src/index.ts --duration 10000            # run for 10s
 *   npx tsx src/index.ts --speed 100                 # fast-forward 100x
 */

import { SimulationRunner } from "./harness/runner.js";
import * as scenarios from "./scenarios/index.js";

function main(): void {
  const args = process.argv.slice(2);

  if (args.includes("--list")) {
    console.log("Available scenarios:");
    for (const [key, scenario] of Object.entries(scenarios)) {
      console.log(`  ${scenario.name.padEnd(24)} ${scenario.description}`);
    }
    return;
  }

  // Support both --scenario=name and --scenario name
  function getArgValue(flag: string): string | undefined {
    const eq = args.find((a) => a.startsWith(`${flag}=`));
    if (eq) return eq.split("=", 2)[1];
    const idx = args.indexOf(flag);
    if (idx >= 0 && idx + 1 < args.length && !args[idx + 1].startsWith("-")) {
      return args[idx + 1];
    }
    return undefined;
  }

  const scenarioName = getArgValue("--scenario") ?? "drive-forward";
  const durationMs = parseInt(getArgValue("--duration") ?? "0", 10);
  const speed = parseInt(getArgValue("--speed") ?? "0", 10);

  // Find the scenario by export key (exact match first, then substring)
  const scenarioEntries = Object.entries(scenarios) as Array<[string, typeof scenarios.driveForward]>;
  const cleanName = scenarioName.toLowerCase().replace(/-/g, "");
  let match = scenarioEntries.find(([key]) => key.toLowerCase() === cleanName);
  if (!match) {
    match = scenarioEntries.find(([key]) => key.toLowerCase().includes(cleanName));
  }

  if (!match) {
    console.error(`Unknown scenario: "${scenarioName}". Use --list to see available scenarios.`);
    process.exit(1);
  }

  const [, scenario] = match;

  console.log(`\nRunning scenario: ${scenario.name}`);
  console.log(`  ${scenario.description}`);
  if (speed > 0) console.log(`  Speed: ${speed}x`);

  const runner = new SimulationRunner();
  runner.configure({ ...scenario.configure(), speed });

  const runMs = durationMs || 3000;
  const result = runner.runDuration(runMs);

  console.log(`\n── Results ──`);
  console.log(`  Duration:       ${result.durationMs}ms`);
  console.log(`  Total frames:   ${result.totalFrames}`);
  console.log(`  High bus:       ${result.highBus.total} frames`);
  console.log(`  Low bus:        ${result.lowBus.total} frames`);
  console.log(`  Validation:     ${result.validationErrors.length === 0 ? "✓ PASS" : "✗ FAIL"}`);
  console.log(`  Safety checks:  ${result.violations.length === 0 ? "✓ PASS" : "✗ FAIL"}`);
  console.log(`  Plant speed:    ${result.plantFinalSpeedMmps} mm/s`);
  console.log(`  Plant steer:    ${result.plantFinalSteerDeg.toFixed(1)}°`);
  console.log(`  Max steer:      ${result.plantMaxSteerDeg.toFixed(1)}°`);
  console.log(`  Brake stroke:   ${result.plantFinalBrakeStrokeMm.toFixed(1)}mm`);

  if (result.validationErrors.length > 0) {
    console.log(`\n  Validation errors:`);
    for (const e of result.validationErrors) {
      console.log(`    [${e.timeMs}ms] ${e.canId} on ${e.bus}: ${e.error}`);
    }
  }

  if (result.violations.length > 0) {
    console.log(`\n  Safety violations:`);
    for (const v of result.violations) {
      console.log(`    [${v.timeMs}ms] ${v.type}: ${v.description}`);
    }
  }

  // Assertions
  console.log(`\n── Assertions ──`);
  let allPassed = true;
  for (const assertion of scenario.assertions(result)) {
    const status = assertion.pass ? "✓" : "✗";
    if (!assertion.pass) allPassed = false;
    console.log(`  ${status} ${assertion.name}`);
    if (!assertion.pass) console.log(`      ${assertion.message}`);
  }

  console.log(`\n${allPassed ? "✓ All assertions passed." : "✗ Some assertions failed."}`);
  process.exit(allPassed ? 0 : 1);
}

main();
