import CAPABILITIES from "../../../protocol/generated/capabilities.json";
import DISCOVERY from "../../../protocol/generated/discovery.json";
import { decode as canonicalDecode, encode as canonicalEncode } from "../../../protocol/codecs/typescript/codec";
import type { CodecStatus as CanonicalCodecStatus } from "../../../protocol/codecs/typescript/types";
import { defaultTimebase } from "./timebase";

export const BUSES = ["high", "low"] as const;
export type Bus = (typeof BUSES)[number];
export type FieldKind = "number" | "boolean" | "enum";
export type CodecStatus = CanonicalCodecStatus;

interface CanonicalField {
  readonly key: string;
  readonly byte: number;
  readonly bit: number;
  readonly bits: number;
  readonly signed?: boolean;
  readonly min?: number;
  readonly max?: number;
  readonly factor?: number;
  readonly offset?: number;
  readonly constant?: number;
  readonly enum?: Readonly<Record<string, string>>;
}

interface CanonicalInstance {
  readonly bus: string;
  readonly id: number;
  readonly frame_format: "standard" | "extended";
  readonly cycle_ms: number;
  readonly sender: string;
  readonly receivers: readonly string[];
}

interface CanonicalMessage {
  readonly canonical_key: string;
  readonly name: string;
  readonly dlc: number;
  readonly byte_order: "big" | "little";
  readonly codec: { readonly strategy: "generated" | "custom"; readonly implementation_id?: string };
  readonly instances: readonly CanonicalInstance[];
  readonly layout: {
    readonly kind: string;
    readonly fields?: readonly CanonicalField[];
    readonly algorithm?: string;
    readonly semantic_support?: string;
  };
}

const CANONICAL_MESSAGES = DISCOVERY.messages as unknown as readonly CanonicalMessage[];

