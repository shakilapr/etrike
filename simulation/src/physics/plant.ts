/**
 * VehiclePlant — simple kinematic vehicle model.
 *
 * Integrates speed, steering, and brake inputs over time. Provides
 * "truth" values that feed back into CAN messages (0x120 speed,
 * 0x201 steering angle, 0x721 brake stroke).
 *
 * Port of tricycle bicycle model with first-order steering lag.
 */

import { WHEELBASE_MM, MAX_SPEED_FWD_MMPS, MAX_SPEED_REV_MMPS } from "./tricycle.js";

export interface PlantState {
  /** Position in world frame (mm). */
  xMm: number;
  yMm: number;
  /** Heading (radians, 0 = +x). */
  headingRad: number;
  /** Current speed (mm/s). */
  speedMmps: number;
  /** Current steering angle (degrees, +right). */
  steerAngleDeg: number;
  /** Current brake stroke (mm). */
  brakeStrokeMm: number;
}

export interface PlantConfig {
  wheelbaseMm?: number;
  maxSpeedMmps?: number;
  maxSteeringDeg?: number;
  /** Steering first-order lag time constant (ms). */
  steerLagMs?: number;
  /** Brake deceleration per mm stroke (mm/s² per mm). */
  brakeDecelMmps2PerMm?: number;
}

const DEFAULTS: Required<PlantConfig> = {
  wheelbaseMm: WHEELBASE_MM,
  maxSpeedMmps: MAX_SPEED_FWD_MMPS,
  maxSteeringDeg: 40,
  steerLagMs: 50,
  brakeDecelMmps2PerMm: 2000,
};

export class VehiclePlant {
  readonly config: Required<PlantConfig>;

  // Current state
  xMm = 0;
  yMm = 0;
  headingRad = 0;
  speedMmps = 0;
  steerAngleDeg = 0;
  brakeStrokeMm = 0;

  // Commands being tracked
  private cmdSpeedMmps = 0;
  private cmdSteerDeg = 0;
  private cmdBrakeStrokeMm = 0;

  // Steering first-order lag state
  private steerLagState = 0;

  // Telemetry
  maxSteerAngleDeg = 0;

  constructor(config: PlantConfig = {}) {
    this.config = { ...DEFAULTS, ...config };
  }

  /** Apply commands (set by ECUs) for this tick. */
  setCommands(speedMmps: number, steerAngleDeg: number, brakeStrokeMm: number): void {
    this.cmdSpeedMmps = speedMmps;
    this.cmdSteerDeg = steerAngleDeg;
    this.cmdBrakeStrokeMm = brakeStrokeMm;
  }

  /**
   * Advance the plant by `dtMs` milliseconds.
   *
   * Order of operations per tick:
   * 1. Apply brake deceleration to speed
   * 2. Apply motor speed (simple rate-limited ramp)
   * 3. Apply steering with first-order lag
   * 4. Integrate position using bicycle model
   */
  tick(dtMs: number): void {
    const dtSec = dtMs / 1000;
    const { maxSpeedMmps, brakeDecelMmps2PerMm, steerLagMs, wheelbaseMm } = this.config;

    // 1. Brake deceleration
    if (this.cmdBrakeStrokeMm > 0) {
      const decel = this.cmdBrakeStrokeMm * brakeDecelMmps2PerMm;
      const speedSign = Math.sign(this.speedMmps);
      this.speedMmps -= speedSign * decel * dtSec;
      // Don't overshoot zero
      if (Math.sign(this.speedMmps) !== speedSign) {
        this.speedMmps = 0;
      }
    }

    // 2. Motor speed — simple acceleration model
    const accelLimit = 3000; // mm/s² (tunable)
    const speedDiff = this.cmdSpeedMmps - this.speedMmps;
    if (Math.abs(speedDiff) > 0.5) {
      const maxDelta = accelLimit * dtSec;
      this.speedMmps += Math.max(-maxDelta, Math.min(maxDelta, speedDiff));
    } else {
      this.speedMmps = this.cmdSpeedMmps;
    }

    // Clamp speed
    this.speedMmps = Math.max(-MAX_SPEED_REV_MMPS, Math.min(maxSpeedMmps, this.speedMmps));

    // 3. Steering — first-order lag: τ * dδ/dt + δ = δ_cmd
    // Discrete: δ += (δ_cmd − δ) * dt/τ
    if (steerLagMs > 0) {
      const alpha = dtMs / steerLagMs;
      this.steerAngleDeg += (this.cmdSteerDeg - this.steerAngleDeg) * Math.min(alpha, 1);
    } else {
      this.steerAngleDeg = this.cmdSteerDeg;
    }

    // Track max steer
    this.maxSteerAngleDeg = Math.max(this.maxSteerAngleDeg, Math.abs(this.steerAngleDeg));

    // 4. Integrate position using bicycle model
    // Only move if speed is non-zero
    if (Math.abs(this.speedMmps) > 0.5) {
      const velocityMs = this.speedMmps / 1000;
      const wheelbaseM = wheelbaseMm / 1000;
      const deltaRad = (this.steerAngleDeg * Math.PI) / 180;
      const turnRate = (velocityMs / wheelbaseM) * Math.tan(deltaRad);

      this.headingRad += turnRate * dtSec;
      this.xMm += velocityMs * Math.cos(this.headingRad) * dtSec * 1000;
      this.yMm += velocityMs * Math.sin(this.headingRad) * dtSec * 1000;
    }

    // Brake stroke — first-order lag (SEB response ~30ms)
    const brakeAlpha = dtMs / 30;
    this.brakeStrokeMm += (this.cmdBrakeStrokeMm - this.brakeStrokeMm) * Math.min(brakeAlpha, 1);
  }

  /** Get current state snapshot. */
  getState(): PlantState {
    return {
      xMm: this.xMm,
      yMm: this.yMm,
      headingRad: this.headingRad,
      speedMmps: this.speedMmps,
      steerAngleDeg: this.steerAngleDeg,
      brakeStrokeMm: this.brakeStrokeMm,
    };
  }

  /** Reset to initial state. */
  reset(): void {
    this.xMm = 0;
    this.yMm = 0;
    this.headingRad = 0;
    this.speedMmps = 0;
    this.cmdSpeedMmps = 0;
    this.steerAngleDeg = 0;
    this.cmdSteerDeg = 0;
    this.steerLagState = 0;
    this.brakeStrokeMm = 0;
    this.cmdBrakeStrokeMm = 0;
    this.maxSteerAngleDeg = 0;
  }
}
