import {
  decode,
  encode,
  frame as protocolFrame,
  type CodecStatus,
} from "../../protocol/codecs/typescript/index.js";
import {
  lookup,
  METADATA,
  WIRE_HASH,
} from "../../protocol/generated/typescript/etrike-protocol.js";

import type { BusId, SimFrame, SimNodeId } from "./core/types.js";

export { METADATA, WIRE_HASH };

export type MessageKey = keyof typeof METADATA;

export interface DecodedSimFrame {
  key: MessageKey;
  status: CodecStatus;
  values?: Record<string, unknown>;
}

function numericId(canId: string): number {
  return Number.parseInt(canId.replace(/^0x/i, ""), 16);
}

function textId(id: number): string {
  return `0x${id.toString(16).toUpperCase().padStart(3, "0")}`;
}

export function routeFor(bus: string, id: number) {
  return lookup(bus, id);
}

export function routeForSimFrame(input: Pick<SimFrame, "bus" | "canId">) {
  return routeFor(input.bus, numericId(input.canId));
}

export function decodeSimFrame(input: SimFrame): DecodedSimFrame | undefined {
  const route = routeForSimFrame(input);
  if (route === undefined) return undefined;
  const [status, values] = decode(
    route.key,
    protocolFrame(input.bus, numericId(input.canId), "standard", input.data, input.dlc),
  );
  return { key: route.key, status, values } as DecodedSimFrame;
}

export function decodeAs(input: SimFrame, key: MessageKey): Record<string, unknown> | undefined {
  const decoded = decodeSimFrame(input);
  return decoded?.key === key && decoded.status === "ok" ? decoded.values : undefined;
}

function fieldLimits(field: any): [number, number] {
  const rawMinimum = field.signed ? -(2 ** (field.bits - 1)) : 0;
  const rawMaximum = field.signed ? 2 ** (field.bits - 1) - 1 : 2 ** field.bits - 1;
  const factor = field.factor ?? 1;
  const offset = field.offset ?? 0;
  return [field.min ?? rawMinimum * factor + offset, field.max ?? rawMaximum * factor + offset];
}

function preprocessValues(key: MessageKey, values: Readonly<Record<string, unknown>>): Record<string, unknown> {
  const message = METADATA[key];
  if (message.codec.strategy !== "generated") {
    return { ...values };
  }
  const processed = { ...values };
  for (const field of message.layout.fields ?? []) {
    const value = values[field.key];
    if (typeof value === "number") {
      const [minimum, maximum] = fieldLimits(field);
      const clamped = Math.max(minimum, Math.min(maximum, value));
      const factor = field.factor ?? 1;
      const offset = field.offset ?? 0;
      const rounded = Math.round((clamped - offset) / factor) * factor + offset;
      processed[field.key] = rounded;
    }
  }
  return processed;
}

export function encodeSimFrame(
  key: MessageKey,
  values: Readonly<Record<string, unknown>>,
  bus: BusId,
  sender: SimNodeId,
  simTimeMs: number,
): SimFrame {
  const processedValues = preprocessValues(key, values);
  const [status, encoded] = encode(key, processedValues, bus);
  if (status !== "ok" || encoded === undefined) {
    throw new RangeError(`cannot encode ${key} on ${bus}: ${status}`);
  }
  return {
    simTimeMs,
    bus,
    canId: textId(encoded.id),
    name: METADATA[key].name,
    dlc: encoded.dlc,
    data: Array.from(encoded.data),
    sender,
    decoded: { ...processedValues },
  };
}

export function customRawSimFrame(
  key: MessageKey,
  data: ArrayLike<number>,
  bus: BusId,
  sender: SimNodeId,
  simTimeMs: number,
): SimFrame {
  const message = METADATA[key];
  if (message.codec.strategy !== "custom") {
    throw new RangeError(`${key} has a generated encoder`);
  }
  const instance = message.instances.find(candidate => candidate.bus === bus);
  if (instance === undefined) throw new RangeError(`${key} is not routed on ${bus}`);
  const payload = Array.from(data);
  if (payload.length !== message.dlc) {
    throw new RangeError(`${key} requires ${message.dlc} bytes, got ${payload.length}`);
  }
  return {
    simTimeMs,
    bus,
    canId: textId(instance.id),
    name: message.name,
    dlc: message.dlc,
    data: payload,
    sender,
  };
}
