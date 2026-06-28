/**
 * RT Steering Control — port of rt-esp32/src/steering_control.h.
 *
 * 6-state steering state machine:
 *   BOOT_WAIT (500ms) → LISTEN_SYNC (wait for 0x201) → ACTIVE (transmit 0x169)
 *   ACTIVE → ESTOP_RAMP_TO_ZERO (non-obstacle: ramp 20°/s)
 *   ACTIVE → ESTOP_HOLD_THEN_SILENT (obstacle: hold 500ms, then silent)
 *   LISTEN_SYNC → FAULT (5s timeout)
 *   ESTOP_HOLD_THEN_SILENT → FAULT (after hold period)
 */

import {
  STEER_BOOT_WAIT_MS,
  STEER_CMD_RATE_HZ,
  STEER_SYNC_TIMEOUT_MS,
  STEER_ESTOP_RAMP_DEG_S,
  STEER_ESTOP_HOLD_MS,
  computeDynamicLimit,
} from "../physics/tricycle.js";
import { RtKinematicsController } from "./rt-kinematics.js";
import type { DriveCommand } from "../core/types.js";

// ── State enum ──────────────────────────────────────────────────────

export enum SteerState {
  BOOT_WAIT,
  LISTEN_SYNC,
  ACTIVE,
  ESTOP_RAMP_TO_ZERO,
  ESTOP_HOLD_THEN_SILENT,
  FAULT,
}

// ── Output type (mirrors can::VcuSesReq) ────────────────────────────

export interface VcuSesReq {
  alignEnable: number;
  controlEnable: number;
  targetAngle: number;      // 0.1° units, +right
  targetSpeed: number;      // deg/s
  rollCntEnable: number;
  checksumEnable: number;
  rollingCounter: number;   // 0–15
  vehicleSpeed: number;     // km/h, u8
}

// ── Steering Controller ─────────────────────────────────────────────

export class RtSteeringController {
  state: SteerState = SteerState.BOOT_WAIT;
  private timer = 0;             // ticks at 50 Hz for BOOT_WAIT
  private activeAngle = 0;       // 0.1° units
  private rollCounter = 0;       // 0–15
  private speedMmps = 0;
  private syncStartMs = 0;
  private estopHoldStartMs = 0;
  private estopHoldAngle = 0;
  private tickCount = 0;         // counts total tick() calls (at drive rate)

  // ── Tick — call this at the steering rate (typically 50 Hz) ───────

  /**
   * Process one steering tick.
   * @param sesAngleRaw  EPS-C angle in 0.1° units, or `null` if no data
   * @param sesAngleStatus 0x201 byte0 bit0: 0=center finding, 1=aligned
   * @param nowMs  Simulation time in ms
   * @returns VcuSesReq if should transmit, null otherwise
   */
  tick(sesAngleRaw: number | null, sesAngleStatus: number, nowMs: number): VcuSesReq | null {
    this.tickCount++;

    switch (this.state) {
      case SteerState.BOOT_WAIT: {
        const bootWaitTicks = Math.round(STEER_BOOT_WAIT_MS / (1000 / STEER_CMD_RATE_HZ));
        if (++this.timer >= bootWaitTicks) {
          this.state = SteerState.LISTEN_SYNC;
          this.timer = 0;
          this.syncStartMs = nowMs;
        }
        return null;
      }

      case SteerState.LISTEN_SYNC: {
        // Timeout: 5s without valid 0x201 → FAULT
        if (nowMs - this.syncStartMs > STEER_SYNC_TIMEOUT_MS) {
          this.state = SteerState.FAULT;
          return null;
        }
        // Wait for valid angle data
        if (sesAngleRaw === null) return null;
        // Alignment check: EPS-C must report angle_status == 1
        if (sesAngleStatus === 0) return null;
        // Synchronized — convert raw steer-by-wire (0.1°+30000 offset) to millidegrees
        this.activeAngle = (sesAngleRaw - 30000) * 100;
        this.state = SteerState.ACTIVE;
        return this.buildCommand();
      }

      case SteerState.ACTIVE:
        return this.buildCommand();

      case SteerState.ESTOP_RAMP_TO_ZERO: {
        // Ramp toward 0° at kSteerEstopRampDegS (20°/s)
        // activeAngle is in millidegrees. Ramp step = 20 deg/s * 1000 / 50 Hz
        const rampStep = Math.round(STEER_ESTOP_RAMP_DEG_S * 1000 / STEER_CMD_RATE_HZ);
        if (this.activeAngle > rampStep) {
          this.activeAngle -= rampStep;
        } else if (this.activeAngle < -rampStep) {
          this.activeAngle += rampStep;
        } else {
          this.activeAngle = 0;
          // Gap #6: if exit was requested, transition back to ACTIVE
          if (this.estopExitPending) {
            this.estopExitPending = false;
            this.state = SteerState.ACTIVE;
          }
        }
        return this.buildCommand();
      }

      case SteerState.ESTOP_HOLD_THEN_SILENT: {
        if (nowMs - this.estopHoldStartMs < STEER_ESTOP_HOLD_MS) {
          this.activeAngle = this.estopHoldAngle;
          return this.buildCommand();
        }
        // Hold period expired → silent-stop
        this.state = SteerState.FAULT;
        return null;
      }

      case SteerState.FAULT:
        return null;
    }
    return null;
  }

