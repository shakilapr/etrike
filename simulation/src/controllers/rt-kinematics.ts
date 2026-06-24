/**
 * RT Kinematics Controller — port of rt-esp32/src/physics_model.cpp.
 *
 * Wraps TricycleKinematics with RT-specific glue:
 *   - Dynamic angle clamp
 *   - Obstacle speed limiting
 *   - Following error threshold
 *   - Geometry-based steering rate
 */

import {
  TricycleKinematics,
  computeDynamicLimit,
  computeFollowingErrorThreshold,
  STEER_HARD_LIMIT_DEG,
  ANGLE_CLAMP_BASE_DEG,
  ANGLE_CLAMP_SPEED_RANGE,
  STEER_RATE_MIN_DEG_S,
  STEER_RATE_MAX_DEG_S,
  STEER_RATE_RANGE_DEG_S,
  WHEELBASE_MM,
} from "../physics/tricycle.js";
import type { DriveCommand, ResolvedSetpoint } from "../core/types.js";

export class RtKinematicsController {
  private kinematics = new TricycleKinematics();

  /**
   * Resolve a Host drive command (0x300) into motor speed + steering angle.
   */
  resolve(cmd: DriveCommand): ResolvedSetpoint {
    const raw = this.kinematics.resolve({
      speedMmps: cmd.speedMmps,
      yawRateMradS: cmd.yawRateMradS,
    }, cmd.gear);

    // Apply dynamic angle clamp
    const dynamicLimit = computeDynamicLimit(raw.motorSpeedMmps);
    const rawAngleDeg = raw.steerAngleMdeg / 1000;
    const clampedAngleDeg = Math.max(-dynamicLimit, Math.min(dynamicLimit, rawAngleDeg));

    return {
      motorSpeedMmps: raw.motorSpeedMmps,
      steerAngleDeg: clampedAngleDeg,
      gear: cmd.gear,
    };
  }

  /** Apply obstacle distance limit to motor speed. */
  obstacleLimit(targetMmps: number, obstacleMm: number): number {
    return TricycleKinematics.obstacleLimit(targetMmps, obstacleMm);
  }

  /**
   * Calculate target slew rate for EPS-C based on vehicle speed.
   * rate_deg_s = 125 + (speed_kmh − 2) × (400/23), clamped [125, 525]
   */
  static computeSteerRate(speedMmps: number): number {
    const speedKmh = Math.abs(speedMmps) * 3.6 / 1000;
    const rate = STEER_RATE_MIN_DEG_S
      + (speedKmh - 2.0) * (STEER_RATE_RANGE_DEG_S / ANGLE_CLAMP_SPEED_RANGE);
    return Math.max(STEER_RATE_MIN_DEG_S, Math.min(STEER_RATE_MAX_DEG_S, rate));
  }

  getDynamicLimit(speedMmps: number): number {
    return computeDynamicLimit(speedMmps);
  }

  getFollowingErrorThreshold(speedMmps: number): number {
    return computeFollowingErrorThreshold(speedMmps);
  }

  reset(): void {
    this.kinematics.reset();
  }
}
