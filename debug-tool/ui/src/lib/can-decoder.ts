import { decoder, initCanDatabase } from "@etrike/debug-shared";
export * from "@etrike/debug-shared";
import type { Bus } from "@etrike/debug-shared";

import highYaml from "../../../../shared/can/can_high.yaml?raw";
import lowYaml from "../../../../shared/can/can_low.yaml?raw";

// Initialize the decoder for the frontend at module load time
initCanDatabase();

export function encodePayload(bus: Bus, id: string, values: Record<string, number | boolean>): { dlc: number; data: number[] } {
  return decoder.encode(bus, id, values);
}

export function formatBytes(data: number[]): string {
  if (data.length === 0) return "--";
  return data.map((b) => b.toString(16).padStart(2, "0").toUpperCase()).join(" ");
}

export function formatDecoded(decoded: Record<string, unknown>): string {
  const keys = Object.keys(decoded).filter((k) => !k.endsWith("_name") && !k.endsWith("_label"));
  if (keys.length === 0) return "event";
  return keys.map((k) => `${k.replace("_mmps", "")}=${decoded[k]}`).join(", ");
}

export function frameTime(frame: { ts: number }): string {
  const ts = frame.ts > 1_000_000_000_000 ? frame.ts : frame.ts * 1000;
  const d = new Date(ts);
  return `${d.getHours().toString().padStart(2, "0")}:${d.getMinutes().toString().padStart(2, "0")}:${d.getSeconds().toString().padStart(2, "0")}.${d.getMilliseconds().toString().padStart(3, "0")}`;
}

export function frameAge(frame: { ts: number }): string {
  const ts = frame.ts > 1_000_000_000_000 ? frame.ts : frame.ts * 1000;
  return `${Math.floor(Date.now() - ts)} ms`;
}

