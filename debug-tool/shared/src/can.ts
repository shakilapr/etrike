// WARNING: This CAN message catalog is hand-maintained.
// When adding/changing messages, also update the duplicate copy in:
//   debug-tool/ui/src/lib/can-decoder.ts
// The single source of truth is: shared/can/can_signals.yaml
import { readI16BE, readU16BE, readI16LE, readU16LE, readI24BE, readI32BE, readU32BE, readU32LE } from "./read-helpers";

export const BUSES = ["high", "low"] as const;
export type Bus = (typeof BUSES)[number];

export type FieldKind = "number" | "boolean" | "enum";

export interface CanField {
  key: string;
  label: string;
  kind: FieldKind;
  unit?: string;
  min?: number;
  max?: number;
  step?: number;
  options?: Array<{ label: string; value: number }>;
}

export interface CanMessageDef {
  bus: Bus;
  id: string;
  name: string;
  sender: string;
  dlc: number;
  period: string;
  injectable: boolean;
  fields: CanField[];
}

export interface CanFrame {
  ts: number;
  bus: Bus;
  id: string;
  name: string;
  dlc: number;
  data: number[];
  decoded: Record<string, unknown>;
  row_id?: number;
  ts_real?: number;
  ts_device?: number;
}

export interface BusStats {
  active: boolean;
  total: number;
  fps: number;
  load_pct: number;
  tec: number;
  rec: number;
  by_id: Record<string, number>;
}

export interface CanStats {
  type?: "stats";
  ts: number;
  uptime_s: number;
  buses: Record<Bus, BusStats>;
}

export interface InjectionTemplate {
  bus: Bus;
  id: string;
  name: string;
  description: string;
  dlc: number;
  values: Record<string, number | boolean>;
}

export const MODE_OPTIONS = [
  { label: "MANUAL", value: 0 },
  { label: "AUTO", value: 1 },
  { label: "ESTOP", value: 2 }
];

// Mode command (0x110) only accepts MANUAL/AUTO — ESTOP is not selectable via mode button.
export const MODE_CMD_OPTIONS = [
  { label: "MANUAL", value: 0 },
  { label: "AUTO", value: 1 }
];

export const GEAR_OPTIONS = [
  { label: "N", value: 0 },
  { label: "D", value: 1 },
  { label: "S", value: 2 },
  { label: "R", value: 3 }
];

const bool = (key: string, label: string): CanField => ({ key, label, kind: "boolean" });
const num = (key: string, label: string, unit?: string, min?: number, max?: number, step = 1): CanField => ({
  key,
  label,
  kind: "number",
  unit,
  min,
  max,
  step
});
const modeField: CanField = { key: "mode", label: "Mode", kind: "enum", options: MODE_OPTIONS };
const modeCmdField: CanField = { key: "mode", label: "Mode", kind: "enum", options: MODE_CMD_OPTIONS };
const gearField: CanField = { key: "gear", label: "Gear", kind: "enum", options: GEAR_OPTIONS };
const SAFETY_STATE_OPTIONS = [
  { label: "Normal", value: 0 },
  { label: "InternalEstop", value: 1 },
  { label: "Fault", value: 2 }
];
const STEER_STATE_OPTIONS = [
  { label: "Idle", value: 0 },
  { label: "Active", value: 1 },
  { label: "Fault", value: 2 },
  { label: "Inhibited", value: 3 },
  { label: "Disabled", value: 4 },
  { label: "Unknown", value: 5 }
];

function msg(
  bus: Bus,
  id: string,
  name: string,
  sender: string,
  period: string,
  dlc: number,
  injectable: boolean,
  fields: CanField[]
): CanMessageDef {
  return { bus, id, name, sender, period, dlc, injectable, fields };
}

const safetyFields = [bool("estop_active", "ESTOP active"), bool("heartbeat_ok", "Heartbeat OK"), bool("light_left", "Light: left turn"), bool("light_right", "Light: right turn"), bool("light_brake", "Light: brake"), bool("light_head", "Light: head")];
const throttleFields = [num("speed_mmps", "Speed", "mm/s", -500, 3000, 10)];
const motorFeedbackFields = [
  num("actual_speed_mmps", "Actual speed", "mm/s", -500, 3000, 10),
  { key: "gear_state", label: "Gear state", kind: "enum", options: GEAR_OPTIONS } satisfies CanField,
  num("fault_flags", "Fault flags", undefined, 0, 255)
];
const lightFields = [bool("left_turn", "Left turn"), bool("right_turn", "Right turn"), bool("brake_light", "Brake light"), bool("headlight", "Headlight")];
const diagFields = [
  modeField,
  bool("brake_engaged", "Brake engaged"),
  bool("brake_fault", "Brake fault"),
  bool("hb_ok", "Heartbeat OK"),
  bool("estop_active", "ESTOP active"),
  num("free_heap_kb", "Free heap", "KB", 0, 65535),
  num("tec", "TEC", undefined, 0, 255),
  num("rec", "REC", undefined, 0, 255)
];
const heartbeatFields = [num("alive_ctr", "Alive counter", undefined, 0, 255), num("health_flags", "Health flags", undefined, 0, 15)];

