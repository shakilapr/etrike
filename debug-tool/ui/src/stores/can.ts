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

  clear(): void {
    this.head = 0;
    this.count = 0;
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
    frameBuffer.clear();
    frameBuffer.pushMany(input);
    frameStore.set(frameBuffer.toArray());
    
    // Also clear _latestById so we don't carry over old frames across sets
    for (const key in _latestById) delete _latestById[key];
    
    for (const f of input) {
      if (f) _latestById[`${f.bus}:${f.id}`] = f;
    }
    latestById.set({ ..._latestById });
  },
  update(updater: (current: CanFrame[]) => CanFrame[]): void {
    frameStore.update((current) => {
      const next = updater(current);
      for (const f of next) {
        if (f) _latestById[`${f.bus}:${f.id}`] = f;
      }
      latestById.set({ ..._latestById });
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

export function ingestInitialFrames(input: CanFrame[]): void {
  // Reset the buffer on initial load
  frameBuffer.clear();
  for (const key in _latestById) delete _latestById[key];
  
  const limited = input.slice(-FRAME_BUFFER_SIZE);
  limited.forEach((f) => frameBuffer.push(f));
  frameStore.set(frameBuffer.toArray());
  for (const f of limited) {
    if (f) _latestById[`${f.bus}:${f.id}`] = f;
  }
  latestById.set({ ..._latestById });
}

const _latestById: Record<string, CanFrame> = {};
let pendingFrames = false;

function flushFrames() {
  frameStore.set(frameBuffer.toArray());
  latestById.set({ ..._latestById });
  pendingFrames = false;
}

export function ingestMessage(message: { type: string; payload: unknown }): void {
  if (message.type === "can_frame") {
    const frame = message.payload as CanFrame;
    if (!frame) return;
    frameBuffer.push(frame);
    _latestById[`${frame.bus}:${frame.id}`] = frame;
    
    if (!pendingFrames) {
      pendingFrames = true;
      requestAnimationFrame(flushFrames);
    }
  } else if (message.type === "can_frames_batch") {
    const batch = message.payload as CanFrame[];
    if (!batch || !Array.isArray(batch) || batch.length === 0) return;
    frameBuffer.pushMany(batch);
    for (const f of batch) {
      if (f) _latestById[`${f.bus}:${f.id}`] = f;
    }
    
    if (!pendingFrames) {
      pendingFrames = true;
      requestAnimationFrame(flushFrames);
    }
  } else if (message.type === "stats") {
    stats.set(message.payload as CanStats);
  } else if (message.type === "status") {
    status.update((current) => ({ ...current, ...(message.payload as Record<string, unknown>) }));
  } else if (message.type === "cmd_ack") {
    commandAcks.update((current) => [message.payload as Record<string, unknown>, ...current].slice(0, 30));
  }
}
