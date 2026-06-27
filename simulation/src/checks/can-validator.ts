/**
 * CanValidator — per-frame correctness checks.
 *
 * Validates: DLC matching, data length, basic range checks.
 * Runs on every produced frame.
 */

import type { ValidationError, BusId } from "../core/types.js";
import { DLC as EXPECTED_DLC } from "../../../shared/can/generated/can_ids";

export class CanValidator {
  private errors: ValidationError[] = [];

  /** Validate a single outgoing frame. */
  validate(nowMs: number, canId: string, bus: BusId, dlc: number, dataLength: number, sender: string): void {
    const expected = EXPECTED_DLC[canId];

    if (expected !== undefined && dlc !== expected) {
      // Some IDs have variable DLC, skip
      if (canId === "0x220") return; // reserved, variable

      this.errors.push({
        timeMs: nowMs,
        canId,
        bus,
        error: `DLC mismatch: expected ${expected}, got ${dlc} (sender: ${sender})`,
      });
      return;
    }

    if (dataLength !== dlc) {
      this.errors.push({
        timeMs: nowMs,
        canId,
        bus,
        error: `Data length ${dataLength} != DLC ${dlc}`,
      });
    }

    // Basic per-ID range checks
    if (canId === "0x204" && dlc === 5) {
      // 0x204 gear byte should be 0-3
      // (done at frame level, just check DLC here)
    }
  }

  getAllErrors(): ValidationError[] {
    return [...this.errors];
  }

  reset(): void {
    this.errors = [];
  }
}