export interface MessageCapabilities {
  readonly rawMonitoring: boolean;
  readonly semanticDecode: boolean;
  readonly decodedInjection: boolean;
  readonly codecStrategy: "generated" | "custom";
  readonly implementation?: string;
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
  period: string;
  injectable: boolean;
  receivers?: string[];
  comment?: string;
  byteOrder: string;
  fields: CanField[];
  canonicalKey: string;
  frameFormat: "standard" | "extended";
  capabilities: MessageCapabilities;
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
  readonly codec_status?: CodecStatus;
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

export type AdapterEventType = "bus_off" | "recovery" | "overflow" | "disconnect" | "error_frame" | "timestamp_reset";

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

export class ValidationError extends CodecError {}
export class SchemaError extends CodecError {}

export class UnsupportedDecodedInjectionError extends CodecError {
  constructor(public readonly bus: string, public readonly id: string) {
    super(`Decoded injection is unsupported: bus=${bus}, id=${id}`);
  }
}

export const PROTOCOL_HASH = CAPABILITIES.wire_hash;
export const PROTOCOL_CAPABILITIES = CAPABILITIES;

const INJECTABLE_IDENTITIES = new Set([
  "high:0x001",
  "high:0x300",
  "high:0x301",
  "high:0x302",
  "high:0x400",
  "high:0x7FC",
  "low:0x001",
  "low:0x302",
]);

const CUSTOM_DECODERS = new Set([
  "ses:vcu_ses_req", "ses:ses_status", "ses:ses_test",
  "seb:vcu_seb_req", "seb:seb_status", "seb:seb_test", "seb:seb_version",
]);

const CUSTOM_ENCODERS = new Set(["ses:vcu_ses_req", "seb:vcu_seb_req"]);

const FIELD_ALIASES: Readonly<Record<string, Readonly<Record<string, string>>>> = {
  "hmi:hmi_mode_req": { req_mode: "HMI_ReqMode" },
  "hmi:hmi_pwr_req": { req_start: "HMI_ReqStart" },
  "rt:steer_diag": {
    angle_0_1deg: "SteerDiag_Angle0_1deg", fault: "SteerDiag_Fault",
    motor_current: "SteerDiag_MotorCurrent", ecu_temp: "SteerDiag_ECUTemp", reserved: "SteerDiag_Reserved",
  },
  "rt:brake_diag": {
    pressure_raw: "BrakeDiag_PressureRaw", fault: "BrakeDiag_Fault",
    motor_current: "BrakeDiag_MotorCurrent", ecu_temp: "BrakeDiag_ECUTemp", reserved: "BrakeDiag_Reserved",
  },
  "sys:sys_diag_rpt": {
    mode: "SYS_DiagMode", brake_engaged: "SYS_DiagBrakeEngaged", brake_fault: "SYS_DiagBrakeFault",
    estop_active: "SYS_DiagEstopActive", free_heap_kb: "SYS_DiagFreeHeapKb", tec: "SYS_DiagTec", rec: "SYS_DiagRec",
  },
  "sys:sys_heartbeat": { alive_ctr: "SYS_AliveCtr" },
  "ses:vcu_ses_req": {
    target_angle_raw: "target_angle", target_speed_raw: "target_speed", vehicle_speed_raw: "SES_VehSpd",
  },
  "ses:ses_status": {
    angle_aligned: "angle_status", steering_angle_raw: "str_angle", target_angle_speed_raw: "tgt_angle_spd",
    steering_torque_raw: "SES_SteeringTorq", rolling_counter_enabled: "SES_RollCntEnStatus",
    checksum_enabled: "SES_ChecksumEnStatus",
  },
  "ses:ses_test": {
    motor_current_raw: "SES_MtrCurt", ecu_temperature_raw: "SES_ECUTemp", supply_voltage_raw: "SES_PowVolt",
  },
  "seb:vcu_seb_req": {
    alignment_enable: "align_enable", stroke_request_raw: "stroke_req", pressure_request_raw: "pressure_req",
  },
  "seb:seb_status": {
    control_enabled: "control_enable_sts", control_mode: "control_mode_sts", auto_brake_status: "SEB_AutoBrakeStatus",
    stroke_value_raw: "stroke_value", pressure_value_raw: "pressure_value", angle_value_raw: "angle_value",
    rolling_counter_enabled: "SEB_RollCntEnStatus", checksum_enabled: "SEB_ChecksumEnStatus",
  },
  "seb:seb_test": {
    motor_current_raw: "SEB_MtrCurr", ecu_temperature_raw: "SEB_ECUTemp", supply_voltage_raw: "SEB_PowVolt",
  },
  "seb:seb_version": { software_raw: "SEB_SW_Version", hardware_raw: "SEB_HW_Version" },
};

function formatCanId(id: number): string {
  return `0x${id.toString(16).toUpperCase().padStart(3, "0")}`;
}

function fieldName(messageKey: string, key: string): string {
  return FIELD_ALIASES[messageKey]?.[key] ?? key;
}

function fieldDefinition(messageKey: string, field: CanonicalField): CanField {
  const key = fieldName(messageKey, field.key);
  const options = field.enum === undefined
    ? undefined
    : Object.entries(field.enum).map(([value, label]) => ({ value: Number(value), label }));
  const boolean = field.bits === 1 && options === undefined;
  return {
    key,
    label: key,
    kind: options ? "enum" : boolean ? "boolean" : "number",
    min: field.min,
    max: field.max,
    step: field.factor,
    options,
    _byte: field.byte,
    _bit_offset: field.bit,
    _size: field.bits,
    _type: field.signed ? "signed" : "unsigned",
    _factor: field.factor ?? 1,
    _offset: field.offset ?? 0,
  };
}

export const CAN_MESSAGES: CanMessageDef[] = CANONICAL_MESSAGES
  .flatMap((message) => message.instances
    .filter((instance): instance is CanonicalInstance & { bus: Bus } => instance.bus === "high" || instance.bus === "low")
    .map((instance) => {
      const id = formatCanId(instance.id);
      const generated = message.codec.strategy === "generated";
      return {
        bus: instance.bus,
        id,
        name: message.name,
        sender: instance.sender,
        dlc: message.dlc,
        period: instance.cycle_ms === 0 ? "event" : `${instance.cycle_ms}ms`,
        injectable: INJECTABLE_IDENTITIES.has(`${instance.bus}:${id}`),
        receivers: [...instance.receivers],
        comment: message.layout.algorithm,
        byteOrder: message.byte_order === "big" ? "motorola" : "intel",
        fields: (message.layout.fields ?? []).map((field) => fieldDefinition(message.canonical_key, field)),
        canonicalKey: message.canonical_key,
        frameFormat: instance.frame_format,
        capabilities: {
          rawMonitoring: true,
          semanticDecode: generated || CUSTOM_DECODERS.has(message.canonical_key),
          decodedInjection: generated || CUSTOM_ENCODERS.has(message.canonical_key),
          codecStrategy: message.codec.strategy,
          implementation: message.codec.implementation_id,
        },
      };
    }))
  .sort((left, right) => left.bus.localeCompare(right.bus) || Number.parseInt(left.id.slice(2), 16) - Number.parseInt(right.id.slice(2), 16));

export let CAN_BY_BUS_ID = new Map(CAN_MESSAGES.map((item) => [`${item.bus}:${item.id}`, item]));

function compatibilityValues(definition: CanMessageDef, values: Record<string, unknown>): Record<string, unknown> {
  const aliases = FIELD_ALIASES[definition.canonicalKey] ?? {};
  const output: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(values)) {
    if (key === "raw") continue;
    output[aliases[key] ?? key] = value;
  }
  for (const field of definition.fields) {
    if (field.kind === "boolean" && typeof output[field.key] === "number") output[field.key] = output[field.key] !== 0;
    if (field.options && typeof output[field.key] === "number") {
      const option = field.options.find((candidate) => candidate.value === output[field.key]);
      if (option) output[`${field.key}_name`] = option.label;
    }
  }
  return output;
}

