/**
 * SYS Brake Control — port of sys-esp32/src/brake_control.h.
 *
 * 4-state brake state machine:
 *   BOOT_WAIT (500ms) → LISTEN_SYNC (wait for 0x721) → ACTIVE
 *   LISTEN_SYNC → DEGRADED (2s timeout)
 *   DEGRADED → ACTIVE (recover when 0x721 arrives)
 *
 * Produces 0x7B9 VCU_SEB_REQ with rolling counter + checksum.
 */

export enum BrakeState {
  BOOT_WAIT,
  LISTEN_SYNC,
  ACTIVE,
  DEGRADED,
}

// Conversion constants
const BRAKE_STROKE_SCALE = 0.05;
const BRAKE_STROKE_OFFSET = -30.0;
const SEB_MAX_PRESSURE_RAW = 100;

/** Convert stroke mm to steer-by-wire raw value. */
function strokeToRaw(mm: number): number {
  return Math.round((mm - BRAKE_STROKE_OFFSET) / BRAKE_STROKE_SCALE);
}

/** Convert kPa to SEB pressure raw. */
function kpaToPressureRaw(kpa: number): number {
  const raw = Math.round(kpa / 50);
  return Math.min(raw, SEB_MAX_PRESSURE_RAW);
}

export interface VcuSebReq {
  alignEnable: number;
  controlEnable: number;
  controlMode: number;     // 0=Stroke, 1=Pressure
  autoBrake: number;       // 0=manual, 1=auto brake active (bit 3 of byte 0)
  strokeReq: number;       // raw units
  pressureReq: number;     // raw units
  rollCntEnable: number;
  checksumEnable: number;
  rollingCounter: number;
}

export class SysBrakeController {
  state: BrakeState = BrakeState.BOOT_WAIT;
  private bootTimer = 0;
  private rollCounter = 0;
  private sebAligned = false; // bit 0 of 0x721[0]

  // Gap #13: brake following-error tracking
  private actualStrokeRaw = 600;          // from 0x721 bytes 2-3 (0mm)
  private cmdStrokeRaw = 600;             // last commanded stroke
  private followingErrStartMs = -1;
  private brakeFollowingError = false;

  /** Feed SEB status with stroke feedback for following-error monitor (gap #13). */
  feedSebStatus(statusByte0: number, strokeRaw?: number): void {
    this.sebAligned = (statusByte0 & 1) !== 0;
    if (strokeRaw !== undefined) {
      this.actualStrokeRaw = strokeRaw;
    }
  }

  /**
   * Process one brake tick (50 Hz).
   * @returns VcuSebReq if a 0x7B9 frame should be sent, null otherwise.
   */
  tick(lever: boolean, estop: boolean, brakeKpa: number): VcuSebReq | null {
    switch (this.state) {
      case BrakeState.BOOT_WAIT: {
        const bootTicks = 25; // 500ms / 20ms
        if (++this.bootTimer >= bootTicks) {
          this.state = BrakeState.LISTEN_SYNC;
          this.bootTimer = 0;
        }
        return null;
      }

      case BrakeState.LISTEN_SYNC: {
        if (this.sebAligned) {
          this.state = BrakeState.ACTIVE;
          return this.buildCommand(lever, estop, brakeKpa);
        }
        // 2-second sync timeout → DEGRADED
        if (++this.bootTimer >= 100) { // 2000ms / 20ms = 100
          this.state = BrakeState.DEGRADED;
          return this.buildCommand(lever, estop, 0);
        }
        return null;
      }

      case BrakeState.ACTIVE:
        return this.buildCommand(lever, estop, brakeKpa);

      case BrakeState.DEGRADED: {
        if (this.sebAligned) {
          this.state = BrakeState.ACTIVE;
        }
        return this.buildCommand(lever, estop, 0); // DEGRADED: lever-only
      }
    }
    return null;
  }

  private buildCommand(lever: boolean, estop: boolean, brakeKpa: number): VcuSebReq {
    let controlMode = 0;
    let strokeReq = strokeToRaw(0);   // 600 = 0mm
    let pressureReq = 0;

    if (estop) {
      // ESTOP: Stroke Mode, max stroke 27mm → raw 1140
      controlMode = 0;
      strokeReq = strokeToRaw(27);
      pressureReq = 0;
    } else if (brakeKpa > 0) {
      // Pressure Mode from 0x205
      controlMode = 1;
      strokeReq = strokeToRaw(0);
      pressureReq = kpaToPressureRaw(brakeKpa);
    } else if (lever) {
      // Manual lever: Stroke Mode, 15mm → raw 900
      controlMode = 0;
      strokeReq = strokeToRaw(15);
      pressureReq = 0;
    } else {
      // Released: Stroke Mode, 0mm → raw 600
      controlMode = 0;
      strokeReq = strokeToRaw(0);
      pressureReq = 0;
    }

    const rc = this.rollCounter;
    this.rollCounter = (this.rollCounter + 1) & 0x0F;

    const autoBrake = (brakeKpa > 0 || estop) ? 1 : 0;

    // Store commanded stroke for following-error monitor (gap #13)
    this.cmdStrokeRaw = strokeReq;

    return {
      alignEnable: 1,
      controlEnable: 1,
      controlMode,
      autoBrake,
      strokeReq,
      pressureReq,
      rollCntEnable: 1,
      checksumEnable: 1,
      rollingCounter: rc,
    };
  }

  /** Check brake following-error: cmd vs actual stroke >3mm for >100ms (gap #13). */
  checkFollowingError(nowMs: number): void {
    const diff = Math.abs(this.cmdStrokeRaw - this.actualStrokeRaw);
    const RAW_3MM = 60;  // 3mm in raw units (3 / 0.05)
    if (diff > RAW_3MM) {
      if (this.followingErrStartMs < 0) {
        this.followingErrStartMs = nowMs;
      } else if (nowMs - this.followingErrStartMs >= 100) {
        this.brakeFollowingError = true;
      }
    } else {
      this.followingErrStartMs = -1;
      this.brakeFollowingError = false;
    }
  }

  getDiagnostics(): { brakeFollowingError: boolean } {
    return { brakeFollowingError: this.brakeFollowingError };
  }

  reset(): void {
    this.state = BrakeState.BOOT_WAIT;
    this.bootTimer = 0;
    this.rollCounter = 0;
    this.sebAligned = false;
  }
}
