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
const gearField: CanField = { key: "gear", label: "Gear", kind: "enum", options: GEAR_OPTIONS };

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

const safetyFields = [bool("estop_active", "ESTOP active"), bool("heartbeat_ok", "Heartbeat OK")];
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
  bool("hb_ok", "Heartbeat OK"),
  bool("estop_active", "ESTOP active"),
  num("free_heap_kb", "Free heap", "KB", 0, 65535),
  num("tec", "TEC", undefined, 0, 255),
  num("rec", "REC", undefined, 0, 255)
];
const heartbeatFields = [num("alive_ctr", "Alive counter", undefined, 0, 255)];

export const CAN_MESSAGES: CanMessageDef[] = [
  msg("high", "0x001", "SAFETY_ESTOP", "any", "event", 0, true, []),
  msg("high", "0x011", "SYS_SAFETY_STS", "RT (fwd)", "5 Hz", 2, true, safetyFields),
  msg("high", "0x120", "SYS_THROTTLE_STS", "RT (fwd)", "100 Hz", 2, true, throttleFields),
  msg("high", "0x206", "MTR_MOTOR_FBK", "RT (fwd)", "50 Hz", 4, true, motorFeedbackFields),
  msg("high", "0x210", "RT_STATE_RPT", "RT", "10 Hz", 3, true, [modeField, bool("steer_valid", "Steer valid"), bool("reversing", "Reversing")]),
  msg("high", "0x220", "RT_PID_RPT", "RT", "reserved", 6, false, [num("speed_setpoint", "Setpoint", "mm/s"), num("speed_measured", "Measured", "mm/s"), num("pid_output", "PID output")]),
  msg("high", "0x300", "HOST_DRIVE_CMD", "Jetson", "<=100 Hz", 8, true, [num("speed_mmps", "Speed", "mm/s", -500, 3000, 10), num("yaw_rate_mrad_s", "Yaw rate", "mrad/s", -3000, 3000, 10), gearField]),
  msg("high", "0x301", "HOST_BRAKE_REQ", "Jetson", "demand", 4, true, [num("brake_pressure_kpa", "Brake pressure", "kPa", 0, 20000, 100)]),
  msg("high", "0x302", "HOST_LIGHT_CMD", "Jetson", "change", 1, true, lightFields),
  msg("high", "0x400", "HOST_OBSTACLE_DIST", "Jetson", "10 Hz", 4, true, [num("distance_mm", "Distance", "mm", 0, 4294967295, 10)]),
  msg("high", "0x600", "SYS_DIAG_RPT", "RT (fwd)", "1 Hz", 8, false, diagFields),
  msg("high", "0x7FC", "JETSON_HEARTBEAT", "Jetson", "2 Hz", 1, true, heartbeatFields),
  msg("high", "0x7FD", "RT_HEARTBEAT", "RT", "2 Hz", 1, false, heartbeatFields),

  msg("low", "0x001", "SAFETY_ESTOP", "any", "event", 0, true, []),
  msg("low", "0x011", "SYS_SAFETY_STS", "SYS", "5 Hz", 2, true, safetyFields),
  msg("low", "0x012", "SYS_DCDC_CMD", "SYS", "change", 1, false, [bool("enable", "Enable")]),
  msg("low", "0x110", "SYS_MODE_CMD", "SYS", "change", 1, true, [modeField]),
  msg("low", "0x120", "SYS_THROTTLE_STS", "MTR", "100 Hz", 2, true, throttleFields),
  msg("low", "0x169", "VCU_SES_REQ", "RT", "50 Hz", 8, true, [num("target_angle", "Target angle", "0.1 deg", -3000, 780), num("target_speed", "Target speed", "deg/s", 125, 1250), bool("control_enable", "Control enable"), num("rolling_counter", "Rolling counter", undefined, 0, 15), num("checksum", "Checksum", undefined, 0, 255)]),
  msg("low", "0x201", "SES_STATUS", "EPS-C", "100 Hz", 8, true, [bool("angle_status", "Angle status"), num("str_angle", "Steer angle", "0.1 deg"), num("tgt_angle_spd", "Target angle speed", "deg/s"), num("error_status", "Error status", undefined, 0, 3)]),
  msg("low", "0x202", "SES_ERRINFO", "EPS-C", "10 Hz", 8, false, [num("fault_mask", "Fault mask")]),
  msg("low", "0x203", "SES_VERSION", "EPS-C", "1 Hz", 8, false, [num("sw_version", "SW version"), num("hw_version", "HW version")]),
  msg("low", "0x204", "RT_DRIVE_CMD", "RT", "100 Hz", 5, true, [num("motor_speed_mmps", "Motor speed", "mm/s", -500, 3000, 10), gearField]),
  msg("low", "0x205", "RT_BRAKE_CMD", "RT", "50 Hz", 4, true, [num("brake_pressure_kpa", "Brake pressure", "kPa", 0, 20000, 100)]),
  msg("low", "0x206", "MTR_MOTOR_FBK", "MTR", "50 Hz", 4, true, motorFeedbackFields),
  msg("low", "0x302", "HOST_LIGHT_CMD", "RT (fwd)", "change", 1, true, lightFields),
  msg("low", "0x600", "SYS_DIAG_RPT", "SYS", "1 Hz", 8, false, diagFields),
  msg("low", "0x6FA", "SES_TEST", "EPS-C", "100 Hz", 8, false, [num("motor_current", "Motor current"), num("ecu_temp", "ECU temp"), num("supply_voltage", "Supply voltage")]),
  msg("low", "0x6FB", "SEB_TEST", "SEB", "100 Hz", 8, false, [num("motor_current", "Motor current"), num("ecu_temp", "ECU temp"), num("supply_voltage", "Supply voltage")]),
  msg("low", "0x721", "SEB_STATUS", "SEB", "100 Hz", 8, true, [num("stroke_value", "Stroke value", "raw"), num("pressure_value", "Pressure value", "raw"), num("angle_value", "Angle value", "raw"), num("error_status", "Error status", undefined, 0, 3)]),
  msg("low", "0x731", "SEB_ERRINFO", "SEB", "10 Hz", 8, false, [num("fault_mask", "Fault mask")]),
  msg("low", "0x741", "SEB_VERSION", "SEB", "1 Hz", 8, false, [num("sw_version", "SW version"), num("hw_version", "HW version")]),
  msg("low", "0x7B9", "VCU_SEB_REQ", "RT/SYS", "50 Hz", 8, true, [num("stroke_req", "Stroke request", "raw"), num("pressure_req", "Pressure request", "raw"), num("control_mode", "Control mode", undefined, 0, 3), num("rolling_counter", "Rolling counter", undefined, 0, 15), num("checksum", "Checksum", undefined, 0, 255)]),
  msg("low", "0x7FD", "RT_HEARTBEAT", "RT", "2 Hz", 1, false, heartbeatFields),
  msg("low", "0x7FE", "SYS_HEARTBEAT", "SYS", "10 Hz", 1, false, heartbeatFields)
];

