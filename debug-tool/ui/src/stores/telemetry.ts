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

export const ecuPresence = derived([latestById, now], ([$latest, $now]): EcuPresence => ({
  rt:  recent($latest["high:0x7FD"], $now) || recent($latest["high:0x210"], $now),
  sys: recent($latest["high:0x7FE"], $now) || recent($latest["high:0x011"], $now) || recent($latest["low:0x7FE"], $now) || recent($latest["low:0x011"], $now),
  mtr: recent($latest["high:0x206"], $now) || recent($latest["low:0x206"], $now),
  ses: recent($latest["low:0x201"], $now),
  seb: recent($latest["low:0x721"], $now),
}));

export const telemetry = derived([latestById, now], ([$latest, $now]): Telemetry => {
  // Lights / indicators: prefer SYS_SAFETY_STS (0x011) on high bus, fall back to low
  const safetyHigh = $latest["high:0x011"]?.decoded;
  const safetyLow  = $latest["low:0x011"]?.decoded;
  const safety = safetyHigh ?? safetyLow;

  // Throttle speed: prefer high bus, fall back to low
  const throttleHigh = $latest["high:0x120"]?.decoded;
  const throttleLow  = $latest["low:0x120"]?.decoded;
  const throttle = throttleHigh ?? throttleLow;

  // Motor feedback (gear, backup speed)
  const motorHigh = $latest["high:0x206"]?.decoded;
  const motorLow  = $latest["low:0x206"]?.decoded;
  const motor = motorHigh ?? motorLow;

  // Steering angle: try SES_STATUS (0x201, low bus), fall back to STEER_DIAG (0x310, high)
  const ses = $latest["low:0x201"]?.decoded;
  const steerDiag = $latest["high:0x310"]?.decoded;

  // Brake: prefer RT_BRAKE_CMD (0x205, low), fall back to HOST_BRAKE_REQ (0x301, high), then BRAKE_DIAG (0x311)
  const brakeCmd = $latest["low:0x205"]?.decoded;
  const brakeReq = $latest["high:0x301"]?.decoded;
  const brakeDiag = $latest["high:0x311"]?.decoded;

  // State report (mode, safety)
  const stateRpt = $latest["high:0x210"]?.decoded;

  // Speed: prefer throttle, fall back to motor feedback
  let motorSpeedKmh: number | null = null;
  const speedMmps = numField(throttle, "speed_mmps") ?? numField(motor, "actual_speed_mmps");
  if (speedMmps !== null) motorSpeedKmh = Math.round((speedMmps * 3.6) / 10) / 100; // mm/s → km/h, 2 decimals

  // Steering angle — clamp to sane range (±90° for a steering column)
  let steerAngleDeg: number | null = null;
  const sesAngle = numField(ses, "str_angle");               // 0.1 deg
  const diagAngle = numField(steerDiag, "SteerDiag_Angle0_1deg"); // deg
  let rawAngle: number | null = null;
  if (sesAngle !== null) rawAngle = Math.round(sesAngle) / 10;
  else if (diagAngle !== null) rawAngle = Math.round(diagAngle) / 10;
  // Clamp to valid steering range instead of hiding UI
  if (rawAngle !== null) steerAngleDeg = Math.max(-90, Math.min(90, rawAngle));

  // Brake pressure (convert kPa → MPa) — clamp to 0..25 MPa
  let brakePressureMpa: number | null = null;
  const kpa = numField(brakeCmd, "brake_pressure_kpa") ?? numField(brakeReq, "brake_pressure_kpa");
  if (kpa !== null && kpa >= 0 && kpa <= 25000) brakePressureMpa = Math.round(kpa / 10) / 100;
  else {
    const rawMpa = numField(brakeDiag, "BrakeDiag_PressureRaw");
    if (rawMpa !== null && rawMpa >= 0 && rawMpa < 25) brakePressureMpa = Math.round(rawMpa * 100) / 100;
  }

  return {
    leftTurn: boolField(safety, "light_left"),
    rightTurn: boolField(safety, "light_right"),
    headlight: boolField(safety, "light_head"),
    brakeLight: boolField(safety, "light_brake"),
    estopActive: boolField(safety, "estop_active"),
    heartbeatOk: boolField(safety, "heartbeat_ok"),
    motorSpeedKmh,
    steerAngleDeg,
    brakePressureMpa,
    gear: gearLabel(numField(motor, "gear_state")),
    mode: modeLabel(numField(stateRpt, "mode")),
    safetyState: safetyLabel(numField(stateRpt, "safety_state")),
  };
});
