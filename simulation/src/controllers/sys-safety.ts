/**
 * SYS Safety Monitor — port of sys-esp32/src/safety_monitor.cpp.
 *
 * Monitors:
 *   - ESTOP GPIO (simulated as state bit)
 *   - Brake lever (simulated as state bit)
 *   - RT heartbeat (0x7FD) with frozen counter detection
 *   - EGAS L2: 0x204 setpoint vs 0x206 actual speed mismatch
 */

import {
  STARTUP_GRACE_PERIOD_MS,
} from "../physics/tricycle.js";

/** SYS heartbeat timeout for RT (0x7FD at 2Hz, 2 missed = 1000ms). */
const HEARTBEAT_TIMEOUT_MS_RT = 1000;

/** EGAS L2 speed mismatch threshold (mm/s). */
const EGAS_SPEED_THRESHOLD_MMPS = 500;

/** EGAS L2 fault must persist this long to trigger ESTOP (ms). */
const EGAS_FAULT_DURATION_MS = 500;

export class SysSafetyMonitor {
  private estopActive = false;
  private brakeLeverPressed = false;

  // RT heartbeat tracking
  private lastHbMs = 0;
  private lastHbCtr = 0;
  private hbEverSeen = false;

  // EGAS L2 state
  private egasFaultStartMs = -1;
  private egasFaultActive = false;

  // ── GPIO simulation ──────────────────────────────────────────────

  setEstop(active: boolean): void { this.estopActive = active; }
  setBrakeLever(pressed: boolean): void { this.brakeLeverPressed = pressed; }

  get estop(): boolean { return this.estopActive; }
  get brakeLever(): boolean { return this.brakeLeverPressed; }

  // ── Heartbeat ────────────────────────────────────────────────────

  /** Feed RT heartbeat alive counter from 0x7FD. */
  feedHeartbeatRt(nowMs: number, aliveCtr: number): void {
    // Frozen counter detection: same value as last = stuck CAN controller
    if (this.hbEverSeen && aliveCtr === this.lastHbCtr) {
      return; // don't update timestamp — will time out
    }
    this.lastHbCtr = aliveCtr;
    this.lastHbMs = nowMs;
    this.hbEverSeen = true;
  }

  /** Returns true if RT heartbeat is fresh. */
  heartbeatOk(nowMs: number): boolean {
    // Startup grace: if never seen, OK for first 3 seconds
    if (!this.hbEverSeen) {
      return nowMs < STARTUP_GRACE_PERIOD_MS;
    }
    return (nowMs - this.lastHbMs) < HEARTBEAT_TIMEOUT_MS_RT;
  }

  // ── EGAS L2 ──────────────────────────────────────────────────────

  /**
   * Monitor EGAS Level 2: compare 0x204 commanded speed vs 0x206 actual.
   * Returns true if a fault is detected and persisted long enough.
   */
  checkEgasL2(nowMs: number, cmdSpeedMmps: number, actualSpeedMmps: number): boolean {
    const mismatch = Math.abs(cmdSpeedMmps - actualSpeedMmps) > EGAS_SPEED_THRESHOLD_MMPS;

    if (mismatch) {
      if (this.egasFaultStartMs < 0) {
        this.egasFaultStartMs = nowMs;
      }
      if (nowMs - this.egasFaultStartMs >= EGAS_FAULT_DURATION_MS) {
        this.egasFaultActive = true;
        return true;
      }
    } else {
      this.egasFaultStartMs = -1;
      this.egasFaultActive = false;
    }
    return false;
  }

  get egasActive(): boolean { return this.egasFaultActive; }

  /** Placeholder — MTR feedback processing not yet implemented (gap #15). */
  feedMtrFeedback(
    _fbk: { actualSpeed: number; gearState: number; faultFlags: number },
    _nowMs: number,
  ): void {
    // stub — MTR ESTOP_ACTIVE bit detection not implemented
  }

  /** Placeholder — MTR ESTOP ACK check not yet implemented (gap #15). */
  mtrEstopAcked(): boolean {
    return false; // stub — not implemented
  }

  reset(): void {
    this.estopActive = false;
    this.brakeLeverPressed = false;
    this.lastHbMs = 0;
    this.lastHbCtr = 0;
    this.hbEverSeen = false;
    this.egasFaultStartMs = -1;
    this.egasFaultActive = false;
  }
}
