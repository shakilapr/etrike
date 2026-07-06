/**
 * MTR Motor model — TypeScript implementation.
 * Receives 0x204 drive commands, generates 0x206 feedback + 0x120 throttle.
 */
import type { CanFrame } from "../../types/can";
import type { EcuModel, EcuConfig, EcuState } from "../ecu-model";

export class MtrModel implements EcuModel {
  readonly id = "mtr";

  private actualSpeed = 0;
  private gear = 0;
  private faults = 0;
  private callbacks: Array<(frame: CanFrame) => void> = [];
  private frameQueue: CanFrame[] = [];
  private tickCount = 0;

  config(_params: EcuConfig): void {}
  start(): void { this.actualSpeed = 0; this.gear = 0; this.faults = 0; this.brakeActive = false; }
  stop(): void {}
  state(): EcuState { return { ecu: this.id, healthy: this.faults === 0, faultFlags: this.faults, uptimeMs: 0 }; }

  private brakeActive = false;

  ingest(frame: CanFrame): void {
    if (frame.id === "0x001") { this.actualSpeed = 0; this.faults |= 1; return; }
    if (frame.id === "0x204") {
      const d = frame.decoded as Record<string, unknown>;
      const targetSpeed = (d.motor_speed_mmps as number) ?? 0;
      const targetGear = (d.gear as number) ?? 0;
      if (targetSpeed === 0 && this.faults & 1) {
        this.actualSpeed = 0;
      } else if (this.brakeActive) {
        // Brake reduces speed regardless of drive command
        this.actualSpeed = this.actualSpeed * 0.7;
        if (this.actualSpeed < 10) this.actualSpeed = 0;
      } else {
        this.actualSpeed = this.actualSpeed + (targetSpeed - this.actualSpeed) * 0.15;
      }
      this.gear = targetGear;
      if (Math.abs(targetSpeed - this.actualSpeed) < 1) this.actualSpeed = targetSpeed;
    }
    if (frame.id === "0x205") {
      const d = frame.decoded as Record<string, unknown>;
      const kpa = (d.brake_pressure_kpa as number) ?? 0;
      this.brakeActive = kpa > 100;
    }
    if (frame.id === "0x110") {
      const d = frame.decoded as Record<string, unknown>;
      const m = (d.mode as number) ?? 0;
      if (m <= 1) { this.faults &= ~1; this.brakeActive = false; }
    }
  }

  tick(_dtMs: number): CanFrame[] {
    this.tickCount++;
    this.frameQueue = [];

    // Motor feedback 0x206 (50 Hz)
    if (this.tickCount % 2 === 0) {
      const spd = Math.round(this.actualSpeed);
      this.emit("low", "0x206", 4, [(spd>>8)&0xFF, spd&0xFF, this.gear, this.faults],
        "MTR_MOTOR_FBK", { actual_speed_mmps: spd, gear_state: this.gear, fault_flags: this.faults });
      // Also forward to high bus (gateway function)
      this.emit("high", "0x206", 4, [(spd>>8)&0xFF, spd&0xFF, this.gear, this.faults],
        "MTR_MOTOR_FBK", { actual_speed_mmps: spd, gear_state: this.gear, fault_flags: this.faults });
    }

    // Throttle status 0x120 (100 Hz)
    const spd = Math.round(this.actualSpeed);
    this.emit("low", "0x120", 2, [(spd>>8)&0xFF, spd&0xFF], "SYS_THROTTLE_STS", { speed_mmps: spd });

    return [...this.frameQueue];
  }

  onFrame(callback: (frame: CanFrame) => void): void { this.callbacks.push(callback); }

  private emit(bus: "high"|"low", id: string, dlc: number, data: number[], name: string, decoded: Record<string, unknown>): void {
    const frame: CanFrame = { ts: Date.now()/1000, bus, id, name, dlc, data, decoded };
    this.frameQueue.push(frame);
    for (const cb of this.callbacks) cb(frame);
  }

  /** Called by physics model to feed actual plant speed. */
  setActualSpeed(mmps: number): void { this.actualSpeed = mmps; }
}