export const CAN_BY_BUS_ID = new Map(CAN_MESSAGES.map((item) => [`${item.bus}:${item.id}`, item]));

export const INJECTION_TEMPLATES: InjectionTemplate[] = [
  { bus: "high", id: "0x300", name: "High drive 2.0 m/s", description: "Jetson drive command in D gear.", dlc: 8, values: { speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1 } },
  { bus: "high", id: "0x301", name: "High brake 5 MPa", description: "Jetson brake request.", dlc: 4, values: { brake_pressure_kpa: 5000 } },
  { bus: "low", id: "0x204", name: "Low motor 2.0 m/s", description: "RT to SYS drive command.", dlc: 5, values: { motor_speed_mmps: 2000, gear: 1 } },
  { bus: "low", id: "0x205", name: "Low brake 5 MPa", description: "RT to SYS brake command.", dlc: 4, values: { brake_pressure_kpa: 5000 } },
  { bus: "low", id: "0x169", name: "Steer center", description: "EPS-C steering command.", dlc: 8, values: { target_angle: 0, target_speed: 328, control_enable: true, rolling_counter: 1, checksum: 0 } },
  { bus: "high", id: "0x7FC", name: "Jetson heartbeat", description: "Single high-bus Jetson heartbeat.", dlc: 1, values: { alive_ctr: 1 } }
];

export function normalizeBus(input: unknown): Bus {
  return input === "low" ? "low" : "high";
}

export function normalizeCanId(input: string | number): string {
  if (typeof input === "number") return `0x${input.toString(16).toUpperCase().padStart(3, "0")}`;
  const trimmed = input.trim();
  const value = trimmed.toLowerCase().startsWith("0x") ? Number.parseInt(trimmed.slice(2), 16) : Number.parseInt(trimmed, 16);
  return Number.isFinite(value) ? `0x${value.toString(16).toUpperCase().padStart(3, "0")}` : trimmed.toUpperCase();
}

