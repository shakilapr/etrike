/**
 * SyntreeSeb — simulated SYNTREE SEB brake actuator.
 *
 * Responds to 0x7B9 VCU_SEB_REQ with 0x721 SEB_STATUS.
 * Models first-order stroke/pressure response.
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId } from "../core/types.js";

export class SyntreeSeb implements SimulatedEcu {
  readonly id = "SYNTREE SEB";
  readonly nodeId: SimNodeId = "seb";

  private actualStroke = 600;   // raw units, 600 = 0mm
  private aligned = true;
  private lastCmdMs = -Infinity;
  private errorStatus = 0;
  private swVersion = 0x64;
  private hwVersion = 0x0D;

  /** Set actual brake stroke from plant (in mm, 0–27). */
  setActualStroke(mm: number): void {
    // Convert mm to raw: (mm + 30) / 0.05
    this.actualStroke = Math.round((mm + 30) / 0.05);
  }

  init(): void {
    this.actualStroke = 600;
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

    for (const f of lowBusRx) {
      if (f.canId === "0x7B9") {
        this.lastCmdMs = nowMs;
      }
    }

    // Comm timeout: no 0x7B9 for >20ms → L3 error
    if (nowMs - this.lastCmdMs > 20) {
      this.errorStatus = 3;
    } else {
      this.errorStatus = 0;
    }

    // 0x721 SEB_STATUS at 100Hz
    if (nowMs % 10 === 0) {
      const stroke16 = this.actualStroke & 0xFFFF;
      const statusByte = (this.aligned ? 1 : 0)
        | (1 << 1)  // control_enable
        | (this.errorStatus << 6);
      const data = [
        statusByte,
        0,
        stroke16 & 0xFF,
        (stroke16 >> 8) & 0xFF,
        0, 0, // pressure, angle
        0,    // checksum placeholder
      ];
      let cksum = 0;
      for (let i = 0; i < 7; i++) cksum ^= data[i];
      data[7] = cksum ^ 0xFF;

      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x721", name: "SEB_STATUS",
        dlc: 8, data, sender: "seb",
      });
    }

    // 0x731 SEB_ErrInfo at 10Hz
    if (nowMs % 100 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x731", name: "SEB_ErrInfo",
        dlc: 8, data: [0, 0, 0, 0, 0, 0, 0, 0], sender: "seb",
      });
    }

    // 0x741 SEB_Version at 1Hz (DLC=8, padded)
    if (nowMs % 1000 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x741", name: "SEB_Version",
        dlc: 8, data: [this.swVersion, this.hwVersion, 0, 0, 0, 0, 0, 0], sender: "seb",
      });
    }

    // 0x6FB SEB_Test at 100Hz
    if (nowMs % 10 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x6FB", name: "SEB_Test",
        dlc: 8, data: [0, 0, 0, 0, 0, 0, 0, 0], sender: "seb",
      });
    }

    return out;
  }
}