export const CAN_MESSAGES: CanMessageDef[] = [
  msg("high", "0x001", "SAFETY_ESTOP", "any", "event", 0, true, []),
  msg("high", "0x011", "SYS_SAFETY_STS", "RT (fwd)", "5 Hz", 3, true, safetyFields),
  msg("high", "0x120", "SYS_THROTTLE_STS", "RT (fwd)", "100 Hz", 2, true, throttleFields),
  msg("high", "0x206", "MTR_MOTOR_FBK", "RT (fwd)", "50 Hz", 4, true, motorFeedbackFields),
  msg("high", "0x210", "RT_STATE_RPT", "RT", "10 Hz", 6, true, [modeField, { key: "safety_state", label: "Safety state", kind: "enum", options: SAFETY_STATE_OPTIONS }, num("estop_reason", "ESTOP reason", undefined, 0, 7), bool("reversing", "Reversing"), num("rx_overflow", "RX overflow", undefined, 0, 255), num("task_health", "Task health", undefined, 0, 255), { key: "steer_state", label: "Steer state", kind: "enum", options: STEER_STATE_OPTIONS }]),
  msg("high", "0x220", "RT_PID_RPT", "RT", "reserved", 6, false, [num("speed_setpoint", "Setpoint", "mm/s"), num("speed_measured", "Measured", "mm/s"), num("pid_output", "PID output")]),
  msg("high", "0x300", "HOST_DRIVE_CMD", "Host", "<=100 Hz", 8, true, [num("speed_mmps", "Speed", "mm/s", -500, 3000, 10), num("yaw_rate_mrad_s", "Yaw rate", "mrad/s", -3000, 3000, 10), gearField]),
  msg("high", "0x301", "HOST_BRAKE_REQ", "Host", "demand", 4, true, [num("brake_pressure_kpa", "Brake pressure", "kPa", 0, 20000, 100)]),
  msg("high", "0x302", "HOST_LIGHT_CMD", "Host", "change", 1, true, lightFields),
  msg("high", "0x400", "HOST_OBSTACLE_DIST", "Host", "10 Hz", 4, true, [num("distance_mm", "Distance", "mm", 0, 4294967295, 10)]),
  msg("high", "0x600", "SYS_DIAG_RPT", "RT (fwd)", "1 Hz", 8, false, diagFields),
  msg("high", "0x7FC", "HOST_HEARTBEAT", "Host", "2 Hz", 2, true, heartbeatFields),
  msg("high", "0x7FD", "RT_HEARTBEAT", "RT", "2 Hz", 2, false, heartbeatFields),

  msg("low", "0x001", "SAFETY_ESTOP", "any", "event", 0, true, []),
  msg("low", "0x011", "SYS_SAFETY_STS", "SYS", "5 Hz", 3, true, safetyFields),
  msg("low", "0x012", "SYS_DCDC_CMD", "SYS", "change", 1, false, [bool("enable", "Enable")]),
  msg("low", "0x110", "SYS_MODE_CMD", "SYS", "change", 1, true, [modeCmdField]),
  msg("low", "0x120", "SYS_THROTTLE_STS", "MTR", "100 Hz", 2, true, throttleFields),
  msg("low", "0x169", "VCU_SES_REQ", "RT", "50 Hz", 8, true, [bool("alignment_enable", "Alignment enable"), num("target_angle", "Target angle", "0.1 deg", -3000, 780), num("target_speed", "Target speed", "deg/s", 125, 1250), bool("control_enable", "Control enable"), num("rolling_counter", "Rolling counter", undefined, 0, 15), num("checksum", "Checksum", undefined, 0, 255)]),
  msg("low", "0x201", "SES_STATUS", "EPS-C", "100 Hz", 8, true, [bool("angle_status", "Angle status"), num("control_mode_sts", "Control mode status", undefined, 0, 3), num("str_angle", "Steer angle", "0.1 deg"), num("tgt_angle_spd", "Target angle speed", "deg/s"), num("error_status", "Error status", undefined, 0, 3), num("rolling_counter", "Rolling counter", undefined, 0, 15), num("checksum", "Checksum", undefined, 0, 255)]),
  msg("low", "0x202", "SES_ERRINFO", "EPS-C", "10 Hz", 8, true, [num("fault_mask", "Fault mask")]),
  msg("low", "0x203", "SES_VERSION", "EPS-C", "1 Hz", 8, true, [num("sw_version", "SW version"), num("hw_version", "HW version")]),
  msg("low", "0x204", "RT_DRIVE_CMD", "RT", "100 Hz", 5, true, [num("motor_speed_mmps", "Motor speed", "mm/s", -500, 3000, 10), gearField]),
  msg("low", "0x205", "RT_BRAKE_CMD", "RT", "50 Hz", 4, true, [num("brake_pressure_kpa", "Brake pressure", "kPa", 0, 20000, 100)]),
  msg("low", "0x206", "MTR_MOTOR_FBK", "MTR", "50 Hz", 4, true, motorFeedbackFields),
  msg("low", "0x302", "HOST_LIGHT_CMD", "RT (fwd)", "change", 1, true, lightFields),
  msg("high", "0x310", "STEER_DIAG", "RT", "10 Hz", 8, false, [
    num("SteerDiag_Angle0_1deg", "Angle", "deg", -700, 700),
    bool("SteerDiag_Fault", "Fault"),
    num("SteerDiag_MotorCurrent", "Motor current", "A", 0, 60),
    num("SteerDiag_ECUTemp", "ECU temp", "degC", 0, 255),
  ]),
  msg("high", "0x311", "BRAKE_DIAG", "RT", "10 Hz", 8, false, [
    num("BrakeDiag_PressureRaw", "Pressure", "MPa", 0, 32),
    bool("BrakeDiag_Fault", "Fault"),
    num("BrakeDiag_MotorCurrent", "Motor current", "A", -255, 255),
    num("BrakeDiag_ECUTemp", "ECU temp", "degC", -40, 215),
  ]),
  msg("low", "0x600", "SYS_DIAG_RPT", "SYS", "1 Hz", 8, false, diagFields),
  msg("low", "0x6FA", "SES_TEST", "EPS-C", "100 Hz", 8, false, [num("motor_current", "Motor current"), num("ecu_temp", "ECU temp"), num("supply_voltage", "Supply voltage")]),
  msg("low", "0x6FB", "SEB_TEST", "SEB", "100 Hz", 8, false, [num("motor_current", "Motor current"), num("ecu_temp", "ECU temp"), num("supply_voltage", "Supply voltage")]),
  msg("low", "0x721", "SEB_STATUS", "SEB", "100 Hz", 8, true, [bool("alignment_status", "Alignment status"), bool("control_enable_sts", "Control enable status"), num("control_mode_sts", "Control mode status", undefined, 0, 3), bool("auto_brake_sts", "Auto brake status"), num("stroke_value", "Stroke value", "raw"), num("pressure_value", "Pressure value", "raw"), num("angle_value", "Angle value", "raw"), num("error_status", "Error status", undefined, 0, 3), num("rolling_counter", "Rolling counter", undefined, 0, 15), num("checksum", "Checksum", undefined, 0, 255)]),
  msg("low", "0x731", "SEB_ERRINFO", "SEB", "10 Hz", 8, true, [num("fault_mask", "Fault mask")]),
  msg("low", "0x741", "SEB_VERSION", "SEB", "1 Hz", 8, true, [num("sw_version", "SW version"), num("hw_version", "HW version")]),
  msg("low", "0x7B9", "VCU_SEB_REQ", "SYS", "50 Hz", 8, true, [bool("align_enable", "Align enable"), bool("control_enable", "Control enable"), num("stroke_req", "Stroke request", "raw"), num("pressure_req", "Pressure request", "raw"), bool("auto_brake", "Auto brake"), num("control_mode", "Control mode", undefined, 0, 3), num("rolling_counter", "Rolling counter", undefined, 0, 15), num("checksum", "Checksum", undefined, 0, 255)]),
  msg("low", "0x7FD", "RT_HEARTBEAT", "RT", "2 Hz", 2, false, heartbeatFields),
  msg("low", "0x7FE", "SYS_HEARTBEAT", "SYS", "10 Hz", 2, true, heartbeatFields)
];

