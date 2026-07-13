import { DynamicCanDecoder } from "./dynamic-decoder";
import { defaultTimebase } from "./timebase";

export const BUSES = ["high", "low"] as const;
export type Bus = (typeof BUSES)[number];

export type FieldKind = "number" | "boolean" | "enum";

export class CodecError extends Error {
  constructor(message: string) {
    super(message);
    this.name = this.constructor.name;
  }
}

export class UnknownMessageError extends CodecError {
  constructor(public readonly bus: string, public readonly id: string) {
    super(`Unknown message: bus=${bus}, id=${id}`);
  }
}

export class ValidationError extends CodecError {
  constructor(message: string) {
    super(message);
  }
}

export class SchemaError extends CodecError {
  constructor(message: string) {
    super(message);
  }
}

export interface CanField {
  key: string;
  label: string;
  kind: FieldKind;
  unit?: string;
  min?: number;
  max?: number;
  step?: number;
  options?: Array<{ label: string; value: number }>;
  _byte: number;
  _bit_offset: number;
  _size: number;
  _type: string;
  _factor: number;
  _offset: number;
}

export interface CanMessageDef {
  bus: Bus;
  id: string;
  name: string;
  sender: string;
  dlc: number;
  period: string; // "100ms" or "event"
  injectable: boolean;
  receivers?: string[];
  comment?: string;
  byteOrder: string;
  fields: CanField[];
}

export interface CanDataFrame {
  readonly id: string;
  readonly dlc: number;
  readonly data: readonly number[];
  readonly ext?: boolean;
  readonly rtr?: boolean;
}

export interface DecodedMessage {
  readonly name: string;
  readonly signals: Record<string, unknown>;
}

export interface RoutedFrame {
  readonly ts_us: string;
  readonly seq: number;
  readonly bus: Bus;
  readonly frame: CanDataFrame;
  readonly decoded?: DecodedMessage;
  readonly row_id?: number;
  readonly ts?: number;
  readonly ts_real?: number;
  readonly ts_device?: number;
}

export type CanFrame = RoutedFrame;

export type AdapterEventType = 
  | "bus_off" 
  | "recovery" 
  | "overflow" 
  | "disconnect" 
  | "error_frame" 
  | "timestamp_reset";

