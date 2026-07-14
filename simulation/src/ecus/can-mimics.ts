/**
 * Stateful CAN Mimics
 *
 * Implements behavior for Host, RT, SYS, SES, SEB, MTR, and PWT nodes
 * capable of full stateful responses to test cases and fuzzing.
 */

import type { SimFrame, SimNodeId } from "../core/types.js";
import { customRawSimFrame, decodeAs, encodeSimFrame } from "../protocol.js";

export interface MimicState {
  // Common state
  estopActive: boolean;
  heartbeatCtr: number;
}

export abstract class CanMimic {
  abstract readonly nodeId: SimNodeId;
  protected state: MimicState = { estopActive: false, heartbeatCtr: 0 };

  /** Reset internal state */
  reset(): void {
    this.state = { estopActive: false, heartbeatCtr: 0 };
  }

  /** Process incoming frames and return generated frames */
  abstract tick(nowMs: number, rxFrames: SimFrame[]): SimFrame[];

  /** Inject fault or override state directly */
  setEstop(active: boolean): void {
    this.state.estopActive = active;
  }
}

export class HostMimic extends CanMimic {
  readonly nodeId: SimNodeId = "host";
  
  tick(nowMs: number, rxFrames: SimFrame[]): SimFrame[] {
    const out: SimFrame[] = [];
    if (nowMs % 100 === 0) {
      this.state.heartbeatCtr = (this.state.heartbeatCtr + 1) & 0xFF;
      out.push(encodeSimFrame("host:host_heartbeat", {
        alive_ctr: this.state.heartbeatCtr,
        health_flags: 0,
      }, "high", this.nodeId, nowMs));
    }
    return out;
  }
}

export class SebMimic extends CanMimic {
  readonly nodeId: SimNodeId = "seb";
  private currentStrokeRaw = 600; // 0mm
  
  tick(nowMs: number, rxFrames: SimFrame[]): SimFrame[] {
    const out: SimFrame[] = [];
    
    // Process incoming 0x7B9
    for (const f of rxFrames) {
      const command = decodeAs(f, "seb:vcu_seb_req");
      if (command !== undefined) {
        const mode = Number(command.control_mode);
        if (mode === 0) {
          const strokeReq = Number(command.stroke_request_raw);
          // Simple smoothing simulation towards target
          this.currentStrokeRaw += Math.sign(strokeReq - this.currentStrokeRaw) * Math.min(10, Math.abs(strokeReq - this.currentStrokeRaw));
        }
      }
    }

    if (nowMs % 20 === 0) {
      this.state.heartbeatCtr = (this.state.heartbeatCtr + 1) & 0xFF;
      const data = [
          1, // aligned
          0,
          this.currentStrokeRaw & 0xFF,
          (this.currentStrokeRaw >> 8) & 0xFF,
          0, 0,
          0x03 | ((this.state.heartbeatCtr & 0x0F) << 4),
          0 // Checksum placeholder
        ];
      let checksum = 0;
      for (let index = 0; index < 7; index += 1) checksum ^= data[index];
      data[7] = checksum ^ 0xFF;
      out.push(customRawSimFrame("seb:seb_status", data, "low", this.nodeId, nowMs));
    }
    return out;
  }
}

// TODO: Implement mimics for RT, SYS, SES, MTR, PWT.
