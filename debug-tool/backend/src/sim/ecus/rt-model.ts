import { ID_SAFETY_ESTOP, ID_HOST_DRIVE_CMD, ID_HOST_STEER_CMD, ID_HOST_BRAKE_REQ, ID_SYS_MODE_CMD, ID_RT_DRIVE_CMD, ID_RT_BRAKE_CMD, ID_VCU_SES_REQ, ID_SES_STATUS, ID_MTR_MOTOR_FBK, ID_RT_MOTION_RPT, ID_RT_STATE_RPT, ID_RT_HEARTBEAT } from "@etrike/debug-shared";
/**
 * RT Gateway model — TypeScript implementation.
 * Stores latest HOST commands in ingest(), processes them in tick().
 * This ensures ESTOP takes effect even if injected after a drive command
 * in the same tick cycle.
 */
import { decoder } from "@etrike/debug-shared";
import type { CanFrame } from "../../types/can";
import type { EcuModel, EcuConfig, EcuState } from "../ecu-model";

export class RtModel implements EcuModel {
  readonly id = "rt";

  private mode = 0;
  private safety = 0;
  private estopPending = false;
  private hbCounter = 0;
  private callbacks: Array<(frame: CanFrame) => void> = [];
  private frameQueue: CanFrame[] = [];
  private bypassSesSync = false;

  // Latest command state — set by ingest(), consumed by tick()
  private latestSpeed = 0;
  private latestYaw = 0;
  private latestGear = 0;
  private latestBrakeKpa = 0;
  private latestDirectSteer = 0;
  private directSteerValid = false;
  private lastDirectSteerCounter = -1;
  private lastDirectSteerMs = -Infinity;
  private measuredSpeed = 0;
  private measuredGear = 0;
  private lastMtrFeedbackMs = -Infinity;
  private measuredSteer = 0;
  private steerFeedbackValid = false;
  private lastSesFeedbackMs = -Infinity;
  private elapsedMs = 0;
  private motionCounter = 0;
  private steerRoll = 0;

  config(params: EcuConfig): void {
    this.bypassSesSync = params.bypasses?.sesSync ?? false;
  }

  start(): void {
    this.mode = 0; this.safety = 0; this.estopPending = false;
    this.latestSpeed = 0; this.latestGear = 0; this.latestBrakeKpa = 0;
    this.directSteerValid = false; this.lastDirectSteerCounter = -1;
    this.lastDirectSteerMs = -Infinity; this.lastMtrFeedbackMs = -Infinity;
    this.lastSesFeedbackMs = -Infinity; this.elapsedMs = 0; this.motionCounter = 0;
  }
  stop(): void {}
  state(): EcuState { return { ecu: this.id, mode: ["MANUAL","AUTO","ESTOP"][this.mode], safety: ["Normal","InternalEstop","Fault"][this.safety], healthy: true, uptimeMs: 0 }; }

  ingest(frame: CanFrame): void {
    if (frame.frame.id === ID_SAFETY_ESTOP) { this.estopPending = true; this.latestSpeed = 0; return; }
    if (frame.frame.id === ID_HOST_DRIVE_CMD && frame.bus === "high") {
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      this.latestSpeed = (d.speed_mmps as number) ?? 0;
      this.latestYaw = (d.yaw_rate_mrad_s as number) ?? 0;
      this.latestGear = (d.gear as number) ?? 1;
    }
    if (frame.frame.id === ID_HOST_STEER_CMD && frame.bus === "high") {
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      const counter = (d.rolling_counter as number) ?? -1;
      if (counter !== this.lastDirectSteerCounter) {
        this.lastDirectSteerCounter = counter;
        this.latestDirectSteer = (d.steer_angle_0_1deg as number) ?? 0;
        this.directSteerValid = d.angle_valid === true || d.angle_valid === 1;
        this.lastDirectSteerMs = this.elapsedMs;
      }
    }
    if (frame.frame.id === ID_HOST_BRAKE_REQ && frame.bus === "high") {
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      this.latestBrakeKpa = (d.brake_pressure_kpa as number) ?? 0;
    }
    if (frame.frame.id === ID_SYS_MODE_CMD) {
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      const m = (d.mode as number) ?? 0;
      if (m <= 1) { this.mode = m; this.estopPending = false; }
    }
    if (frame.frame.id === ID_MTR_MOTOR_FBK && frame.bus === "low") {
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      this.measuredSpeed = (d.actual_speed_mmps as number) ?? 0;
      this.measuredGear = (d.gear_state as number) ?? 0;
      this.lastMtrFeedbackMs = this.elapsedMs;
    }
    if (frame.frame.id === ID_SES_STATUS && frame.bus === "low") {
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      this.measuredSteer = (d.str_angle as number) ?? 0;
      this.steerFeedbackValid = d.angle_status === true || d.angle_status === 1;
      this.lastSesFeedbackMs = this.elapsedMs;
    }
  }