export function findMessage(bus: Bus, id: string): CanMessageDef | undefined {
  const normalized = normalizeCanId(id);
  return CAN_BY_BUS_ID.get(`${bus}:${normalized}`) ?? CAN_MESSAGES.find((item) => item.id === normalized);
}

export function getMessageName(bus: Bus, id: string): string {
  return findMessage(bus, id)?.name ?? `UNKNOWN_${normalizeCanId(id)}`;
}

export function defaultStats(): CanStats {
  return { ts: Date.now() / 1000, uptime_s: 0, buses: { high: emptyBusStats(), low: emptyBusStats() } };
}

export function normalizeStats(input: Partial<CanStats> & Record<string, unknown>): CanStats {
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
  return { ts: typeof input.ts === "number" ? input.ts : Date.now(), bus, id, name: input.name ?? getMessageName(bus, id), dlc, data: fullData.slice(0, dlc), decoded };
}

export function decodeFrame(bus: Bus, id: string, data: number[]): Record<string, unknown> {
  const bytes = normalizeBytes(data);
  switch (normalizeCanId(id)) {
    case "0x001": return {};
    case "0x011": return { estop_active: bytes[0] !== 0, heartbeat_ok: bytes[1] !== 0 };
    case "0x012": return { enable: bytes[0] !== 0 };
    case "0x110": return { mode: bytes[0] ?? 0, mode_name: modeName(bytes[0] ?? 0) };
    case "0x120": return { speed_mmps: readI16BE(bytes, 0) };
    case "0x169": return { alignment_enable: Boolean(bytes[0] & 1), control_enable: Boolean(bytes[0] & 2), target_angle: readI16LE(bytes, 2), target_speed: (bytes[4] ?? 0) | (((bytes[5] ?? 0) & 0x0f) << 8), rolling_counter: ((bytes[5] ?? 0) >> 4) & 0x0f, checksum: bytes[7] ?? 0 };
    case "0x201": return { angle_status: Boolean(bytes[0] & 1), control_mode_sts: ((bytes[0] ?? 0) >> 1) & 3, error_status: ((bytes[0] ?? 0) >> 6) & 3, str_angle: readI16LE(bytes, 2), tgt_angle_spd: readI16LE(bytes, 4), rolling_counter: ((bytes[6] ?? 0) >> 4) & 0x0f, checksum: bytes[7] ?? 0 };
    case "0x202": return decodeFaultMask(bytes, "ses");
    case "0x203": return { sw_version: bytes[0] ?? 0, hw_version: bytes[1] ?? 0 };
    case "0x204": return { motor_speed_mmps: readI32BE(bytes, 0), gear: bytes[4] ?? 0, gear_name: gearName(bytes[4] ?? 0) };
    case "0x205": return { brake_pressure_kpa: readI32BE(bytes, 0) };
    case "0x206": return { actual_speed_mmps: readI16BE(bytes, 0), gear_state: bytes[2] ?? 0, gear_name: gearName(bytes[2] ?? 0), fault_flags: bytes[3] ?? 0 };
    case "0x210": return { mode: bytes[0] ?? 0, mode_name: modeName(bytes[0] ?? 0), steer_valid: bytes[1] !== 0, reversing: bytes[2] !== 0 };
    case "0x220": return { speed_setpoint: readI16BE(bytes, 0), speed_measured: readI16BE(bytes, 2), pid_output: readI16BE(bytes, 4) };
    case "0x300": return { speed_mmps: readI32BE(bytes, 0), yaw_rate_mrad_s: readI24BE(bytes, 4), gear: bytes[7] ?? 0, gear_name: gearName(bytes[7] ?? 0) };
    case "0x301": return { brake_pressure_kpa: readI32BE(bytes, 0) };
    case "0x302": return { left_turn: Boolean(bytes[0] & 1), right_turn: Boolean(bytes[0] & 2), brake_light: Boolean(bytes[0] & 4), headlight: Boolean(bytes[0] & 8) };
    case "0x400": { const distance = readU32BE(bytes, 0); return { distance_mm: distance, distance_label: distance === 0xffffffff ? "clear" : `${distance} mm` }; }
    case "0x600": return { mode: bytes[0] ?? 0, mode_name: modeName(bytes[0] ?? 0), brake_engaged: bytes[1] !== 0, hb_ok: bytes[2] !== 0, estop_active: bytes[3] !== 0, free_heap_kb: readU16BE(bytes, 4), tec: bytes[6] ?? 0, rec: bytes[7] ?? 0 };
    case "0x6FA":
    case "0x6FB": return { motor_current: readI16LE(bytes, 1), ecu_temp: readU16LE(bytes, 3), supply_voltage: readU16LE(bytes, 5) };
    case "0x721": return { alignment_status: Boolean(bytes[0] & 1), control_enable_sts: Boolean(bytes[0] & 2), control_mode_sts: ((bytes[0] ?? 0) >> 2) & 3, auto_brake_sts: Boolean(bytes[0] & 0x10), error_status: ((bytes[0] ?? 0) >> 6) & 3, stroke_value: readU16LE(bytes, 2), pressure_value: bytes[3] ?? 0, angle_value: readI16LE(bytes, 5), rolling_counter: ((bytes[6] ?? 0) >> 4) & 0x0f, checksum: bytes[7] ?? 0 };
    case "0x731": return decodeFaultMask(bytes, "seb");
    case "0x741": return { sw_version: bytes[0] ?? 0, hw_version: bytes[1] ?? 0 };
    case "0x7B9": return { align_enable: Boolean(bytes[0] & 1), control_enable: Boolean(bytes[0] & 2), control_mode: ((bytes[0] ?? 0) >> 2) & 1, auto_brake: Boolean(bytes[0] & 8), stroke_req: readU16LE(bytes, 2), pressure_req: bytes[3] ?? 0, rolling_counter: ((bytes[6] ?? 0) >> 4) & 0x0f, checksum: bytes[7] ?? 0 };
    case "0x7FC":
    case "0x7FD":
    case "0x7FE": return { alive_ctr: bytes[0] ?? 0 };
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

function readI16BE(bytes: number[], offset: number): number {
  const value = ((bytes[offset] ?? 0) << 8) | (bytes[offset + 1] ?? 0);
  return value & 0x8000 ? value - 0x10000 : value;
}
function readU16BE(bytes: number[], offset: number): number {
  return (((bytes[offset] ?? 0) << 8) | (bytes[offset + 1] ?? 0)) >>> 0;
}
function readI16LE(bytes: number[], offset: number): number {
  const value = (bytes[offset] ?? 0) | ((bytes[offset + 1] ?? 0) << 8);
  return value & 0x8000 ? value - 0x10000 : value;
}
function readU16LE(bytes: number[], offset: number): number {
  return ((bytes[offset] ?? 0) | ((bytes[offset + 1] ?? 0) << 8)) >>> 0;
}
function readI24BE(bytes: number[], offset: number): number {
  const value = ((bytes[offset] ?? 0) << 16) | ((bytes[offset + 1] ?? 0) << 8) | (bytes[offset + 2] ?? 0);
  return value & 0x800000 ? value - 0x1000000 : value;
}
function readI32BE(bytes: number[], offset: number): number {
  return ((bytes[offset] ?? 0) << 24) | ((bytes[offset + 1] ?? 0) << 16) | ((bytes[offset + 2] ?? 0) << 8) | (bytes[offset + 3] ?? 0);
}
function readU32BE(bytes: number[], offset: number): number {
  return (((bytes[offset] ?? 0) * 0x1000000) + ((bytes[offset + 1] ?? 0) << 16) + ((bytes[offset + 2] ?? 0) << 8) + (bytes[offset + 3] ?? 0)) >>> 0;
}
function readU32LE(bytes: number[], offset: number): number {
  return ((bytes[offset] ?? 0) + ((bytes[offset + 1] ?? 0) << 8) + ((bytes[offset + 2] ?? 0) << 16) + ((bytes[offset + 3] ?? 0) * 0x1000000)) >>> 0;
}
function decodeFaultMask(bytes: number[], prefix: "ses" | "seb"): Record<string, unknown> {
  const faultMask = readU32LE(bytes, 0);
  return { fault_mask: faultMask, fault_mask_hex: `0x${faultMask.toString(16).toUpperCase().padStart(8, "0")}`, l3_fault: prefix === "seb" ? (faultMask & 0x007e3ffc) !== 0 : (faultMask & 0x003c3c00) !== 0 };
}
function modeName(mode: number): string {
  return MODE_OPTIONS.find((item) => item.value === mode)?.label ?? "?";
}
function gearName(gear: number): string {
  return GEAR_OPTIONS.find((item) => item.value === gear)?.label ?? "?";
}
