/**
 * FaultInjector — scheduled fault injection during simulation.
 *
 * Supports: dropMessage, corruptMessage, freezeHeartbeat, triggerEstop,
 * setEstopGpio, setBrakeLever, setModeButton.
 */

import type { FaultSpec, FaultType, SimNodeId, BusId } from "../core/types.js";
import type { SimulationContext } from "../ecus/base.js";

export interface ActiveFault {
  type: FaultType;
  canId?: string;
  bus?: BusId;
  byteIndex?: number;
  xorMask?: number;
  target?: SimNodeId;
}

export class FaultInjector {
  private schedule: FaultSpec[] = [];
  private activeFaults: ActiveFault[] = [];

  /** Load fault schedule (sorted by atMs). */
  load(faults: FaultSpec[]): void {
    this.schedule = [...faults].sort((a, b) => a.atMs - b.atMs);
  }

  /**
   * Process the fault schedule for the current tick.
   * Returns any context mutations to apply.
   */
  tick(nowMs: number, ctx: SimulationContext): ContextMutation {
    const mutation: ContextMutation = {};

    // Activate any faults due at this timestamp (faults persist once activated)
    while (this.schedule.length > 0 && this.schedule[0].atMs <= nowMs) {
      const fault = this.schedule.shift()!;
      this.activeFaults.push({
        type: fault.type,
        canId: fault.canId,
        bus: fault.bus,
        byteIndex: fault.byteIndex,
        xorMask: fault.xorMask,
        target: fault.target,
      });

      switch (fault.type) {
        case "triggerEstop":
          mutation.estopActive = true;
          break;
        case "setEstopGpio":
          mutation.estopGpio = fault.pressed ?? true;
          break;
        case "setBrakeLever":
          mutation.brakeLever = fault.pressed ?? true;
          break;
        case "setModeButton":
          // Toggle mode in context
          mutation.mode = "auto";
          break;
      }
    }

    return mutation;
  }

  /** Whether a frame should be dropped this tick. */
  shouldDrop(canId: string, bus: BusId): boolean {
    return this.activeFaults.some(
      (f) => f.type === "dropMessage" && f.canId === canId && f.bus === bus,
    );
  }

  /** Corrupt a frame's data if a corrupt fault is active for it. */
  corrupt(canId: string, bus: BusId, data: number[]): number[] {
    const fault = this.activeFaults.find(
      (f) => f.type === "corruptMessage" && f.canId === canId && f.bus === bus,
    );
    if (!fault) return data;

    const corrupted = [...data];
    if (fault.byteIndex !== undefined && fault.byteIndex < corrupted.length) {
      corrupted[fault.byteIndex] ^= fault.xorMask ?? 0xFF;
    }
    return corrupted;
  }

  /** Whether a heartbeat from this node is frozen. */
  isHeartbeatFrozen(nodeId: SimNodeId): boolean {
    return this.activeFaults.some(
      (f) => f.type === "freezeHeartbeat" && f.target === nodeId,
    );
  }
}

export interface ContextMutation {
  estopActive?: boolean;
  estopGpio?: boolean;
  brakeLever?: boolean;
  mode?: "manual" | "auto" | "estop";
}
