import { derived, writable } from "svelte/store";
import type { BackendStatus } from "../lib/api";
import type { CanFrame, CanStats, BusStats } from "../lib/can-decoder";

const INITIAL_FRAME_LIMIT = 800;
const STREAM_FRAME_LIMIT = 1000;

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

function appendFrameBounded(current: CanFrame[], frame: CanFrame, limit = STREAM_FRAME_LIMIT): CanFrame[] {
  const overflow = current.length - limit + 1;
  if (overflow > 0) current.splice(0, overflow);
  current.push(frame);
  return current;
}

function appendBatchBounded(current: CanFrame[], batch: CanFrame[], limit = STREAM_FRAME_LIMIT): CanFrame[] {
  const valid = batch.filter(Boolean);
  if (valid.length >= limit) return valid.slice(-limit);

  const overflow = current.length + valid.length - limit;
  if (overflow > 0) current.splice(0, overflow);
  current.push(...valid);
  return current;
}

function setLatestFrame(frame: CanFrame): void {
  latestById.update((current) => {
    current[`${frame.bus}:${frame.id}`] = frame;
    return current;
  });
}

function setLatestBatch(batch: CanFrame[]): void {
  latestById.update((current) => {
    for (const frame of batch) {
      if (frame) current[`${frame.bus}:${frame.id}`] = frame;
    }
    return current;
  });
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
  frames.set(input.slice(-INITIAL_FRAME_LIMIT));
}

export function ingestMessage(message: { type: string; payload: unknown }): void {
  if (message.type === "can_frame") {
    const frame = message.payload as CanFrame;
    if (!frame) return;
    frameStore.update((current) => appendFrameBounded(current, frame));
    setLatestFrame(frame);
  } else if (message.type === "can_frames_batch") {
    const batch = message.payload as CanFrame[];
    if (!Array.isArray(batch) || batch.length === 0) return;

    frameStore.update((current) => appendBatchBounded(current, batch));
    setLatestBatch(batch);
  } else if (message.type === "stats") {
    stats.set(message.payload as CanStats);
  } else if (message.type === "status") {
    status.update((current) => ({ ...current, ...(message.payload as Record<string, unknown>) }));
  } else if (message.type === "cmd_ack") {
    commandAcks.update((current) => [message.payload as Record<string, unknown>, ...current].slice(0, 30));
  }
}
