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
  STARTUP_GRACE_PERIOD_MS,
  computeFollowingErrorThreshold,
} from "../physics/tricycle.js";
import { decodeAs, encodeSimFrame } from "../protocol.js";

export class RtEcu implements SimulatedEcu {
  readonly id = "RT ESP32-S3";
  readonly nodeId: SimNodeId = "rt";

  private kinematics = new RtKinematicsController();
  private steering = new RtSteeringController();
  private sebRollCounter = 0;  // Gap #12: rolling counter for RT→0x7B9

  private lastHostCmdMs = -Infinity;
  private hostCmdEverSeen = false;
  private startupMs: number | null = null;
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
  private directSteerAngle01deg = 0;
  private directSteerValid = false;
  private lastDirectSteerMs = -Infinity;
  private lastDirectSteerCtr = -1;
  private measuredSpeedMmps = 0;
  private physicalGear = 0;
  private lastMtrFeedbackMs = -Infinity;
  private lastSesFeedbackMs = -Infinity;
  private motionCounter = 0;

  init(): void {
    this.kinematics.reset();
    this.steering.reset();
    this.lastHostCmdMs = -Infinity;
    this.hostCmdEverSeen = false;
    this.startupMs = null;
    this.lastSysHbMs = -Infinity;
    this.lastSysHbCtr = -1;
    this.lastHostHbCtr = -1;
    this.sysHbEverSeen = false;
    this.lastHostHbMs = -Infinity;
    this.currentMode = "manual";
    this.hostDriveCmd = { speedMmps: 0, yawRateMradS: 0, gear: 0 };
    this.hostBrakeKpa = 0;
    this.rtHbCtrLow = 0;
    this.rtHbCtrHigh = 0;
    this.lastSpeedMmps = 0;
    this.sesAngleRaw = null;
    this.sesAngleStatus = 0;
    this.steerFollowErrTicks = 0;
    this.lastCmdAngleRaw = null;
    this.directSteerAngle01deg = 0;
    this.directSteerValid = false;
    this.lastDirectSteerMs = -Infinity;
    this.lastDirectSteerCtr = -1;
    this.measuredSpeedMmps = 0;
    this.physicalGear = 0;
    this.lastMtrFeedbackMs = -Infinity;
    this.lastSesFeedbackMs = -Infinity;
    this.motionCounter = 0;
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
      const drive = decodeAs(f, "host:host_drive_cmd");
      if (drive !== undefined) {
        this.lastHostCmdMs = nowMs;
        this.hostCmdEverSeen = true;
        if (ctx.mode === "auto" && !ctx.estopActive) {
          this.hostDriveCmd = {
            speedMmps: Number(drive.speed_mmps),
            yawRateMradS: Number(drive.yaw_rate_mrad_s),
            gear: Number(drive.gear),
          };
        }
        continue;
      }
      const brake = decodeAs(f, "host:host_brake_req");
      if (brake !== undefined) {
        this.hostBrakeKpa = Number(brake.brake_pressure_kpa);
        continue;
      }
      const directSteer = decodeAs(f, "host:host_steer_cmd");
      if (directSteer !== undefined) {
        const counter = Number(directSteer.rolling_counter);
        if (counter !== this.lastDirectSteerCtr) {
          this.lastDirectSteerCtr = counter;
          this.directSteerAngle01deg = Number(directSteer.steer_angle_0_1deg);
          this.directSteerValid = Boolean(directSteer.angle_valid);
          this.lastDirectSteerMs = nowMs;
        }
        continue;
      }
      const heartbeat = decodeAs(f, "host:host_heartbeat");
      if (heartbeat !== undefined) {
          const counter = Number(heartbeat.alive_ctr);
          if (counter !== this.lastHostHbCtr) {
            this.lastHostHbMs = nowMs;
            this.lastHostHbCtr = counter;
          }
        continue;
      }
      if (decodeAs(f, "safety:safety_estop") !== undefined) {
        if (f.sender !== "rt") out.push({ ...f, bus: "low", sender: "rt" });
        continue;
      }
      if (decodeAs(f, "host:host_light_cmd") !== undefined) {
        out.push({ ...f, bus: "low", sender: "rt" });
        continue;
      }
      const obstacle = decodeAs(f, "host:host_obstacle_dist");
      if (obstacle !== undefined) {
        this.obstacleDistanceMm = Number(obstacle.distance_mm);
      }
    }

