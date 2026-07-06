import type { Bus, CanFrame } from "../types/can";

/**
 * Virtual dual-channel CAN bus.
 * Routes frames between ECU models within the SimulationEngine.
 * Models subscribe to specific CAN IDs and receive frames addressed to them.
 */
export class VirtualCanBus {
  private highBus: CanFrame[] = [];
  private lowBus: CanFrame[] = [];
  private subscribers = new Map<string, Array<(frame: CanFrame) => void>>();

  /** Send a frame onto the virtual bus. */
  send(frame: CanFrame): void {
    const bus = frame.bus === "low" ? this.lowBus : this.highBus;
    bus.push(frame);
    // Notify subscribers
    const key = `${frame.bus}:${frame.id}`;
    const subs = this.subscribers.get(key);
    if (subs) for (const cb of subs) cb(frame);
  }

  /** Subscribe to a specific CAN ID on a specific bus. */
  subscribe(bus: Bus, id: string, callback: (frame: CanFrame) => void): void {
    const key = `${bus}:${id}`;
    if (!this.subscribers.has(key)) this.subscribers.set(key, []);
    this.subscribers.get(key)!.push(callback);
  }

  /** Drain all pending frames from a bus (called after each tick). */
  drain(bus: Bus): CanFrame[] {
    const arr = bus === "low" ? this.lowBus : this.highBus;
    const frames = [...arr];
    arr.length = 0;
    return frames;
  }

  /** Clear all buses and subscribers. */
  reset(): void {
    this.highBus.length = 0;
    this.lowBus.length = 0;
    this.subscribers.clear();
  }
}
