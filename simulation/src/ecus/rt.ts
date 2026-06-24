/**
 * RtEcu — simulated RT ESP32-S3 (dual-bus gateway, kinematics, steering).
 *
 * Receives Jetson commands on high bus, produces actuator commands on low bus.
 * Bridges selected messages between buses.
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId, DriveCommand } from "../core/types.js";
import { RtKinematicsController } from "../controllers/rt-kinematics.js";
import {
  RtSteeringController,
  SteerState,
} from "../controllers/rt-steering.js";
import {
  JETSON_HEARTBEAT_TIMEOUT_MS,
  STEER_CMD_RATE_HZ,
  ASSIST_STOP_KPA,
  OBSTACLE_MAX_KPA,
  MAX_BRAKE_KPA,
  CMD_STALE_TIMEOUT_MS,
} from "../physics/tricycle.js";

export class RtEcu implements SimulatedEcu {
  readonly id = "RT ESP32-S3";
  readonly nodeId: SimNodeId = "rt";

  private kinematics = new RtKinematicsController();
  private steering = new RtSteeringController();

  private lastJetsonCmdMs = -Infinity;
  private lastSysHbMs = -Infinity;
  private lastSysHbCtr = 0;
  private sysHbEverSeen = false;
  private lastJetsonHbMs = -Infinity;
  private currentMode: "manual" | "auto" | "estop" = "manual";
  private obstacleDistanceMm = 3000; // default: clear
  private jetsonDriveCmd: DriveCommand = { speedMmps: 0, yawRateMradS: 0, gear: 0 };
  private jetsonBrakeKpa = 0;
  private rtHbCtrLow = 0;
  private rtHbCtrHigh = 0;
  private lastSpeedMmps = 0;
  private sesAngleRaw: number | null = null;  // 0.1° units
  private sesAngleStatus = 0;

  init(): void {
    this.kinematics.reset();
    this.steering.reset();
  }

  shutdown(): void {
    // nothing to clean up
  }

  tick(
    nowMs: number,
    highBusRx: SimFrame[],
    lowBusRx: SimFrame[],
    ctx: SimulationContext,
  ): SimFrame[] {
    this.currentMode = ctx.mode;
    const out: SimFrame[] = [];

    // ── Process high-bus frames ──────────────────────────────
    for (const f of highBusRx) {
      switch (f.canId) {
        case "0x300": {
          // HOST_DRIVE_CMD — Jetson → RT
          this.lastJetsonCmdMs = nowMs;
          if (ctx.mode === "auto" && !ctx.estopActive) {
            this.jetsonDriveCmd = {
              speedMmps: (f.data[0] << 24 | f.data[1] << 16 | f.data[2] << 8 | f.data[3]) >> 0,
              yawRateMradS: ((f.data[4] << 16 | f.data[5] << 8 | f.data[6]) << 8) >> 8, // i24 BE sign-extend
              gear: f.data[7] ?? 0,
            };
          }
          break;
        }
        case "0x301": {
          // HOST_BRAKE_REQ
          this.jetsonBrakeKpa = (f.data[0] << 24 | f.data[1] << 16 | f.data[2] << 8 | f.data[3]) >> 0;
          break;
        }
        case "0x7FC": {
          // JETSON_HEARTBEAT
          this.lastJetsonHbMs = nowMs;
          break;
        }
        case "0x001": {
          // ESTOP — forward to low bus
          out.push({ ...f, bus: "low", sender: "rt" });
          break;
        }
      }
    }

    // ── Process low-bus frames ──────────────────────────────
    for (const f of lowBusRx) {
      switch (f.canId) {
        case "0x7FE": {
          // SYS_HEARTBEAT — monitor for 200ms timeout
          this.lastSysHbMs = nowMs;
          this.sysHbEverSeen = true;
          break;
        }
        case "0x201": {
          // SES_STATUS — EPS-C steering feedback
          // Extract angle (bytes 2-3, u16 LE, 0.1°/bit, offset -3000)
          const angleRaw = (f.data[3] << 8 | f.data[2]) & 0xFFFF; // u16 LE
          this.sesAngleRaw = angleRaw;
          this.sesAngleStatus = f.data[0] & 1;
          break;
        }
        case "0x001": {
          // ESTOP — forward to high bus
          out.push({ ...f, bus: "high", sender: "rt" });
          break;
        }
        // Category 1 forward: low→high
        case "0x011":
        case "0x120":
        case "0x206":
        case "0x600":
          out.push({ ...f, bus: "high", sender: "rt" });
          break;
      }
    }

    // ── Check safety conditions ─────────────────────────────

    // Command staleness
    const cmdStale = nowMs - this.lastJetsonCmdMs > CMD_STALE_TIMEOUT_MS;
    // SYS heartbeat timeout (200ms)
    const sysHbTimeout = this.sysHbEverSeen && nowMs - this.lastSysHbMs > 200;
    // Jetson heartbeat timeout (1500ms)
    const jetsonHbTimeout = this.lastJetsonHbMs >= 0 && nowMs - this.lastJetsonHbMs > JETSON_HEARTBEAT_TIMEOUT_MS;

    const shouldEstop = ctx.estopActive || cmdStale || sysHbTimeout;

    // ── Kinematics (100 Hz) ─────────────────────────────────
    if (nowMs % 10 === 0) {
      const cmd = shouldEstop || ctx.mode !== "auto"
        ? { speedMmps: 0, yawRateMradS: 0, gear: 0 }
        : this.jetsonDriveCmd;

      const resolved = this.kinematics.resolve(cmd);

      // 0x204 RT_DRIVE_CMD on low bus (100 Hz)
      const speed = resolved.motorSpeedMmps;
      out.push({
        simTimeMs: nowMs,
        bus: "low",
        canId: "0x204",
        name: "RT_DRIVE_CMD",
        dlc: 5,
        data: [
          (speed >> 24) & 0xFF,
          (speed >> 16) & 0xFF,
          (speed >> 8) & 0xFF,
          speed & 0xFF,
          resolved.gear & 0xFF,
        ],
        sender: "rt",
      });

      this.lastSpeedMmps = speed;
    }

    // ── Brake command (50 Hz) ───────────────────────────────
    if (nowMs % 20 === 0) {
      const obstacleKpa = this.computeObstacleKpa();
      let brakeKpa = Math.max(obstacleKpa, this.jetsonBrakeKpa);

      if (shouldEstop) brakeKpa = MAX_BRAKE_KPA;
      else if (jetsonHbTimeout) brakeKpa = ASSIST_STOP_KPA;

      // 0x205 RT_BRAKE_CMD on low bus (50 Hz)
      out.push({
        simTimeMs: nowMs,
        bus: "low",
        canId: "0x205",
        name: "RT_BRAKE_CMD",
        dlc: 4,
        data: [
          (brakeKpa >> 24) & 0xFF,
          (brakeKpa >> 16) & 0xFF,
          (brakeKpa >> 8) & 0xFF,
          brakeKpa & 0xFF,
        ],
        sender: "rt",
      });
    }

    // ── Steering (50 Hz) ────────────────────────────────────
    if (nowMs % (1000 / STEER_CMD_RATE_HZ) === 0) {
      if (shouldEstop && this.steering.state === SteerState.ACTIVE) {
        this.steering.startEstop(false, nowMs);
      } else if (!shouldEstop && (
        this.steering.state === SteerState.ESTOP_RAMP_TO_ZERO ||
        this.steering.state === SteerState.ESTOP_HOLD_THEN_SILENT
      )) {
        this.steering.exitEstop();
      }

      // Feed EPS-C data to steering controller
      const cmd = this.steering.tick(this.sesAngleRaw, this.sesAngleStatus, nowMs);
      if (cmd) {
        // Build 0x169 VCU_SES_REQ (SYNTREE LE encoding)
        const angle16 = cmd.targetAngle & 0xFFFF;
        const speedRaw = cmd.targetSpeed & 0xFFFF;
        const rollCnt = (this.rtHbCtrLow & 0xF);
        const data = [
          (cmd.alignEnable & 1) | ((cmd.controlEnable & 1) << 1),
          0,
          angle16 & 0xFF,
          (angle16 >> 8) & 0xFF,
          speedRaw & 0xFF,
          // Byte 5: speed[11:8] in bits 2-3, security signals overlay bits 0-1 and 4-7.
          // RollCntEnable(bit0)=1, ChecksumEnable(bit1)=1, RollCnt(bits 4-7), speed[11:8] in bits 2-3.
          1                          // bit 0: RollCntEnable = 1 (MUST be 1)
          | (1 << 1)                  // bit 1: ChecksumEnable = 1 (MUST be 1)
          | (((speedRaw >> 8) & 0x3) << 2)  // bits 2-3: speed bits 9-8
          | ((rollCnt & 0xF) << 4),   // bits 4-7: Rolling counter
          cmd.vehicleSpeed & 0xFF,
          0, // checksum placeholder
        ];
        // Compute checksum: XOR(bytes 0-6) ^ 0xFF (per SYNTREE CSV spec)
        let cksum = 0;
        for (let i = 0; i < 7; i++) cksum ^= data[i];
        data[7] = cksum ^ 0xFF;

        out.push({
          simTimeMs: nowMs,
          bus: "low",
          canId: "0x169",
          name: "VCU_SES_REQ",
          dlc: 8,
          data,
          sender: "rt",
        });
      }
    }

    // ── RT_STATE_RPT 0x210 on high bus (10 Hz) ──────────────
    if (nowMs % 100 === 0) {
      const modeByte = ctx.mode === "auto" ? 1 : ctx.mode === "estop" ? 2 : 0;
      out.push({
        simTimeMs: nowMs,
        bus: "high",
        canId: "0x210",
        name: "RT_STATE_RPT",
        dlc: 3,
        data: [modeByte, this.kinematics.getDynamicLimit(this.lastSpeedMmps) > 5 ? 1 : 0, 0],
        sender: "rt",
      });
    }

    // ── Heartbeats (2 Hz) ───────────────────────────────────
    if (nowMs % 500 === 0) {
      this.rtHbCtrLow = (this.rtHbCtrLow + 1) & 0xFF;
      this.rtHbCtrHigh = (this.rtHbCtrHigh + 1) & 0xFF;

      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x7FD", name: "RT_HEARTBEAT",
        dlc: 1, data: [this.rtHbCtrLow], sender: "rt",
      });
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x7FD", name: "RT_HEARTBEAT",
        dlc: 1, data: [this.rtHbCtrHigh], sender: "rt",
      });
    }

    return out;
  }

  private computeObstacleKpa(): number {
    // Linear: 300mm→5000kPa, 3000mm→0kPa
    if (this.obstacleDistanceMm <= 300) return OBSTACLE_MAX_KPA;
    if (this.obstacleDistanceMm >= 3000) return 0;
    const t = (this.obstacleDistanceMm - 300) / (3000 - 300);
    return Math.round(OBSTACLE_MAX_KPA * (1 - t));
  }

  /** Set obstacle distance (from 0x400, called by simulation runner). */
  setObstacle(mm: number): void {
    this.obstacleDistanceMm = mm;
  }

  /** For testing: get steering state. */
  getSteeringState(): SteerState {
    return this.steering.state;
  }
}