export const CAN_BY_BUS_ID = new Map(CAN_MESSAGES.map((item) => [`${item.bus}:${item.id}`, item]));

export const INJECTION_TEMPLATES: InjectionTemplate[] = [
  // ── Host simulation (high bus) ──────────────────────────────
  { bus: "high", id: "0x300", name: "Host drive 2.0 m/s", description: "Host drive command in D gear.", dlc: 8, values: { speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1 } },
  { bus: "high", id: "0x301", name: "Host brake 5 MPa", description: "Host brake request.", dlc: 4, values: { brake_pressure_kpa: 5000 } },
  { bus: "high", id: "0x7FC", name: "Host heartbeat", description: "Host heartbeat. Inject every 500ms.", dlc: 2, values: { alive_ctr: 1, health_flags: 0 } },
  // ── RT simulation (low bus) ─────────────────────────────────
  { bus: "low", id: "0x204", name: "RT drive 2.0 m/s", description: "RT to MTR drive command.", dlc: 5, values: { motor_speed_mmps: 2000, gear: 1 } },
  { bus: "low", id: "0x205", name: "RT brake 5 MPa", description: "RT to SYS brake command.", dlc: 4, values: { brake_pressure_kpa: 5000 } },
  { bus: "low", id: "0x169", name: "RT steer center", description: "RT to EPS-C steering command.", dlc: 8, values: { target_angle: 0, target_speed: 328, control_enable: true, rolling_counter: 1, checksum: 0 } },
  { bus: "low", id: "0x7FD", name: "RT heartbeat", description: "RT heartbeat on low bus. Inject every 500ms.", dlc: 2, values: { alive_ctr: 1, health_flags: 0 } },
  // ── Bench bypass: simulate absent peer ECUs ─────────────────
  { bus: "low", id: "0x201", name: "EPS-C status (bypass)", description: "Synthetic EPS-C status: 0° centered, aligned. Inject every 100ms.", dlc: 8, values: { angle_status: true, str_angle: 0, tgt_angle_spd: 0, error_status: 0, rolling_counter: 1, checksum: 0 } },
  { bus: "low", id: "0x721", name: "SEB status (bypass)", description: "Synthetic SEB status: aligned, 0mm stroke. Inject every 100ms.", dlc: 8, values: { alignment_status: true, stroke_value: 600, pressure_value: 0, error_status: 0, rolling_counter: 1, checksum: 0 } },
  { bus: "low", id: "0x206", name: "MTR feedback (bypass)", description: "Synthetic MTR feedback: 500 mm/s, D gear, no faults. Inject every 50ms.", dlc: 4, values: { actual_speed_mmps: 500, gear_state: 1, fault_flags: 0 } },
  { bus: "low", id: "0x7FE", name: "SYS heartbeat (bypass)", description: "Synthetic SYS heartbeat for RT bench. Inject every 100ms.", dlc: 2, values: { alive_ctr: 1, health_flags: 0 } },
  { bus: "low", id: "0x001", name: "ESTOP trigger", description: "DLC=0 ESTOP frame. Triggers emergency stop on all nodes.", dlc: 0, values: {} },
];