  tick(dtMs: number): CanFrame[] {
    this.elapsedMs += dtMs;
    this.hbCounter = (this.hbCounter + 1) & 0xFF;
    this.frameQueue = [];

    // Process commands (respecting ESTOP)
    const speed = this.estopPending || this.mode === 2 ? 0 : this.latestSpeed;
    const gear = this.estopPending || this.mode === 2 ? 0 : this.latestGear;
    const brakeKpa = this.estopPending ? 5000 : this.latestBrakeKpa;

    // Drive command 0x204 (50 Hz)
    if (this.hbCounter % 2 === 0) {
      const s = Math.round(speed);
      this.emit("low", ID_RT_DRIVE_CMD, "RT_DRIVE_CMD", { motor_speed_mmps: s, gear });
    }

    // Brake command 0x205 (10 Hz) — only when braking
    if (brakeKpa > 0 && this.hbCounter % 10 === 0) {
      const k = Math.round(brakeKpa);
      this.emit("low", ID_RT_BRAKE_CMD, "RT_BRAKE_CMD", { brake_pressure_kpa: k });
    }

    // Steering command 0x169 (50 Hz) — convert yaw rate to steer angle
    if (this.hbCounter % 2 === 0 && !this.estopPending && this.mode !== 2) {
      const directFresh = this.elapsedMs - this.lastDirectSteerMs <= 100;
      const steerAngle = this.directSteerValid && directFresh
        ? Math.max(-450, Math.min(450, Math.round(this.latestDirectSteer)))
        : Math.round(this.latestYaw * 0.05); // legacy yaw-rate fallback
      this.steerRoll = (this.steerRoll + 1) & 0x0F;
      const data = [1 | (1 << 1), 0,
        steerAngle & 0xFF, (steerAngle >> 8) & 0xFF,
        328 & 0xFF, 1 | (1 << 1) | (this.steerRoll << 4), 0, 0];
      let cksum = 0; for (let i = 0; i < 7; i++) cksum ^= data[i]; data[7] = cksum ^ 0xFF;
      this.emit("low", ID_VCU_SES_REQ, "VCU_SES_REQ", {
        alignment_enable: true, control_enable: true, target_angle: steerAngle,
        target_speed: 328, rolling_counter: this.steerRoll, checksum: data[7],
      });
    }

    // Coherent physical motion report 0x121 (100 Hz).
    const speedFresh = this.elapsedMs - this.lastMtrFeedbackMs <= 100;
    const steerFresh = this.elapsedMs - this.lastSesFeedbackMs <= 100 && this.steerFeedbackValid;
    const yawRate = speedFresh && steerFresh
      ? Math.round(this.measuredSpeed * Math.tan(this.measuredSteer * 0.1 * Math.PI / 180) / 1.5)
      : 0;
    this.emit("high", ID_RT_MOTION_RPT, "RT_MOTION_RPT", {
      speed_mmps: Math.round(this.measuredSpeed), yaw_rate_mrad_s: yawRate,
      gear: this.measuredGear, speed_valid: speedFresh ? 1 : 0,
      yaw_rate_valid: speedFresh && steerFresh ? 1 : 0,
      gear_valid: speedFresh ? 1 : 0, reserved: 0,
      rolling_counter: this.motionCounter,
    });
    this.motionCounter = (this.motionCounter + 1) & 0xFF;

    // State report 0x210 (10 Hz)
    if (this.hbCounter % 10 === 0) {
      this.emit("high", ID_RT_STATE_RPT, "RT_STATE_RPT", { mode: this.mode, safety_state: this.estopPending ? 1 : 0, steer_state: this.bypassSesSync ? 1 : (this.estopPending ? 4 : 1) });
    }

    // Heartbeat 0x7FD (2 Hz)
    if (this.hbCounter % 50 === 0) {
      this.emit("high", ID_RT_HEARTBEAT, "RT_HEARTBEAT", { alive_ctr: this.hbCounter, health_flags: 0 });
    }

    return [...this.frameQueue];
  }

  onFrame(callback: (frame: CanFrame) => void): void { this.callbacks.push(callback); }

  private emit(bus: "high"|"low", id: string, name: string, signals: Record<string, unknown>): void {
    const encoded = decoder.encode(bus, id, signals as Record<string, number|boolean>);
    const frame: CanFrame = {
      ts: Date.now()/1000,
      ts_us: "",
      seq: 0,
      bus,
      frame: { id, dlc: encoded.dlc, data: encoded.data, ext: false, rtr: false },
      decoded: { name, signals }
    };
    this.frameQueue.push(frame);
    for (const cb of this.callbacks) cb(frame);
  }
}
