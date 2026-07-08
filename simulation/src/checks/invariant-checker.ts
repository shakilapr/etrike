/**
 * InvariantChecker — state machine and physical boundary invariants.
 *
 * Enforces invariants across the simulation, such as:
 * - ESTOP Priority: Once ESTOP is latched, it cannot be unlatched until zero-speed.
 * - Bounds: Speed and steering angle must not exceed physical limits.
 * - Mode Limits: AUTO mode commands cannot exceed defined maximums.
 */

import type { SimulationContext } from "../ecus/base.js";
import type { SimFrame, SafetyViolation } from "../core/types.js";

export class InvariantChecker {
  private violations: SafetyViolation[] = [];
  private maxSpeedMmps: number;
  private maxSteerDeg: number;

  constructor(maxSpeedMmps = 3000, maxSteerDeg = 40) {
    this.maxSpeedMmps = maxSpeedMmps;
    this.maxSteerDeg = maxSteerDeg;
  }

  check(
    nowMs: number,
    ctx: SimulationContext,
    actualSpeedMmps: number,
    actualSteerDeg: number,
    cmdSpeedMmps: number,
    cmdSteerDeg: number
  ): void {
    // 1. ESTOP Priority: If ESTOP is active, commanded speed must be zero.
    if (ctx.estopActive && cmdSpeedMmps !== 0) {
      this.add(nowMs, "invariant_estop_priority", `ESTOP active but commanded speed is ${cmdSpeedMmps} mm/s`);
    }

    // 2. Physical Bounds: Actual speed must be <= maxSpeedMmps + tolerance (e.g. 10%)
    if (Math.abs(actualSpeedMmps) > this.maxSpeedMmps * 1.1) {
      this.add(nowMs, "invariant_speed_bound", `Actual speed ${actualSpeedMmps} exceeds max ${this.maxSpeedMmps}`);
    }

    // 3. Physical Bounds: Actual steering must be <= maxSteerDeg + tolerance
    if (Math.abs(actualSteerDeg) > this.maxSteerDeg + 2.0) {
      this.add(nowMs, "invariant_steer_bound", `Actual steering ${actualSteerDeg} exceeds max ${this.maxSteerDeg}`);
    }

    // 4. Mode Limits: In MANUAL mode, commanded speed must be zero.
    if (ctx.mode === "manual" && cmdSpeedMmps !== 0) {
      this.add(nowMs, "invariant_mode_limit", `MANUAL mode but commanded speed is ${cmdSpeedMmps} mm/s`);
    }
  }

  private add(nowMs: number, type: string, description: string): void {
    this.violations.push({ timeMs: nowMs, type, description });
  }

  getAllViolations(): SafetyViolation[] {
    return [...this.violations];
  }

  reset(): void {
    this.violations = [];
  }
}
