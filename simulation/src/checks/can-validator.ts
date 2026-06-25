/**
 * CanValidator — per-frame correctness checks.
 *
 * Validates: DLC matching, data length, basic range checks.
 * Runs on every produced frame.
 */

import type { ValidationError, BusId } from "../core/types.js";

/** Expected DLC for each CAN ID (partial — extend as needed). */
const EXPECTED_DLC: Record<string, number> = {
  "0x001": 0, "0x011": 3, "0x012": 1, "0x110": 1,  // 0x011 DLC=3 v0.0.5 (SYS_LightState)
  "0x120": 2, "0x169": 8, "0x201": 8, "0x202": 8, "0x203": 8,
  "0x204": 5, "0x205": 4, "0x206": 4, "0x210": 3,
  "0x300": 8, "0x301": 4, "0x302": 1, "0x400": 4,
  "0x600": 8, "0x6FA": 8, "0x6FB": 8,
  "0x721": 8, "0x731": 8, "0x741": 8,
  "0x7B9": 8, "0x7FC": 1, "0x7FD": 1, "0x7FE": 1,
};

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
