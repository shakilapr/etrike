/**
 * HOST model — drive-by-wire commander.
 * Generates 0x300 (drive), 0x301 (brake), 0x7FC (heartbeat).
 * Speed/brake/gear/yaw are set externally (keyboard/controller/scenario).
 */
import type { CanFrame } from "../../types/can";
import type { EcuModel, EcuConfig, EcuState } from "../ecu-model";

export class HostModel implements EcuModel {
  readonly id = "host";

  speedMmps = 0;
  yawMradS = 0;
  gear = 1; // D
  brakeKpa = 0;
  private hbCounter = 0;
  private callbacks: Array<(frame: CanFrame) => void> = [];
  private frameQueue: CanFrame[] = [];
  private tickCount = 0;

  config(_params: EcuConfig): void {}
  start(): void {}
  stop(): void {}
  state(): EcuState { return { ecu: this.id, healthy: true, uptimeMs: 0 }; }

  ingest(_frame: CanFrame): void { /* HOST doesn't receive CAN commands in simulation */ }

  tick(_dtMs: number): CanFrame[] {
    this.tickCount++;
    this.frameQueue = [];
    this.hbCounter = (this.hbCounter + 1) & 0xFF;

    // Drive command 0x300 (50 Hz)
    if (this.tickCount % 2 === 0) {
      const s = Math.round(this.speedMmps);
      const y = Math.round(this.yawMradS);
      this.emit("high", "0x300", 8,
        [(s>>24)&0xFF,(s>>16)&0xFF,(s>>8)&0xFF,s&0xFF,
         (y>>16)&0xFF,(y>>8)&0xFF,y&0xFF,this.gear],
        "HOST_DRIVE_CMD", { speed_mmps: s, yaw_rate_mrad_s: y, gear: this.gear });
    }

    // Brake request 0x301 (10 Hz) — only when braking
    if (this.brakeKpa > 0 && this.tickCount % 10 === 0) {
      const k = Math.round(this.brakeKpa);
      this.emit("high", "0x301", 4, [(k>>24)&0xFF,(k>>16)&0xFF,(k>>8)&0xFF,k&0xFF],
        "HOST_BRAKE_REQ", { brake_pressure_kpa: k });
    }

    // Heartbeat 0x7FC (2 Hz)
    if (this.tickCount % 50 === 0) {
      this.emit("high", "0x7FC", 2, [this.hbCounter, 0], "HOST_HEARTBEAT",
        { alive_ctr: this.hbCounter, health_flags: 0 });
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