export function normalizeBus(input: unknown): Bus {
  if (input === undefined || input === null || input === "") return "high";
  if (input === "high" || input === "low") return input;
  throw new Error(`invalid CAN bus: ${String(input)}`);
}

export function normalizeCanId(input: string | number): string {
  if (typeof input === "number") return `0x${input.toString(16).toUpperCase().padStart(3, "0")}`;
  const trimmed = input.trim();
  const value = trimmed.toLowerCase().startsWith("0x") ? Number.parseInt(trimmed.slice(2), 16) : Number.parseInt(trimmed, 16);
  return Number.isFinite(value) ? `0x${value.toString(16).toUpperCase().padStart(3, "0")}` : trimmed.toUpperCase();
}

export function findMessage(bus: Bus, id: string): CanMessageDef | undefined {
  const normalized = normalizeCanId(id);
  return CAN_BY_BUS_ID.get(`${bus}:${normalized}`);
}

export function getMessageName(bus: Bus, id: string): string {
  return findMessage(bus, id)?.name ?? `UNKNOWN_${normalizeCanId(id)}`;
}

export function defaultStats(): CanStats {
  return { ts: Date.now() / 1000, uptime_s: 0, buses: { high: emptyBusStats(), low: emptyBusStats() } };
}

export function normalizeStats(input: Partial<CanStats> | Record<string, unknown>): CanStats {
  const buses = input.buses && typeof input.buses === "object" ? (input.buses as Partial<Record<Bus, Partial<BusStats>>>) : {};
  return {
    type: "stats",
    ts: typeof input.ts === "number" ? input.ts : Date.now() / 1000,
    uptime_s: typeof input.uptime_s === "number" ? input.uptime_s : 0,
    buses: { high: normalizeBusStats(buses.high), low: normalizeBusStats(buses.low) }
  };
}

