/**
 * MTR Motor Controller — port of MTR STM32 logic.
 *
 * Receives 0x204 RT_DRIVE_CMD, writes DAC (MCP4725), controls gear relays,
 * produces 0x120 speed and 0x206 motor feedback.
 */

import { MAX_SPEED_FWD_MMPS, CMD_STALE_TIMEOUT_MS, STARTUP_GRACE_PERIOD_MS } from "../physics/tricycle.js";
import type { SimFrame, BusId } from "../core/types.js";

export interface MtrState {
  dacValue: number;
  gear: number;           // 0=N, 1=D, 2=S, 3=R
  faultFlags: number;     // bit0=ESTOP, bit1=CMD_TO, bit2=ADC_FAULT, bit3=GEAR_CONFLICT
  actualSpeedMmps: number;
}

export class MtrMotorController {
  private dacValue = 0;
  private gear = 0;               // 0=N
  private faultFlags = 0;
  private lastCmdMs = -Infinity;
  private estopActive = false;

  /** Called every tick. Returns frames to send. */
  tick(nowMs: number, rxFrames: SimFrame[], actualSpeedMmps: number, estop: boolean): SimFrame[] {
    this.estopActive = estop;
    const out: SimFrame[] = [];

    // Process incoming 0x204 (RT_DRIVE_CMD)
    for (const f of rxFrames) {
      if (f.canId === "0x204") {
        this.lastCmdMs = nowMs;
        if (!estop) {
          this.gear = f.data[4] ?? 0;
          const speed = (f.data[0] << 24 | f.data[1] << 16 | f.data[2] << 8 | f.data[3]) >> 0;
          this.dacValue = Math.round((Math.abs(speed) / MAX_SPEED_FWD_MMPS) * 4095);
        }
      }
      if (f.canId === "0x001") {
        this.estopActive = true;
      }
    }

    // ESTOP: DAC=0, all relays off
    if (estop || this.estopActive) {
      this.dacValue = 0;
      this.gear = 0;
      this.faultFlags |= 1; // bit0=ESTOP
    }

    // Command staleness check (Gap #16: masked during startup grace)
    if (nowMs >= STARTUP_GRACE_PERIOD_MS && nowMs - this.lastCmdMs > CMD_STALE_TIMEOUT_MS) {
      this.dacValue = 0;
      this.gear = 0;
      this.faultFlags |= 2; // bit1=CMD_TIMEOUT
    } else {
      this.faultFlags &= ~2;
    }

    // 0x120 SYS_THROTTLE_STS at 100Hz (every 10ms)
    if (nowMs % 10 === 0) {
      const speedBuf = new Array<number>(2);
      const speed16 = Math.round(actualSpeedMmps) & 0xFFFF;
      speedBuf[0] = (speed16 >> 8) & 0xFF;
      speedBuf[1] = speed16 & 0xFF;
      out.push({
        simTimeMs: nowMs,
        bus: "low",
        canId: "0x120",
        name: "SYS_THROTTLE_STS",
        dlc: 2,
        data: speedBuf,
        sender: "mtr",
      });
    }

    // 0x206 MTR_MOTOR_FBK at 50Hz (every 20ms)
    if (nowMs % 20 === 0) {
      const actual16 = Math.round(actualSpeedMmps) & 0xFFFF;
      out.push({
        simTimeMs: nowMs,
        bus: "low",
        canId: "0x206",
        name: "MTR_MOTOR_FBK",
        dlc: 4,
        data: [
          (actual16 >> 8) & 0xFF,
          actual16 & 0xFF,
          this.gear & 0xFF,
          this.faultFlags & 0xFF,
        ],
        sender: "mtr",
      });
    }

    return out;
  }

  getState(): MtrState {
    return {
      dacValue: this.dacValue,
      gear: this.gear,
      faultFlags: this.faultFlags,
      actualSpeedMmps: 0,
    };
  }

  reset(): void {
    this.dacValue = 0;
    this.gear = 0;
    this.faultFlags = 0;
    this.lastCmdMs = -Infinity;
    this.estopActive = false;
  }
}
