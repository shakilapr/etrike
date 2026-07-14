/**
 * MTR Motor Controller — port of MTR STM32 logic.
 *
 * Receives 0x204 RT_DRIVE_CMD, writes DAC (MCP4725), controls gear relays,
 * produces 0x120 speed and 0x206 motor feedback.
 */

import { MAX_SPEED_FWD_MMPS, CMD_STALE_TIMEOUT_MS, STARTUP_GRACE_PERIOD_MS } from "../physics/tricycle.js";
import type { SimFrame } from "../core/types.js";
import { decodeAs, encodeSimFrame } from "../protocol.js";

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
  private initTimeMs = 0; // simulation-time reference, set on first tick or constructor (Gap #16)

  /** Called every tick. Returns frames to send. */
  tick(nowMs: number, rxFrames: SimFrame[], actualSpeedMmps: number, estop: boolean): SimFrame[] {
    this.estopActive = estop;
    const out: SimFrame[] = [];

    // Process incoming 0x204 (RT_DRIVE_CMD)
    for (const f of rxFrames) {
      const drive = decodeAs(f, "rt:rt_drive_cmd");
      if (drive !== undefined) {
        this.lastCmdMs = nowMs;
        if (!estop) {
          this.gear = Number(drive.gear);
          const speed = Number(drive.motor_speed_mmps);
          this.dacValue = Math.round((Math.abs(speed) / MAX_SPEED_FWD_MMPS) * 4095);
        }
      }
      if (decodeAs(f, "safety:safety_estop") !== undefined) {
        this.estopActive = true;
      }
    }

    // ESTOP: DAC=0, all relays off
    if (estop || this.estopActive) {
      this.dacValue = 0;
      this.gear = 0;
      this.faultFlags |= 1; // bit0=ESTOP
    }

    // Command staleness check (Gap #16: startup grace only when no command ever received)
    const neverReceivedCmd = this.lastCmdMs <= -Infinity;
    const inStartupGrace = neverReceivedCmd && (nowMs - this.initTimeMs) < STARTUP_GRACE_PERIOD_MS;
    if (!inStartupGrace && nowMs - this.lastCmdMs > CMD_STALE_TIMEOUT_MS) {
      this.dacValue = 0;
      this.gear = 0;
      this.faultFlags |= 2; // bit1=CMD_TIMEOUT
    } else {
      this.faultFlags &= ~2;
    }

    // 0x120 SYS_THROTTLE_STS at 100Hz (every 10ms)
    if (nowMs % 10 === 0) {
      out.push(encodeSimFrame("mtr:sys_throttle_sts", {
        speed_mmps: Math.round(actualSpeedMmps),
      }, "low", "mtr", nowMs));
    }

    // 0x206 MTR_MOTOR_FBK at 50Hz (every 20ms)
    if (nowMs % 20 === 0) {
      out.push(encodeSimFrame("mtr:mtr_motor_fbk", {
        actual_speed_mmps: Math.round(actualSpeedMmps),
        gear_state: this.gear,
        fault_flags: this.faultFlags,
      }, "low", "mtr", nowMs));
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