function canonicalValues(definition: CanMessageDef, values: Readonly<Record<string, number | boolean>>): Record<string, unknown> {
  const aliases = FIELD_ALIASES[definition.canonicalKey] ?? {};
  const reverseAliases = new Map(Object.entries(aliases).map(([canonical, legacy]) => [legacy, canonical]));
  const output: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(values)) output[reverseAliases.get(key) ?? key] = value;
  for (const field of definition.fields) {
    const canonicalKey = reverseAliases.get(field.key) ?? field.key;
    if (typeof output[canonicalKey] === "boolean" && definition.capabilities.codecStrategy === "generated") {
      output[canonicalKey] = output[canonicalKey] ? 1 : 0;
    }
  }
  return output;
}

export interface DecodeFrameResult {
  readonly status: CodecStatus | "unknown_message";
  readonly signals: Record<string, unknown>;
}

export function decodeFrameResult(bus: Bus, id: string, data: readonly number[], dlc = data.length): DecodeFrameResult {
  const definition = findMessage(bus, id);
  if (!definition) return { status: "unknown_message", signals: { bus } };
  const [status, value] = canonicalDecode(definition.canonicalKey, {
    bus,
    id: Number.parseInt(definition.id.slice(2), 16),
    frameFormat: definition.frameFormat,
    data: Uint8Array.from(data),
    dlc,
  });
  if (status !== "ok" || value === undefined) return { status, signals: {} };
  const signals = compatibilityValues(definition, value);
  if (definition.capabilities.codecStrategy === "custom" && definition.dlc > 0) signals.checksum = data[definition.dlc - 1];
  return { status, signals };
}

class CanonicalCanCodec {
  getMessages(): CanMessageDef[] {
    return CAN_MESSAGES;
  }

  getDef(bus: string, id: string): CanMessageDef | undefined {
    if (bus !== "high" && bus !== "low") return undefined;
    return findMessage(bus, id);
  }

  decode(bus: string, id: string, data: number[]): Record<string, unknown> {
    if (bus !== "high" && bus !== "low") return { bus };
    return decodeFrameResult(bus, id, data).signals;
  }

  encode(bus: string, id: string, values: Record<string, number | boolean>): { dlc: number; data: number[] } {
    if (bus !== "high" && bus !== "low") throw new UnknownMessageError(bus, id);
    const definition = findMessage(bus, id);
    if (!definition) throw new UnknownMessageError(bus, id);
    if (!definition.capabilities.decodedInjection) throw new UnsupportedDecodedInjectionError(bus, definition.id);
    let result;
    try {
      result = canonicalEncode(definition.canonicalKey, canonicalValues(definition, values), bus);
    } catch (error) {
      if (error instanceof RangeError) throw new UnsupportedDecodedInjectionError(bus, definition.id);
      throw error;
    }
    const [status, frame] = result;
    if (status !== "ok" || frame === undefined) throw new ValidationError(`Cannot encode ${bus}:${definition.id}: ${status}`);
    return { dlc: frame.dlc, data: Array.from(frame.data) };
  }
}

export const decoder = new CanonicalCanCodec();

export function initCanDatabase(): void {
  CAN_BY_BUS_ID = new Map(CAN_MESSAGES.map((item) => [`${item.bus}:${item.id}`, item]));
}

export const INJECTION_TEMPLATES: InjectionTemplate[] = [
  { bus: "high", id: "0x300", name: "Host drive 2.0 m/s", description: "Host drive command in D gear.", dlc: 8, values: { speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1 } },
  { bus: "high", id: "0x301", name: "Host brake 5 MPa", description: "Host brake request.", dlc: 4, values: { brake_pressure_kpa: 5000 } },
  { bus: "high", id: "0x7FC", name: "Host heartbeat", description: "Host heartbeat. Inject every 500ms.", dlc: 2, values: { alive_ctr: 1, health_flags: 0 } },
  { bus: "low", id: "0x001", name: "ESTOP trigger", description: "DLC=0 ESTOP frame.", dlc: 0, values: {} },
];

export function normalizeBus(input: unknown): Bus {
  if (input === undefined || input === null || input === "") return "high";
  if (input === "high" || input === "low") return input;
  throw new Error(`invalid CAN bus: ${String(input)}`);
}

