import { ID_VCU_SES_REQ, ID_SES_STATUS, ID_SES_ErrInfo, ID_SES_Version, ID_SES_Test } from "@etrike/debug-shared";
/**
 * SES steering actuator model.
 * Receives 0x169 VCU_SES_REQ, generates 0x201 SES_STATUS + 0x202/0x203/0x6FA.
 * Models first-order angle tracking with rate limiting.
 */
import { decoder } from "@etrike/debug-shared";
import type { CanFrame } from "../../types/can";
import type { EcuModel, EcuConfig, EcuState } from "../ecu-model";

export class SesModel implements EcuModel {
  readonly id = "ses";

  private angle = 0;       // signed 0.1 deg units
  private targetAngle = 0;
  private aligned = true;
  private errorStatus = 0; // 0=Normal, 1=L1, 2=L2, 3=L3
  private lastCmdMs = 0;
  private swVer = 0x64;    // 1.00
  private hwVer = 0x0D;    // 1.3
  private roll = 0;
  private tickMs = 0;
  private callbacks: Array<(frame: CanFrame) => void> = [];
  private frameQueue: CanFrame[] = [];

  config(_p: EcuConfig): void {}
  start(): void { this.angle = 0; this.targetAngle = 0; this.aligned = true; this.errorStatus = 0; }
  stop(): void {}
  state(): EcuState { return { ecu: this.id, healthy: this.errorStatus < 3, faultFlags: this.errorStatus, uptimeMs: this.tickMs }; }

  ingest(frame: CanFrame): void {
    if (frame.frame.id === ID_VCU_SES_REQ && frame.bus === "low") {
      this.lastCmdMs = this.tickMs;
      const d = (frame.decoded?.signals ?? {}) as Record<string, unknown>;
      this.targetAngle = (d.target_angle as number) ?? 0;
      this.aligned = d.alignment_enable === true || d.alignment_enable === 1;
    }
  }

  tick(dtMs: number): CanFrame[] {
    this.tickMs += dtMs;
    this.frameQueue = [];

    // Comm timeout: no 0x169 for >30ms -> L3
    if (this.tickMs - this.lastCmdMs > 30) this.errorStatus = 3;
    else if (this.errorStatus === 3) this.errorStatus = 0;

    // First-order angle tracking
    const rate = 0.3;
    this.angle = Math.round(this.angle + (this.targetAngle - this.angle) * rate);

    // 0x201 SES_STATUS at 100Hz (every 10ms)
    if (this.tickMs % 10 === 0) {
      const a16 = this.angle & 0xFFFF;
      const statusByte = (this.aligned ? 1 : 0) | (1 << 1) | (this.errorStatus << 6);
      const data = [statusByte, 0, a16 & 0xFF, (a16 >> 8) & 0xFF, 0, 0, 0, 0];
      this.roll = (this.roll + 1) & 0x0F;
      data[6] = 1 | (1 << 1) | (this.roll << 4);
      let cksum = 0; for (let i = 0; i < 7; i++) cksum ^= data[i];
      data[7] = cksum ^ 0xFF;
      this.emit("low", ID_SES_STATUS, "SES_STATUS", {
        angle_status: this.aligned, str_angle: this.angle, error_status: this.errorStatus,
        rolling_counter: this.roll, checksum: data[7],
      });
    }

    // 0x202 SES_ErrInfo at 10Hz
    if (this.tickMs % 100 === 0) {
      const isL3 = this.errorStatus === 3;
      this.emit("low", ID_SES_ErrInfo, "SES_ErrInfo", { fault_mask: isL3 ? 0x303 : 0, l3_fault: isL3 });
    }

    // 0x203 SES_Version at 1Hz
    if (this.tickMs % 1000 === 0) {
      this.emit("low", ID_SES_Version, "SES_Version", { sw_version: this.swVer, hw_version: this.hwVer });
    }

    // 0x6FA SES_Test at 100Hz
    if (this.tickMs % 10 === 0) {
      const mc = 0; const temp = Math.round(25 / 0.5); const volt = Math.round(12 / 0.00390625);
      this.emit("low", ID_SES_Test, "SES_TEST", { motor_current: mc, ecu_temp: 25, supply_voltage: 12 });
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
