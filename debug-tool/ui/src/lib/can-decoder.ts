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
  id: string;
  name: string;
  dlc: number;
  data: number[];
  decoded: Record<string, unknown>;
  row_id?: number;
  ts_real?: number;
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

export const FALLBACK_IDS: CanMessageDef[] = [
  {
    id: "0x300",
    name: "HOST_DRIVE_CMD",
    dlc: 8,
    period: "<=100 Hz",
    injectable: true,
    fields: [
      { key: "speed_mmps", label: "Speed", kind: "number", unit: "mm/s", min: -500, max: 3000, step: 10 },
      {
        key: "yaw_rate_mrad_s",
        label: "Yaw rate",
        kind: "number",
        unit: "mrad/s",
        min: -3000,
        max: 3000,
        step: 10
      },
      {
        key: "gear",
        label: "Gear",
        kind: "enum",
        options: [
          { label: "N", value: 0 },
          { label: "D", value: 1 },
          { label: "S", value: 2 },
          { label: "R", value: 3 }
        ]
      }
    ]
  }
];

export function encodePayload(id: string, values: Record<string, number | boolean>): { dlc: number; data: number[] } {
  const bytes = Array.from({ length: 8 }, () => 0);

  switch (normalizeCanId(id)) {
    case "0x001":
      return { dlc: 0, data: [] };
    case "0x011":
      bytes[0] = values.estop_active ? 1 : 0;
      bytes[1] = values.heartbeat_ok ? 1 : 0;
      return { dlc: 2, data: bytes.slice(0, 2) };
    case "0x120":
      writeI16(bytes, 0, numberValue(values.speed_mmps));
      return { dlc: 2, data: bytes.slice(0, 2) };
    case "0x300":
      writeI32(bytes, 0, numberValue(values.speed_mmps));
      writeI24(bytes, 4, numberValue(values.yaw_rate_mrad_s));
      bytes[7] = numberValue(values.gear);
      return { dlc: 8, data: bytes };
    case "0x301":
      writeI32(bytes, 0, numberValue(values.brake_pressure_kpa));
      return { dlc: 4, data: bytes.slice(0, 4) };
    case "0x302":
      bytes[0] =
        (values.left_turn ? 0x01 : 0) |
        (values.right_turn ? 0x02 : 0) |
        (values.brake_light ? 0x04 : 0) |
        (values.headlight ? 0x08 : 0);
      return { dlc: 1, data: bytes.slice(0, 1) };
    case "0x400":
      writeU32(bytes, 0, numberValue(values.distance_mm));
      return { dlc: 4, data: bytes.slice(0, 4) };
    case "0x7FC":
    case "0x7FD":
      bytes[0] = numberValue(values.alive_ctr) & 0xff;
      return { dlc: 1, data: bytes.slice(0, 1) };
    default:
      return { dlc: 0, data: [] };
  }
}

export function formatBytes(data: number[]): string {
  if (data.length === 0) return "--";
  return data.map((byte) => byte.toString(16).toUpperCase().padStart(2, "0")).join(" ");
}

export function formatDecoded(decoded: Record<string, unknown>): string {
  const entries = Object.entries(decoded).filter(([key]) => !key.endsWith("_name") && !key.endsWith("_label"));
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

function numberValue(value: unknown): number {
  if (typeof value === "boolean") return value ? 1 : 0;
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
}

function writeI16(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffff;
  bytes[offset] = (raw >> 8) & 0xff;
  bytes[offset + 1] = raw & 0xff;
}

function writeI24(bytes: number[], offset: number, value: number): void {
  const raw = value & 0xffffff;
  bytes[offset] = (raw >> 16) & 0xff;
  bytes[offset + 1] = (raw >> 8) & 0xff;
  bytes[offset + 2] = raw & 0xff;
}

function writeI32(bytes: number[], offset: number, value: number): void {
  bytes[offset] = (value >> 24) & 0xff;
  bytes[offset + 1] = (value >> 16) & 0xff;
  bytes[offset + 2] = (value >> 8) & 0xff;
  bytes[offset + 3] = value & 0xff;
}

function writeU32(bytes: number[], offset: number, value: number): void {
  const raw = value >>> 0;
  bytes[offset] = Math.floor(raw / 0x1000000) & 0xff;
  bytes[offset + 1] = (raw >> 16) & 0xff;
  bytes[offset + 2] = (raw >> 8) & 0xff;
  bytes[offset + 3] = raw & 0xff;
}
