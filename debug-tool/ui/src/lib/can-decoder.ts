export const BUSES = ["high", "low"] as const;
export type Bus = (typeof BUSES)[number];

export type CanId =
  | "0x001" | "0x011" | "0x012" | "0x110" | "0x120" | "0x169"
  | "0x201" | "0x202" | "0x203" | "0x204" | "0x205" | "0x206"
  | "0x210" | "0x220" | "0x300" | "0x301" | "0x302" | "0x400"
  | "0x600" | "0x6FA" | "0x6FB" | "0x721" | "0x731" | "0x741"
  | "0x7B9" | "0x7FC" | "0x7FD" | "0x7FE";

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

// ── CAN message catalog (mirrors backend types/can.ts) ──

const MODE_OPTIONS = [
  { label: "MANUAL", value: 0 },
  { label: "AUTO", value: 1 },
  { label: "ESTOP", value: 2 }
];

const GEAR_OPTIONS = [
  { label: "N", value: 0 },
  { label: "D", value: 1 },
  { label: "S", value: 2 },
  { label: "R", value: 3 }
];

const bool = (key: string, label: string): CanField => ({ key, label, kind: "boolean" });
const num = (key: string, label: string, unit?: string, min?: number, max?: number, step = 1): CanField => ({
  key, label, kind: "number", unit, min, max, step
});
const modeField: CanField = { key: "mode", label: "Mode", kind: "enum", options: MODE_OPTIONS };
const gearField: CanField = { key: "gear", label: "Gear", kind: "enum", options: GEAR_OPTIONS };

