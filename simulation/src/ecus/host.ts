/**
 * HostEcu — simulated Host ECU (ROS 2 bridge).
 *
 * Sends drive commands, brake requests, light commands, obstacle distance,
 * and heartbeat. Follows a configurable drive cycle (sequence of steps).
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId, DriveCycleStep } from "../core/types.js";
import { decodeAs, encodeSimFrame } from "../protocol.js";

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
      const state = decodeAs(f, "rt:rt_state_rpt");
      if (state !== undefined) {
        this.lastRtStateRpt = {
          mode: Number(state.mode),
          safetyState: Number(state.safety_state),
        };
        continue;
      }
      const steer = decodeAs(f, "rt:steer_diag");
      if (steer !== undefined) {
        this.lastSteerDiag = { angle: Number(steer.angle_0_1deg), fault: Number(steer.fault) };
        continue;
      }
      const brake = decodeAs(f, "rt:brake_diag");
      if (brake !== undefined) {
        this.lastBrakeDiag = {
          pressureRaw: Math.round(Number(brake.pressure_raw) / 0.05),
          fault: Number(brake.fault),
        };
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
      out.push(encodeSimFrame("host:host_drive_cmd", {
        speed_mmps: speed,
        yaw_rate_mrad_s: yaw,
        gear,
      }, "high", "host", nowMs));
    }

    // ── 0x301 HOST_BRAKE_REQ (on demand, sent at 50Hz here) ────
    if (nowMs % 20 === 0) {
      out.push(encodeSimFrame("host:host_brake_req", { brake_pressure_kpa: 0 }, "high", "host", nowMs));
    }

    // ── 0x400 HOST_OBSTACLE_DIST (10 Hz) ────────────────────────
    if (nowMs % 100 === 0) {
      out.push(encodeSimFrame("host:host_obstacle_dist", {
        distance_mm: this.obstacleDistanceMm >>> 0,
      }, "high", "host", nowMs));
    }

    // ── 0x302 HOST_LIGHT_CMD (on change in firmware; periodic in sim) ─
    if (nowMs % 100 === 0) {
      out.push(encodeSimFrame("host:host_light_cmd", {
        left_turn: 0,
        right_turn: 0,
        brake_light: 0,
        headlight: 0,
      }, "high", "host", nowMs));
    }

    // ── 0x7FC HOST_HEARTBEAT (2 Hz) ───────────────────────────
    if (nowMs % 500 === 0) {
      this.heartbeatCtr = (this.heartbeatCtr + 1) & 0xFF;
      out.push(encodeSimFrame("host:host_heartbeat", {
        alive_ctr: this.heartbeatCtr,
        health_flags: 0,
      }, "high", "host", nowMs));
    }

    return out;
  }
}
