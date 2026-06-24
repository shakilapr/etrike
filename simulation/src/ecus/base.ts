/**
 * Base interface for all simulated ECUs.
 *
 * Each ECU runs in a fixed order each tick:
 *   1. Receive frames delivered on each bus this tick
 *   2. Execute internal logic (state machines, controllers)
 *   3. Return frames to transmit on each bus
 */

import type { BusId, SimFrame, SimNodeId } from "../core/types.js";

export interface SimulationContext {
  nowMs: number;
  ticks: number;
  mode: "manual" | "auto" | "estop";
  estopActive: boolean;
  brakeLeverPressed: boolean;
}

export interface SimulatedEcu {
  /** Human-readable name. */
  readonly id: string;

  /** Which ECU node this is. */
  readonly nodeId: SimNodeId;

  /** Called once at simulation start. */
  init(): void;

  /**
   * Called every tick. The ECU receives all frames delivered on each
   * bus during this tick, runs its logic, and returns any frames it
   * wants to transmit (the runner will schedule them on the appropriate bus).
   */
  tick(
    nowMs: number,
    highBusRx: SimFrame[],
    lowBusRx: SimFrame[],
    ctx: SimulationContext,
  ): SimFrame[];

  /** Graceful shutdown. */
  shutdown(): void;
}