    // ── Process low-bus frames ──────────────────────────────
    for (const f of lowBusRx) {
      const heartbeat = decodeAs(f, "sys:sys_heartbeat");
      if (heartbeat !== undefined) {
          const counter = Number(heartbeat.alive_ctr);
          if (counter !== this.lastSysHbCtr) {
            this.lastSysHbMs = nowMs;
            this.lastSysHbCtr = counter;
          }
          this.sysHbEverSeen = true;
        continue;
      }
      const steering = decodeAs(f, "ses:ses_status");
      if (steering !== undefined) {
        this.sesAngleRaw = Number(steering.steering_angle_raw);
        this.sesAngleStatus = steering.angle_aligned === true ? 1 : 0;
        this.lastSesFeedbackMs = nowMs;
        continue;
      }
      const motor = decodeAs(f, "mtr:mtr_motor_fbk");
      if (motor !== undefined) {
        this.measuredSpeedMmps = Number(motor.actual_speed_mmps);
        this.physicalGear = Number(motor.gear_state);
        this.lastMtrFeedbackMs = nowMs;
        out.push({ ...f, bus: "high", sender: "rt" });
        continue;
      }
      if (decodeAs(f, "safety:safety_estop") !== undefined) {
        if (f.sender !== "rt") out.push({ ...f, bus: "high", sender: "rt" });
        continue;
      }
      if (
        decodeAs(f, "sys:sys_safety_sts") !== undefined ||
        decodeAs(f, "mtr:sys_throttle_sts") !== undefined ||
        decodeAs(f, "sys:sys_diag_rpt") !== undefined
      ) {
        out.push({ ...f, bus: "high", sender: "rt" });
      }
    }

    // ── Check safety conditions ─────────────────────────────

    if (this.startupMs === null) {
      this.startupMs = nowMs;
    }
    const timeSinceStartup = nowMs - this.startupMs;

    // Command staleness
    const cmdStale = nowMs - this.lastHostCmdMs > CMD_STALE_TIMEOUT_MS
      && (this.hostCmdEverSeen || timeSinceStartup > STARTUP_GRACE_PERIOD_MS);
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
      const directFresh = nowMs - this.lastDirectSteerMs <= 100;
      if (this.directSteerValid && directFresh) {
        const dynamicLimit = this.kinematics.getDynamicLimit(resolved.motorSpeedMmps);
        resolved.steerAngleDeg = Math.max(
          -dynamicLimit,
          Math.min(dynamicLimit, this.directSteerAngle01deg * 0.1),
        );
      }

      // Feed resolved steering target to steering controller (matching C++ main.cpp line 429)
      this.steering.setTarget(
        Math.round(resolved.steerAngleDeg * 1000),  // degrees → millidegrees
        resolved.motorSpeedMmps,
      );

