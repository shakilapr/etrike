/**
 * Error types for simulation failures.
 */

export class SimulationError extends Error {
  constructor(
    message: string,
    public readonly timeMs: number,
  ) {
    super(`[t=${timeMs}ms] ${message}`);
    this.name = "SimulationError";
  }
}

export class SafetyViolationError extends SimulationError {
  constructor(
    message: string,
    timeMs: number,
    public readonly violationType: string,
  ) {
    super(message, timeMs);
    this.name = "SafetyViolationError";
  }
}

export class TimeoutError extends SimulationError {
  constructor(
    message: string,
    timeMs: number,
    public readonly timeoutMs: number,
  ) {
    super(message, timeMs);
    this.name = "TimeoutError";
  }
}

export class RangeError_ extends SimulationError {
  constructor(
    message: string,
    timeMs: number,
    public readonly signal: string,
    public readonly value: number,
    public readonly min: number,
    public readonly max: number,
  ) {
    super(message, timeMs);
    this.name = "RangeError";
  }
}
