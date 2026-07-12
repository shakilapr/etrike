import type { Bus, CanFrame } from "../types/can";

export type FrameSource = "physical" | "emulated" | "simulated";

export interface FrameRouterEvent {
  type: "collision";
  key: string;
  existing: FrameSource;
  incoming: FrameSource;
  frame: CanFrame;
}

/**
 * Per-(bus,id) source routing table.
 * Enforces the invariant that each (bus, id) pair has exactly one
 * authoritative source. Collisions (same ID from two different sources)
 * are logged and the existing source wins.
 */
export class FrameRouter {
  private sources = new Map<string, FrameSource>();
  private listeners: Array<(event: FrameRouterEvent) => void> = [];

  private key(bus: Bus, id: string): string {
    return `${bus}:${id}`;
  }

  /** Set the authoritative source for a given CAN ID. */
  setSource(bus: Bus, id: string, source: FrameSource): void {
    this.sources.set(this.key(bus, id), source);
  }

  /** Remove a source entry. */
  removeSource(bus: Bus, id: string): void {
    this.sources.delete(this.key(bus, id));
  }

  /** Bulk-set sources from a partial mapping. "*" entries are skipped (auto-detect). */
  setSources(entries: Record<string, FrameSource | "*">): void {
    for (const [rawKey, source] of Object.entries(entries)) {
      if (source === "*") continue;
      const [bus, id] = rawKey.split(":");
      if ((bus === "high" || bus === "low") && id) {
        this.setSource(bus, id, source);
      }
    }
  }

  /** Clear all source entries. */
  clear(): void {
    this.sources.clear();
  }

  /**
   * Resolve which source a frame should come from.
   * Returns the frame with source metadata if accepted, or null if the
   * frame should be dropped (silent collision loss).
   */
  resolve(frame: CanFrame, incomingSource: FrameSource): CanFrame | null {
    const k = this.key(frame.bus, frame.frame.id);
    const existing = this.sources.get(k);

    if (!existing) {
      // No rule — auto-accept and auto-register the source
      this.sources.set(k, incomingSource);
      return frame;
    }

    if (existing === incomingSource) {
      return frame; // same source — accept
    }

    // Collision: different source claims same ID
    this.emitCollision(k, existing, incomingSource, frame);
    return null; // existing source wins — drop incoming frame
  }

  /** Register a collision listener. */
  onCollision(listener: (event: FrameRouterEvent) => void): void {
    this.listeners.push(listener);
  }

  /** Get the current source table (for debugging). */
  getSourceTable(): Record<string, FrameSource> {
    const result: Record<string, FrameSource> = {};
    for (const [key, source] of this.sources) {
      result[key] = source;
    }
    return result;
  }

  private emitCollision(
    key: string,
    existing: FrameSource,
    incoming: FrameSource,
    frame: CanFrame
  ): void {
    const event: FrameRouterEvent = { type: "collision", key, existing, incoming, frame };
    for (const listener of this.listeners) {
      try { listener(event); } catch { /* don't let one broken listener break the router */ }
    }
  }
}