export function normalizeCanId(input: string | number): string {
  if (typeof input === "number") return formatCanId(input);
  const trimmed = input.trim();
  const value = trimmed.toLowerCase().startsWith("0x") ? Number.parseInt(trimmed.slice(2), 16) : Number.parseInt(trimmed, 16);
  return Number.isFinite(value) ? formatCanId(value) : trimmed.toUpperCase();
}

export function findMessage(bus: Bus, id: string): CanMessageDef | undefined {
  return CAN_BY_BUS_ID.get(`${bus}:${normalizeCanId(id)}`);
}

export function getMessageName(bus: Bus, id: string): string {
  return findMessage(bus, id)?.name ?? `UNKNOWN_${normalizeCanId(id)}`;
}

export function decodeFrame(bus: Bus, id: string, data: number[]): Record<string, unknown> {
  return decodeFrameResult(bus, id, data).signals;
}

export function defaultStats(): CanStats {
  return { ts: Date.now() / 1000, uptime_s: 0, buses: { high: emptyBusStats(), low: emptyBusStats() } };
}

export function normalizeStats(input: Partial<CanStats> | Record<string, unknown>): CanStats {
  const buses = input.buses && typeof input.buses === "object" ? input.buses as Partial<Record<Bus, Partial<BusStats>>> : {};
  return {
    type: "stats",
    ts: typeof input.ts === "number" ? input.ts : Date.now() / 1000,
    uptime_s: typeof input.uptime_s === "number" ? input.uptime_s : 0,
    buses: { high: normalizeBusStats(buses.high), low: normalizeBusStats(buses.low) },
  };
}

export function normalizeFrame(input: any): RoutedFrame {
  const legacyInput = input.frame === undefined ? input : {};
  const bus = normalizeBus(input.bus ?? legacyInput.bus);
  const id = normalizeCanId(input.frame?.id ?? legacyInput.id);
  const rawData = input.frame?.data ?? legacyInput.data ?? [];
  const fullData = normalizeBytes(rawData).slice(0, 8);
  const requestedDlc = input.frame?.dlc ?? legacyInput.dlc;
  const dlc = typeof requestedDlc === "number" ? requestedDlc : Math.min(fullData.length, 8);
  const suppliedSignals = input.decoded?.signals ?? legacyInput.decoded;
  const decodedResult = suppliedSignals && Object.keys(suppliedSignals).length > 0
    ? { status: input.decoded?.codec_status ?? "ok", signals: suppliedSignals }
    : decodeFrameResult(bus, id, fullData.slice(0, dlc), dlc);
  const name = input.decoded?.name ?? legacyInput.name ?? getMessageName(bus, id);
  let ts = typeof input.ts === "number" ? input.ts : Date.now() / 1000;
  if (ts > 1_000_000_000_000) ts /= 1000;
  let ts_us = input.ts_us;
  let seq = input.seq;
  if (!ts_us || typeof seq !== "number") {
    const fallback = defaultTimebase.now();
    ts_us ||= fallback.ts_us;
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
    decoded: { name, signals: decodedResult.signals, codec_status: decodedResult.status === "unknown_message" ? "wrong_message_id" : decodedResult.status },
  };
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
    by_id: input.by_id && typeof input.by_id === "object" ? input.by_id : {},
  };
}

export function validateDataBytes(data: unknown, dlc: number): number[] {
  const normalized = (Array.isArray(data) ? data : []).map((value) => {
    const number = Number(value);
    return Number.isNaN(number) ? 0 : Math.max(0, Math.min(255, Math.floor(number)));
  });
  while (normalized.length < dlc) normalized.push(0);
  return normalized.slice(0, dlc);
}

function normalizeBytes(input: any): number[] {
  if (Array.isArray(input)) return input.map((value) => Number(value) || 0);
  if (input instanceof Uint8Array || (typeof globalThis !== "undefined" && (globalThis as any).Buffer?.isBuffer?.(input))) return Array.from(input);
  if (input && typeof input === "object" && "length" in input && typeof input[0] === "number") return Array.from(input);
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
    const inHigh = CAN_BY_BUS_ID.has(`high:${id}`);
    const inLow = CAN_BY_BUS_ID.has(`low:${id}`);
    if (inHigh && !inLow) {
      this.highHits += 1;
      if (this.highHits >= 3) this.locked = "high";
    } else if (inLow && !inHigh) {
      this.lowHits += 1;
      if (this.lowHits >= 3) this.locked = "low";
    }
    return this.locked ?? (this.lowHits > 0 && this.highHits === 0 ? "low" : "high");
  }

  get state(): BusDetectorState {
    if (this.locked && this.lastFeedAt > 0 && Date.now() / 1000 - this.lastFeedAt > BusDetector.STALE_TIMEOUT_S) this.reset();
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