export function normalizeFrame(input: Partial<CanFrame> & { id: string; data: number[] }): CanFrame {
  const bus = normalizeBus(input.bus);
  const id = normalizeCanId(input.id);
  const fullData = normalizeBytes(input.data).slice(0, 8);
  const dlc = typeof input.dlc === "number" ? input.dlc : Math.min(input.data.length, 8);
  const decoded = input.decoded && Object.keys(input.decoded).length > 0 ? input.decoded : decodeFrame(bus, id, fullData);
  // Normalize timestamp: raw ts may be milliseconds (CANalyst bridge) or seconds.
  // If ts > 1e12 it's milliseconds, convert to seconds.
  let ts = typeof input.ts === "number" ? input.ts : Date.now() / 1000;
  if (ts > 1_000_000_000_000) ts = ts / 1000;
  return { ts, bus, id, name: input.name ?? getMessageName(bus, id), dlc, data: fullData.slice(0, dlc), decoded };
}

export function decodeFrame(bus: Bus, id: string, data: number[]): Record<string, unknown> {
  const bytes = normalizeBytes(data);
  switch (normalizeCanId(id)) {
    case "0x001": return {};
    case "0x011": return { estop_active: bytes[0] !== 0, heartbeat_ok: bytes[1] !== 0, light_left: Boolean((bytes[2] ?? 0) & 0x01), light_right: Boolean((bytes[2] ?? 0) & 0x02), light_brake: Boolean((bytes[2] ?? 0) & 0x04), light_head: Boolean((bytes[2] ?? 0) & 0x08) };
    case "0x012": return { enable: bytes[0] !== 0 };
    case "0x110": return { mode: bytes[0] ?? 0, mode_name: modeName(bytes[0] ?? 0) };
    case "0x120": return { speed_mmps: readI16BE(bytes, 0) };
    case "0x169": return { alignment_enable: Boolean(bytes[0] & 1), control_enable: Boolean(bytes[0] & 2), target_angle: readI16LE(bytes, 2), target_speed: (bytes[4] ?? 0) | (((bytes[5] ?? 0) & 0x0C) << 6), roll_cnt_enable: Boolean((bytes[5] ?? 0) & 0x01), checksum_enable: Boolean((bytes[5] ?? 0) & 0x02), rolling_counter: ((bytes[5] ?? 0) >> 4) & 0x0f, vehicle_speed: bytes[6] ?? 0, checksum: bytes[7] ?? 0 };
    case "0x201": return { angle_status: Boolean(bytes[0] & 1), control_mode_sts: ((bytes[0] ?? 0) >> 1) & 3, error_status: ((bytes[0] ?? 0) >> 6) & 3, str_angle: readI16LE(bytes, 2), tgt_angle_spd: readI16LE(bytes, 4), torque: ((bytes[5] ?? 0) * 0.1 - 12.1), roll_cnt_enable_sts: Boolean((bytes[6] ?? 0) & 0x01), checksum_enable_sts: Boolean((bytes[6] ?? 0) & 0x02), rolling_counter: ((bytes[6] ?? 0) >> 4) & 0x0f, checksum: bytes[7] ?? 0 };
    case "0x202": return decodeSesFaults(bytes);
    case "0x203": return { sw_version: bytes[0] ?? 0, hw_version: bytes[1] ?? 0 };
    case "0x204": return { motor_speed_mmps: readI32BE(bytes, 0), gear: bytes[4] ?? 0, gear_name: gearName(bytes[4] ?? 0) };
    case "0x205": return { brake_pressure_kpa: readI32BE(bytes, 0) };
    case "0x206": return { actual_speed_mmps: readI16BE(bytes, 0), gear_state: bytes[2] ?? 0, gear_name: gearName(bytes[2] ?? 0), fault_flags: bytes[3] ?? 0 };
    case "0x210": return { mode: bytes[0] ?? 0, mode_name: modeName(bytes[0] ?? 0), safety_state: (bytes[1] ?? 0) & 0x03, estop_reason: ((bytes[1] ?? 0) >> 4) & 0x0f, reversing: bytes[2] !== 0, rx_overflow: bytes[3] ?? 0, task_health: bytes[4] ?? 0, steer_state: bytes[5] ?? 0 };
    case "0x220": return { speed_setpoint: readI16BE(bytes, 0), speed_measured: readI16BE(bytes, 2), pid_output: readI16BE(bytes, 4) };
    case "0x300": return { speed_mmps: readI32BE(bytes, 0), yaw_rate_mrad_s: readI24BE(bytes, 4), gear: bytes[7] ?? 0, gear_name: gearName(bytes[7] ?? 0) };
    case "0x301": return { brake_pressure_kpa: readI32BE(bytes, 0) };
    case "0x302": return { left_turn: Boolean(bytes[0] & 1), right_turn: Boolean(bytes[0] & 2), brake_light: Boolean(bytes[0] & 4), headlight: Boolean(bytes[0] & 8) };
    case "0x310": return { SteerDiag_Angle0_1deg: readU16BE(bytes, 0) * 0.1 - 3000, SteerDiag_Fault: bytes[2] !== 0, SteerDiag_MotorCurrent: readI16BE(bytes, 3) * 0.01, SteerDiag_ECUTemp: readI16BE(bytes, 5) * 0.1 };
    case "0x311": return { BrakeDiag_PressureRaw: readI16BE(bytes, 0) * 0.05, BrakeDiag_Fault: bytes[2] !== 0, BrakeDiag_MotorCurrent: readI16BE(bytes, 3) * 0.01, BrakeDiag_ECUTemp: readI16BE(bytes, 5) * 0.1 };
    case "0x400": { const distance = readU32BE(bytes, 0); return { distance_mm: distance, distance_label: distance === 0xffffffff ? "clear" : `${distance} mm` }; }
    case "0x600": return { mode: bytes[0] ?? 0, mode_name: modeName(bytes[0] ?? 0), brake_engaged: Boolean((bytes[1] ?? 0) & 0x01), brake_fault: Boolean((bytes[1] ?? 0) & 0x02), hb_ok: bytes[2] !== 0, estop_active: bytes[3] !== 0, free_heap_kb: readU16BE(bytes, 4), tec: bytes[6] ?? 0, rec: bytes[7] ?? 0 };
    case "0x6FA": return { motor_current: (readI16LE(bytes, 1) * 0.0078125), ecu_temp: (readU16LE(bytes, 3) * 0.5), supply_voltage: (readU16LE(bytes, 5) * 0.00390625) };
    case "0x6FB": return { motor_current: (readI16LE(bytes, 1) * 0.0078125), ecu_temp: (readU16LE(bytes, 3) * 0.5 - 40), supply_voltage: (readU16LE(bytes, 5) * 0.00390625) };
    case "0x721": { const angleRaw = (bytes[5] ?? 0) | (((bytes[6] ?? 0) & 0x0C) << 6); return { alignment_status: Boolean(bytes[0] & 1), control_enable_sts: Boolean(bytes[0] & 2), control_mode_sts: ((bytes[0] ?? 0) >> 2) & 3, auto_brake_sts: Boolean(bytes[0] & 0x10), error_status: ((bytes[0] ?? 0) >> 6) & 3, stroke_value: readU16LE(bytes, 2), pressure_value: bytes[3] ?? 0, angle_value: angleRaw, roll_cnt_enable_sts: Boolean((bytes[6] ?? 0) & 0x01), checksum_enable_sts: Boolean((bytes[6] ?? 0) & 0x02), rolling_counter: ((bytes[6] ?? 0) >> 4) & 0x0f, checksum: bytes[7] ?? 0 }; }
    case "0x731": return decodeSebFaults(bytes);
    case "0x741": return { sw_version: bytes[0] ?? 0, hw_version: bytes[1] ?? 0 };
    case "0x7B9": return { align_enable: Boolean(bytes[0] & 1), control_enable: Boolean(bytes[0] & 2), control_mode: ((bytes[0] ?? 0) >> 2) & 1, auto_brake: Boolean(bytes[0] & 8), stroke_req: readU16LE(bytes, 2), pressure_req: bytes[3] ?? 0, roll_cnt_enable: Boolean((bytes[6] ?? 0) & 0x01), checksum_enable: Boolean((bytes[6] ?? 0) & 0x02), rolling_counter: ((bytes[6] ?? 0) >> 4) & 0x0f, checksum: bytes[7] ?? 0 };
    case "0x7FC":
    case "0x7FD":
    case "0x7FE": return { alive_ctr: bytes[0] ?? 0, health_flags: bytes[1] ?? 0 };
    default: return { bus };
  }
}

