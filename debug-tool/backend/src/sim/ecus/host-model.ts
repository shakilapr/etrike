import { ID_HOST_DRIVE_CMD, ID_HOST_BRAKE_REQ, ID_HOST_HEARTBEAT } from "@etrike/debug-shared";
/**
 * HOST model — drive-by-wire commander.
 * Generates 0x300 (drive), 0x301 (brake), 0x7FC (heartbeat).
 * Speed/brake/gear/yaw are set externally (keyboard/controller/scenario).
 */
import { getDecoder } from "@etrike/debug-shared";
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
      this.emit("high", ID_HOST_DRIVE_CMD, "HOST_DRIVE_CMD", { speed_mmps: s, yaw_rate_mrad_s: y, gear: this.gear });
    }

    // Brake request 0x301 (10 Hz) — only when braking
    if (this.brakeKpa > 0 && this.tickCount % 10 === 0) {
      const k = Math.round(this.brakeKpa);
      this.emit("high", ID_HOST_BRAKE_REQ, "HOST_BRAKE_REQ", { brake_pressure_kpa: k });
    }

    // Heartbeat 0x7FC (2 Hz)
    if (this.tickCount % 50 === 0) {
      this.emit("high", ID_HOST_HEARTBEAT, "HOST_HEARTBEAT", { alive_ctr: this.hbCounter, health_flags: 0 });
    }

    return [...this.frameQueue];
  }

  onFrame(callback: (frame: CanFrame) => void): void { this.callbacks.push(callback); }

  private emit(bus: "high"|"low", id: string, dlc: number, data: number[], name: string, decoded: Record<string, unknown>): void {
    const frame: CanFrame = {
      ts: Date.now()/1000,
      ts_us: "",
      seq: 0,
      bus,
      frame: { id, dlc, data, ext: false, rtr: false },
      decoded: { name, signals: decoded }
    };
    this.frameQueue.push(frame);
    for (const cb of this.callbacks) cb(frame);
  }
}
