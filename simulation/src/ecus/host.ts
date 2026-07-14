/**
 * HostEcu — simulated Host ECU (ROS 2 bridge).
 *
 * Sends drive commands, brake requests, light commands, obstacle distance,
 * and heartbeat. Follows a configurable drive cycle (sequence of steps).
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId, DriveCycleStep } from "../core/types.js";

export class HostEcu implements SimulatedEcu {
  readonly id = "Host";
  readonly nodeId: SimNodeId = "host";

  private driveCycle: DriveCycleStep[] = [];
  private stepIndex = 0;
  private stepStartMs = 0;
  private heartbeatCtr = 0;
  private obstacleDistanceMm = 0xFFFFFFFF; // max = clear

  // I11: Incoming telemetry store (latest values from 0x210/0x310/0x311)
  private lastRtStateRpt: { mode: number; safetyState: number } | null = null;
  private lastSteerDiag: { angle: number; fault: number } | null = null;
  private lastBrakeDiag: { pressureRaw: number; fault: number } | null = null;

  /** Latest RT_STATE_RPT (0x210) data. */
  getRtStateRpt(): { mode: number; safetyState: number } | null { return this.lastRtStateRpt; }
  /** Latest STEER_DIAG (0x310) data. */
  getSteerDiag(): { angle: number; fault: number } | null { return this.lastSteerDiag; }
  /** Latest BRAKE_DIAG (0x311) data. */
  getBrakeDiag(): { pressureRaw: number; fault: number } | null { return this.lastBrakeDiag; }

  /** Set the drive cycle. Each step has {speedMmps, yawRateMradS, gear, durationMs}. */
  setDriveCycle(cycle: DriveCycleStep[]): void {
    this.driveCycle = cycle;
    this.stepIndex = 0;
    this.stepStartMs = 0;
  }

  /** Set obstacle distance in mm (0x400). */
  setObstacle(mm: number): void {
    this.obstacleDistanceMm = mm;
  }

  init(): void {
    // nothing
  }

  shutdown(): void {
    // nothing
  }

  tick(
    nowMs: number,
    highBusRx: SimFrame[],
    _lowBusRx: SimFrame[],
    ctx: SimulationContext,
  ): SimFrame[] {
    const out: SimFrame[] = [];

    // ── Process incoming high-bus telemetry (I11) ───────────────
    for (const f of highBusRx) {
      switch (f.canId) {
        case "0x210": {
          // RT_STATE_RPT — store latest mode + safety_state (mask byte 1 bits 0-1)
          this.lastRtStateRpt = { mode: f.data[0], safetyState: f.data[1] & 0x03 };
          break;
        }
        case "0x310": {
          // STEER_DIAG — angle i16 BE bytes 0-1, fault byte 2
          const angle = ((f.data[0] << 8) | f.data[1]) << 16 >> 16; // i16 sign-extend
          this.lastSteerDiag = { angle, fault: f.data[2] ?? 0 };
          break;
        }
        case "0x311": {
          // BRAKE_DIAG — pressure u16 BE bytes 0-1, fault byte 2
          const pressureRaw = ((f.data[0] << 8) | f.data[1]) & 0xFFFF;
          this.lastBrakeDiag = { pressureRaw, fault: f.data[2] ?? 0 };
          break;
        }
      }
    }

    // ── Advance drive cycle ─────────────────────────────────────
    let speed = 0;
    let yaw = 0;
    let gear = 0;

    if (this.driveCycle.length > 0 && this.stepIndex < this.driveCycle.length) {
      const step = this.driveCycle[this.stepIndex];
      const elapsed = nowMs - this.stepStartMs;
      if (elapsed >= step.durationMs) {
        this.stepStartMs = nowMs;
        this.stepIndex++;
      }
      if (this.stepIndex < this.driveCycle.length) {
        const active = this.driveCycle[this.stepIndex];
        speed = active.speedMmps;
        yaw = active.yawRateMradS;
        gear = active.gear;
      }
    }

    // ── 0x300 HOST_DRIVE_CMD (100 Hz) ───────────────────────────
    if (nowMs % 10 === 0 && ctx.mode === "auto") {
      const s32 = speed | 0;
      const y24 = yaw & 0xFFFFFF;
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x300", name: "HOST_DRIVE_CMD",
        dlc: 8, data: [
          (s32 >> 24) & 0xFF, (s32 >> 16) & 0xFF, (s32 >> 8) & 0xFF, s32 & 0xFF,
          (y24 >> 16) & 0xFF, (y24 >> 8) & 0xFF, y24 & 0xFF,
          gear & 0xFF,
        ], sender: "host",
      });
    }

    // ── 0x301 HOST_BRAKE_REQ (on demand, sent at 50Hz here) ────
    if (nowMs % 20 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x301", name: "HOST_BRAKE_REQ",
        dlc: 4, data: [0, 0, 0, 0], sender: "host",
      });
    }

    // ── 0x400 HOST_OBSTACLE_DIST (10 Hz) ────────────────────────
    if (nowMs % 100 === 0) {
      const mm = this.obstacleDistanceMm >>> 0;
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x400", name: "HOST_OBSTACLE_DIST",
        dlc: 4, data: [
          (mm >> 24) & 0xFF, (mm >> 16) & 0xFF, (mm >> 8) & 0xFF, mm & 0xFF,
        ], sender: "host",
      });
    }

    // ── 0x302 HOST_LIGHT_CMD (on change in firmware; periodic in sim) ─
    if (nowMs % 100 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x302", name: "HOST_LIGHT_CMD",
        dlc: 1, data: [0], sender: "host",
      });
    }

    // ── 0x7FC HOST_HEARTBEAT (2 Hz) ───────────────────────────
    if (nowMs % 500 === 0) {
      this.heartbeatCtr = (this.heartbeatCtr + 1) & 0xFF;
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x7FC", name: "HOST_HEARTBEAT",
        dlc: 2, data: [this.heartbeatCtr, 0], sender: "host",
      });
    }

    return out;
  }
}
