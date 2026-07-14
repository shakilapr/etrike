import { ID_RT_HEARTBEAT, ID_SAFETY_ESTOP, ID_RT_BRAKE_CMD, ID_SYS_MODE_CMD, ID_SYS_SAFETY_STS, ID_SYS_DIAG_RPT, ID_VCU_SEB_REQ, ID_SYS_HEARTBEAT } from "@etrike/debug-shared";
/**
 * SYS Safety/Body model — TypeScript implementation.
 * Monitors heartbeats, generates safety status (0x011), diagnostics (0x600),
 * heartbeat (0x7FE), and mode commands.
 */
import { decoder } from "@etrike/debug-shared";
import type { CanFrame } from "../../types/can";
import type { EcuModel, EcuConfig, EcuState } from "../ecu-model";

export class SysModel implements EcuModel {
  readonly id = "sys";

  private rtHbAlive = false;
  private estopActive = false;
  private mode = 0;
  private hbCounter = 0;
  private latestBrakeKpa = 0;
  private sebRoll = 0;
  private callbacks: Array<(frame: CanFrame) => void> = [];
  private frameQueue: CanFrame[] = [];
  private tickCount = 0;

  config(_params: EcuConfig): void {}
  start(): void { this.mode = 0; this.estopActive = false; }
  stop(): void {}
  state(): EcuState { return { ecu: this.id, mode: ["MANUAL","AUTO","ESTOP"][this.mode], healthy: true, uptimeMs: 0 }; }

  ingest(frame: CanFrame): void {
    if (frame.frame.id === ID_RT_HEARTBEAT) { this.rtHbAlive = true; }
    if (frame.frame.id === ID_SAFETY_ESTOP) { this.estopActive = true; this.latestBrakeKpa = 5000; }
    if (frame.frame.id === ID_RT_BRAKE_CMD) {
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      this.latestBrakeKpa = (d.brake_pressure_kpa as number) ?? 0;
    }
    if (frame.frame.id === ID_SYS_MODE_CMD) {
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
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
      this.emit("low", ID_SYS_SAFETY_STS, "SYS_SAFETY_STS", { estop_active: this.estopActive, heartbeat_ok: this.rtHbAlive });
      this.rtHbAlive = false; // reset each cycle
    }

    // Diagnostics 0x600 (1 Hz)
    if (this.tickCount % 100 === 0) {
      this.emit("low", ID_SYS_DIAG_RPT, "SYS_DIAG_RPT", { SYS_DiagMode: this.mode, hb_ok: this.rtHbAlive, SYS_DiagEstopActive: this.estopActive });
    }

    // Brake forwarding to SEB 0x7B9 (50 Hz) — only when braking
    if (this.latestBrakeKpa > 0 && this.hbCounter % 2 === 0 && !this.estopActive) {
      this.sebRoll = (this.sebRoll + 1) & 0x0F;
      const stroke = Math.round(this.latestBrakeKpa / 10);
      const data = [1 | (1 << 1), 0, stroke & 0xFF, (stroke >> 8) & 0xFF, 0, 0, 0, 0];
      data[6] = 1 | (1 << 1) | (this.sebRoll << 4);
      let cksum = 0; for (let i = 0; i < 7; i++) cksum ^= data[i]; data[7] = cksum ^ 0xFF;
      this.emit("low", ID_VCU_SEB_REQ, "VCU_SEB_REQ", {
        align_enable: true, control_enable: true, stroke_req: stroke,
        rolling_counter: this.sebRoll, checksum: data[7],
      });
    }

    // SYS heartbeat 0x7FE (10 Hz)
    if (this.tickCount % 10 === 0) {
      this.emit("low", ID_SYS_HEARTBEAT, "SYS_HEARTBEAT", { SYS_AliveCtr: this.hbCounter, health_flags: 0 });
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