export function validateDataBytes(data: unknown, dlc: number): number[] {
  if (!Array.isArray(data)) throw new Error("data must be an array");
  if (dlc < 0 || dlc > 8) throw new Error("dlc must be between 0 and 8");
  if (data.length !== dlc) throw new Error(`data length must match dlc (${dlc})`);
  return data.map((value, index) => {
    if (!Number.isInteger(value) || value < 0 || value > 255) throw new Error(`data[${index}] must be an integer byte`);
    return value;
  });
}

function emptyBusStats(): BusStats {
  return { active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {} };
}

function normalizeBusStats(input: Partial<BusStats> | undefined): BusStats {
  return { active: Boolean(input?.active), total: numberOr(input?.total, 0), fps: numberOr(input?.fps, 0), load_pct: numberOr(input?.load_pct, 0), tec: numberOr(input?.tec, 0), rec: numberOr(input?.rec, 0), by_id: input?.by_id && typeof input.by_id === "object" ? input.by_id : {} };
}

function normalizeBytes(data: number[]): number[] {
  const bytes = data.map((value) => Number(value) & 0xff);
  while (bytes.length < 8) bytes.push(0);
  return bytes;
}

function numberOr(input: unknown, fallback: number): number {
  return typeof input === "number" && Number.isFinite(input) ? input : fallback;
}