function msg(bus: Bus, id: string, name: string, sender: string, period: string, dlc: number, injectable: boolean, fields: CanField[]): CanMessageDef {
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

const CAN_BY_BUS_ID = new Map(CAN_MESSAGES.map((item) => [`${item.bus}:${item.id}`, item]));

export function findMessage(bus: Bus, id: string): CanMessageDef | undefined {
  const normalized = normalizeCanId(id);
  return CAN_BY_BUS_ID.get(`${bus}:${normalized}`) ?? CAN_MESSAGES.find((item) => item.id === normalized);
}

export function getMessageName(bus: Bus, id: string): string {
  return findMessage(bus, id)?.name ?? `UNKNOWN_${normalizeCanId(id)}`;
}

// ── Encode (injection) ──

export function encodePayload(bus: Bus, id: string, values: Record<string, number | boolean>): { dlc: number; data: number[] } {
  const bytes = Array.from({ length: 8 }, () => 0);
  const key = `${bus}:${normalizeCanId(id)}`;

  switch (key) {
    case "high:0x001":
    case "low:0x001":
      return { dlc: 0, data: [] };

    case "high:0x011":
    case "low:0x011":
      bytes[0] = values.estop_active ? 1 : 0;
      bytes[1] = values.heartbeat_ok ? 1 : 0;
      return { dlc: 2, data: bytes.slice(0, 2) };

    case "high:0x120":
    case "low:0x120":
      writeI16BE(bytes, 0, numberValue(values.speed_mmps));
      return { dlc: 2, data: bytes.slice(0, 2) };

    case "low:0x110":
      bytes[0] = numberValue(values.mode);
      return { dlc: 1, data: bytes.slice(0, 1) };

    case "low:0x169":
      bytes[0] = (values.control_enable ? 0x02 : 0) | (values.alignment_enable ? 0x01 : 0);
      writeI16LE(bytes, 2, numberValue(values.target_angle));
      bytes[4] = numberValue(values.target_speed) & 0xff;
      bytes[5] = ((numberValue(values.rolling_counter) & 0x0f) << 4) | ((numberValue(values.target_speed) >> 8) & 0x0f);
      bytes[7] = numberValue(values.checksum) & 0xff;
      return { dlc: 8, data: bytes };

    case "high:0x206":
    case "low:0x206":
      writeI16BE(bytes, 0, numberValue(values.actual_speed_mmps));
      bytes[2] = numberValue(values.gear_state);
      bytes[3] = numberValue(values.fault_flags);
      return { dlc: 4, data: bytes.slice(0, 4) };

    case "high:0x210":
      bytes[0] = numberValue(values.mode);
      bytes[1] = values.steer_valid ? 1 : 0;
      bytes[2] = values.reversing ? 1 : 0;
      return { dlc: 3, data: bytes.slice(0, 3) };

    case "high:0x300":
      writeI32BE(bytes, 0, numberValue(values.speed_mmps));
      writeI24BE(bytes, 4, numberValue(values.yaw_rate_mrad_s));
      bytes[7] = numberValue(values.gear);
      return { dlc: 8, data: bytes };

    case "high:0x301":
      writeI32BE(bytes, 0, numberValue(values.brake_pressure_kpa));
      return { dlc: 4, data: bytes.slice(0, 4) };

    case "high:0x302":
    case "low:0x302":
      bytes[0] =
        (values.left_turn ? 0x01 : 0) |
        (values.right_turn ? 0x02 : 0) |
        (values.brake_light ? 0x04 : 0) |
        (values.headlight ? 0x08 : 0);
      return { dlc: 1, data: bytes.slice(0, 1) };

    case "high:0x400":
      writeU32BE(bytes, 0, numberValue(values.distance_mm));
      return { dlc: 4, data: bytes.slice(0, 4) };

    case "low:0x204":
      writeI32BE(bytes, 0, numberValue(values.motor_speed_mmps));
      bytes[4] = numberValue(values.gear);
      return { dlc: 5, data: bytes.slice(0, 5) };

    case "low:0x205":
      writeI32BE(bytes, 0, numberValue(values.brake_pressure_kpa));
      return { dlc: 4, data: bytes.slice(0, 4) };

    case "low:0x7B9":
      bytes[0] =
        (values.align_enable ? 0x01 : 0) |
        (values.control_enable ? 0x02 : 0) |
        ((numberValue(values.control_mode) & 1) << 2);
      writeU16LE(bytes, 2, numberValue(values.stroke_req));
      bytes[3] = numberValue(values.pressure_req) & 0xff;
      bytes[6] = ((numberValue(values.rolling_counter) & 0x0f) << 4);
      bytes[7] = numberValue(values.checksum) & 0xff;
      return { dlc: 8, data: bytes };

    case "high:0x7FC":
    case "high:0x7FD":
    case "low:0x7FD":
    case "low:0x7FE":
      bytes[0] = numberValue(values.alive_ctr) & 0xff;
      return { dlc: 1, data: bytes.slice(0, 1) };

    default:
      return { dlc: 0, data: [] };
  }
}

// ── Formatting helpers ──

export function formatBytes(data: number[]): string {
  if (data.length === 0) return "--";
  return data.map((byte) => byte.toString(16).toUpperCase().padStart(2, "0")).join(" ");
}

export function formatDecoded(decoded: Record<string, unknown>): string {
  const entries = Object.entries(decoded).filter(([key]) => !key.endsWith("_name") && !key.endsWith("_label") && !key.endsWith("_hex"));
  if (entries.length === 0) return "event";

  return entries
    .map(([key, value]) => {
      const shortKey = key
        .replace("_mmps", "")
        .replace("_mrad_s", "")
        .replace("_kpa", "")
        .replace("_mm", "")
        .replaceAll("_", " ");
      return `${shortKey}=${String(value)}`;
    })
    .join("  ");
}

export function frameTime(frame: CanFrame): string {
  const seconds = frame.ts_real ?? frame.ts;
  const date = new Date(seconds * 1000);
  return date.toLocaleTimeString([], {
    hour12: false,
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    fractionalSecondDigits: 3
  });
}

export function normalizeCanId(id: string): string {
  const value = id.toLowerCase().startsWith("0x") ? Number.parseInt(id.slice(2), 16) : Number.parseInt(id, 16);
  if (!Number.isFinite(value)) return id.toUpperCase();
  return `0x${value.toString(16).toUpperCase().padStart(3, "0")}`;
}

export function normalizeBus(input: unknown): Bus {
  return input === "low" ? "low" : "high";
}

// ── Internal helpers ──

export function numberValue(value: unknown): number {
  if (typeof value === "boolean") return value ? 1 : 0;
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
}

function writeI16BE(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffff;
  bytes[offset] = (raw >> 8) & 0xff;
  bytes[offset + 1] = raw & 0xff;
}

function writeI24BE(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffffff;
  bytes[offset] = (raw >> 16) & 0xff;
  bytes[offset + 1] = (raw >> 8) & 0xff;
  bytes[offset + 2] = raw & 0xff;
}

function writeI32BE(bytes: number[], offset: number, value: number): void {
  bytes[offset] = (value >> 24) & 0xff;
  bytes[offset + 1] = (value >> 16) & 0xff;
  bytes[offset + 2] = (value >> 8) & 0xff;
  bytes[offset + 3] = value & 0xff;
}

function writeU32BE(bytes: number[], offset: number, value: number): void {
  const raw = value >>> 0;
  bytes[offset] = Math.floor(raw / 0x1000000) & 0xff;
  bytes[offset + 1] = (raw >> 16) & 0xff;
  bytes[offset + 2] = (raw >> 8) & 0xff;
  bytes[offset + 3] = raw & 0xff;
}

function writeI16LE(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffff;
  bytes[offset] = raw & 0xff;
  bytes[offset + 1] = (raw >> 8) & 0xff;
}

function writeU16LE(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffff;
  bytes[offset] = raw & 0xff;
  bytes[offset + 1] = (raw >> 8) & 0xff;
}