      // 0x204 RT_DRIVE_CMD on low bus (100 Hz)
      const speed = resolved.motorSpeedMmps;
      out.push(encodeSimFrame("rt:rt_drive_cmd", {
        motor_speed_mmps: speed,
        gear: resolved.gear,
      }, "low", "rt", nowMs));

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
        out.push(encodeSimFrame("safety:safety_estop", {}, "low", "rt", nowMs));
        out.push(encodeSimFrame("safety:safety_estop", {}, "high", "rt", nowMs));
      }

      // 0x205 RT_BRAKE_CMD on low bus (50 Hz) — suppressed in MANUAL mode (EPS-C standalone, SYS handles brake)
      if (ctx.mode !== "manual") {
        out.push(encodeSimFrame("rt:rt_brake_cmd", {
          brake_pressure_kpa: brakeKpa,
        }, "low", "rt", nowMs));
      }

      // CMP3: RT sends 0x7B9 in AUTO mode as 1-hop brake (steering ACTIVE, not ESTOP)
      // Matches real firmware: forwards g_brake_kpa_to_send as pressure-mode VCU_SEB_REQ
      if (ctx.mode === "auto" && this.steering.state === SteerState.ACTIVE && !shouldEstop && !sysHbTimeout) {
        const pressureRaw = Math.min(Math.round(brakeKpa / 50), 100);
        out.push(encodeSimFrame("seb:vcu_seb_req", {
          alignment_enable: false,
          control_enable: true,
          control_mode: 1,
          auto_brake: pressureRaw > 0,
          stroke_request_raw: 0,
          pressure_request_raw: pressureRaw,
          rolling_counter: this.sebRollCounter,
        }, "low", "rt", nowMs));
        this.sebRollCounter = (this.sebRollCounter + 1) & 0x0F;
      }

      // Gap #12: RT takes over 0x7B9 on SYS heartbeat loss (stroke=max)
      // Matches firmware VcuSebReq::pack() — per steer-by-wire CSV: strokemode, stroke=1140(max), rolling counter, xor^0xFF checksum
      if (sysHbTimeout) {
        const strokeRaw = 1140;  // 27mm max: (27+30)/0.05
        out.push(encodeSimFrame("seb:vcu_seb_req", {
          alignment_enable: false,
          control_enable: true,
          control_mode: 0,
          auto_brake: true,
          stroke_request_raw: strokeRaw,
          pressure_request_raw: 0,
          rolling_counter: this.sebRollCounter,
        }, "low", "rt", nowMs));
        this.sebRollCounter = (this.sebRollCounter + 1) & 0x0F;
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
        if (ctx.mode !== "manual") {
          out.push(encodeSimFrame("ses:vcu_ses_req", {
            alignment_enable: cmd.alignEnable === 1,
            control_enable: cmd.controlEnable === 1,
            target_angle_raw: cmd.targetAngle,
            target_speed_raw: cmd.targetSpeed,
            rolling_counter: cmd.rollingCounter,
            vehicle_speed_raw: cmd.vehicleSpeed,
          }, "low", "rt", nowMs));
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
      const state = {
        mode: modeByte,
        safety_state: safetyState,
        estop_reason: 0,
        reversing: 0,
        rx_overflow: 0,
        task_health: 0x0F,
        steer_state: this.steering.getState(),
      };
      out.push(encodeSimFrame("rt:rt_state_rpt", state, "high", "rt", nowMs));
      // Also send on low bus so SYS can read RT safety_state
      out.push(encodeSimFrame("rt:rt_state_rpt", state, "low", "rt", nowMs));

      // 0x310 STEER_DIAG (10 Hz, high bus, DLC=8)
      // angle in 0.1°/bit signed i16 BE, NO OFFSET (unlike 0x169 encoding)
      const sesAngle01deg = this.sesAngleRaw !== null ? (this.sesAngleRaw - 30000) : 0;
      const steerDiagAngle = Math.max(-32768, Math.min(32767, sesAngle01deg));
      out.push(encodeSimFrame("rt:steer_diag", {
        angle_0_1deg: steerDiagAngle / 10,
        fault: 0,
        motor_current: 0,
        ecu_temp: 0,
      }, "high", "rt", nowMs));

      // 0x311 BRAKE_DIAG (10 Hz, high bus, DLC=8)
      out.push(encodeSimFrame("rt:brake_diag", {
        pressure_raw: this.computeObstacleKpa() / 1000,
        fault: 0,
        motor_current: 0,
        ecu_temp: 0,
      }, "high", "rt", nowMs));
      // 0x220 RT_PID_RPT (10 Hz, high bus, DLC=6)
      out.push(encodeSimFrame("rt:rt_pid_rpt", {
        speed_setpoint: 0,
        speed_measured: 0,
        pid_output: 0,
      }, "high", "rt", nowMs));
    }

    // ── RT_MOTION_RPT 0x121 on high bus (100 Hz) ───────────
    if (nowMs % 10 === 0) {
      const speedFresh = nowMs - this.lastMtrFeedbackMs <= 100;
      const steerFresh = nowMs - this.lastSesFeedbackMs <= 100 && this.sesAngleStatus === 1;
      const angle01deg = this.sesAngleRaw !== null ? this.sesAngleRaw - 30000 : 0;
      const yaw = speedFresh && steerFresh
        ? Math.round(this.measuredSpeedMmps * Math.tan(angle01deg * 0.1 * Math.PI / 180) / 1.5)
        : 0;
      out.push(encodeSimFrame("rt:rt_motion_rpt", {
        speed_mmps: this.measuredSpeedMmps,
        yaw_rate_mrad_s: yaw,
        gear: this.physicalGear,
        speed_valid: speedFresh ? 1 : 0,
        yaw_rate_valid: speedFresh && steerFresh ? 1 : 0,
        gear_valid: speedFresh ? 1 : 0,
        reserved: 0,
        rolling_counter: this.motionCounter,
      }, "high", "rt", nowMs));
      this.motionCounter = (this.motionCounter + 1) & 0xFF;
    }

    // ── Heartbeats (2 Hz) ───────────────────────────────────
    if (nowMs % 500 === 0) {
      this.rtHbCtrLow = (this.rtHbCtrLow + 1) & 0xFF;
      this.rtHbCtrHigh = (this.rtHbCtrHigh + 1) & 0xFF;

      out.push(encodeSimFrame("rt:rt_heartbeat", {
        alive_ctr: this.rtHbCtrLow,
        health_flags: this.healthFlags(ctx, shouldEstop),
      }, "low", "rt", nowMs));
      out.push(encodeSimFrame("rt:rt_heartbeat", {
        alive_ctr: this.rtHbCtrHigh,
        health_flags: this.healthFlags(ctx, shouldEstop),
      }, "high", "rt", nowMs));
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

  private healthFlags(ctx: SimulationContext, estop: boolean): number {
    return 0x01
      | (estop || ctx.estopActive ? 0x02 : 0)
      | (ctx.mode === "auto" ? 0x04 : 0)
      | 0x08;
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