function decodeSesFaults(bytes: number[]): Record<string, unknown> {
  const mask = readU32LE(bytes, 0);
  return {
    fault_mask: mask,
    fault_mask_hex: `0x${mask.toString(16).toUpperCase().padStart(8, "0")}`,
    l3_fault: (mask & 0x003c3c00) !== 0,
    // Byte 0
    SES_ECUUnderVolt: Boolean(bytes[0] & 0x01),
    SES_ECUOverVolt: Boolean(bytes[0] & 0x02),
    SES_CanComErr: Boolean(bytes[0] & 0x04),
    SES_ECUTempErr: Boolean(bytes[0] & 0x08),
    SES_DomainSC: Boolean(bytes[0] & 0x10),
    SES_DomainV: Boolean(bytes[0] & 0x20),
    SES_DomainT: Boolean(bytes[0] & 0x40),
    SES_TempSensor: Boolean(bytes[0] & 0x80),
    // Byte 1
    SES_AngleP_OC: Boolean(bytes[1] & 0x01),
    SES_AngleP_AF: Boolean(bytes[1] & 0x02),
    SES_AngleS_OC: Boolean(bytes[1] & 0x04),
    SES_AngleS_AF: Boolean(bytes[1] & 0x08),
    SES_SensorPow: Boolean(bytes[1] & 0x10),
    SES_Alignment: Boolean(bytes[1] & 0x20),
    SES_OverAngle: Boolean(bytes[1] & 0x40),
    SES_StrMtrStall: Boolean(bytes[1] & 0x80),
    // Byte 2
    SES_MtrCurtFault: Boolean(bytes[2] & 0x01),
    SES_SensorCL: Boolean(bytes[2] & 0x02),
    SES_TorqT1_OC: Boolean(bytes[2] & 0x04),
    SES_TorqT1_AF: Boolean(bytes[2] & 0x08),
    SES_TorqT2_OC: Boolean(bytes[2] & 0x10),
    SES_TorqT2_AF: Boolean(bytes[2] & 0x20),
    SES_SentAngle: Boolean(bytes[2] & 0x40),
    SES_StrMtrIdling: Boolean(bytes[2] & 0x80),
    // Byte 3
    SES_EPROM: Boolean(bytes[3] & 0x01),
    // Byte 7
    SES_VehSpdSnapshot: bytes[7] ?? 0,
  };
}

function decodeSebFaults(bytes: number[]): Record<string, unknown> {
  const mask = readU32LE(bytes, 0);
  return {
    fault_mask: mask,
    fault_mask_hex: `0x${mask.toString(16).toUpperCase().padStart(8, "0")}`,
    l3_fault: (mask & 0x007e3ffc) !== 0,
    // Byte 0
    SEB_ECUUnderVolt: Boolean(bytes[0] & 0x01),
    SEB_ECUOverVolt: Boolean(bytes[0] & 0x02),
    SEB_CanComErr: Boolean(bytes[0] & 0x04),
    SEB_ECUTempErr: Boolean(bytes[0] & 0x08),
    SEB_DomainSC: Boolean(bytes[0] & 0x10),
    SEB_DomainV: Boolean(bytes[0] & 0x20),
    SEB_DomainT: Boolean(bytes[0] & 0x40),
    SEB_AngleP_OC: Boolean(bytes[0] & 0x80),
    // Byte 1
    SEB_AngleP_AF: Boolean(bytes[1] & 0x01),
    SEB_AngleS_OC: Boolean(bytes[1] & 0x02),
    SEB_AngleS_AF: Boolean(bytes[1] & 0x04),
    SEB_NoPreSensor: Boolean(bytes[1] & 0x08),
    SEB_SensorUCL: Boolean(bytes[1] & 0x20),
    SEB_AlignmentErr: Boolean(bytes[1] & 0x40),
    SEB_AngleOver: Boolean(bytes[1] & 0x80),
    // Byte 2
    SEB_MtrStall: Boolean(bytes[2] & 0x02),
    SEB_MtrDC: Boolean(bytes[2] & 0x04),
    SEB_OilErr: Boolean(bytes[2] & 0x08),
    SEB_InitOil: Boolean(bytes[2] & 0x10),
    SEB_SentValue: Boolean(bytes[2] & 0x20),
    SEB_MtrNoLoad: Boolean(bytes[2] & 0x40),
    // Byte 3
    SEB_PreSensorOver: Boolean(bytes[3] & 0x01),
    SEB_LowVoltCharging: Boolean(bytes[3] & 0x02),
  };
}
function modeName(mode: number): string {
  return MODE_OPTIONS.find((item) => item.value === mode)?.label ?? "?";
}
function gearName(gear: number): string {
  return GEAR_OPTIONS.find((item) => item.value === gear)?.label ?? "?";
}

