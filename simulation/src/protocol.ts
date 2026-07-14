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

export function encodeSimFrame(
  key: MessageKey,
  values: Readonly<Record<string, unknown>>,
  bus: BusId,
  sender: SimNodeId,
  simTimeMs: number,
): SimFrame {
  const [status, encoded] = encode(key, values, bus);
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
    decoded: { ...values },
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
