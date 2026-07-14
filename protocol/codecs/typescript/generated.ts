import DISCOVERY from "../../generated/discovery.json";
import { type CanFrame, type CodecStatus, type DecodeResult, type EncodeResult, frame, validateFrame } from "./types";

interface FieldMetadata {
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

interface InstanceMetadata {
  readonly bus: string;
  readonly id: number;
  readonly frame_format: "standard" | "extended";
}

interface MessageMetadata {
  readonly byte_order: "big" | "little";
  readonly codec: { readonly strategy: "generated" | "custom" };
  readonly dlc: number;
  readonly instances: readonly InstanceMetadata[];
  readonly layout: { readonly kind: string; readonly fields?: readonly FieldMetadata[] };
}

interface DiscoveryMessage extends Omit<MessageMetadata, "instances"> {
  readonly canonical_key: string;
  readonly instances: readonly (Omit<InstanceMetadata, "id"> & { readonly id: string })[];
}

const metadata: Readonly<Record<string, MessageMetadata>> = Object.fromEntries(
  (DISCOVERY.messages as unknown as readonly DiscoveryMessage[]).map((message) => [
    message.canonical_key,
    {
      ...message,
      instances: message.instances.map((instance) => ({ ...instance, id: Number(instance.id) })),
    },
  ]),
);

export function hasMessage(message: string): boolean {
  return metadata[message] !== undefined;
}

export function isGenerated(message: string): boolean {
  return metadata[message]?.codec.strategy === "generated";
}

function generatedMessage(message: string): MessageMetadata {
  const selected = metadata[message];
  if (selected === undefined) throw new RangeError(`unknown message: ${message}`);
  if (selected.codec.strategy !== "generated") throw new RangeError(`${message} selects a custom codec`);
  return selected;
}

function instanceFor(message: MessageMetadata, bus: string): InstanceMetadata | undefined {
  const matches = message.instances.filter((instance) => instance.bus === bus);
  return matches.length === 1 ? matches[0] : undefined;
}

function limits(field: FieldMetadata): readonly [number, number] {
  const rawMinimum = field.signed ? -(2 ** (field.bits - 1)) : 0;
  const rawMaximum = field.signed ? 2 ** (field.bits - 1) - 1 : 2 ** field.bits - 1;
  const factor = field.factor ?? 1;
  const offset = field.offset ?? 0;
  return [field.min ?? rawMinimum * factor + offset, field.max ?? rawMaximum * factor + offset];
}

function isExhaustiveEnum(field: FieldMetadata): boolean {
  if (field.enum === undefined) return false;
  const rawMinimum = field.signed ? -(2 ** (field.bits - 1)) : 0;
  const rawMaximum = field.signed ? 2 ** (field.bits - 1) - 1 : 2 ** field.bits - 1;
  const factor = field.factor ?? 1;
  const offset = field.offset ?? 0;
  const rawMinVal = field.min !== undefined ? Math.round((field.min - offset) / factor) : rawMinimum;
  const rawMaxVal = field.max !== undefined ? Math.round((field.max - offset) / factor) : rawMaximum;
  const range = rawMaxVal - rawMinVal + 1;
  return Object.keys(field.enum).length >= range;
}

function encodePayload(message: MessageMetadata, values: Readonly<Record<string, unknown>>): DecodeResult<Uint8Array> {
  const payload = new Uint8Array(message.dlc);
  for (const field of message.layout.fields ?? []) {
    const value = field.constant ?? values[field.key];
    if (typeof value !== "number" || !Number.isFinite(value)) return ["value_out_of_range"];
    const [minimum, maximum] = limits(field);
    if (value < minimum || value > maximum) return ["value_out_of_range"];
    const unrounded = (value - (field.offset ?? 0)) / (field.factor ?? 1);
    let raw = Math.round(unrounded);
    if (Math.abs(unrounded - raw) > 1e-9) return ["value_out_of_range"];
    if (field.enum !== undefined && isExhaustiveEnum(field) && !(String(raw) in field.enum)) return ["invalid_enum"];
    if (field.signed && raw < 0) raw += 2 ** field.bits;

    if (field.bit === 0 && field.bits % 8 === 0) {
      const width = field.bits / 8;
      for (let index = 0; index < width; index += 1) {
        const shift = message.byte_order === "big" ? width - index - 1 : index;
        payload[field.byte + index] = Math.floor(raw / 2 ** (shift * 8)) & 0xff;
      }
    } else {
      const start = field.byte * 8 + field.bit;
      for (let offset = 0; offset < field.bits; offset += 1) {
        const position = start + offset;
        payload[Math.floor(position / 8)] |= (Math.floor(raw / 2 ** offset) % 2) << (position % 8);
      }
    }
  }
  return ["ok", payload];
}

export function encodeGenerated(
  messageKey: string,
  values: Readonly<Record<string, unknown>>,
  bus: string,
): EncodeResult {
  let message: MessageMetadata;
  try {
    message = generatedMessage(messageKey);
  } catch (error) {
    if (error instanceof RangeError && error.message.startsWith("unknown message")) return ["wrong_message_id"];
    throw error;
  }
  const instance = instanceFor(message, bus);
  if (instance === undefined) return ["wrong_message_id"];
  const [status, payload] = encodePayload(message, values);
  if (status !== "ok" || payload === undefined) return [status];
  return ["ok", frame(bus, instance.id, instance.frame_format, payload)];
}

export function decodeGenerated(messageKey: string, input: CanFrame): DecodeResult<Record<string, number>> {
  let message: MessageMetadata;
  try {
    message = generatedMessage(messageKey);
  } catch (error) {
    if (error instanceof RangeError && error.message.startsWith("unknown message")) return ["wrong_message_id"];
    throw error;
  }
  const instance = instanceFor(message, input.bus);
  if (instance === undefined) return ["wrong_message_id"];
  const status = validateFrame(input, {
    bus: instance.bus,
    id: instance.id,
    frameFormat: instance.frame_format,
    dlc: message.dlc,
  });
  if (status !== "ok") return [status];

  const values: Record<string, number> = {};
  for (const field of message.layout.fields ?? []) {
    let raw = 0;
    if (field.bit === 0 && field.bits % 8 === 0) {
      const width = field.bits / 8;
      for (let index = 0; index < width; index += 1) {
        const shift = message.byte_order === "big" ? width - index - 1 : index;
        raw += input.data[field.byte + index] * 2 ** (shift * 8);
      }
    } else {
      const start = field.byte * 8 + field.bit;
      for (let offset = 0; offset < field.bits; offset += 1) {
        const position = start + offset;
        raw += ((input.data[Math.floor(position / 8)] >> (position % 8)) & 1) * 2 ** offset;
      }
    }
    if (field.signed && raw >= 2 ** (field.bits - 1)) raw -= 2 ** field.bits;
    if (field.enum !== undefined && isExhaustiveEnum(field) && !(String(raw) in field.enum)) return ["invalid_enum"];
    const value = raw * (field.factor ?? 1) + (field.offset ?? 0);
    const [minimum, maximum] = limits(field);
    if (value < minimum || value > maximum) return ["value_out_of_range"];
    if (field.constant !== undefined && value !== field.constant) return ["constant_mismatch"];
    values[field.key] = value;
  }
  return ["ok", values];
}

export function decodeGeneratedInto(
  message: string,
  input: CanFrame,
  output: Record<string, unknown>,
): CodecStatus {
  const [status, value] = decodeGenerated(message, input);
  if (status === "ok" && value !== undefined) {
    for (const key of Object.keys(output)) delete output[key];
    Object.assign(output, value);
  }
  return status;
}
