import type { FastifyInstance } from "fastify";
import { CAN_MESSAGES } from "../types/can";
import type { Bus, CanFrame, CanStats } from "../types/can";

export type StreamEvent =
  | { type: "can_frame"; payload: CanFrame }
  | { type: "stats"; payload: CanStats }
  | { type: "cmd_ack"; payload: object }
  | { type: "status"; payload: object }
  | { type: "can_ids"; payload: { messages: Array<{ bus: string; id: string; name: string }> } };

interface ClientState {
  socket: {
    readyState: number;
    send: (data: string) => void;
    ping: () => void;
    close?: (code?: number, reason?: string) => void;
    on: (event: string, cb: (payload?: unknown) => void) => void;
    terminate?: () => void;
  };
  buses: Set<Bus> | null;
  ids: Set<string> | null;
  lastPong: number;
}

const OPEN = 1;

export class StreamHub {
  private clients = new Set<ClientState>();
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private staleTimer: ReturnType<typeof setInterval> | null = null;

  registerRoutes(app: FastifyInstance): void {
    app.get("/ws", { websocket: true }, (socket) => {
      if (this.clients.size >= 100) {
        socket.close(1013, "Too many connections");
        return;
      }
      const client: ClientState = { socket: socket as ClientState["socket"], buses: null, ids: null, lastPong: Date.now() };
      this.clients.add(client);

      client.socket.on("message", (payload) => {
        this.handleClientMessage(client, payload);
      });

      client.socket.on("pong", () => {
        client.lastPong = Date.now();
      });

      client.socket.on("close", () => {
        this.clients.delete(client);
      });

      this.send(client, { type: "status", payload: { connected: true } });

      // Push initial state sync: CAN message catalog so the UI can render
      // without waiting for the first frame.
      this.send(client, { type: "can_ids", payload: { messages: CAN_MESSAGES } });
    });

    // Keepalive: send ping every 30s, expect pong from clients
    this.pingTimer = setInterval(() => {
      for (const client of this.clients) {
        if (client.socket.readyState === 1) {
          client.socket.ping();
        }
      }
    }, 30000);

    // Evict clients with no pong response within 60s (zombie connection guard)
    this.staleTimer = setInterval(() => {
      const stale = Date.now() - 60000;
      for (const client of this.clients) {
        if (client.lastPong < stale) {
          client.socket.terminate?.();
          client.socket.close?.(1001, "pong timeout");
          this.clients.delete(client);
        }
      }
    }, 60000);
  }

  broadcast(event: StreamEvent): void {
    const encoded = JSON.stringify(event);

    for (const client of this.clients) {
      if (client.socket.readyState !== OPEN) {
        this.clients.delete(client);
        continue;
      }

      if (event.type === "can_frame") {
        if (client.buses && !client.buses.has(event.payload.bus)) continue;
        if (client.ids && !client.ids.has(String(event.payload.id))) continue;
      }

      client.socket.send(encoded);
    }
  }

  close(): void {
    if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
    if (this.staleTimer) { clearInterval(this.staleTimer); this.staleTimer = null; }
    for (const client of this.clients) {
      try { client.socket.close?.(); } catch { /* ignore */ }
    }
    this.clients.clear();
  }

  clientCount(): number {
    return this.clients.size;
  }

  private send(client: ClientState, event: StreamEvent): void {
    if (client.socket.readyState === OPEN) {
      client.socket.send(JSON.stringify(event));
    }
  }

  private handleClientMessage(client: ClientState, payload: unknown): void {
    try {
      const text = Buffer.isBuffer(payload) ? payload.toString("utf8") : String(payload ?? "");
      const message = JSON.parse(text) as { type?: string; buses?: string[]; ids?: string[] };

      if (message.type === "filter") {
        client.buses =
          message.buses && message.buses.length > 0
            ? new Set(message.buses.filter((bus): bus is Bus => bus === "high" || bus === "low"))
            : null;
        client.ids = message.ids && message.ids.length > 0 ? new Set(message.ids) : null;
      }
    } catch {
      this.send(client, {
        type: "status",
        payload: { warning: "invalid websocket message" }
      });
    }
  }
}