  // ── Commands ──────────────────────────────────────────────────────

  /** Set desired steering angle (called by RT control task at 100 Hz).
   *  @param angleMdeg desired angle in millidegrees (matching C++ set_target). */
  setTarget(angleMdeg: number, speedMmps: number): void {
    if (this.state === SteerState.ACTIVE) {
      this.activeAngle = Math.round(angleMdeg);
      this.speedMmps = speedMmps;
    }
  }

  /** Trigger ESTOP behavior. */
  startEstop(obstacleTriggered: boolean, nowMs: number): void {
    if (this.state === SteerState.ACTIVE) {
      if (obstacleTriggered) {
        this.state = SteerState.ESTOP_HOLD_THEN_SILENT;
        // Clamp hold angle to dynamic limit for current speed (Gap #9)
        const maxAngleDeg = computeDynamicLimit(this.speedMmps);
        const maxAngleMdeg = Math.round(maxAngleDeg * 1000);
        this.estopHoldAngle = Math.max(-maxAngleMdeg, Math.min(maxAngleMdeg, this.activeAngle));
        this.estopHoldStartMs = nowMs;
      } else {
        this.state = SteerState.ESTOP_RAMP_TO_ZERO;
      }
    }
  }

  /** Reset from FAULT to LISTEN_SYNC. */
  resetToListen(nowMs: number): void {
    if (this.state === SteerState.FAULT) {
      this.state = SteerState.LISTEN_SYNC;
      this.syncStartMs = nowMs;
    }
  }

  /** Return current state (for test access). */
  getState(): SteerState {
    return this.state;
  }

  /** Return the instantaneous dynamic limit for steering angle (millidegrees). */
  getHoldAngle(): number {
    return this.estopHoldAngle;
  }

  // Gap #6: pending exit flag — deferred until ramp/hold completes
  private estopExitPending = false;

  /** Exit ESTOP states — deferred until ramp/hold completes (Gap #6). */
  exitEstop(): void {
    if (this.state === SteerState.ESTOP_RAMP_TO_ZERO
      || this.state === SteerState.ESTOP_HOLD_THEN_SILENT) {
      this.estopExitPending = true;
    }
  }

  // ── Private helpers ───────────────────────────────────────────────

  private buildCommand(): VcuSesReq {
    const speedKmh = Math.abs(this.speedMmps) * 3.6 / 1000;
    const rateDegS = RtKinematicsController.computeSteerRate(this.speedMmps);
    const rollingCounter = this.rollCounter;
    this.rollCounter = (this.rollCounter + 1) & 0x0F;

    // Convert millidegrees to steer-by-wire raw format:
    //   raw = (angle_mdeg / 100) + 30000   (0.1° units with -3000 offset)
    const rawAngle = Math.round(this.activeAngle / 100 + 30000);

    return {
      alignEnable: 1,
      controlEnable: 1,
      targetAngle: rawAngle,
      targetSpeed: Math.round(rateDegS),
      rollCntEnable: 1,
      checksumEnable: 1,
      rollingCounter,
      vehicleSpeed: Math.max(0, Math.min(Math.round(speedKmh), 255)),
    };
  }

  /** Reset to power-on state. */
  reset(): void {
    this.state = SteerState.BOOT_WAIT;
    this.timer = 0;
    this.activeAngle = 0;
    this.rollCounter = 0;
    this.speedMmps = 0;
    this.estopHoldStartMs = 0;
    this.estopHoldAngle = 0;
    this.tickCount = 0;
  }
}
