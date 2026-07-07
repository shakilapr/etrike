import { derived, writable } from "svelte/store";
import type { BackendStatus } from "../lib/api";
import type { CanFrame, CanStats, BusStats } from "../lib/can-decoder";

const emptyBusStats = (): BusStats => ({
  active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {}
});

export const latestById = writable<Record<string, CanFrame>>({});

function buildLatestById(input: CanFrame[]): Record<string, CanFrame> {
  const latest: Record<string, CanFrame> = {};
  for (const frame of input) {
    if (frame) latest[`${frame.bus}:${frame.id}`] = frame;
  }
  return latest;
}

const frameStore = writable<CanFrame[]>([]);
export const frames = {
  subscribe: frameStore.subscribe,
  set(input: CanFrame[]): void {
    frameStore.set(input);
    latestById.set(buildLatestById(input));
  },
  update(updater: (current: CanFrame[]) => CanFrame[]): void {
    frameStore.update((current) => {
      const next = updater(current);
      latestById.set(buildLatestById(next));
      return next;
    });
  }
};
export const stats = writable<CanStats>({
  ts: Date.now() / 1000,
  uptime_s: 0,
  buses: { high: emptyBusStats(), low: emptyBusStats() }
});
export const status = writable<Partial<BackendStatus>>({
  backend_online: false,
  adapter_connected: false,
  esp32_connected: false
});
export const wsConnected = writable(false);
export const commandAcks = writable<Record<string, unknown>[]>([]);

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
    const frame = message.payload as CanFrame;
    if (!frame) return;
    frameStore.update((current) => [...current, frame].slice(-1000));
    latestById.update((current) => ({ ...current, [`${frame.bus}:${frame.id}`]: frame }));
  } else if (message.type === "can_frames_batch") {
    const batch = message.payload as CanFrame[];
    if (batch.length === 0) return;

    frameStore.update((current) => [...current, ...batch].slice(-1000));
    latestById.update((current) => {
      const next = { ...current };
      for (const f of batch) {
        if (f) next[`${f.bus}:${f.id}`] = f;
      }
      return next;
    });
  } else if (message.type === "stats") {
    stats.set(message.payload as CanStats);
  } else if (message.type === "status") {
    status.update((current) => ({ ...current, ...(message.payload as Record<string, unknown>) }));
  } else if (message.type === "cmd_ack") {
    commandAcks.update((current) => [message.payload as Record<string, unknown>, ...current].slice(0, 30));
  }
}
