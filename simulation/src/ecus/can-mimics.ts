/**
 * Stateful CAN Mimics
 *
 * Implements behavior for Host, RT, SYS, SES, SEB, MTR, and PWT nodes
 * capable of full stateful responses to test cases and fuzzing.
 */

import type { SimFrame, SimNodeId } from "../core/types.js";

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
      out.push({
        simTimeMs: nowMs, bus: "high", canId: "0x7FC", name: "HOST_HEARTBEAT",
        dlc: 1, data: [this.state.heartbeatCtr], sender: this.nodeId
      });
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
      if (f.canId === "0x7B9") {
        const mode = (f.data[0] >> 2) & 1; // 0=Stroke, 1=Pressure
        if (mode === 0) {
          const strokeReq = f.data[2] | (f.data[3] << 8);
          // Simple smoothing simulation towards target
          this.currentStrokeRaw += Math.sign(strokeReq - this.currentStrokeRaw) * Math.min(10, Math.abs(strokeReq - this.currentStrokeRaw));
        }
      }
    }

    if (nowMs % 20 === 0) {
      this.state.heartbeatCtr = (this.state.heartbeatCtr + 1) & 0xFF;
      out.push({
        simTimeMs: nowMs, bus: "low", canId: "0x721", name: "SEB_STATUS",
        dlc: 8, data: [
          1, // aligned
          0,
          this.currentStrokeRaw & 0xFF,
          (this.currentStrokeRaw >> 8) & 0xFF,
          0, 0,
          this.state.heartbeatCtr,
          0 // Checksum placeholder
        ], sender: this.nodeId
      });
    }
    return out;
  }
}

// TODO: Implement mimics for RT, SYS, SES, MTR, PWT.
