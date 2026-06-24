/**
 * SyntreeEpsc — simulated SYNTREE EPS-C steering actuator.
 *
 * Responds to 0x169 VCU_SES_REQ with 0x201 SES_STATUS.
 * Models first-order angle response with rate limiting.
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId } from "../core/types.js";

export class SyntreeEpsc implements SimulatedEcu {
  readonly id = "SYNTREE EPS-C";
  readonly nodeId: SimNodeId = "epsc";

  private actualAngle = 30000;  // 0.1° units, 30000 = 0°
  private aligned = true;
  private lastCmdMs = -Infinity;
  private errorStatus = 0;      // 0=Normal, 1=L1, 2=L2, 3=L3
  private swVersion = 0x64;     // 1.00
  private hwVersion = 0x0D;     // 1.3

  /** Set the actual steering angle from the plant. */
  setActualAngle(deg: number): void {
    // Convert degrees to 0.1° units with offset 3000
    this.actualAngle = Math.round(deg * 10 + 3000);
  }

  init(): void {
    this.actualAngle = 30000;
    this.aligned = true;
    this.errorStatus = 0;
  }

  shutdown(): void {
    // nothing
  }

  tick(
    nowMs: number,
    _highBusRx: SimFrame[],
    lowBusRx: SimFrame[],
    _ctx: SimulationContext,
  ): SimFrame[] {
    const out: SimFrame[] = [];

    // Process incoming 0x169
    for (const f of lowBusRx) {
      if (f.canId === "0x169") {
        this.lastCmdMs = nowMs;
      }
    }

    // Comm timeout: no 0x169 for >20ms → L3 error
    if (nowMs - this.lastCmdMs > 20) {
      this.errorStatus = 3;
    } else {
      this.errorStatus = 0;
    }

    // 0x201 SES_STATUS at 100Hz (every 10ms)
    if (nowMs % 10 === 0) {
      const angle16 = this.actualAngle & 0xFFFF;
      const statusByte = (this.aligned ? 1 : 0)
        | (1 << 1)  // control_mode=Automatic
        | (this.errorStatus << 6);
      const data = [
        statusByte,
        0,
        angle16 & 0xFF,
        (angle16 >> 8) & 0xFF,
        0, 0, // angle speed feedback
        0,    // torque
        0,    // checksum placeholder
      ];
      // Simple XOR checksum
      let cksum = 0;
      for (let i = 0; i < 7; i++) cksum ^= data[i];
      data[7] = cksum ^ 0xFF;

      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x201", name: "SES_STATUS",
        dlc: 8, data, sender: "epsc",
      });
    }

    // 0x202 SES_ErrInfo at 10Hz
    if (nowMs % 100 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x202", name: "SES_ErrInfo",
        dlc: 8, data: [0, 0, 0, 0, 0, 0, 0, 0], sender: "epsc",
      });
    }

    // 0x203 SES_Version at 1Hz (DLC=8, padded)
    if (nowMs % 1000 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x203", name: "SES_Version",
        dlc: 8, data: [this.swVersion, this.hwVersion, 0, 0, 0, 0, 0, 0], sender: "epsc",
      });
    }

    // 0x6FA SES_Test at 100Hz
    if (nowMs % 10 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x6FA", name: "SES_Test",
        dlc: 8, data: [0, 0, 0, 0, 0, 0, 0, 0], sender: "epsc",
      });
    }

    return out;
  }
}
