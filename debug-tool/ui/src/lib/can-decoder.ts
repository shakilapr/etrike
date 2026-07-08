export * from "@etrike/debug-shared";
import { normalizeCanId } from "@etrike/debug-shared";
import type { Bus, CanFrame } from "@etrike/debug-shared";

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
      bytes[5] = ((numberValue(values.rolling_counter) & 0x0f) << 4) | ((numberValue(values.target_speed) >> 8) & 0x03) << 2 | 0x03;
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
      bytes[6] = 0x03 | ((numberValue(values.rolling_counter) & 0x0f) << 4);
      bytes[7] = numberValue(values.checksum) & 0xff;
      return { dlc: 8, data: bytes };

    case "low:0x202": {
      const fm202 = numberValue(values.fault_mask);
      bytes[0] = fm202 & 0xff;
      bytes[1] = (fm202 >> 8) & 0xff;
      bytes[2] = (fm202 >> 16) & 0xff;
      bytes[3] = (fm202 >>> 24) & 0xff;
      if (values.SES_VehSpdSnapshot !== undefined) bytes[7] = numberValue(values.SES_VehSpdSnapshot) & 0xff;
      return { dlc: 8, data: bytes };
    }

    case "low:0x731": {
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
      const angleVal = numberValue(values.angle_value) & 0x0FFF;
      bytes[5] = angleVal & 0xFF;
      bytes[6] = 1 | (1 << 1) | (((angleVal >> 8) & 0x3) << 2) | ((numberValue(values.rolling_counter) & 0xF) << 4);
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
      bytes[6] = 0x03 | ((numberValue(values.rolling_counter) & 0x0f) << 4);
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
