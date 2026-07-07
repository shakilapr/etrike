// WARNING: This CAN message catalog is hand-maintained.
// When adding/changing messages, also update the duplicate copy in:
//   debug-tool/backend/src/types/can.ts
// The single source of truth is: shared/can/can_signals.yaml

export const BUSES = ["high", "low"] as const;
export type Bus = (typeof BUSES)[number];

export type CanId =
  | "0x001" | "0x011" | "0x012" | "0x110" | "0x120" | "0x169"
  | "0x201" | "0x202" | "0x203" | "0x204" | "0x205" | "0x206"
  | "0x210" | "0x220" | "0x300" | "0x301" | "0x302"
  | "0x310" | "0x311"
  | "0x400" | "0x600" | "0x6FA" | "0x6FB" | "0x721" | "0x731" | "0x741"
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

const MODE_CMD_OPTIONS = [
  { label: "MANUAL", value: 0 },
  { label: "AUTO", value: 1 }
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

function msg(bus: Bus, id: string, name: string, sender: string, period: string, dlc: number, injectable: boolean, fields: CanField[]): CanMessageDef {
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
      bytes[2] = (values.light_left ? 0x01 : 0) | (values.light_right ? 0x02 : 0) | (values.light_brake ? 0x04 : 0) | (values.light_head ? 0x08 : 0);
      return { dlc: 3, data: bytes.slice(0, 3) };

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
      bytes[5] = ((numberValue(values.rolling_counter) & 0x0f) << 4) | ((numberValue(values.target_speed) >> 8) & 0x03) << 2 | 0x03;  // bits 0-1: RollCntEn + ChecksumEn (MUST be 1), bits 2-3: speed[9:8]
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
      bytes[1] = ((numberValue(values.safety_state) & 0x03)) | ((numberValue(values.estop_reason) & 0x0f) << 4);
      bytes[2] = values.reversing ? 1 : 0;
      bytes[3] = values.rx_overflow !== undefined ? numberValue(values.rx_overflow) & 0xff : 0;
      bytes[4] = numberValue(values.task_health) & 0xff;
      bytes[5] = numberValue(values.steer_state) & 0xff;
      return { dlc: 6, data: bytes.slice(0, 6) };

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

    case "high:0x310":
      writeI16BE(bytes, 0, numberValue(values.SteerDiag_Angle0_1deg));
      bytes[2] = values.SteerDiag_Fault ? 1 : 0;
      writeI16BE(bytes, 3, numberValue(values.SteerDiag_MotorCurrent));
      writeI16BE(bytes, 5, numberValue(values.SteerDiag_ECUTemp));
      bytes[7] = 0;
      return { dlc: 8, data: bytes };

    case "high:0x311":
      writeI16BE(bytes, 0, numberValue(values.BrakeDiag_PressureRaw));
      bytes[2] = values.BrakeDiag_Fault ? 1 : 0;
      writeI16BE(bytes, 3, numberValue(values.BrakeDiag_MotorCurrent));
      writeI16BE(bytes, 5, numberValue(values.BrakeDiag_ECUTemp));
      bytes[7] = 0;
      return { dlc: 8, data: bytes };

    case "low:0x204":
      writeI32BE(bytes, 0, numberValue(values.motor_speed_mmps));
      bytes[4] = numberValue(values.gear);
      return { dlc: 5, data: bytes.slice(0, 5) };

    case "low:0x205":
      writeI32BE(bytes, 0, numberValue(values.brake_pressure_kpa));
      return { dlc: 4, data: bytes.slice(0, 4) };

    case "low:0x201":
      bytes[0] =
        (values.angle_status ? 0x01 : 0) |
        ((numberValue(values.control_mode_sts) & 3) << 1) |
        ((numberValue(values.error_status) & 3) << 6);
      writeI16LE(bytes, 2, numberValue(values.str_angle));
      writeI16LE(bytes, 4, numberValue(values.tgt_angle_spd));
      if (values.torque !== undefined) bytes[5] = Math.round((numberValue(values.torque) + 12.1) / 0.1) & 0xff;
      bytes[6] = 0x03 | ((numberValue(values.rolling_counter) & 0x0f) << 4);  // bits 0-1: roll_cnt_en_sts + checksum_en_sts (MUST be 1)
      bytes[7] = numberValue(values.checksum) & 0xff;
      return { dlc: 8, data: bytes };

    case "low:0x202": {
      // SES_ERRINFO: bytes 0-3 = fault mask u32 LE, byte 7 = VehSpdSnapshot
      const fm202 = numberValue(values.fault_mask);
      bytes[0] = fm202 & 0xff;
      bytes[1] = (fm202 >> 8) & 0xff;
      bytes[2] = (fm202 >> 16) & 0xff;
      bytes[3] = (fm202 >>> 24) & 0xff;
      if (values.SES_VehSpdSnapshot !== undefined) bytes[7] = numberValue(values.SES_VehSpdSnapshot) & 0xff;
      return { dlc: 8, data: bytes };
    }

    case "low:0x731": {
      // SEB_ERRINFO: bytes 0-3 = fault mask u32 LE
      const fm731 = numberValue(values.fault_mask);
      bytes[0] = fm731 & 0xff;
      bytes[1] = (fm731 >> 8) & 0xff;
      bytes[2] = (fm731 >> 16) & 0xff;
      bytes[3] = (fm731 >>> 24) & 0xff;
      return { dlc: 8, data: bytes };
    }

    case "low:0x721":
      bytes[0] =
        (values.alignment_status ? 0x01 : 0) |
        (values.control_enable_sts ? 0x02 : 0) |
        ((numberValue(values.control_mode_sts) & 3) << 2) |
        ((numberValue(values.error_status) & 3) << 6);
      writeU16LE(bytes, 2, numberValue(values.stroke_value));
      bytes[3] = numberValue(values.pressure_value) & 0xff;
      // Angle: 12-bit effective (bits 8-9 in byte 6 bits 2-3, upper nibble overlaid by security echo)
      const angleVal = numberValue(values.angle_value) & 0x0FFF;
      bytes[5] = angleVal & 0xFF;
      bytes[6] = 1                          // bit 0: RollCntEnStatus
               | (1 << 1)                    // bit 1: ChecksumEnStatus
               | (((angleVal >> 8) & 0x3) << 2)  // bits 2-3: angle bits 9-8
               | ((numberValue(values.rolling_counter) & 0xF) << 4);  // bits 4-7: RollCntStatus
      bytes[7] = numberValue(values.checksum) & 0xff;
      return { dlc: 8, data: bytes };

    case "low:0x7B9":
      bytes[0] =
        (values.align_enable ? 0x01 : 0) |
        (values.control_enable ? 0x02 : 0) |
        ((numberValue(values.control_mode) & 1) << 2) |
        (values.auto_brake ? 0x08 : 0);
      writeU16LE(bytes, 2, numberValue(values.stroke_req));
      bytes[3] = numberValue(values.pressure_req) & 0xff;
      bytes[6] = 0x03 | ((numberValue(values.rolling_counter) & 0x0f) << 4);  // bits 0-1: roll_cnt_en + checksum_en (MUST be 1)
      bytes[7] = numberValue(values.checksum) & 0xff;
      return { dlc: 8, data: bytes };

    case "high:0x7FC":
    case "low:0x7FD":
    case "low:0x7FE":
      bytes[0] = numberValue(values.alive_ctr) & 0xff;
      bytes[1] = numberValue(values.health_flags) & 0xff;
      return { dlc: 2, data: bytes.slice(0, 2) };

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
  const seconds = normalizeTimestampSeconds(frame.ts_real ?? frame.ts);
  const date = new Date(seconds * 1000);
  return date.toLocaleTimeString([], {
    hour12: false,
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    fractionalSecondDigits: 3
  });
}

export function frameAge(frame: { ts_real?: number; ts?: number } | undefined): string {
  if (!frame) return "--";
  const stamp = normalizeTimestampSeconds(frame.ts_real ?? frame.ts);
  if (!stamp || stamp <= 0) return "--";
  const age = Math.max(Date.now() / 1000 - stamp, 0);
  if (age < 1) return `${Math.round(age * 1000)} ms`;
  if (age < 60) return `${age.toFixed(1)} s`;
  return `${Math.round(age)} s`;
}

function normalizeTimestampSeconds(stamp: number | undefined): number {
  if (!stamp) return 0;
  return stamp > 1_000_000_000_000 ? stamp / 1000 : stamp;
}

export function normalizeCanId(id: string): string {
  const value = id.toLowerCase().startsWith("0x") ? Number.parseInt(id.slice(2), 16) : Number.parseInt(id, 16);
  if (!Number.isFinite(value)) return id.toUpperCase();
  return `0x${value.toString(16).toUpperCase().padStart(3, "0")}`;
}

export function normalizeBus(input: unknown): Bus {
  if (input === undefined || input === null || input === "") return "high";
  if (input === "high" || input === "low") return input;
  throw new Error(`invalid CAN bus: ${String(input)}`);
}

// ── Internal helpers ──

export function numberValue(value: unknown): number {
  if (typeof value === "boolean") return value ? 1 : 0;
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
}

export function writeI16BE(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffff;
  bytes[offset] = (raw >> 8) & 0xff;
  bytes[offset + 1] = raw & 0xff;
}

export function writeI24BE(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffffff;
  bytes[offset] = (raw >> 16) & 0xff;
  bytes[offset + 1] = (raw >> 8) & 0xff;
  bytes[offset + 2] = raw & 0xff;
}

export function writeI32BE(bytes: number[], offset: number, value: number): void {
  bytes[offset] = (value >> 24) & 0xff;
  bytes[offset + 1] = (value >> 16) & 0xff;
  bytes[offset + 2] = (value >> 8) & 0xff;
  bytes[offset + 3] = value & 0xff;
}

export function writeU32BE(bytes: number[], offset: number, value: number): void {
  const raw = value >>> 0;
  bytes[offset] = Math.floor(raw / 0x1000000) & 0xff;
  bytes[offset + 1] = (raw >> 16) & 0xff;
  bytes[offset + 2] = (raw >> 8) & 0xff;
  bytes[offset + 3] = raw & 0xff;
}

export function writeI16LE(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffff;
  bytes[offset] = raw & 0xff;
  bytes[offset + 1] = (raw >> 8) & 0xff;
}

export function writeU16LE(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffff;
  bytes[offset] = raw & 0xff;
  bytes[offset + 1] = (raw >> 8) & 0xff;
}

// ── Decode (mirrors backend/src/types/can.ts decodeFrame — keep in sync) ──

function normalizeBytes(data: number[]): number[] {
  const bytes = data.map((value) => Number(value) & 0xff);
  while (bytes.length < 8) bytes.push(0);
  return bytes;
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

function decodeSesFaults(bytes: number[]): Record<string, unknown> {
  const mask = readU32LE(bytes, 0);
  return {
    fault_mask: mask,
    fault_mask_hex: `0x${mask.toString(16).toUpperCase().padStart(8, "0")}`,
    l3_fault: (mask & 0x003c3c00) !== 0,
    SES_ECUUnderVolt: Boolean(bytes[0] & 0x01),
    SES_ECUOverVolt: Boolean(bytes[0] & 0x02),
    SES_CanComErr: Boolean(bytes[0] & 0x04),
    SES_ECUTempErr: Boolean(bytes[0] & 0x08),
    SES_DomainSC: Boolean(bytes[0] & 0x10),
    SES_DomainV: Boolean(bytes[0] & 0x20),
    SES_DomainT: Boolean(bytes[0] & 0x40),
    SES_TempSensor: Boolean(bytes[0] & 0x80),
    SES_AngleP_OC: Boolean(bytes[1] & 0x01),
    SES_AngleP_AF: Boolean(bytes[1] & 0x02),
    SES_AngleS_OC: Boolean(bytes[1] & 0x04),
    SES_AngleS_AF: Boolean(bytes[1] & 0x08),
    SES_SensorPow: Boolean(bytes[1] & 0x10),
    SES_Alignment: Boolean(bytes[1] & 0x20),
    SES_OverAngle: Boolean(bytes[1] & 0x40),
    SES_StrMtrStall: Boolean(bytes[1] & 0x80),
    SES_MtrCurtFault: Boolean(bytes[2] & 0x01),
    SES_SensorCL: Boolean(bytes[2] & 0x02),
    SES_TorqT1_OC: Boolean(bytes[2] & 0x04),
    SES_TorqT1_AF: Boolean(bytes[2] & 0x08),
    SES_TorqT2_OC: Boolean(bytes[2] & 0x10),
    SES_TorqT2_AF: Boolean(bytes[2] & 0x20),
    SES_SentAngle: Boolean(bytes[2] & 0x40),
    SES_StrMtrIdling: Boolean(bytes[2] & 0x80),
    SES_EPROM: Boolean(bytes[3] & 0x01),
    SES_VehSpdSnapshot: bytes[7] ?? 0,
  };
}

function decodeSebFaults(bytes: number[]): Record<string, unknown> {
  const mask = readU32LE(bytes, 0);
  return {
    fault_mask: mask,
    fault_mask_hex: `0x${mask.toString(16).toUpperCase().padStart(8, "0")}`,
    l3_fault: (mask & 0x007e3ffc) !== 0,
    SEB_ECUUnderVolt: Boolean(bytes[0] & 0x01),
    SEB_ECUOverVolt: Boolean(bytes[0] & 0x02),
    SEB_CanComErr: Boolean(bytes[0] & 0x04),
    SEB_ECUTempErr: Boolean(bytes[0] & 0x08),
    SEB_DomainSC: Boolean(bytes[0] & 0x10),
    SEB_DomainV: Boolean(bytes[0] & 0x20),
    SEB_DomainT: Boolean(bytes[0] & 0x40),
    SEB_AngleP_OC: Boolean(bytes[0] & 0x80),
    SEB_AngleP_AF: Boolean(bytes[1] & 0x01),
    SEB_AngleS_OC: Boolean(bytes[1] & 0x02),
    SEB_AngleS_AF: Boolean(bytes[1] & 0x04),
    SEB_NoPreSensor: Boolean(bytes[1] & 0x08),
    SEB_SensorUCL: Boolean(bytes[1] & 0x20),
    SEB_AlignmentErr: Boolean(bytes[1] & 0x40),
    SEB_AngleOver: Boolean(bytes[1] & 0x80),
    SEB_MtrStall: Boolean(bytes[2] & 0x02),
    SEB_MtrDC: Boolean(bytes[2] & 0x04),
    SEB_OilErr: Boolean(bytes[2] & 0x08),
    SEB_InitOil: Boolean(bytes[2] & 0x10),
    SEB_SentValue: Boolean(bytes[2] & 0x20),
    SEB_MtrNoLoad: Boolean(bytes[2] & 0x40),
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
