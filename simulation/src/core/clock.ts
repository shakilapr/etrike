/**
 * SimulationClock — tick-based time model.
 *
 * Three modes:
 *   - Step (speed=0): advance by exact ticks for unit tests
 *   - Real-time (speed=1): 1ms wall-clock = 1ms simulated
 *   - Fast-forward (speed≥100): run 60s simulated in <1s wall-clock
 */
export class SimulationClock {
  /** Current simulated time in milliseconds. */
  nowMs = 0;

  /** Tick counter — increments once per tick. */
  ticks = 0;

  /** Speed multiplier. 0 = step mode, 1 = real-time, >1 = fast-forward. */
  speed: number;

  /** When true, tick() will stop advancing. */
  paused = false;

  constructor(speed = 0) {
    this.speed = speed;
  }

  /** Advance the clock by one tick (1 ms simulated time). */
  tick(): void {
    if (this.paused) return;
    this.nowMs += 1;
    this.ticks += 1;
  }

  /** Advance by `dtMs` ticks. Useful for batch-stepping in step mode. */
  step(dtMs: number): void {
    for (let i = 0; i < dtMs; i++) {
      this.tick();
    }
  }

  /** Reset to t=0. */
  reset(): void {
    this.nowMs = 0;
    this.ticks = 0;
    this.paused = false;
  }

  /** Current time in seconds (convenience). */
  get nowSec(): number {
    return this.nowMs / 1000;
  }
}
