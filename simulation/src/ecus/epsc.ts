/**
 * Sbwc — simulated steer-by-wire unit steering actuator.
 *
 * Responds to 0x169 VCU_SES_REQ with 0x201 SES_STATUS.
 * Models first-order angle response with rate limiting.
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId } from "../core/types.js";
import { customRawSimFrame, decodeAs } from "../protocol.js";

export class Sbwc implements SimulatedEcu {
  readonly id = "steer-by-wire unit";
  readonly nodeId: SimNodeId = "epsc";

  private actualAngle = 30000;  // 0.1° units, 30000 = 0°
  private aligned = true;
  private lastCmdMs = -Infinity;
  private errorStatus = 0;      // 0=Normal, 1=L1, 2=L2, 3=L3
  private swVersion = 0x64;     // 1.00
  private hwVersion = 0x0D;     // 1.3
  private startupMs: number | null = null;
  private hasSeenCommand = false;

  /** Startup grace: tolerate missing 0x169 while RT completes boot/sync. */
  private static readonly EPSC_STARTUP_GRACE_MS = 1000;

  /** Set the actual steering angle from the plant. */
  setActualAngle(deg: number): void {
    // Convert degrees to 0.1° units with offset 30000
    this.actualAngle = Math.round(deg * 10 + 30000);
  }

  init(): void {
    this.actualAngle = 30000;
    this.aligned = true;
    this.lastCmdMs = -Infinity;
    this.errorStatus = 0;
    this.startupMs = null;
    this.hasSeenCommand = false;
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
      if (decodeAs(f, "ses:vcu_ses_req") !== undefined) {
        this.lastCmdMs = nowMs;
        this.hasSeenCommand = true;
      }
    }

    if (this.startupMs === null) {
      this.startupMs = nowMs;
    }
    const timeSinceStartup = nowMs - this.startupMs;

    // Comm timeout: no 0x169 for >20ms → L3 error.
    if (nowMs - this.lastCmdMs > 20 && (this.hasSeenCommand || timeSinceStartup > Sbwc.EPSC_STARTUP_GRACE_MS)) {
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

      out.push(customRawSimFrame("ses:ses_status", data, "low", "epsc", nowMs));
    }

    // 0x202 SES_ErrInfo at 10Hz (24 fault bits in LE byte order)
    if (nowMs % 100 === 0) {
      // All faults clear during normal operation.
      // When errorStatus===L3, set critical fault bits:
      //   byte1 bit0: AngleP_OC (L3), byte1 bit1: AngleP_AF (L3)
      const isL3 = this.errorStatus === 3;
      out.push(customRawSimFrame("ses:ses_err_info", [
          isL3 ? 0x03 : 0,  // byte0: ECU_UnderVolt|ECU_OverVolt if L3
          isL3 ? 0x03 : 0,  // byte1: AngleP_OC|AngleP_AF if L3
          0, 0, 0, 0, 0, 0,
        ], "low", "epsc", nowMs));
    }

    // 0x203 SES_Version at 1Hz (DLC=8, padded)
    if (nowMs % 1000 === 0) {
      out.push(customRawSimFrame(
        "ses:ses_version",
        [this.swVersion, this.hwVersion, 0, 0, 0, 0, 0, 0],
        "low", "epsc", nowMs,
      ));
    }

    // 0x6FA SES_Test at 100Hz (telemetry: motor current, ECU temp, voltage)
    if (nowMs % 10 === 0) {
      // Realistic idle values: motor_current=0A, ecu_temp=25°C, voltage=12V
      const motorCurrent = 0; // A, scaled: 0 / 0.0078125 = 0
      const ecuTemp = Math.round(25 / 0.5); // 25°C, scaled: 25/0.5 = 50 = 0x32
      const powVolt = Math.round(12 / 0.00390625); // 12V, scaled: 12/0.00390625 = 3072 = 0x0C00
      out.push(customRawSimFrame("ses:ses_test", [
          0,  // byte 0: reserved
          motorCurrent & 0xFF, (motorCurrent >> 8) & 0xFF,  // bytes 1-2: motor_current i16 LE
          ecuTemp & 0xFF, (ecuTemp >> 8) & 0xFF,  // bytes 3-4: ecu_temp u16 LE
          powVolt & 0xFF, (powVolt >> 8) & 0xFF,  // bytes 5-6: power_voltage u16 LE
          0,  // byte 7: reserved
        ], "low", "epsc", nowMs));
    }

    return out;
  }
}