export interface AdapterEvent {
  readonly type: "adapter_event";
  readonly event_type: AdapterEventType;
  readonly ts_us: string;
  readonly seq: number;
  readonly bus?: Bus;
  readonly details?: Record<string, unknown>;
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

export const decoder = new DynamicCanDecoder();

import { CAN_MESSAGES as GENERATED_MESSAGES } from "./generated/can-metadata";

export let CAN_MESSAGES: CanMessageDef[] = GENERATED_MESSAGES;
export let CAN_BY_BUS_ID = new Map<string, CanMessageDef>();

export function initCanDatabase() {
  decoder.loadMessages(CAN_MESSAGES);
  CAN_BY_BUS_ID = new Map(CAN_MESSAGES.map((item) => [`${item.bus}:${item.id}`, item]));

  // Register checksum hooks
  const computeXor = (data: number[]) => {
    if (data.length < 8) return;
    let chk = 0;
    for (let i = 0; i < 7; i++) {
      chk ^= data[i];
    }
    data[7] = chk ^ 0xFF;
  };
  
  decoder.registerEncoderHook("low", "0x169", computeXor);
  decoder.registerEncoderHook("low", "0x7B9", computeXor);
}

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

// Accepts both legacy flat frames and new nested frames to ease migration
export function normalizeFrame(input: any): RoutedFrame {
  const isNested = input.frame !== undefined;
  const legacyInput = isNested ? {} : input;
  
  const bus = normalizeBus(input.bus ?? legacyInput.bus);
  const id = normalizeCanId(input.frame?.id ?? legacyInput.id);
  const rawData = input.frame?.data ?? legacyInput.data ?? [];
  const fullData = normalizeBytes(rawData).slice(0, 8);
  const dlc = typeof (input.frame?.dlc ?? legacyInput.dlc) === "number" ? (input.frame?.dlc ?? legacyInput.dlc) : Math.min(rawData.length, 8);
  
  const decodedMap = input.decoded?.signals ?? legacyInput.decoded;
  const decoded = decodedMap && Object.keys(decodedMap).length > 0 ? decodedMap : decodeFrame(bus, id, fullData);
  const name = input.decoded?.name ?? legacyInput.name ?? getMessageName(bus, id);
  
  let ts = typeof input.ts === "number" ? input.ts : Date.now() / 1000;
  if (ts > 1_000_000_000_000) ts = ts / 1000;
  
  let ts_us = input.ts_us;
  let seq = input.seq;
  if (!ts_us || typeof seq !== "number") {
    const fallback = defaultTimebase.now();
    if (!ts_us) ts_us = fallback.ts_us;
    if (typeof seq !== "number") seq = fallback.seq;
  }
  
  return { 
    ts, 
    ts_us, 
    seq, 
    bus, 
    frame: {
      id,
      dlc,
      data: fullData.slice(0, dlc),
      ext: input.frame?.ext ?? legacyInput.ext ?? false,
      rtr: input.frame?.rtr ?? legacyInput.rtr ?? false,
    },
    decoded: {
      name,
      signals: decoded
    }
  };
}

export function decodeFrame(bus: Bus, id: string, data: number[]): Record<string, unknown> {
  return decoder.decode(bus, id, data);
}

function emptyBusStats(): BusStats {
  return { active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {} };
}

function normalizeBusStats(input?: Partial<BusStats>): BusStats {
  if (!input) return emptyBusStats();
  return {
    active: !!input.active,
    total: typeof input.total === "number" ? input.total : 0,
    fps: typeof input.fps === "number" ? input.fps : 0,
    load_pct: typeof input.load_pct === "number" ? input.load_pct : 0,
    tec: typeof input.tec === "number" ? input.tec : 0,
    rec: typeof input.rec === "number" ? input.rec : 0,
    by_id: input.by_id && typeof input.by_id === "object" ? (input.by_id as Record<string, number>) : {}
  };
}

export function validateDataBytes(data: unknown, dlc: number): number[] {
  const arr = Array.isArray(data) ? data : [];
  const normalized = arr.map((v) => {
    const num = Number(v);
    return isNaN(num) ? 0 : Math.max(0, Math.min(255, Math.floor(num)));
  });
  while (normalized.length < dlc) normalized.push(0);
  return normalized.slice(0, dlc);
}

function normalizeBytes(input: any): number[] {
  if (Array.isArray(input)) return input.map((v) => Number(v) || 0);
  if (input instanceof Uint8Array || (typeof globalThis !== 'undefined' && (globalThis as any).Buffer?.isBuffer?.(input))) return Array.from(input);
  if (input && typeof input === 'object' && 'length' in input && typeof input[0] === 'number') return Array.from(input);
  return [];
}

export interface BusDetectorState {
  detected: boolean;
  bus: Bus;
  confidence: "none" | "low" | "high";
  highHits: number;
  lowHits: number;
}

export class BusDetector {
  private highHits = 0;
  private lowHits = 0;
  private locked: Bus | null = null;
  private lastFeedAt = 0;

  private static readonly STALE_TIMEOUT_S = 10;

  feed(canId: string): Bus {
    this.lastFeedAt = Date.now() / 1000;
    if (this.locked) return this.locked;

    const id = normalizeCanId(canId);
    
    // Check if ID uniquely appears on high or low bus
    const inHigh = CAN_MESSAGES.some((m) => m.bus === "high" && m.id === id);
    const inLow = CAN_MESSAGES.some((m) => m.bus === "low" && m.id === id);

    if (inHigh && !inLow) {
      this.highHits += 1;
      if (this.highHits >= 3) this.locked = "high";
    } else if (inLow && !inHigh) {
      this.lowHits += 1;
      if (this.lowHits >= 3) this.locked = "low";
    }

    return this.locked ?? "high";
  }

  get state(): BusDetectorState {
    if (this.locked && this.lastFeedAt > 0 && (Date.now() / 1000 - this.lastFeedAt) > BusDetector.STALE_TIMEOUT_S) {
      this.locked = null;
      this.highHits = 0;
      this.lowHits = 0;
    }
    if (this.locked) return { detected: true, bus: this.locked, confidence: "high", highHits: this.highHits, lowHits: this.lowHits };
    if (this.highHits > 0 && this.lowHits > 0) return { detected: false, bus: "high", confidence: "none", highHits: this.highHits, lowHits: this.lowHits };
    if (this.highHits > 0) return { detected: false, bus: "high", confidence: "low", highHits: this.highHits, lowHits: this.lowHits };
    if (this.lowHits > 0) return { detected: false, bus: "low", confidence: "low", highHits: this.highHits, lowHits: this.lowHits };
    return { detected: false, bus: "high", confidence: "none", highHits: 0, lowHits: 0 };
  }

  reset(): void {
    this.highHits = 0;
    this.lowHits = 0;
    this.locked = null;
    this.lastFeedAt = 0;
  }
}
initCanDatabase();
