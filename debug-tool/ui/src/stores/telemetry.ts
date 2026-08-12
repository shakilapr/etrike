import { ID_RT_HEARTBEAT, ID_RT_STATE_RPT, ID_RT_MOTION_RPT, ID_SYS_HEARTBEAT, ID_SYS_SAFETY_STS, ID_MTR_MOTOR_FBK, ID_SES_STATUS, ID_SEB_STATUS, ID_SYS_THROTTLE_STS, ID_STEER_DIAG, ID_RT_BRAKE_CMD, ID_HOST_BRAKE_REQ, ID_BRAKE_DIAG, SIG_HOST_DRIVE_CMD_SPEED_MMPS, SIG_RT_MOTION_RPT_SPEED_MMPS, SIG_MTR_MOTOR_FBK_ACTUAL_SPEED_MMPS, SIG_SES_STATUS_STR_ANGLE, SIG_STEER_DIAG_STEERDIAG_ANGLE0_1DEG, SIG_RT_BRAKE_CMD_BRAKE_PRESSURE_KPA, SIG_BRAKE_DIAG_BRAKEDIAG_PRESSURERAW, SIG_SYS_SAFETY_STS_LIGHT_LEFT, SIG_SYS_SAFETY_STS_LIGHT_RIGHT, SIG_SYS_SAFETY_STS_LIGHT_HEAD, SIG_SYS_SAFETY_STS_LIGHT_BRAKE, SIG_SYS_SAFETY_STS_ESTOP_ACTIVE, SIG_SYS_SAFETY_STS_HEARTBEAT_OK, SIG_MTR_MOTOR_FBK_GEAR_STATE, SIG_SYS_MODE_CMD_MODE, SIG_RT_STATE_RPT_SAFETY_STATE } from "@etrike/debug-shared";
import { derived, readable } from "svelte/store";
import { latestById } from "./can";

/** Ticks every 1s to force staleness re-evaluation. */
export const now = readable(Date.now() / 1000, (set) => {
  const timer = setInterval(() => set(Date.now() / 1000), 1000);
  return () => clearInterval(timer);
});

/** Real-time vehicle telemetry extracted from the latest CAN frame of each ID. */
export interface Telemetry {
  /** Turn signal bulbs */
  leftTurn: boolean;
  rightTurn: boolean;
  /** Headlight on */
  headlight: boolean;
  /** Brake light on */
  brakeLight: boolean;
  /** ESTOP active (from safety status) */
  estopActive: boolean;
  /** Heartbeat OK */
  heartbeatOk: boolean;

  /** Motor speed in km/h (converted from mm/s) */
  motorSpeedKmh: number | null;
  /** Steering angle in degrees */
  steerAngleDeg: number | null;
  /** Brake pressure in MPa */
  brakePressureMpa: number | null;
  /** Gear: "N" | "D" | "S" | "R" | null */
  gear: string | null;
  /** Mode: "MANUAL" | "AUTO" | "ESTOP" | null */
  mode: string | null;
  /** Safety state: "Normal" | "InternalEstop" | "Fault" | null */
  safetyState: string | null;
}

const GEAR_LABELS = ["N", "D", "S", "R"];
// Mode values: 0=MANUAL, 1=AUTO, 2=ESTOP (from CAN protocol 0x110/0x210)
const MODE_LABELS = ["MANUAL", "AUTO", "ESTOP"];
const SAFETY_LABELS = ["Normal", "InternalEstop", "Fault"];

function boolField(decoded: Record<string, unknown> | undefined, key: string): boolean {
  return decoded?.[key] === true || decoded?.[key] === 1;
}

function numField(decoded: Record<string, unknown> | undefined, key: string): number | null {
  const v = decoded?.[key];
  if (typeof v === "number" && Number.isFinite(v)) return v;
  return null;
}

