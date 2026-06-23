export type CanId =
  | "0x001"
  | "0x011"
  | "0x120"
  | "0x210"
  | "0x220"
  | "0x300"
  | "0x301"
  | "0x302"
  | "0x400"
  | "0x600"
  | "0x7FC"
  | "0x7FD";

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
  id: CanId;
  name: string;
  dlc: number;
  period: string;
  injectable: boolean;
  fields: CanField[];
}

export interface CanFrame {
  ts: number;
  id: CanId | string;
  name: string;
  dlc: number;
  data: number[];
  decoded: Record<string, unknown>;
}

export interface CanStats {
  ts: number;
  uptime_s: number;
  total_frames: number;
  frames_per_s: number;
  bus_load_pct: number;
  tec: number;
  rec: number;
  by_id: Record<string, number>;
}

export interface InjectionTemplate {
  id: CanId;
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

export const CAN_MESSAGES: CanMessageDef[] = [
  {
    id: "0x001",
    name: "SAFETY_ESTOP",
    dlc: 0,
    period: "event",
    injectable: true,
    fields: []
  },
  {
    id: "0x011",
    name: "SYS_SAFETY_STS",
    dlc: 2,
    period: "5 Hz",
    injectable: true,
    fields: [
      { key: "estop_active", label: "ESTOP active", kind: "boolean" },
      { key: "heartbeat_ok", label: "Heartbeat OK", kind: "boolean" }
    ]
  },
  {
    id: "0x120",
    name: "SYS_THROTTLE_STS",
    dlc: 2,
    period: "100 Hz",
    injectable: true,
    fields: [
      {
        key: "speed_mmps",
        label: "Speed",
        kind: "number",
        unit: "mm/s",
        min: -500,
        max: 3000,
        step: 10
      }
    ]
  },
  {
    id: "0x210",
    name: "RT_STATE_RPT",
    dlc: 3,
    period: "10 Hz",
    injectable: false,
    fields: [
      { key: "mode", label: "Mode", kind: "enum", options: MODE_OPTIONS },
      { key: "steer_valid", label: "Steer valid", kind: "boolean" },
      { key: "reversing", label: "Reversing", kind: "boolean" }
    ]
  },
  {
    id: "0x220",
    name: "RT_PID_RPT",
    dlc: 6,
    period: "reserved",
    injectable: false,
    fields: [
      {
        key: "speed_setpoint_mmps",
        label: "Setpoint",
        kind: "number",
        unit: "mm/s",
        min: -500,
        max: 3000,
        step: 10
      },
      {
        key: "speed_measured_mmps",
        label: "Measured",
        kind: "number",
        unit: "mm/s",
        min: -500,
        max: 3000,
        step: 10
      },
      {
        key: "pid_output",
        label: "PID output",
        kind: "number",
        min: -32768,
        max: 32767,
        step: 1
      }
    ]
  },
  {
    id: "0x300",
    name: "HOST_DRIVE_CMD",
    dlc: 8,
    period: "<=100 Hz",
    injectable: true,
    fields: [
      {
        key: "speed_mmps",
        label: "Speed",
        kind: "number",
        unit: "mm/s",
        min: -500,
        max: 3000,
        step: 10
      },
      {
        key: "yaw_rate_mrad_s",
        label: "Yaw rate",
        kind: "number",
        unit: "mrad/s",
        min: -3000,
        max: 3000,
        step: 10
      },
      { key: "gear", label: "Gear", kind: "enum", options: GEAR_OPTIONS }
    ]
  },
  {
    id: "0x301",
    name: "HOST_BRAKE_REQ",
    dlc: 4,
    period: "demand",
    injectable: true,
    fields: [
      {
        key: "brake_pressure_kpa",
        label: "Brake pressure",
        kind: "number",
        unit: "kPa",
        min: 0,
        max: 20000,
        step: 100
      }
    ]
  },
  {
    id: "0x302",
    name: "HOST_LIGHT_CMD",
    dlc: 1,
    period: "change",
    injectable: true,
    fields: [
      { key: "left_turn", label: "Left turn", kind: "boolean" },
      { key: "right_turn", label: "Right turn", kind: "boolean" },
      { key: "brake_light", label: "Brake light", kind: "boolean" },
      { key: "headlight", label: "Headlight", kind: "boolean" }
    ]
  },
  {
    id: "0x400",
    name: "HOST_OBSTACLE_DIST",
    dlc: 4,
    period: "10 Hz",
    injectable: true,
    fields: [
      {
        key: "distance_mm",
        label: "Distance",
        kind: "number",
        unit: "mm",
        min: 0,
        max: 4294967295,
        step: 10
      }
    ]
  },
  {
    id: "0x600",
    name: "SYS_DIAG_RPT",
    dlc: 8,
    period: "1 Hz",
    injectable: false,
    fields: [
      { key: "mode", label: "Mode", kind: "enum", options: MODE_OPTIONS },
      { key: "brake_engaged", label: "Brake engaged", kind: "boolean" },
      { key: "heartbeat_ok", label: "Heartbeat OK", kind: "boolean" },
      { key: "estop_active", label: "ESTOP active", kind: "boolean" },
      {
        key: "free_heap_kb",
        label: "Free heap",
        kind: "number",
        unit: "KB",
        min: 0,
        max: 65535
      },
      { key: "tec", label: "TEC", kind: "number", min: 0, max: 255 },
      { key: "rec", label: "REC", kind: "number", min: 0, max: 255 }
    ]
  },
  {
    id: "0x7FC",
    name: "JETSON_HEARTBEAT",
    dlc: 1,
    period: "2 Hz",
    injectable: true,
    fields: [{ key: "alive_ctr", label: "Alive counter", kind: "number", min: 0, max: 255 }]
  },
  {
    id: "0x7FD",
    name: "RT_HEARTBEAT",
    dlc: 1,
    period: "2 Hz",
    injectable: false,
    fields: [{ key: "alive_ctr", label: "Alive counter", kind: "number", min: 0, max: 255 }]
  }
];

export const CAN_BY_ID = new Map(CAN_MESSAGES.map((msg) => [msg.id, msg]));

export const INJECTION_TEMPLATES: InjectionTemplate[] = [
  {
    id: "0x300",
    name: "Drive 2.0 m/s",
    description: "Forward drive command in D gear.",
    dlc: 8,
    values: { speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1 }
  },
  {
    id: "0x300",
    name: "Slow reverse",
    description: "Low-speed reverse command.",
    dlc: 8,
    values: { speed_mmps: -300, yaw_rate_mrad_s: 0, gear: 3 }
  },
  {
    id: "0x301",
    name: "Brake 5 MPa",
    description: "Demand 5000 kPa brake pressure.",
    dlc: 4,
    values: { brake_pressure_kpa: 5000 }
  },
  {
    id: "0x302",
    name: "Hazard lights",
    description: "Left and right indicators on.",
    dlc: 1,
    values: { left_turn: true, right_turn: true, brake_light: false, headlight: false }
  },
  {
    id: "0x400",
    name: "Obstacle clear",
    description: "No obstacle reading.",
    dlc: 4,
    values: { distance_mm: 4294967295 }
  },
  {
    id: "0x7FC",
    name: "Jetson heartbeat",
    description: "Single heartbeat frame.",
    dlc: 1,
    values: { alive_ctr: 1 }
  }
];

export function normalizeCanId(input: string | number): CanId | string {
  if (typeof input === "number") {
    return `0x${input.toString(16).toUpperCase().padStart(3, "0")}`;
  }

  const trimmed = input.trim();
  const value = trimmed.toLowerCase().startsWith("0x")
    ? Number.parseInt(trimmed.slice(2), 16)
    : Number.parseInt(trimmed, 16);

  if (!Number.isFinite(value)) return trimmed.toUpperCase();
  return `0x${value.toString(16).toUpperCase().padStart(3, "0")}`;
}

export function isKnownCanId(id: string): id is CanId {
  return CAN_BY_ID.has(id as CanId);
}

export function getMessageName(id: string): string {
  return CAN_BY_ID.get(id as CanId)?.name ?? `UNKNOWN_${id}`;
}

export function decodeFrame(id: string, data: number[]): Record<string, unknown> {
  const bytes = normalizeBytes(data);

  switch (normalizeCanId(id)) {
    case "0x001":
      return {};
    case "0x011":
      return {
        estop_active: bytes[0] !== 0,
        heartbeat_ok: bytes[1] !== 0
      };
    case "0x120":
      return { speed_mmps: readI16(bytes, 0) };
    case "0x210": {
      const mode = bytes[0] ?? 0;
      return {
        mode,
        mode_name: modeName(mode),
        steer_valid: bytes[1] !== 0,
        reversing: bytes[2] !== 0
      };
    }
    case "0x220":
      return {
        speed_setpoint_mmps: readI16(bytes, 0),
        speed_measured_mmps: readI16(bytes, 2),
        pid_output: readI16(bytes, 4)
      };
    case "0x300": {
      const gear = bytes[7] ?? 0;
      return {
        speed_mmps: readI32(bytes, 0),
        yaw_rate_mrad_s: readI24(bytes, 4),
        gear,
        gear_name: gearName(gear)
      };
    }
    case "0x301":
      return { brake_pressure_kpa: readI32(bytes, 0) };
    case "0x302": {
      const flags = bytes[0] ?? 0;
      return {
        left_turn: Boolean(flags & 0x01),
        right_turn: Boolean(flags & 0x02),
        brake_light: Boolean(flags & 0x04),
        headlight: Boolean(flags & 0x08)
      };
    }
    case "0x400": {
      const distance = readU32(bytes, 0);
      return {
        distance_mm: distance,
        distance_label: distance === 0xffffffff ? "clear" : `${distance} mm`
      };
    }
    case "0x600": {
      const mode = bytes[0] ?? 0;
      return {
        mode,
        mode_name: modeName(mode),
        brake_engaged: bytes[1] !== 0,
        heartbeat_ok: bytes[2] !== 0,
        estop_active: bytes[3] !== 0,
        free_heap_kb: readU16(bytes, 4),
        tec: bytes[6] ?? 0,
        rec: bytes[7] ?? 0
      };
    }
    case "0x7FC":
    case "0x7FD":
      return { alive_ctr: bytes[0] ?? 0 };
    default:
      return {};
  }
}

export function normalizeFrame(input: Partial<CanFrame> & { id: string; data: number[] }): CanFrame {
  const id = normalizeCanId(input.id);
  const data = normalizeBytes(input.data).slice(0, 8);
  const dlc = typeof input.dlc === "number" ? input.dlc : data.length;
  const decoded = input.decoded && Object.keys(input.decoded).length > 0 ? input.decoded : decodeFrame(id, data);

  return {
    ts: typeof input.ts === "number" ? input.ts : Date.now() / 1000,
    id,
    name: input.name ?? getMessageName(id),
    dlc,
    data,
    decoded
  };
}

export function validateDataBytes(data: unknown, dlc: number): number[] {
  if (!Array.isArray(data)) {
    throw new Error("data must be an array");
  }

  if (dlc < 0 || dlc > 8) {
    throw new Error("dlc must be between 0 and 8");
  }

  if (data.length !== dlc) {
    throw new Error(`data length must match dlc (${dlc})`);
  }

  return data.map((value, index) => {
    if (!Number.isInteger(value) || value < 0 || value > 255) {
      throw new Error(`data[${index}] must be an integer byte`);
    }
    return value;
  });
}

function normalizeBytes(data: number[]): number[] {
  const bytes = data.map((value) => Number(value) & 0xff);
  while (bytes.length < 8) bytes.push(0);
  return bytes;
}

function readI16(bytes: number[], offset: number): number {
  const value = ((bytes[offset] ?? 0) << 8) | (bytes[offset + 1] ?? 0);
  return value & 0x8000 ? value - 0x10000 : value;
}

function readU16(bytes: number[], offset: number): number {
  return (((bytes[offset] ?? 0) << 8) | (bytes[offset + 1] ?? 0)) >>> 0;
}

function readI24(bytes: number[], offset: number): number {
  const value = ((bytes[offset] ?? 0) << 16) | ((bytes[offset + 1] ?? 0) << 8) | (bytes[offset + 2] ?? 0);
  return value & 0x800000 ? value - 0x1000000 : value;
}

function readI32(bytes: number[], offset: number): number {
  return (
    ((bytes[offset] ?? 0) << 24) |
    ((bytes[offset + 1] ?? 0) << 16) |
    ((bytes[offset + 2] ?? 0) << 8) |
    (bytes[offset + 3] ?? 0)
  );
}

function readU32(bytes: number[], offset: number): number {
  return (
    ((bytes[offset] ?? 0) * 0x1000000) +
    ((bytes[offset + 1] ?? 0) << 16) +
    ((bytes[offset + 2] ?? 0) << 8) +
    (bytes[offset + 3] ?? 0)
  ) >>> 0;
}

function modeName(mode: number): string {
  return MODE_OPTIONS.find((item) => item.value === mode)?.label ?? "?";
}

function gearName(gear: number): string {
  return GEAR_OPTIONS.find((item) => item.value === gear)?.label ?? "?";
}
