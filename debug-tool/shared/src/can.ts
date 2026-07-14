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
  readonly id: string | number;
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
  let options = field.enum === undefined
    ? undefined
    : Object.entries(field.enum).map(([value, label]) => ({ value: Number(value), label }));
  if (messageKey === "rt:rt_state_rpt" && field.key === "safety_state") {
    options = [
      { value: 0, label: "Normal" },
      { value: 1, label: "Warning" },
      { value: 2, label: "Fault" },
      { value: 3, label: "Estop" },
    ];
  }
  const boolean = (field.bits === 1 || field.max === 1) && options === undefined;
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

function getCustomFields(canonicalKey: string): CanonicalField[] {
  switch (canonicalKey) {
    case "ses:vcu_ses_req":
      return [
        { key: "alignment_enable", byte: 0, bit: 0, bits: 1 },
        { key: "control_enable", byte: 0, bit: 1, bits: 1 },
        { key: "target_angle", byte: 2, bit: 0, bits: 16, signed: true, factor: 0.1, offset: -3000 },
        { key: "target_speed", byte: 4, bit: 0, bits: 16, factor: 1, offset: 0 },
        { key: "rolling_counter", byte: 5, bit: 4, bits: 4 },
        { key: "checksum", byte: 7, bit: 0, bits: 8 },
      ];
    case "ses:ses_status":
      return [
        { key: "angle_status", byte: 0, bit: 0, bits: 1 },
        { key: "error_status", byte: 0, bit: 2, bits: 2 },
        { key: "str_angle", byte: 2, bit: 0, bits: 16, signed: true, factor: 0.1, offset: -3000 },
        { key: "tgt_angle_spd", byte: 4, bit: 0, bits: 16, signed: true, factor: 0.5, offset: 0 },
        { key: "SES_SteeringTorq", byte: 5, bit: 0, bits: 8, factor: 0.1, offset: -12.1 },
        { key: "rolling_counter", byte: 6, bit: 4, bits: 4 },
        { key: "checksum", byte: 7, bit: 0, bits: 8 },
      ];
    case "ses:ses_err_info":
      return [
        { key: "SES_ECUUnderVolt", byte: 0, bit: 0, bits: 1 },
        { key: "SES_ECUOverVolt", byte: 0, bit: 1, bits: 1 },
        { key: "SES_CanComErr", byte: 0, bit: 2, bits: 1 },
        { key: "SES_ECUTempErr", byte: 0, bit: 3, bits: 1 },
        { key: "SES_DomainSC", byte: 0, bit: 4, bits: 1 },
        { key: "SES_DomainV", byte: 0, bit: 5, bits: 1 },
        { key: "SES_DomainT", byte: 0, bit: 6, bits: 1 },
        { key: "SES_TempSensor", byte: 0, bit: 7, bits: 1 },
        { key: "SES_AngleP_OC", byte: 1, bit: 0, bits: 1 },
        { key: "SES_AngleP_AF", byte: 1, bit: 1, bits: 1 },
        { key: "SES_AngleS_OC", byte: 1, bit: 2, bits: 1 },
        { key: "SES_AngleS_AF", byte: 1, bit: 3, bits: 1 },
        { key: "SES_SensorPow", byte: 1, bit: 4, bits: 1 },
        { key: "SES_Alignment", byte: 1, bit: 5, bits: 1 },
        { key: "SES_OverAngle", byte: 1, bit: 6, bits: 1 },
        { key: "SES_StrMtrStall", byte: 1, bit: 7, bits: 1 },
        { key: "SES_MtrCurtFault", byte: 2, bit: 0, bits: 1 },
        { key: "SES_SensorCL", byte: 2, bit: 1, bits: 1 },
        { key: "SES_TorqT1_OC", byte: 2, bit: 2, bits: 1 },
        { key: "SES_TorqT1_AF", byte: 2, bit: 3, bits: 1 },
        { key: "SES_TorqT2_OC", byte: 2, bit: 4, bits: 1 },
        { key: "SES_TorqT2_AF", byte: 2, bit: 5, bits: 1 },
        { key: "SES_SentAngle", byte: 2, bit: 6, bits: 1 },
        { key: "SES_StrMtrIdling", byte: 2, bit: 7, bits: 1 },
        { key: "SES_EPROM", byte: 3, bit: 0, bits: 1 },
        { key: "SES_VehSpdSnapshot", byte: 7, bit: 0, bits: 8 },
      ];
    case "ses:ses_version":
      return [
        { key: "SES_SW_Version", byte: 0, bit: 0, bits: 8, factor: 0.01 },
        { key: "SES_HW_Version", byte: 1, bit: 0, bits: 8, factor: 0.1 },
      ];
    case "ses:ses_test":
      return [
        { key: "SES_MtrCurt", byte: 1, bit: 0, bits: 16, signed: true },
        { key: "SES_ECUTemp", byte: 3, bit: 0, bits: 16 },
        { key: "SES_PowVolt", byte: 5, bit: 0, bits: 16 },
      ];
    case "seb:vcu_seb_req":
      return [
        { key: "align_enable", byte: 0, bit: 0, bits: 1 },
        { key: "control_enable", byte: 0, bit: 1, bits: 1 },
        { key: "control_mode", byte: 0, bit: 2, bits: 1 },
        { key: "auto_brake", byte: 0, bit: 3, bits: 1 },
        { key: "stroke_req", byte: 2, bit: 0, bits: 16 },
        { key: "pressure_req", byte: 3, bit: 0, bits: 8 },
        { key: "rolling_counter", byte: 6, bit: 4, bits: 4 },
        { key: "checksum", byte: 7, bit: 0, bits: 8 },
      ];
    case "seb:seb_status":
      return [
        { key: "alignment_status", byte: 0, bit: 0, bits: 1 },
        { key: "control_enable_sts", byte: 0, bit: 1, bits: 1 },
        { key: "control_mode_sts", byte: 0, bit: 2, bits: 2 },
        { key: "auto_brake_status", byte: 0, bit: 4, bits: 1 },
        { key: "error_status", byte: 0, bit: 6, bits: 2 },
        { key: "stroke_value", byte: 2, bit: 0, bits: 16 },
        { key: "pressure_value", byte: 3, bit: 0, bits: 8 },
        { key: "angle_value", byte: 5, bit: 0, bits: 16, signed: true },
        { key: "rolling_counter", byte: 6, bit: 4, bits: 4 },
        { key: "checksum", byte: 7, bit: 0, bits: 8 },
      ];
    case "seb:seb_err_info":
      return [
        { key: "SEB_ECUUnderVolt", byte: 0, bit: 0, bits: 1 },
        { key: "SEB_ECUOverVolt", byte: 0, bit: 1, bits: 1 },
        { key: "SEB_CanComErr", byte: 0, bit: 2, bits: 1 },
        { key: "SEB_ECUTempErr", byte: 0, bit: 3, bits: 1 },
        { key: "SEB_DomainSC", byte: 0, bit: 4, bits: 1 },
        { key: "SEB_DomainV", byte: 0, bit: 5, bits: 1 },
        { key: "SEB_DomainT", byte: 0, bit: 6, bits: 1 },
        { key: "SEB_AngleP_OC", byte: 0, bit: 7, bits: 1 },
        { key: "SEB_AngleP_AF", byte: 1, bit: 0, bits: 1 },
        { key: "SEB_AngleS_OC", byte: 1, bit: 1, bits: 1 },
        { key: "SEB_AngleS_AF", byte: 1, bit: 2, bits: 1 },
        { key: "SEB_NoPreSensor", byte: 1, bit: 3, bits: 1 },
        { key: "SEB_SensorUCL", byte: 1, bit: 5, bits: 1 },
        { key: "SEB_AlignmentErr", byte: 1, bit: 6, bits: 1 },
        { key: "SEB_AngleOver", byte: 1, bit: 7, bits: 1 },
        { key: "SEB_MtrStall", byte: 2, bit: 1, bits: 1 },
        { key: "SEB_MtrDC", byte: 2, bit: 2, bits: 1 },
        { key: "SEB_OilErr", byte: 2, bit: 3, bits: 1 },
        { key: "SEB_InitOil", byte: 2, bit: 4, bits: 1 },
        { key: "SEB_SentValue", byte: 2, bit: 5, bits: 1 },
        { key: "SEB_MtrNoLoad", byte: 2, bit: 6, bits: 1 },
        { key: "SEB_PreSensorOver", byte: 3, bit: 0, bits: 1 },
        { key: "SEB_LowVoltCharging", byte: 3, bit: 1, bits: 1 },
      ];
    case "seb:seb_version":
      return [
        { key: "SEB_SW_Version", byte: 0, bit: 0, bits: 8, factor: 0.01 },
        { key: "SEB_HW_Version", byte: 1, bit: 0, bits: 8, factor: 0.1 },
      ];
    case "seb:seb_test":
      return [
        { key: "SEB_MtrCurr", byte: 1, bit: 0, bits: 16, signed: true },
        { key: "SEB_ECUTemp", byte: 3, bit: 0, bits: 16 },
        { key: "SEB_PowVolt", byte: 5, bit: 0, bits: 16 },
      ];
  }
  return [];
}