// ── Bus auto-detection ─────────────────────────────────────────────────

/** CAN IDs that ONLY appear on the high bus — seeing any of these confirms the controller is on HIGH. */
const HIGH_UNIQUE_IDS = new Set(
  CAN_MESSAGES
    .filter((m) => m.bus === "high")
    .filter((m) => !CAN_MESSAGES.some((other) => other.bus === "low" && other.id === m.id))
    .map((m) => m.id)
);

/** CAN IDs that ONLY appear on the low bus — seeing any of these confirms the controller is on LOW. */
const LOW_UNIQUE_IDS = new Set(
  CAN_MESSAGES
    .filter((m) => m.bus === "low")
    .filter((m) => !CAN_MESSAGES.some((other) => other.bus === "high" && other.id === m.id))
    .map((m) => m.id)
);

export interface BusDetectorState {
  detected: boolean;
  bus: Bus;
  confidence: "none" | "low" | "high";
  highHits: number;
  lowHits: number;
}

/**
 * Auto-detects which physical CAN bus a controller is connected to
 * by observing CAN IDs. Unique IDs (like 0x300 for high, 0x169 for low)
 * are the fingerprint — seeing one confirms the bus assignment.
 */
export class BusDetector {
  private highHits = 0;
  private lowHits = 0;
  private locked: Bus | null = null;
  private lastFeedAt = 0;

  /** Auto-reset detection lock after this many seconds of silence. */
  private static readonly STALE_TIMEOUT_S = 10;

  /** Feed a CAN ID to the detector. Returns the best-guess bus. */
  feed(canId: string): Bus {
    this.lastFeedAt = Date.now() / 1000;
    if (this.locked) return this.locked;

    const id = normalizeCanId(canId);

    if (HIGH_UNIQUE_IDS.has(id)) {
      this.highHits += 1;
      if (this.highHits >= 3) this.locked = "high";
    } else if (LOW_UNIQUE_IDS.has(id)) {
      this.lowHits += 1;
      if (this.lowHits >= 3) this.locked = "low";
    }

    return this.locked ?? "high"; // default to high until detected
  }

  get state(): BusDetectorState {
    // Auto-reset lock if no frames for STALE_TIMEOUT_S
    if (this.locked && this.lastFeedAt > 0 && (Date.now() / 1000 - this.lastFeedAt) > BusDetector.STALE_TIMEOUT_S) {
      this.locked = null;
      this.highHits = 0;
      this.lowHits = 0;
    }
    if (this.locked) {
      return { detected: true, bus: this.locked, confidence: "high", highHits: this.highHits, lowHits: this.lowHits };
    }
    if (this.highHits > 0 && this.lowHits > 0) {
      // Mixed hits detected — report actual counts to aid debugging
      return { detected: false, bus: "high", confidence: "none", highHits: this.highHits, lowHits: this.lowHits };
    }
    if (this.highHits > 0) {
      return { detected: false, bus: "high", confidence: "low", highHits: this.highHits, lowHits: this.lowHits };
    }
    if (this.lowHits > 0) {
      return { detected: false, bus: "low", confidence: "low", highHits: this.highHits, lowHits: this.lowHits };
    }
    return { detected: false, bus: "high", confidence: "none", highHits: 0, lowHits: 0 };
  }

  reset(): void {
    this.highHits = 0;
    this.lowHits = 0;
    this.locked = null;
    this.lastFeedAt = 0;
  }
}
