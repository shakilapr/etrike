/**
 * RT Gateway model — TypeScript implementation.
 * Stores latest HOST commands in ingest(), processes them in tick().
 * This ensures ESTOP takes effect even if injected after a drive command
 * in the same tick cycle.
 */
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
  private steerRoll = 0;

  config(params: EcuConfig): void {
    this.bypassSesSync = params.bypasses?.sesSync ?? false;
  }

  start(): void { this.mode = 0; this.safety = 0; this.estopPending = false; this.latestSpeed = 0; this.latestGear = 0; this.latestBrakeKpa = 0; }
  stop(): void {}
  state(): EcuState { return { ecu: this.id, mode: ["MANUAL","AUTO","ESTOP"][this.mode], safety: ["Normal","InternalEstop","Fault"][this.safety], healthy: true, uptimeMs: 0 }; }

  ingest(frame: CanFrame): void {
    if (frame.id === "0x001") { this.estopPending = true; this.latestSpeed = 0; return; }
    if (frame.id === "0x300" && frame.bus === "high") {
      const d = frame.decoded as Record<string, unknown>;
      this.latestSpeed = (d.speed_mmps as number) ?? 0;
      this.latestYaw = (d.yaw_rate_mrad_s as number) ?? 0;
      this.latestGear = (d.gear as number) ?? 1;
    }
    if (frame.id === "0x301" && frame.bus === "high") {
      const d = frame.decoded as Record<string, unknown>;
      this.latestBrakeKpa = (d.brake_pressure_kpa as number) ?? 0;
    }
    if (frame.id === "0x110") {
      const d = frame.decoded as Record<string, unknown>;
      const m = (d.mode as number) ?? 0;
      if (m <= 1) { this.mode = m; this.estopPending = false; }
    }
  }

  tick(_dtMs: number): CanFrame[] {
    this.hbCounter = (this.hbCounter + 1) & 0xFF;
    this.frameQueue = [];

    // Process commands (respecting ESTOP)
    const speed = this.estopPending || this.mode === 2 ? 0 : this.latestSpeed;
    const gear = this.estopPending || this.mode === 2 ? 0 : this.latestGear;
    const brakeKpa = this.estopPending ? 5000 : this.latestBrakeKpa;

    // Drive command 0x204 (50 Hz)
    if (this.hbCounter % 2 === 0) {
      const s = Math.round(speed);
      this.emit("low", "0x204", 5, [(s>>24)&0xFF,(s>>16)&0xFF,(s>>8)&0xFF,s&0xFF,gear],
        "RT_DRIVE_CMD", { motor_speed_mmps: s, gear });
    }

    // Brake command 0x205 (10 Hz) — only when braking
    if (brakeKpa > 0 && this.hbCounter % 10 === 0) {
      const k = Math.round(brakeKpa);
      this.emit("low", "0x205", 4, [(k>>24)&0xFF,(k>>16)&0xFF,(k>>8)&0xFF,k&0xFF],
        "RT_BRAKE_CMD", { brake_pressure_kpa: k });
    }

    // Steering command 0x169 (50 Hz) — convert yaw rate to steer angle
    if (this.hbCounter % 2 === 0 && !this.estopPending && this.mode !== 2) {
      const steerAngle = Math.round(this.latestYaw * 0.05); // mrad/s -> 0.1deg approx
      this.steerRoll = (this.steerRoll + 1) & 0x0F;
      const data = [1 | (1 << 1), 0,
        steerAngle & 0xFF, (steerAngle >> 8) & 0xFF,
        328 & 0xFF, 1 | (1 << 1) | (this.steerRoll << 4), 0, 0];
      let cksum = 0; for (let i = 0; i < 7; i++) cksum ^= data[i]; data[7] = cksum ^ 0xFF;
      this.emit("low", "0x169", 8, data, "VCU_SES_REQ", {
        alignment_enable: true, control_enable: true, target_angle: steerAngle,
        target_speed: 328, rolling_counter: this.steerRoll, checksum: data[7],
      });
    }

    // State report 0x210 (10 Hz)
    if (this.hbCounter % 10 === 0) {
      this.emit("high", "0x210", 6, [this.mode, this.estopPending ? 1 : 0, 0, 0, 15, 5], "RT_STATE_RPT",
        { mode: this.mode, safety_state: this.estopPending ? 1 : 0, steer_state: this.bypassSesSync ? 1 : (this.estopPending ? 4 : 1) });
    }

    // Heartbeat 0x7FD (2 Hz)
    if (this.hbCounter % 50 === 0) {
      this.emit("high", "0x7FD", 2, [this.hbCounter, 0], "RT_HEARTBEAT", { alive_ctr: this.hbCounter, health_flags: 0 });
    }

    return [...this.frameQueue];
  }

  onFrame(callback: (frame: CanFrame) => void): void { this.callbacks.push(callback); }

  private emit(bus: "high"|"low", id: string, dlc: number, data: number[], name: string, decoded: Record<string, unknown>): void {
    const frame: CanFrame = { ts: Date.now()/1000, bus, id, name, dlc, data, decoded };
    this.frameQueue.push(frame);
    for (const cb of this.callbacks) cb(frame);
  }
}
