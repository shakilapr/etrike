import { derived, writable } from "svelte/store";
import type { BackendStatus } from "../lib/api";
import type { CanFrame, CanStats, BusStats } from "../lib/can-decoder";

const emptyBusStats = (): BusStats => ({
  active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {}
});

// ── Ring buffer — avoids allocating a new array on every frame push ──────────
class RingBuffer<T> {
  private buf: (T | undefined)[];
  private head = 0;
  private count = 0;

  constructor(private readonly cap: number) {
    this.buf = new Array(cap);
  }

  push(item: T): void {
    this.buf[this.head] = item;
    this.head = (this.head + 1) % this.cap;
    if (this.count < this.cap) this.count++;
  }

  pushMany(items: T[]): void {
    for (const item of items) this.push(item);
  }

  toArray(): T[] {
    if (this.count === 0) return [];
    if (this.count < this.cap) return this.buf.slice(0, this.count) as T[];
    // Full buffer: return in chronological order (oldest → newest)
    return [...this.buf.slice(this.head), ...this.buf.slice(0, this.head)] as T[];
  }

  get length(): number { return this.count; }
  get latest(): T | undefined { return this.buf[(this.head - 1 + this.cap) % this.cap]; }
}

const FRAME_BUFFER_SIZE = 1000;
const frameBuffer = new RingBuffer<CanFrame>(FRAME_BUFFER_SIZE);

// ── Stores ───────────────────────────────────────────────────────────────────
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
    frameBuffer.pushMany(input);
    frameStore.set(frameBuffer.toArray());
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
  // Reset the buffer on initial load
  const limited = input.slice(-FRAME_BUFFER_SIZE);
  limited.forEach((f) => frameBuffer.push(f));
  frameStore.set(frameBuffer.toArray());
  latestById.set(buildLatestById(limited));
}

export function ingestMessage(message: { type: string; payload: unknown }): void {
  if (message.type === "can_frame") {
    const frame = message.payload as CanFrame;
    if (!frame) return;
    // Ring buffer push — no spread, no slice, no GC pressure
    frameBuffer.push(frame);
    frameStore.set(frameBuffer.toArray());
    latestById.update((current) => ({ ...current, [`${frame.bus}:${frame.id}`]: frame }));
  } else if (message.type === "can_frames_batch") {
    const batch = message.payload as CanFrame[];
    if (batch.length === 0) return;
    frameBuffer.pushMany(batch);
    frameStore.set(frameBuffer.toArray());
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