function isExhaustiveEnum(f: CanField): boolean {
  if (!f.options) return false;
  const rawMin = f.min !== undefined ? Math.round((f.min - f._offset) / f._factor) : 0;
  const rawMax = f.max !== undefined ? Math.round((f.max - f._offset) / f._factor) : (2 ** f._size) - 1;
  const range = rawMax - rawMin + 1;
  return f.options.length >= range;
}

export const CAN_MESSAGES: CanMessageDef[] = CANONICAL_MESSAGES
  .flatMap((message) => message.instances
    .filter((instance): instance is CanonicalInstance & { bus: Bus } => instance.bus === "high" || instance.bus === "low")
    .map((instance) => {
      const id = normalizeCanId(instance.id);
      const generated = message.codec.strategy === "generated";
      const rawFields = (message.layout.fields && message.layout.fields.length > 0)
        ? message.layout.fields
        : getCustomFields(message.canonical_key);
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
        fields: rawFields.map((field) => fieldDefinition(message.canonical_key, field)),
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

export function decodeFrameResult(bus: Bus, id: string, data: readonly number[], dlc?: number): DecodeFrameResult {
  const definition = findMessage(bus, id);
  if (!definition) return { status: "unknown_message", signals: { bus } };

  const buf = new Uint8Array(8);
  for (let i = 0; i < Math.min(data.length, 8); i++) buf[i] = data[i];
  const view = new DataView(buf.buffer);

  const byteOrder = definition.byteOrder;
  const val_le = view.getBigUint64(0, true);
  const val_be = view.getBigUint64(0, false);

  const signals: Record<string, unknown> = {};

  for (const f of definition.fields) {
    const { key, _byte, _bit_offset, _size, _type, _factor, _offset } = f;

    let rawBig = 0n;
    if (byteOrder === "intel") {
      const startBit = BigInt(_byte * 8 + _bit_offset);
      const mask = (1n << BigInt(_size)) - 1n;
      rawBig = (val_le >> startBit) & mask;
    } else { // motorola
      const byteLsb = _byte + Math.floor((_size - 1) / 8);
      const startBit = BigInt((7 - byteLsb) * 8 + _bit_offset);
      const mask = (1n << BigInt(_size)) - 1n;
      rawBig = (val_be >> startBit) & mask;
    }

    let raw = Number(rawBig);

    if (_type === "signed") {
      if (rawBig & (1n << BigInt(_size - 1))) {
        raw = Number(rawBig - (1n << BigInt(_size)));
      }
    }

    let finalVal: number | boolean = raw * _factor + _offset;

    // Handle precision issues with floats
    if (typeof finalVal === "number" && (_factor % 1 !== 0 || _offset % 1 !== 0)) {
      finalVal = Math.round(finalVal * 1000000) / 1000000;
    }

    if (f.kind === "boolean") {
      finalVal = Boolean(raw);
    }

    signals[key] = finalVal;

    // Emit enum label as {key}_name if this field has options
    if (f.options && typeof finalVal === "number") {
      const option = f.options.find((o: any) => o.value === raw);
      if (option) {
        signals[`${key}_name`] = option.label;
      }
    }
  }

  return { status: "ok", signals };
}

class CanonicalCanCodec {
  get messages() {
    return CAN_BY_BUS_ID;
  }

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

    let val_le = 0n;
    let val_be = 0n;
    const byteOrder = definition.byteOrder;

    for (const f of definition.fields) {
      const { key, _byte, _bit_offset, _size, _type, _factor, _offset, min, max, options } = f;
      if (values[key] === undefined) continue;

      let val = Number(values[key]);
      if (!Number.isFinite(val)) {
        throw new ValidationError(`Signal ${key} must be finite`);
      }

      if (typeof min === "number" && typeof max === "number") {
        if (val < min || val > max) {
          throw new ValidationError(`Signal ${key} value ${val} out of range [${min}, ${max}]`);
        }
      }
      
      const rawVal = Math.round((val - _offset) / _factor);
      
      if (options && f.kind === "enum" && isExhaustiveEnum(f)) {
        if (!options.some((o: any) => o.value === rawVal)) {
          throw new ValidationError(`Signal ${key} value ${rawVal} not in allowed options`);
        }
      }

      const minRaw = _type === "signed" ? -(2 ** (_size - 1)) : 0;
      const maxRaw = _type === "signed" ? (2 ** (_size - 1)) - 1 : (2 ** _size) - 1;
      if (rawVal < minRaw || rawVal > maxRaw) {
        throw new ValidationError(`Signal ${key} raw value ${rawVal} overflows bit-width ${_size}`);
      }

      // Handle signed
      let rawBig = BigInt(rawVal);
      if (_type === "signed" && rawVal < 0) {
        rawBig = (1n << BigInt(_size)) + BigInt(rawVal);
      }

      rawBig = rawBig & ((1n << BigInt(_size)) - 1n);

      if (byteOrder === "intel") {
        const startBit = BigInt(_byte * 8 + _bit_offset);
        val_le |= (rawBig << startBit);
      } else {
        const byteLsb = _byte + Math.floor((_size - 1) / 8);
        const startBit = BigInt((7 - byteLsb) * 8 + _bit_offset);
        val_be |= (rawBig << startBit);
      }
    }

    const buf = new Uint8Array(8);
    const view = new DataView(buf.buffer);
    if (byteOrder === "intel") {
      view.setBigUint64(0, val_le, true);
    } else {
      view.setBigUint64(0, val_be, false);
    }

    const finalData = Array.from(buf.subarray(0, definition.dlc));
    
    if (definition.bus === "low" && (definition.id === "0x169" || definition.id === "0x7B9")) {
      let chk = 0;
      for (let i = 0; i < 7; i++) {
        chk ^= finalData[i];
      }
      finalData[7] = chk ^ 0xFF;
    }

    return { dlc: definition.dlc, data: finalData };
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
