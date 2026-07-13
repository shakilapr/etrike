import { ID_VCU_SEB_REQ, ID_SAFETY_ESTOP, ID_SEB_STATUS, ID_SEB_ErrInfo, ID_SEB_Version, ID_SEB_Test } from "@etrike/debug-shared";
/**
 * SEB Brake-by-wire actuator model.
 * Receives 0x7B9 VCU_SEB_REQ, generates 0x721 SEB_STATUS + 0x731/0x741/0x6FB.
 */
import { decoder } from "@etrike/debug-shared";
import type { CanFrame } from "../../types/can";
import type { EcuModel, EcuConfig, EcuState } from "../ecu-model";

export class SebModel implements EcuModel {
  readonly id = "seb";

  private stroke = 600;    // raw, 600 = 0mm
  private targetStroke = 600;
  private aligned = true;
  private errorStatus = 0;
  private lastCmdMs = 0;
  private swVer = 0xC8;    // v2.00
  private hwVer = 0x0D;
  private roll = 0;
  private tickMs = 0;
  private callbacks: Array<(frame: CanFrame) => void> = [];
  private frameQueue: CanFrame[] = [];

  config(_p: EcuConfig): void {}
  start(): void { this.stroke = 600; this.targetStroke = 600; this.aligned = true; this.errorStatus = 0; }
  stop(): void {}
  state(): EcuState { return { ecu: this.id, healthy: this.errorStatus < 3, faultFlags: this.errorStatus, uptimeMs: this.tickMs }; }

  ingest(frame: CanFrame): void {
    if (frame.frame.id === ID_VCU_SEB_REQ && frame.bus === "low") {
      this.lastCmdMs = this.tickMs;
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      this.targetStroke = (d.stroke_req as number) ?? 600;
      this.aligned = (d.align_enable as number) === 1;
    }
    if (frame.frame.id === ID_SAFETY_ESTOP) { this.errorStatus = 3; this.targetStroke = 600; }
  }

  tick(dtMs: number): CanFrame[] {
    this.tickMs += dtMs;
    this.frameQueue = [];

    // Comm timeout: no 0x7B9 for >20ms -> L3
    if (this.tickMs - this.lastCmdMs > 20) this.errorStatus = 3;
    else if (this.errorStatus === 3) this.errorStatus = 0;

    // First-order stroke tracking
    const rate = 0.2;
    this.stroke = Math.round(this.stroke + (this.targetStroke - this.stroke) * rate);

    // 0x721 SEB_STATUS at 100Hz
    if (this.tickMs % 10 === 0) {
      const s16 = this.stroke & 0xFFFF;
      const pressure = this.errorStatus === 3 ? 0 : Math.round(this.stroke / 10);
      const statusByte = (this.aligned ? 1 : 0) | (1 << 1) | (this.errorStatus << 6);
      this.roll = (this.roll + 1) & 0x0F;
      const data = [statusByte, 0, s16 & 0xFF, (s16 >> 8) & 0xFF, pressure & 0xFF, 0, 0, 0];
      data[6] = 1 | (1 << 1) | (this.roll << 4);
      let cksum = 0; for (let i = 0; i < 7; i++) cksum ^= data[i];
      data[7] = cksum ^ 0xFF;
      this.emit("low", ID_SEB_STATUS, "SEB_STATUS", {
        alignment_status: this.aligned, stroke_value: s16, pressure_value: pressure,
        error_status: this.errorStatus, rolling_counter: this.roll, checksum: data[7],
      });
    }

    // 0x731 SEB_ErrInfo at 10Hz
    if (this.tickMs % 100 === 0) {
      const isL3 = this.errorStatus === 3;
      this.emit("low", ID_SEB_ErrInfo, "SEB_ErrInfo", { fault_mask: isL3 ? 0x762FFC : 0, l3_fault: isL3 });
    }

    // 0x741 SEB_Version at 1Hz
    if (this.tickMs % 1000 === 0) {
      this.emit("low", ID_SEB_Version, "SEB_VERSION", { sw_version: this.swVer, hw_version: this.hwVer });
    }

    // 0x6FB SEB_Test at 100Hz
    if (this.tickMs % 10 === 0) {
      const mc = 0; const temp = Math.round(25 / 0.5); const volt = Math.round(12 / 0.00390625);
      this.emit("low", ID_SEB_Test, "SEB_TEST", { motor_current: mc, ecu_temp: 25, supply_voltage: 12 });
    }

    return [...this.frameQueue];
  }

  onFrame(cb: (f: CanFrame) => void): void { this.callbacks.push(cb); }

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