function gearLabel(v: number | null): string | null {
  if (v === null || v === undefined) return null;
  if (v < 0 || v >= GEAR_LABELS.length) return null; // out of range → no data
  return GEAR_LABELS[v] ?? null;
}
function modeLabel(v: number | null): string | null {
  if (v === null || v === undefined) return null;
  return MODE_LABELS[v] ?? null;
}
function safetyLabel(v: number | null): string | null {
  if (v === null || v === undefined) return null;
  return SAFETY_LABELS[v] ?? null;
}

/** Which ECUs are present on the CAN bus (detected via heartbeat/status frames). */
export interface EcuPresence {
  rt: boolean;   // RT controller (0x7FD high, or 0x210)
  sys: boolean;  // SYS controller (0x7FE or 0x011, high bus preferred)
  mtr: boolean;  // Motor (0x206 either bus)
  ses: boolean;  // Steering EPS-C (0x201 low)
  seb: boolean;  // Brake-by-wire SEB (0x721 low)
}

const PRESENCE_TIMEOUT_S = 3;

function recent(frame: { ts: number } | undefined, nowS: number): boolean {
  if (!frame) return false;
  const tsSeconds = frame.ts > 1_000_000_000_000 ? frame.ts / 1000 : frame.ts;
  return nowS - tsSeconds < PRESENCE_TIMEOUT_S;
}

function recentDecoded(frame: { ts: number; decoded?: unknown } | undefined, nowS: number): Record<string, unknown> | undefined {
  if (!recent(frame, nowS)) return undefined;
  return frame?.decoded as Record<string, unknown> | undefined;
}

export const ecuPresence = derived([latestById, now], ([$latest, $now]): EcuPresence => ({
  rt:  recent($latest[`high:${ID_RT_HEARTBEAT}`], $now) || recent($latest[`high:${ID_RT_STATE_RPT}`], $now),
  sys: recent($latest[`high:${ID_SYS_HEARTBEAT}`], $now) || recent($latest[`high:${ID_SYS_SAFETY_STS}`], $now) || recent($latest[`low:${ID_SYS_HEARTBEAT}`], $now) || recent($latest[`low:${ID_SYS_SAFETY_STS}`], $now),
  mtr: recent($latest[`high:${ID_MTR_MOTOR_FBK}`], $now) || recent($latest[`low:${ID_MTR_MOTOR_FBK}`], $now),
  ses: recent($latest[`low:${ID_SES_STATUS}`], $now),
  seb: recent($latest[`low:${ID_SEB_STATUS}`], $now),
}));

