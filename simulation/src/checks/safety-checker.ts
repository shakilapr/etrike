/**
 * SafetyChecker — continuous violation detection during simulation.
 *
 * Checks: heartbeat timeouts, ESTOP response time, mode compliance.
 * Runs each tick and accumulates violations in the result.
 */

import type { SafetyViolation } from "../core/types.js";
import type { SimulationContext } from "../ecus/base.js";

export class SafetyChecker {
  private violations: SafetyViolation[] = [];
  private estopTriggerMs = -1;
  private allNodesStoppedMs = -1;

  /** Record a violation at the current time. */
  add(nowMs: number, type: string, description: string): void {
    this.violations.push({ timeMs: nowMs, type, description });
  }

  /** Mark ESTOP triggered. */
  triggerEstop(nowMs: number): void {
    if (this.estopTriggerMs < 0) {
      this.estopTriggerMs = nowMs;
    }
  }

  /** Check ESTOP response time: all nodes must be safe within 500ms. */
  checkEstopResponse(nowMs: number, ctx: SimulationContext, mtrGearN: boolean, brakeStrokeMax: boolean): void {
    if (this.estopTriggerMs < 0) return;

    const elapsed = nowMs - this.estopTriggerMs;
    if (mtrGearN && brakeStrokeMax && this.allNodesStoppedMs < 0) {
      this.allNodesStoppedMs = nowMs;
      if (elapsed > 500) {
        this.add(nowMs, "estop_response", `ESTOP response took ${elapsed}ms (>500ms limit)`);
      }
    }
  }

  /**
   * Placeholder — ESTOP frame rate limiting not yet implemented (gap #14).
   * Future: only accept 2 ESTOP frames per 500ms sliding window.
   * @returns true if the ESTOP frame is accepted.
   */
  processEstop(_timestampMs: number): boolean {
    return true; // stub — no rate limiting yet
  }

  /** Verify mode compliance: only correct sender transmits 0x7B9 per mode. */
  checkModeCompliance(nowMs: number, canId: string, sender: string, mode: string): void {
    if (canId === "0x7B9") {
      if (mode === "auto" && sender !== "sys") {
        // In AUTO, architecture says SYS should receive pressure CAN and translate — OK for now
      }
      if (mode === "manual" && sender !== "sys") {
        this.add(nowMs, "mode_compliance", `0x7B9 sent by ${sender} in MANUAL mode (expected sys)`);
      }
    }
  }

  getAllViolations(): SafetyViolation[] {
    return [...this.violations];
  }

  reset(): void {
    this.violations = [];
    this.estopTriggerMs = -1;
    this.allNodesStoppedMs = -1;
  }
}
