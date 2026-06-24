/**
 * HostEcu — simulated Host (Jetson Orin) (ROS 2 bridge).
 *
 * Sends drive commands, brake requests, light commands, obstacle distance,
 * and heartbeat. Follows a configurable drive cycle (sequence of steps).
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId, DriveCycleStep } from "../core/types.js";

export class HostEcu implements SimulatedEcu {
  readonly id = "Host (Jetson Orin)";
  readonly nodeId: SimNodeId = "host";

  private driveCycle: DriveCycleStep[] = [];
  private stepIndex = 0;
  private stepStartMs = 0;
  private heartbeatCtr = 0;
  private obstacleDistanceMm = 0xFFFFFFFF; // max = clear

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
    _highBusRx: SimFrame[],
    _lowBusRx: SimFrame[],
    ctx: SimulationContext,
  ): SimFrame[] {
    const out: SimFrame[] = [];

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

    // ── 0x7FC HOST_HEARTBEAT (2 Hz) ───────────────────────────
    if (nowMs % 500 === 0) {
      this.heartbeatCtr = (this.heartbeatCtr + 1) & 0xFF;
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x7FC", name: "HOST_HEARTBEAT",
        dlc: 1, data: [this.heartbeatCtr], sender: "host",
      });
    }

    return out;
  }
}
