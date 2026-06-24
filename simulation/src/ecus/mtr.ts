/**
 * MtrEcu — simulated MTR STM32 motor controller.
 *
 * Receives 0x204 on low bus, controls DAC/gear, produces 0x120/0x206 feedback.
 */

import type { SimulatedEcu, SimulationContext } from "./base.js";
import type { SimFrame, SimNodeId } from "../core/types.js";
import { MtrMotorController } from "../controllers/mtr-motor.js";

export class MtrEcu implements SimulatedEcu {
  readonly id = "MTR STM32";
  readonly nodeId: SimNodeId = "mtr";

  private controller = new MtrMotorController();
  private actualSpeedMmps = 0;

  init(): void {
    this.controller.reset();
  }

  shutdown(): void {
    // nothing
  }

  /** Called by simulation runner to feed actual plant speed. */
  setActualSpeed(mmps: number): void {
    this.actualSpeedMmps = mmps;
  }

  tick(
    nowMs: number,
    _highBusRx: SimFrame[],
    lowBusRx: SimFrame[],
    ctx: SimulationContext,
  ): SimFrame[] {
    return this.controller.tick(nowMs, lowBusRx, this.actualSpeedMmps, ctx.estopActive);
  }
}
