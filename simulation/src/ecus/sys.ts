/**
 * SysEcu — simulated SYS ESP32-S3 (safety, brake, lights, diag, mode).
 *
 * Monitors safety, controls SEB brake via 0x7B9, manages mode transitions,
 * sends 0x011 safety status, 0x110 mode, 0x600 diag, 0x7FE heartbeat.
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId } from "../core/types.js";
import { SysSafetyMonitor } from "../controllers/sys-safety.js";
import { SysBrakeController, BrakeState } from "../controllers/sys-brake.js";

export class SysEcu implements SimulatedEcu {
  readonly id = "SYS ESP32-S3";
  readonly nodeId: SimNodeId = "sys";

  private safety = new SysSafetyMonitor();
  private brake = new SysBrakeController();
  private currentMode: "manual" | "auto" | "estop" = "manual";
  private sysHbCtr = 0;
  private cmdSpeedMmps = 0;
  private actualSpeedMmps = 0;
  private brakeKpa = 0;
  private lights = 0; // bitfield: turn left, turn right, brake, head
  private diagHeapKb = 500;
  private tec = 0;
  private rec = 0;
  private lastSentMode = -1;  // sentinel: first mode change always sends

  // ── Simulation inputs ────────────────────────────────────────────

  setEstopButton(pressed: boolean): void { this.safety.setEstop(pressed); }
  setBrakeLever(pressed: boolean): void { this.safety.setBrakeLever(pressed); }
  setActualSpeed(mmps: number): void { this.actualSpeedMmps = mmps; }

  init(): void {
    this.safety.reset();
    this.brake.reset();
  }

  shutdown(): void {
    // nothing
  }

  tick(
    nowMs: number,
    highBusRx: SimFrame[],
    lowBusRx: SimFrame[],
    ctx: SimulationContext,
  ): SimFrame[] {
    this.currentMode = ctx.mode;
    const out: SimFrame[] = [];
    const estopActive = ctx.estopActive || this.safety.estop;

    // ── Process low-bus frames ──────────────────────────────────
    for (const f of lowBusRx) {
      switch (f.canId) {
        case "0x7FD": {
          // RT_HEARTBEAT
          this.safety.feedHeartbeatRt(nowMs, f.data[0]);
          break;
        }
        case "0x204": {
          // RT_DRIVE_CMD
          this.cmdSpeedMmps = (f.data[0] << 24 | f.data[1] << 16 | f.data[2] << 8 | f.data[3]) >> 0;
          break;
        }
        case "0x205": {
          // RT_BRAKE_CMD
          this.brakeKpa = (f.data[0] << 24 | f.data[1] << 16 | f.data[2] << 8 | f.data[3]) >> 0;
          break;
        }
        case "0x206": {
          // MTR_MOTOR_FBK — for EGAS L2
          // i16 BE with sign extension: shift to bit 31 then arithmetic shift right
          this.actualSpeedMmps = (f.data[0] << 24 | f.data[1] << 16) >> 16;
          break;
        }
        case "0x731": {
          // SEB_ERR_INFO — L3 fault bits trigger ESTOP
          // Byte 0 bits 2-7, Byte 1 bits 0-3,5, Byte 2 bits 1,2,4,5,6
          const l3Active =
            (f.data[0] & 0xFC) !== 0 ||
            (f.data[1] & 0x2F) !== 0 ||
            (f.data[2] & 0x76) !== 0;
          if (l3Active) {
            this.safety.setEstop(true);
          }
          break;
        }
        case "0x721": {
          // SEB_STATUS — for brake sync
          this.brake.feedSebStatus(f.data[0]);
          break;
        }
        case "0x302": {
          // HOST_LIGHT_CMD (forwarded from Host via RT)
          this.lights = f.data[0];
          break;
        }
      }
    }

    // ── EGAS L2 check (every 20ms) ──────────────────────────────
    if (nowMs % 20 === 0) {
      const egasFault = this.safety.checkEgasL2(nowMs, this.cmdSpeedMmps, this.actualSpeedMmps);
      if (egasFault) {
        // EGAS L2 fault triggers ESTOP
        this.safety.setEstop(true);
      }
    }

    const effectiveEstop = estopActive || this.safety.estop;

    // ── Brake control (50 Hz) ────────────────────────────────────
    if (nowMs % 20 === 0) {
      const cmd = this.brake.tick(
        this.safety.brakeLever,
        effectiveEstop,
        effectiveEstop ? 0 : this.brakeKpa,
      );
      if (cmd) {
        // Build 0x7B9 VCU_SEB_REQ (SYNTREE LE encoding)
        const stroke16 = cmd.strokeReq & 0xFFFF;
        const data = [
          ((cmd.alignEnable & 1) | ((cmd.controlEnable & 1) << 1) | (cmd.controlMode << 2) | ((cmd.autoBrake & 1) << 3)),
          0,
          stroke16 & 0xFF,
          // Byte 3: Stroke mode → stroke high byte; Pressure mode → pressure_req
          cmd.controlMode === 1 ? (cmd.pressureReq & 0xFF) : ((stroke16 >> 8) & 0xFF),
          cmd.controlMode === 1 ? 0 : (cmd.pressureReq & 0xFF),
          0,
          (cmd.rollCntEnable ? 0x01 : 0)    // bit 0: RollCntEnable (was WRONG at 0x10=bit4)
          | (cmd.checksumEnable ? 0x02 : 0)   // bit 1: ChecksumEnable (was WRONG at 0x20=bit5)
          | (cmd.rollingCounter << 4),          // bits 4-7: rolling counter
          0, // checksum placeholder
        ];
        // Compute checksum: XOR(bytes 0-6) ^ 0xFF (per SYNTREE CSV spec)
        let cksum = 0;
        for (let i = 0; i < 7; i++) cksum ^= data[i];
        data[7] = cksum ^ 0xFF;

        out.push({
          simTimeMs: nowMs, bus: "low", canId: "0x7B9", name: "VCU_SEB_REQ",
          dlc: 8, data, sender: "sys",
        });
      }
    }

    // ── 0x011 SYS_SAFETY_STS (5 Hz) ─────────────────────────────
    if (nowMs % 200 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x011", name: "SYS_SAFETY_STS",
        dlc: 3, data: [
          effectiveEstop ? 1 : 0,
          this.safety.heartbeatOk(nowMs) ? 1 : 0,
          this.lights & 0x0F,  // v0.0.5: SYS_LightState (low 4 bits)
        ], sender: "sys",
      });
    }

    // ── 0x110 SYS_MODE_CMD (on change only) ──────────────────────
    const modeByte = ctx.mode === "auto" ? 1 : ctx.mode === "estop" ? 2 : 0;
    if (modeByte !== this.lastSentMode) {
      this.lastSentMode = modeByte;
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x110", name: "SYS_MODE_CMD",
        dlc: 1, data: [modeByte], sender: "sys",
      });
    }

    // ── 0x600 SYS_DIAG_RPT (1 Hz) ───────────────────────────────
    if (nowMs % 1000 === 0) {
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x600", name: "SYS_DIAG_RPT",
        dlc: 8, data: [
          ctx.mode === "auto" ? 1 : ctx.mode === "estop" ? 2 : 0,
          this.brake.state === BrakeState.ACTIVE ? 1 : 0,
          this.safety.heartbeatOk(nowMs) ? 1 : 0,
          effectiveEstop ? 1 : 0,
          (this.diagHeapKb >> 8) & 0xFF,
          this.diagHeapKb & 0xFF,
          this.tec,
          this.rec,
        ], sender: "sys",
      });
    }

    // ── 0x7FE SYS_HEARTBEAT (10 Hz) ─────────────────────────────
    if (nowMs % 100 === 0) {
      this.sysHbCtr = (this.sysHbCtr + 1) & 0xFF;
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x7FE", name: "SYS_HEARTBEAT",
        dlc: 1, data: [this.sysHbCtr], sender: "sys",
      });
    }

    return out;
  }
}
