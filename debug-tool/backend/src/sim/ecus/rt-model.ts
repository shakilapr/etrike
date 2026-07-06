/**
 * RT Gateway model — TypeScript implementation.
 * Receives HOST commands (0x300, 0x301) on high bus, forwards to low bus
 * (0x204, 0x205). Generates state reports (0x210), heartbeat (0x7FD).
 */
import type { CanFrame } from "../../types/can";
import type { EcuModel, EcuConfig, EcuState } from "../ecu-model";

export class RtModel implements EcuModel {
  readonly id = "rt";

  private mode = 0;       // 0=MANUAL, 1=AUTO, 2=ESTOP
  private safety = 0;     // 0=Normal
  private estopPending = false;
  private hbCounter = 0;
  private callbacks: Array<(frame: CanFrame) => void> = [];
  private frameQueue: CanFrame[] = [];
  private bypassEpscSync = false;

  config(params: EcuConfig): void {
    this.bypassEpscSync = params.bypasses?.epscSync ?? false;
  }

  start(): void { this.mode = 0; this.safety = 0; this.estopPending = false; }
  stop(): void {}
  state(): EcuState { return { ecu: this.id, mode: ["MANUAL","AUTO","ESTOP"][this.mode], safety: ["Normal","InternalEstop","Fault"][this.safety], healthy: true, uptimeMs: 0 }; }

  ingest(frame: CanFrame): void {
    // ESTOP passthrough
    if (frame.id === "0x001") { this.estopPending = true; return; }

    // HOST drive command → forward to low bus
    if (frame.id === "0x300" && frame.bus === "high") {
      const d = frame.decoded as Record<string, unknown>;
      const speed = (d.speed_mmps as number) ?? 0;
      const gear = (d.gear as number) ?? 1;
      if (this.estopPending || this.mode === 2) {
        this.emit("low", "0x204", 5, [0,0,0,0,0], "RT_DRIVE_CMD", { motor_speed_mmps: 0, gear: 0 });
      } else {
        const data = [(speed>>24)&0xFF,(speed>>16)&0xFF,(speed>>8)&0xFF,speed&0xFF,gear];
        this.emit("low", "0x204", 5, data, "RT_DRIVE_CMD", { motor_speed_mmps: speed, gear });
      }
    }

    // HOST brake → forward
    if (frame.id === "0x301" && frame.bus === "high") {
      const d = frame.decoded as Record<string, unknown>;
      const kpa = (d.brake_pressure_kpa as number) ?? 0;
      const data = [(kpa>>24)&0xFF,(kpa>>16)&0xFF,(kpa>>8)&0xFF,kpa&0xFF];
      this.emit("low", "0x205", 4, data, "RT_BRAKE_CMD", { brake_pressure_kpa: kpa });
    }

    // Mode change
    if (frame.id === "0x110") {
      const d = frame.decoded as Record<string, unknown>;
      const m = (d.mode as number) ?? 0;
      if (m <= 1) { this.mode = m; this.estopPending = false; }
    }
  }

  tick(_dtMs: number): CanFrame[] {
    this.hbCounter = (this.hbCounter + 1) & 0xFF;
    this.frameQueue = [];

    // RT state report 0x210 (10 Hz → every 10th tick at 100Hz)
    if (this.hbCounter % 10 === 0) {
      this.emit("high", "0x210", 6, [this.mode, this.estopPending ? 1 : 0, 0, 0, 15, 5], "RT_STATE_RPT",
        { mode: this.mode, safety_state: this.estopPending ? 1 : 0, steer_state: this.bypassEpscSync ? 1 : (this.estopPending ? 4 : 1) });
    }

    // RT heartbeat 0x7FD (2 Hz → every 50th tick at 100Hz)
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
