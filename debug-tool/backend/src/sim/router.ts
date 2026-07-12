import type { Bus, CanFrame } from "../types/can";

export type FrameSource = "physical" | "emulated" | "simulated";
export type ProducerType = "physical_rx" | "simulation" | "replay" | "user" | "test";

export interface FrameRouterEvent {
  type: "collision";
  key: string;
  existing: FrameSource;
  incoming: FrameSource;
  frame: CanFrame;
}

export interface RoutingContext {
  producer: ProducerType;
}

export interface RoutingDisposition {
  accepted: boolean;
  ui: boolean;
  recording: boolean;
  sim_input: boolean;
  physical_tx: boolean;
  reason?: string;
  frame?: CanFrame;
}

export class FrameRouter {
  private sources = new Map<string, FrameSource>();
  private listeners: Array<(event: FrameRouterEvent) => void> = [];
  private sequenceCounter = 0; // Exactly one sequence per accepted observation

  private key(bus: Bus, id: string): string {
    return `${bus}:${id}`;
  }

  setSource(bus: Bus, id: string, source: FrameSource): void {
    this.sources.set(this.key(bus, id), source);
  }

  removeSource(bus: Bus, id: string): void {
    this.sources.delete(this.key(bus, id));
  }

  setSources(entries: Record<string, FrameSource | "*">): void {
    for (const [rawKey, source] of Object.entries(entries)) {
      if (source === "*") continue;
      const [bus, id] = rawKey.split(":");
      if ((bus === "high" || bus === "low") && id) {
        this.setSource(bus as Bus, id, source as FrameSource);
      }
    }
  }

  clear(): void {
    this.sources.clear();
  }

  route(frame: CanFrame, ctx: RoutingContext): RoutingDisposition {
    const k = this.key(frame.bus, frame.frame.id);
    const existingSource = this.sources.get(k);

    // Map producer to the FrameSource concept used for collision checks
    let mappedIncomingSource: FrameSource = "simulated";
    if (ctx.producer === "physical_rx") mappedIncomingSource = "physical";
    if (ctx.producer === "user") mappedIncomingSource = "emulated"; // Usually user is emulated

    // Collision check logic for sim_input claiming
    let ownsSource = false;
    if (!existingSource) {
      this.sources.set(k, mappedIncomingSource);
      ownsSource = true;
    } else if (existingSource === mappedIncomingSource) {
      ownsSource = true;
    } else {
      this.emitCollision(k, existingSource, mappedIncomingSource, frame);
    }

    const disp: RoutingDisposition = {
      accepted: false,
      ui: false,
      recording: false,
      sim_input: false,
      physical_tx: false,
    };

    // Assign sequence
    const seq = ++this.sequenceCounter;
    const seqFrame = { ...frame, seq };
    disp.frame = seqFrame;
    disp.accepted = true;

    // Apply Architecture Routing Matrix
    switch (ctx.producer) {
      case "physical_rx":
        disp.ui = true;
        disp.recording = true;
        disp.sim_input = ownsSource; // profile opt-in for sensor input only
        disp.physical_tx = false; // never echo
        break;

      case "simulation":
        disp.ui = true;
        disp.recording = true;
        disp.sim_input = ownsSource; // internal model routing
        disp.physical_tx = false; // never
        break;

      case "replay":
        disp.ui = true;
        disp.recording = false; // derived recording is explicit only, not default
        disp.sim_input = false; // never
        disp.physical_tx = false; // never
        break;

      case "user":
        disp.ui = true;
        disp.recording = true;
        disp.sim_input = ownsSource; 
        // Note: physical_tx true means it *can* go to physical TX, but InjectionService 
        // must first approve lease and arm. We allow it here in the router.
        disp.physical_tx = true; 
        break;

      case "test":
        disp.ui = false; // test UI only
        disp.recording = false;
        disp.sim_input = true; // isolated test engine only
        disp.physical_tx = false; // never in production
        break;
    }

    return disp;
  }

  onCollision(listener: (event: FrameRouterEvent) => void): void {
    this.listeners.push(listener);
  }

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