export const telemetry = derived([latestById, now], ([$latest, $now]): Telemetry => {
  // Lights / indicators: prefer SYS_SAFETY_STS (0x011) on high bus, fall back to low
  const safetyHigh = recentDecoded($latest[`high:${ID_SYS_SAFETY_STS}`], $now);
  const safetyLow  = recentDecoded($latest[`low:${ID_SYS_SAFETY_STS}`], $now);
  const safety = safetyHigh ?? safetyLow;

  // Throttle speed: prefer high bus, fall back to low
  const throttleHigh = recentDecoded($latest[`high:${ID_SYS_THROTTLE_STS}`], $now);
  const throttleLow  = recentDecoded($latest[`low:${ID_SYS_THROTTLE_STS}`], $now);
  const throttle = throttleHigh ?? throttleLow;

  // Motor feedback (gear, backup speed)
  const motorHigh = recentDecoded($latest[`high:${ID_MTR_MOTOR_FBK}`], $now);
  const motorLow  = recentDecoded($latest[`low:${ID_MTR_MOTOR_FBK}`], $now);
  const motor = motorHigh ?? motorLow;
  const motion = recentDecoded($latest[`high:${ID_RT_MOTION_RPT}`], $now);
  const motionValid = motion !== undefined &&
    boolField(motion, 'speed_valid') && boolField(motion, 'yaw_rate_valid') && boolField(motion, 'gear_valid');

  // Steering angle: try SES_STATUS (0x201, low bus), fall back to STEER_DIAG (0x310, high)
  const ses = recentDecoded($latest[`low:${ID_SES_STATUS}`], $now);
  const steerDiag = recentDecoded($latest[`high:${ID_STEER_DIAG}`], $now);

  // Brake: prefer RT_BRAKE_CMD (0x205, low), fall back to HOST_BRAKE_REQ (0x301, high), then BRAKE_DIAG (0x311)
  const brakeCmd = recentDecoded($latest[`low:${ID_RT_BRAKE_CMD}`], $now);
  const brakeReq = recentDecoded($latest[`high:${ID_HOST_BRAKE_REQ}`], $now);
  const brakeDiag = recentDecoded($latest[`high:${ID_BRAKE_DIAG}`], $now);

  // State report (mode, safety)
  const stateRpt = recentDecoded($latest[`high:${ID_RT_STATE_RPT}`], $now);

  // Speed: prefer coherent RT motion feedback, then throttle, then motor feedback.
  let motorSpeedKmh: number | null = null;
  const speedMmps = (motionValid ? numField(motion, SIG_RT_MOTION_RPT_SPEED_MMPS) : null) ??
    numField(throttle, SIG_HOST_DRIVE_CMD_SPEED_MMPS) ?? numField(motor, SIG_MTR_MOTOR_FBK_ACTUAL_SPEED_MMPS);
  if (speedMmps !== null) motorSpeedKmh = Math.round((speedMmps * 3.6) / 10) / 100; // mm/s → km/h, 2 decimals

  // Steering angle — clamp to sane range (±90° for a steering column)
  let steerAngleDeg: number | null = null;
  const sesAngle = numField(ses, SIG_SES_STATUS_STR_ANGLE);               // 0.1 deg
  const diagAngle = numField(steerDiag, SIG_STEER_DIAG_STEERDIAG_ANGLE0_1DEG); // deg
  let rawAngle: number | null = null;
  if (sesAngle !== null) rawAngle = Math.round(sesAngle) / 10;
  else if (diagAngle !== null) rawAngle = Math.round(diagAngle) / 10;
  // Clamp to valid steering range instead of hiding UI
  if (rawAngle !== null) steerAngleDeg = Math.max(-90, Math.min(90, rawAngle));

  // Brake pressure (convert kPa → MPa) — clamp to 0..25 MPa
  let brakePressureMpa: number | null = null;
  const kpa = numField(brakeCmd, SIG_RT_BRAKE_CMD_BRAKE_PRESSURE_KPA) ?? numField(brakeReq, SIG_RT_BRAKE_CMD_BRAKE_PRESSURE_KPA);
  if (kpa !== null && kpa >= 0 && kpa <= 25000) brakePressureMpa = Math.round(kpa / 10) / 100;
  else {
    const rawMpa = numField(brakeDiag, SIG_BRAKE_DIAG_BRAKEDIAG_PRESSURERAW);
    if (rawMpa !== null && rawMpa >= 0 && rawMpa < 25) brakePressureMpa = Math.round(rawMpa * 100) / 100;
  }

  return {
    leftTurn: boolField(safety, SIG_SYS_SAFETY_STS_LIGHT_LEFT),
    rightTurn: boolField(safety, SIG_SYS_SAFETY_STS_LIGHT_RIGHT),
    headlight: boolField(safety, SIG_SYS_SAFETY_STS_LIGHT_HEAD),
    brakeLight: boolField(safety, SIG_SYS_SAFETY_STS_LIGHT_BRAKE),
    estopActive: boolField(safety, SIG_SYS_SAFETY_STS_ESTOP_ACTIVE),
    heartbeatOk: boolField(safety, SIG_SYS_SAFETY_STS_HEARTBEAT_OK),
    motorSpeedKmh,
    steerAngleDeg,
    brakePressureMpa,
    gear: gearLabel((motionValid ? numField(motion, 'gear') : null) ?? numField(motor, SIG_MTR_MOTOR_FBK_GEAR_STATE)),
    mode: modeLabel(numField(stateRpt, SIG_SYS_MODE_CMD_MODE)),
    safetyState: safetyLabel(numField(stateRpt, SIG_RT_STATE_RPT_SAFETY_STATE)),
  };
});
