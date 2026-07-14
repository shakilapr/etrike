/**
 * SysEcu — simulated SYS ESP32-S3 (safety, brake, lights, diag, mode).
 *
 * Monitors safety, controls SEB brake via 0x7B9, manages mode transitions,
 * sends 0x011 safety status, 0x110 mode, 0x600 diag, 0x7FE heartbeat.
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId } from "../core/types.js";
import { SysSafetyMonitor } from "../controllers/sys-safety.js";
import { SysBrakeController, BrakeState } from "../controllers/sys-brake.js";
import { decodeAs, encodeSimFrame } from "../protocol.js";

export class SysEcu implements SimulatedEcu {
  readonly id = "SYS ESP32-S3";
  readonly nodeId: SimNodeId = "sys";

  private safety = new SysSafetyMonitor();
  private brake = new SysBrakeController();
  private currentMode: "manual" | "auto" | "estop" = "manual";
  private sysHbCtr = 0;
  private cmdSpeedMmps = 0;
  private actualSpeedMmps = 0;
  private brakeKpa = 0;
  private lights = 0; // bitfield: turn left, turn right, brake, head
  private diagHeapKb = 500;
  private tec = 0;
  private rec = 0;
  private lastSentMode = -1;  // sentinel: first mode change always sends

  // Gap I2: brake-suppression tracking state (matches firmware main.cpp)
  private rtSafetyState = 0;       // from 0x210 byte 1 bits 0-1 (0=Normal)
  private sebRolling = true;       // SEB rolling counter incrementing (H1)
  private lastSebRoll = 0xFF;      // last SEB counter for change detection
  private sebRollInit = false;     // first 0x721 seen?
  private lastSetpointTickMs = 0;  // timestamp of last 0x204 arrival

  // Gap I9a: ESTOP rate-limiting — track ESTOP CAN frame RX timestamps
  private estopTimestamps: number[] = [];
  private lastEstopRateLimitWarningMs = -Infinity;

  // Gap I9b: MTR ESTOP ACK watchdog
  private estopTriggerMs = -1;     // when ESTOP was first triggered (ms)
  private mtrAcked = false;        // whether MTR has acknowledged ESTOP

  // ── Simulation inputs ────────────────────────────────────────────

  setEstopButton(pressed: boolean): void { this.safety.setEstop(pressed); }
  setBrakeLever(pressed: boolean): void { this.safety.setBrakeLever(pressed); }
  setActualSpeed(mmps: number): void { this.actualSpeedMmps = mmps; }

  /**
   * Track ESTOP events for rate-limiting (I9a).
   * Logs a warning if >2 ESTOP frames arrive within a 500ms window,
   * but still processes the ESTOP for safety.
   */
  private trackEstopEvent(nowMs: number): void {
    this.estopTimestamps.push(nowMs);
    // Prune entries older than 500ms
    const windowMs = 500;
    this.estopTimestamps = this.estopTimestamps.filter(t => nowMs - t <= windowMs);
    if (this.estopTimestamps.length > 2 && nowMs - this.lastEstopRateLimitWarningMs >= windowMs) {
      this.lastEstopRateLimitWarningMs = nowMs;
      console.warn(`[SYS] ESTOP rate-limit: ${this.estopTimestamps.length} frames in ${windowMs}ms window (limit 2)`);
    }
    this.safety.setEstop(true);
  }

  init(): void {
    this.safety.reset();
    this.brake.reset();
    this.estopTimestamps = [];
    this.lastEstopRateLimitWarningMs = -Infinity;
    this.estopTriggerMs = -1;
    this.mtrAcked = false;
  }

  shutdown(): void {
    // nothing
  }

  tick(
    nowMs: number,
    highBusRx: SimFrame[],
    lowBusRx: SimFrame[],
    ctx: SimulationContext,
  ): SimFrame[] {
    this.currentMode = ctx.mode;
    const out: SimFrame[] = [];
    const estopActive = ctx.estopActive || this.safety.estop;

    // ── Process low-bus frames ──────────────────────────────────
    for (const f of lowBusRx) {
      const heartbeat = decodeAs(f, "rt:rt_heartbeat");
      if (heartbeat !== undefined) {
        this.safety.feedHeartbeatRt(nowMs, Number(heartbeat.alive_ctr));
        continue;
      }
      const drive = decodeAs(f, "rt:rt_drive_cmd");
      if (drive !== undefined) {
        this.cmdSpeedMmps = Number(drive.motor_speed_mmps);
        this.lastSetpointTickMs = nowMs;
        continue;
      }
      const brake = decodeAs(f, "rt:rt_brake_cmd");
      if (brake !== undefined) {
        this.brakeKpa = Number(brake.brake_pressure_kpa);
        continue;
      }
      const motor = decodeAs(f, "mtr:mtr_motor_fbk");
      if (motor !== undefined) {
        this.actualSpeedMmps = Number(motor.actual_speed_mmps);
        this.safety.feedMtrFeedback({
          actualSpeed: this.actualSpeedMmps,
          gearState: Number(motor.gear_state),
          faultFlags: Number(motor.fault_flags),
        }, nowMs);
        continue;
      }
      if (decodeAs(f, "safety:safety_estop") !== undefined) {
        this.trackEstopEvent(nowMs);
        continue;
      }
      const rtState = decodeAs(f, "rt:rt_state_rpt");
      if (rtState !== undefined) {
        this.rtSafetyState = Number(rtState.safety_state);
        continue;
      }
      const errorInfo = decodeAs(f, "seb:seb_err_info");
      if (errorInfo !== undefined) {
          const raw = errorInfo.raw as Uint8Array;
          const l3Active =
            (raw[0] & 0xFC) !== 0 ||
            (raw[1] & 0x2F) !== 0 ||
            (raw[2] & 0x76) !== 0;
          if (l3Active) {
            this.trackEstopEvent(nowMs);
          }
        continue;
      }
      const sebStatus = decodeAs(f, "seb:seb_status");
      if (sebStatus !== undefined) {
          this.brake.feedSebStatus(Number(sebStatus.status_byte), Number(sebStatus.stroke_value_raw));
          // H1: Track SEB rolling counter (byte 6 bits 4-7). Frozen counter
          // means SEB isn't receiving commands — SYS must resume sending 0x7B9.
          const sebRoll = Number(sebStatus.rolling_counter);
          if (!this.sebRollInit || sebRoll !== this.lastSebRoll) {
            this.sebRollInit = true;
            this.lastSebRoll = sebRoll;
            this.sebRolling = true;
          } else {
            this.sebRolling = false;
          }
        continue;
      }
      const lights = decodeAs(f, "host:host_light_cmd");
      if (lights !== undefined) {
        this.lights = Number(lights.left_turn)
          | (Number(lights.right_turn) << 1)
          | (Number(lights.brake_light) << 2)
          | (Number(lights.headlight) << 3);
      }
    }

    // ── EGAS L2 check (every 20ms) ──────────────────────────────
    if (nowMs % 20 === 0) {
      const egasFault = this.safety.checkEgasL2(nowMs, this.cmdSpeedMmps, this.actualSpeedMmps);
      if (egasFault) {
        // EGAS L2 fault triggers ESTOP
        this.trackEstopEvent(nowMs);
      }
    }

    const effectiveEstop = estopActive || this.safety.estop;

    // ── MTR ESTOP ACK watchdog (I9b) ─────────────────────────────
    if (effectiveEstop) {
      if (this.estopTriggerMs < 0) {
        // ESTOP just became active — start the ACK timer
        this.estopTriggerMs = nowMs;
        this.mtrAcked = false;
      } else if (!this.mtrAcked) {
        // Check if MTR has acknowledged with ESTOP_ACTIVE bit
        this.mtrAcked = this.safety.mtrEstopAcked();
        if (!this.mtrAcked && (nowMs - this.estopTriggerMs) >= 100) {
          // 100ms elapsed without ACK — retrigger ESTOP
          console.warn(`[SYS] MTR ESTOP ACK timeout at ${nowMs}ms — retriggering`);
          this.safety.setEstop(true);
          this.estopTriggerMs = nowMs;  // restart the timer
        }
      }
    } else {
      // No ESTOP — reset watchdog
      this.estopTriggerMs = -1;
      this.mtrAcked = false;
    }

    // ── Brake control (50 Hz) ────────────────────────────────────
    if (nowMs % 20 === 0) {
      const cmd = this.brake.tick(
        this.safety.brakeLever,
        effectiveEstop,
        effectiveEstop ? 0 : this.brakeKpa,
      );

      // Gap I2: 6-condition brake suppression. In AUTO mode when all safety
      // signals are healthy, RT owns the brake and SYS suppresses 0x7B9 to
      // avoid bus collision. SYS resumes sending in MANUAL, ESTOP, rider
      // override (brake lever), RT heartbeat loss, RT safety fault, SEB
      // command failure (stale rolling counter), or stale RT setpoint.
      const rtAlive = this.safety.heartbeatOk(nowMs);
      const rtNormal = this.rtSafetyState === 0;
      const sebAck = this.sebRolling;
      const rtSetpointFresh = (nowMs - this.lastSetpointTickMs) < 200; // kSetpointStaleMs=200
      const suppressSeb = this.currentMode === "auto" && rtAlive && rtNormal
                       && sebAck && !this.safety.brakeLever && !effectiveEstop && rtSetpointFresh;

      if (cmd && !suppressSeb) {
        out.push(encodeSimFrame("seb:vcu_seb_req", {
          alignment_enable: cmd.alignEnable === 1,
          control_enable: cmd.controlEnable === 1,
          control_mode: cmd.controlMode,
          auto_brake: cmd.autoBrake === 1,
          stroke_request_raw: cmd.strokeReq,
          pressure_request_raw: cmd.pressureReq,
          rolling_counter: cmd.rollingCounter,
        }, "low", "sys", nowMs));
      }
    }

    // ── 0x011 SYS_SAFETY_STS (5 Hz) ─────────────────────────────
    if (nowMs % 200 === 0) {
      out.push(encodeSimFrame("sys:sys_safety_sts", {
        estop_active: effectiveEstop ? 1 : 0,
        heartbeat_ok: this.safety.heartbeatOk(nowMs) ? 1 : 0,
        light_left: this.lights & 1,
        light_right: (this.lights >> 1) & 1,
        light_brake: (this.lights >> 2) & 1,
        light_head: (this.lights >> 3) & 1,
      }, "low", "sys", nowMs));
    }

    // ── 0x110 SYS_MODE_CMD (on change only) ──────────────────────
    const modeByte = ctx.mode === "auto" ? 1 : ctx.mode === "estop" ? 2 : 0;
    if (modeByte !== this.lastSentMode) {
      this.lastSentMode = modeByte;
      out.push(encodeSimFrame("sys:sys_mode_cmd", { mode: modeByte }, "low", "sys", nowMs));
    }

    // ── 0x600 SYS_DIAG_RPT (1 Hz) ───────────────────────────────
    if (nowMs % 1000 === 0) {
      out.push(encodeSimFrame("sys:sys_diag_rpt", {
        mode: ctx.mode === "auto" ? 1 : ctx.mode === "estop" ? 2 : 0,
        brake_engaged: this.brake.state === BrakeState.ACTIVE ? 1 : 0,
        brake_fault: this.brake.getDiagnostics().brakeFollowingError ? 1 : 0,
        heartbeat_ok: this.safety.heartbeatOk(nowMs) ? 1 : 0,
        rx_overflow: 0,
        estop_active: effectiveEstop ? 1 : 0,
        free_heap_kb: this.diagHeapKb,
        tec: this.tec,
        rec: this.rec,
      }, "low", "sys", nowMs));
    }

    // ── 0x7FE SYS_HEARTBEAT (10 Hz) ─────────────────────────────
    // PWT is a standalone powertrain node. SYS does not emit the retired
    // low-bus 0x012 gateway command; PWT owns its manufacturer command.
    if (nowMs % 100 === 0) {
      this.sysHbCtr = (this.sysHbCtr + 1) & 0xFF;
      const healthFlags = (this.safety.heartbeatOk(nowMs) ? 0x01 : 0)
        | (effectiveEstop ? 0x02 : 0)
        | (ctx.mode === "auto" ? 0x04 : 0)
        | 0x08; // can_ok always asserted in simulation
      out.push(encodeSimFrame("sys:sys_heartbeat", {
        alive_ctr: this.sysHbCtr,
        heartbeat_ok: healthFlags & 1,
        estop_active: (healthFlags >> 1) & 1,
        mode_auto: (healthFlags >> 2) & 1,
        can_ok: (healthFlags >> 3) & 1,
        task_safety_ok: 0,
        task_brake_ok: 0,
        task_dispatch_ok: 0,
        task_can_tx_ok: 0,
      }, "low", "sys", nowMs));
    }

    return out;
  }
}
