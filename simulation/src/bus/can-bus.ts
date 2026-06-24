/**
 * CanBusModel — scheduled CAN bus with CAN-ID priority ordering.
 *
 * Messages are queued with a delivery timestamp. At each tick, all
 * messages whose scheduled time has arrived are delivered in order
 * of CAN ID priority (lower ID = higher priority, matching real
 * CAN arbitration rules).
 */

import type { BusId, BusStats, SimFrame } from "../core/types.js";

const EMPTY_STATS = (): BusStats => ({
  active: false,
  total: 0,
  fps: 0,
  loadPct: 0,
  tec: 0,
  rec: 0,
  byId: {},
});

interface ScheduledMsg {
  frame: SimFrame;
  deliverAtMs: number;
}

/** Parse a CAN ID string like "0x300" to a numeric priority value. */
function canIdPriority(canId: string): number {
  return parseInt(canId.replace("0x", ""), 16);
}

export class CanBusModel {
  readonly busId: BusId;
  private queue: ScheduledMsg[] = [];
  private deliveredFrames: SimFrame[] = [];
  private statsSnapshot: BusStats = EMPTY_STATS();
  private totalFrames = 0;
  private lastRxMs = -Infinity;
  private startMs = 0;

  constructor(busId: BusId) {
    this.busId = busId;
  }

  /** Schedule a frame for delivery at the given simulation time. */
  schedule(frame: SimFrame, deliverAtMs: number): void {
    this.queue.push({ frame, deliverAtMs });
  }

  /**
   * Deliver all frames whose scheduled time has arrived.
   * Frames are sorted by CAN ID priority (lower ID first).
   */
  deliver(nowMs: number): SimFrame[] {
    // Partition: frames due now vs still pending
    const due: ScheduledMsg[] = [];
    const pending: ScheduledMsg[] = [];

    for (const item of this.queue) {
      if (item.deliverAtMs <= nowMs) {
        due.push(item);
        this.totalFrames++;
        this.lastRxMs = nowMs;
        const key = item.frame.canId;
        this.statsSnapshot.byId[key] = (this.statsSnapshot.byId[key] ?? 0) + 1;
      } else {
        pending.push(item);
      }
    }
    this.queue = pending;
    this.statsSnapshot.total = this.totalFrames;

    // Sort by CAN ID priority: lower ID = higher priority
    due.sort((a, b) => canIdPriority(a.frame.canId) - canIdPriority(b.frame.canId));

    const frames = due.map((d) => d.frame);
    this.deliveredFrames = frames;
    return frames;
  }

  /** Get a snapshot of current bus statistics. */
  getStats(nowMs: number, uptimeS: number): BusStats {
    const active = nowMs - this.lastRxMs < 5000;
    const fps = uptimeS > 0 ? this.totalFrames / uptimeS : 0;
    const loadPct = uptimeS > 0 ? Math.min(100, (this.totalFrames * 108) / (uptimeS * 500_000) * 100) : 0;
    return {
      ...this.statsSnapshot,
      active,
      fps: Math.round(fps * 10) / 10,
      loadPct: Math.round(loadPct * 10) / 10,
    };
  }

  /** All frames delivered this tick. */
  get delivered(): SimFrame[] {
    return this.deliveredFrames;
  }

  /** Reset state. */
  reset(startMs: number): void {
    this.queue = [];
    this.deliveredFrames = [];
    this.statsSnapshot = EMPTY_STATS();
    this.totalFrames = 0;
    this.lastRxMs = -Infinity;
    this.startMs = startMs;
  }

  /** Total queued (not yet delivered) messages. */
  get queueLength(): number {
    return this.queue.length;
  }

  /** Total frames ever delivered. */
  get totalDelivered(): number {
    return this.totalFrames;
  }
}
