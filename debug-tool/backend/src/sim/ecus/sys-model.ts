/**
 * SYS Safety/Body model — TypeScript implementation.
 * Monitors heartbeats, generates safety status (0x011), diagnostics (0x600),
 * heartbeat (0x7FE), and mode commands.
 */
import type { CanFrame } from "../../types/can";
import type { EcuModel, EcuConfig, EcuState } from "../ecu-model";

export class SysModel implements EcuModel {
  readonly id = "sys";

  private rtHbAlive = false;
  private estopActive = false;
  private mode = 0;
  private hbCounter = 0;
  private callbacks: Array<(frame: CanFrame) => void> = [];
  private frameQueue: CanFrame[] = [];
  private tickCount = 0;

  config(_params: EcuConfig): void {}
  start(): void { this.mode = 0; this.estopActive = false; }
  stop(): void {}
  state(): EcuState { return { ecu: this.id, mode: ["MANUAL","AUTO","ESTOP"][this.mode], healthy: true, uptimeMs: 0 }; }

  ingest(frame: CanFrame): void {
    if (frame.id === "0x7FD") { this.rtHbAlive = true; }
    if (frame.id === "0x001") { this.estopActive = true; }
    if (frame.id === "0x110") {
      const d = frame.decoded as Record<string, unknown>;
      const m = (d.mode as number) ?? 0;
      if (m <= 1) { this.mode = m; this.estopActive = false; }
    }
  }

  tick(_dtMs: number): CanFrame[] {
    this.tickCount++;
    this.frameQueue = [];
    this.hbCounter = (this.hbCounter + 1) & 0xFF;

    // Safety status 0x011 (5 Hz)
    if (this.tickCount % 20 === 0) {
      this.emit("low", "0x011", 3, [this.estopActive ? 1 : 0, this.rtHbAlive ? 1 : 0, 0],
        "SYS_SAFETY_STS", { estop_active: this.estopActive, heartbeat_ok: this.rtHbAlive });
      this.rtHbAlive = false; // reset each cycle
    }

    // Diagnostics 0x600 (1 Hz)
    if (this.tickCount % 100 === 0) {
      this.emit("low", "0x600", 8, [this.mode, 0, this.rtHbAlive ? 1 : 0, this.estopActive ? 1 : 0, 0, 0, 0, 0],
        "SYS_DIAG_RPT", { mode: this.mode, hb_ok: this.rtHbAlive, estop_active: this.estopActive });
    }

    // SYS heartbeat 0x7FE (10 Hz)
    if (this.tickCount % 10 === 0) {
      this.emit("low", "0x7FE", 2, [this.hbCounter, 0], "SYS_HEARTBEAT",
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
