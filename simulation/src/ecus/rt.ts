/**
 * RtEcu — simulated RT ESP32-S3 (dual-bus gateway, kinematics, steering).
 *
 * Receives Host commands on high bus, produces actuator commands on low bus.
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
  HOST_HEARTBEAT_TIMEOUT_MS,
  STEER_CMD_RATE_HZ,
  ASSIST_STOP_KPA,
  OBSTACLE_MAX_KPA,
  MAX_BRAKE_KPA,
  CMD_STALE_TIMEOUT_MS,
  computeFollowingErrorThreshold,
} from "../physics/tricycle.js";

export class RtEcu implements SimulatedEcu {
  readonly id = "RT ESP32-S3";
  readonly nodeId: SimNodeId = "rt";

  private kinematics = new RtKinematicsController();
  private steering = new RtSteeringController();
  private sebRollCounter = 0;  // Gap #12: rolling counter for RT→0x7B9

  private lastHostCmdMs = -Infinity;
  private lastSysHbMs = -Infinity;
  private lastSysHbCtr = -1;
  private lastHostHbCtr = -1;
  private sysHbEverSeen = false;
  private lastHostHbMs = -Infinity;
  private currentMode: "manual" | "auto" | "estop" = "manual";
  private obstacleDistanceMm = 3000; // default: clear
  private hostDriveCmd: DriveCommand = { speedMmps: 0, yawRateMradS: 0, gear: 0 };
  private hostBrakeKpa = 0;
  private rtHbCtrLow = 0;
  private rtHbCtrHigh = 0;
  private lastSpeedMmps = 0;
  private sesAngleRaw: number | null = null;  // 0.1° units
  private sesAngleStatus = 0;
  private steerFollowErrTicks = 0;
  private lastCmdAngleRaw: number | null = null;  // 0.1° units, from steering tick

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
          // HOST_DRIVE_CMD — Host → RT
          this.lastHostCmdMs = nowMs;
          if (ctx.mode === "auto" && !ctx.estopActive) {
            this.hostDriveCmd = {
              speedMmps: (f.data[0] << 24 | f.data[1] << 16 | f.data[2] << 8 | f.data[3]) >> 0,
              yawRateMradS: ((f.data[4] << 16 | f.data[5] << 8 | f.data[6]) << 8) >> 8, // i24 BE sign-extend
              gear: f.data[7] ?? 0,
            };
          }
          break;
        }
        case "0x301": {
          // HOST_BRAKE_REQ
          this.hostBrakeKpa = (f.data[0] << 24 | f.data[1] << 16 | f.data[2] << 8 | f.data[3]) >> 0;
          break;
        }
        case "0x7FC": {
          // HOST_HEARTBEAT
          if (f.data[0] !== this.lastHostHbCtr) {
            this.lastHostHbMs = nowMs;
            this.lastHostHbCtr = f.data[0];
          }
          break;
        }
        case "0x001": {
          // ESTOP — forward to low bus
          out.push({ ...f, bus: "low", sender: "rt" });
          break;
        }
        case "0x302": {
          // HOST_LIGHT_CMD — forward high→low to SYS
          out.push({ ...f, bus: "low", sender: "rt" });
          break;
        }
        case "0x400": {
          // HOST_OBSTACLE_DIST — u32 BE mm, consumed by RT for obstacle braking
          this.obstacleDistanceMm = ((f.data[0] << 24) | (f.data[1] << 16) | (f.data[2] << 8) | f.data[3]) >>> 0;
          break;
        }
      }
    }

    // ── Process low-bus frames ──────────────────────────────
    for (const f of lowBusRx) {
      switch (f.canId) {
        case "0x7FE": {
          // SYS_HEARTBEAT — monitor for 200ms timeout
          if (f.data[0] !== this.lastSysHbCtr) {
            this.lastSysHbMs = nowMs;
            this.lastSysHbCtr = f.data[0];
          }
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
    const cmdStale = nowMs - this.lastHostCmdMs > CMD_STALE_TIMEOUT_MS;
    // SYS heartbeat timeout (200ms)
    const sysHbTimeout = this.sysHbEverSeen && nowMs - this.lastSysHbMs > 200;
    // Host heartbeat timeout (1500ms)
    const hostHbTimeout = this.lastHostHbMs >= 0 && nowMs - this.lastHostHbMs > HOST_HEARTBEAT_TIMEOUT_MS;

    let shouldEstop = ctx.estopActive || cmdStale || sysHbTimeout;

    // ── Kinematics (100 Hz) — MANUAL mode: RT does not command actuators
    if (nowMs % 10 === 0) {
      if (ctx.mode !== "manual") {  // gate: RT actuator commands only in AUTO/ESTOP
      const cmd = shouldEstop || ctx.mode !== "auto" || hostHbTimeout
        ? { speedMmps: 0, yawRateMradS: 0, gear: 0 }
        : this.hostDriveCmd;

      const resolved = this.kinematics.resolve(cmd);

      // Feed resolved steering target to steering controller (matching C++ main.cpp line 429)
      this.steering.setTarget(
        Math.round(resolved.steerAngleDeg * 1000),  // degrees → millidegrees
        resolved.motorSpeedMmps,
      );

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

      // Steering following-error check (100 Hz)
      // Threshold in degrees from speed-based lookup, converted to raw (0.1°/bit)
      if (this.sesAngleRaw !== null && this.lastCmdAngleRaw !== null) {
        const thresholdDeg = computeFollowingErrorThreshold(this.lastSpeedMmps);
        const thresholdRaw = thresholdDeg * 10;
        const error = Math.abs(this.lastCmdAngleRaw - this.sesAngleRaw);
        if (error > thresholdRaw) {
          this.steerFollowErrTicks++;
          if (this.steerFollowErrTicks >= 30) {  // 300ms at 100Hz
            shouldEstop = true;
          }
        } else {
          this.steerFollowErrTicks = 0;
        }
      }
      } // ctx.mode !== "manual" gate
    }

    // ── Brake command (50 Hz) ───────────────────────────────
    if (nowMs % 20 === 0) {
      const obstacleKpa = this.computeObstacleKpa();
      let brakeKpa = Math.max(obstacleKpa, this.hostBrakeKpa);

      if (shouldEstop) brakeKpa = MAX_BRAKE_KPA;
      else if (hostHbTimeout) brakeKpa = ASSIST_STOP_KPA;

      // Fix 2: Originate 0x001 ESTOP on both buses on internal fault detection
      // Matches firmware run_safety_checks() — DLC=0, no data
      if (shouldEstop) {
        out.push({ simTimeMs: nowMs, bus: "low", canId: "0x001", name: "ESTOP", dlc: 0, data: [], sender: "rt" });
        out.push({ simTimeMs: nowMs, bus: "high", canId: "0x001", name: "ESTOP", dlc: 0, data: [], sender: "rt" });
      }

      // 0x205 RT_BRAKE_CMD on low bus (50 Hz) — suppressed in MANUAL mode (EPS-C standalone, SYS handles brake)
      if (ctx.mode !== "manual") {
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

      // Gap #12: RT takes over 0x7B9 on SYS heartbeat loss (stroke=max)
      // Matches firmware VcuSebReq::pack() — per steer-by-wire CSV: strokemode, stroke=1140(max), rolling counter, xor^0xFF checksum
      if (sysHbTimeout) {
        const strokeRaw = 1140;  // 27mm max: (27+30)/0.05
        const raw = [0, 0, 0, 0, 0, 0, 0, 0];
        // Byte 0: align=0, control_enable=1, mode=0(Stroke), auto_brake=1
        raw[0] = (0 << 0) | (1 << 1) | (0 << 2) | (1 << 3);
        // Bytes 2-3: stroke_req LE (full 16-bit in Stroke mode)
        raw[2] = strokeRaw & 0xFF;
        raw[3] = (strokeRaw >> 8) & 0xFF;
        // Byte 6: roll_cnt_enable=1, checksum_enable=1, rolling_counter(bits 4-7)
        raw[6] = 0x03 | ((this.sebRollCounter & 0x0F) << 4);
        // Checksum: XOR(raw[0..6]) ^ 0xFF
        let cksum = 0;
        for (let i = 0; i < 7; i++) cksum ^= raw[i];
        raw[7] = cksum ^ 0xFF;
        this.sebRollCounter = (this.sebRollCounter + 1) & 0x0F;
        out.push({
          simTimeMs: nowMs,
          bus: "low",
          canId: "0x7B9",
          name: "VCU_SEB_REQ",
          dlc: 8,
          data: raw,
          sender: "rt",
        });
      }
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
        this.lastCmdAngleRaw = cmd.targetAngle;
        // Build 0x169 VCU_SES_REQ (steer-by-wire LE encoding)
        const angle16 = cmd.targetAngle & 0xFFFF;
        const speedRaw = cmd.targetSpeed & 0xFFFF;
        const rollCnt = cmd.rollingCounter;  // steering's own 50 Hz counter (0-15)
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
        // Compute checksum: XOR(bytes 0-6) ^ 0xFF (per steer-by-wire CSV spec)
        let cksum = 0;
        for (let i = 0; i < 7; i++) cksum ^= data[i];
        data[7] = cksum ^ 0xFF;

        if (ctx.mode !== "manual") {
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
    }

    // ── RT_STATE_RPT 0x210 on high bus (10 Hz) ──────────────
    if (nowMs % 100 === 0) {
      const modeByte = ctx.mode === "auto" ? 1 : ctx.mode === "estop" ? 2 : 0;
      // safety_state: 0=Normal, 1=InternalEstop, 2=Fault
      const ss = this.steering.getState();
      const safetyState = ss === SteerState.ACTIVE ? 0 :
                          ss === SteerState.FAULT ? 2 : 1;
      out.push({
        simTimeMs: nowMs,
        bus: "high",
        canId: "0x210",
        name: "RT_STATE_RPT",
        dlc: 4,
        data: [modeByte, safetyState, 0, 0],
        sender: "rt",
      });
      // Also send on low bus so SYS can read RT safety_state
      out.push({
        simTimeMs: nowMs,
        bus: "low",
        canId: "0x210",
        name: "RT_STATE_RPT",
        dlc: 4,
        data: [modeByte, safetyState, 0, 0],
        sender: "rt",
      });

      // 0x310 STEER_DIAG (10 Hz, high bus, DLC=8)
      // angle in 0.1°/bit signed i16 BE, NO OFFSET (unlike 0x169 encoding)
      const sesAngle01deg = this.sesAngleRaw !== null ? (this.sesAngleRaw - 30000) : 0;
      const steerDiagAngle = Math.max(-32768, Math.min(32767, sesAngle01deg));
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x310", name: "STEER_DIAG", dlc: 8, data: [
          (steerDiagAngle >> 8) & 0xFF, steerDiagAngle & 0xFF,  // angle 0.1° i16 BE
          0, // fault=0 (EPS-C ok)
          0, 0, // motor_current=0 (stub)
          0, 0, // ecu_temp=0 (stub)
          0, // reserved
        ], sender: "rt",
      });

      // 0x311 BRAKE_DIAG (10 Hz, high bus, DLC=8)
      const brakePressureRaw = Math.round((this.computeObstacleKpa() / 1000) / 0.05) & 0xFFFF;
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x311", name: "BRAKE_DIAG", dlc: 8, data: [
          (brakePressureRaw >> 8) & 0xFF, brakePressureRaw & 0xFF, // pressure raw
          0, // fault=0 (SEB ok)
          0, 0, // motor_current=0 (stub)
          0, 0, // ecu_temp=0 (stub)
          0, // reserved
        ], sender: "rt",
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
