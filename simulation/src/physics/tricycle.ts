/**
 * Tricycle kinematics — port of rt-esp32/src/physics_model.cpp.
 *
 * Delta trike inverse bicycle model: delta = atan2(L * w, |v|)
 */

// ── Shared constants (from shared_config.h) ──────────────────────────
export const WHEELBASE_MM = 1500;
export const OBSTACLE_STOP_MM = 300;
export const OBSTACLE_CLEAR_MM = 3000;
export const MAX_SPEED_FWD_MMPS = 3000;
export const MAX_SPEED_REV_MMPS = 500;
export const LOW_SPEED_THRESH_MMPS = 50;
export const CMD_STALE_TIMEOUT_MS = 500;
export const HOST_HEARTBEAT_TIMEOUT_MS = 1500;
export const STARTUP_GRACE_PERIOD_MS = 3000;
export const MAX_BRAKE_KPA = 20000;
export const OBSTACLE_MAX_KPA = 5000;
export const ASSIST_STOP_KPA = 2000;

// ── RT constants (from rt-esp32/src/config.h) ────────────────────────
export const STEER_HARD_LIMIT_DEG = 40.0;
export const STEER_FOLLOWING_ERR_MIN_DEG = 2.0;
export const STEER_FOLLOWING_ERR_FACTOR = 0.25;
export const STEER_FOLLOWING_ERR_MS = 300;
export const STEER_CMD_RATE_HZ = 50;
export const STEER_BOOT_WAIT_MS = 500;
export const ANGLE_CLAMP_BASE_DEG = 40.0;
export const ANGLE_CLAMP_MIN_DEG = 5.0;
export const ANGLE_CLAMP_RANGE_DEG = 35.0;
export const ANGLE_CLAMP_SPEED_RANGE = 23.0;
export const STEER_RATE_MIN_DEG_S = 125.0;
export const STEER_RATE_MAX_DEG_S = 525.0;
export const STEER_RATE_RANGE_DEG_S = 400.0;
export const STEER_SYNC_TIMEOUT_MS = 5000;
export const STEER_ESTOP_RAMP_DEG_S = 20.0;
export const STEER_ESTOP_HOLD_MS = 500;

/** Convert degrees to radians. */
function deg2rad(d: number): number {
  return d * Math.PI / 180;
}

/** Convert radians to degrees. */
function rad2deg(r: number): number {
  return r * 180 / Math.PI;
}

/**
 * Dynamic angle clamp.
 * limit_deg = 40.0 − (speed_kmh − 2.0) × (35.0/23.0), clamped [5.0, 40.0]
 */
export function computeDynamicLimit(speedMmps: number): number {
  const speedKmh = speedMmps * 3.6 / 1000;
  const limitDeg = ANGLE_CLAMP_BASE_DEG
    - (speedKmh - 2.0) * (ANGLE_CLAMP_RANGE_DEG / ANGLE_CLAMP_SPEED_RANGE);
  return Math.max(ANGLE_CLAMP_MIN_DEG, Math.min(ANGLE_CLAMP_BASE_DEG, limitDeg));
}

/**
 * Following error threshold: max(2.0, 0.25 × dynamic_limit_deg).
 */
export function computeFollowingErrorThreshold(speedMmps: number): number {
  const dynamicLimit = computeDynamicLimit(speedMmps);
  return Math.max(STEER_FOLLOWING_ERR_MIN_DEG, STEER_FOLLOWING_ERR_FACTOR * dynamicLimit);
}

export interface DriveCmd {
  speedMmps: number;
  yawRateMradS: number;
}

export interface ResolvedSetpoint {
  motorSpeedMmps: number;
  steerAngleMdeg: number;   // +right, ±45000 (millidegrees)
  steerValid: boolean;
  steerSaturated: boolean;
  reversing: boolean;
  cmdGear: number;
}

/**
 * Tricycle kinematics model — inverse bicycle.
 * Port of PhysicsModel::resolve() from physics_model.cpp.
 */
export class TricycleKinematics {
  private steerHoldRad = 0;

  resolve(cmd: DriveCmd, gear = 1): ResolvedSetpoint {
    let v = cmd.speedMmps / 1000;        // m/s
    const w = cmd.yawRateMradS / 1000;   // rad/s
    const L = WHEELBASE_MM / 1000;       // m
    const kYawEpsilon = 0.001;
    const steerLimitRad = deg2rad(STEER_HARD_LIMIT_DEG);
    const lowSpeedMps = LOW_SPEED_THRESH_MMPS / 1000;

    let steer = 0;
    let ok = false;
    let saturated = false;

    if (Math.abs(v) > lowSpeedMps) {
      const requestedSteer = Math.atan2(L * w, Math.abs(v));
      saturated = Math.abs(requestedSteer) > steerLimitRad;
      steer = Math.max(-steerLimitRad, Math.min(steerLimitRad, requestedSteer));
      this.steerHoldRad = steer;
      ok = !saturated;
    } else if (Math.abs(w) > kYawEpsilon) {
      // Convert pure yaw into minimum-radius forward arc
      const minRadiusM = L / Math.tan(steerLimitRad);
      const turnSpeedMps = Math.abs(w) * minRadiusM;
      v = Math.max(lowSpeedMps, Math.min(MAX_SPEED_FWD_MMPS / 1000, turnSpeedMps));
      steer = w > 0 ? steerLimitRad : -steerLimitRad;
      this.steerHoldRad = steer;
      ok = true;
    } else {
      // Decay toward straight at low speed
      const kSteerDecayFactor = 0.8;
      steer = this.steerHoldRad * kSteerDecayFactor;
    }

    // Clamp speed
    v = Math.max(-MAX_SPEED_REV_MMPS / 1000, Math.min(MAX_SPEED_FWD_MMPS / 1000, v));

    return {
      motorSpeedMmps: Math.round(v * 1000),
      steerAngleMdeg: Math.round(rad2deg(steer) * 1000),
      steerValid: ok,
      steerSaturated: saturated,
      reversing: v < 0 && Math.round(v * 1000) < 0,
      cmdGear: gear,
    };
  }

  /**
   * Obstacle speed limiter: linearly scales speed from 0 at stop-dist
   * to full speed at clear-dist.
   */
  static obstacleLimit(targetMmps: number, obstacleMm: number): number {
    if (obstacleMm <= OBSTACLE_STOP_MM) return 0;
    if (obstacleMm >= OBSTACLE_CLEAR_MM) return targetMmps;
    const t = (obstacleMm - OBSTACLE_STOP_MM) / (OBSTACLE_CLEAR_MM - OBSTACLE_STOP_MM);
    return Math.round(targetMmps * t);
  }

  reset(): void {
    this.steerHoldRad = 0;
  }
}
