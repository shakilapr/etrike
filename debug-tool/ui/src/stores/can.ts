import { derived, writable } from "svelte/store";
import type { BackendStatus } from "../lib/api";
import type { CanFrame, CanStats, BusStats } from "../lib/can-decoder";

const emptyBusStats = (): BusStats => ({
  active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {}
});

export const frames = writable<CanFrame[]>([]);
export const stats = writable<CanStats>({
  ts: Date.now() / 1000,
  uptime_s: 0,
  buses: { high: emptyBusStats(), low: emptyBusStats() }
});
export const status = writable<Partial<BackendStatus>>({
  backend_online: false,
  esp32_connected: false
});
export const wsConnected = writable(false);
export const commandAcks = writable<Record<string, unknown>[]>([]);

export const latestById = derived(frames, ($frames) => {
  const latest: Record<string, CanFrame> = {};
  for (const frame of $frames) {
    latest[`${frame.bus}:${frame.id}`] = frame;
  }
  return latest;
});

export const recentFrameRate = derived(frames, ($frames) => {
  if ($frames.length < 2) return 0;
  const newest = $frames.at(-1)?.ts ?? 0;
  const since = newest - 5;
  return $frames.filter((frame) => frame.ts >= since).length / 5;
});

export function ingestInitialFrames(input: CanFrame[]): void {
  frames.set(input.slice(-800));
}

export function ingestMessage(message: { type: string; payload: unknown }): void {
  if (message.type === "can_frame") {
    frames.update((current) => [...current, message.payload as CanFrame].slice(-1000));
  } else if (message.type === "stats") {
    stats.set(message.payload as CanStats);
  } else if (message.type === "status") {
    status.update((current) => ({ ...current, ...(message.payload as Record<string, unknown>) }));
  } else if (message.type === "cmd_ack") {
    commandAcks.update((current) => [message.payload as Record<string, unknown>, ...current].slice(0, 30));
  }
}
