import type { FastifyInstance } from "fastify";
import type { Bus, CanFrame, CanStats } from "../types/can";

export type StreamEvent =
  | { type: "can_frame"; payload: CanFrame }
  | { type: "stats"; payload: CanStats }
  | { type: "cmd_ack"; payload: object }
  | { type: "status"; payload: object };

interface ClientState {
  socket: {
    readyState: number;
    send: (data: string) => void;
    on: (event: string, cb: (payload?: unknown) => void) => void;
  };
  buses: Set<Bus> | null;
  ids: Set<string> | null;
}

const OPEN = 1;

export class StreamHub {
  private clients = new Set<ClientState>();

  registerRoutes(app: FastifyInstance): void {
    app.get("/ws", { websocket: true }, (socket) => {
      const client: ClientState = { socket: socket as ClientState["socket"], buses: null, ids: null };
      this.clients.add(client);

      client.socket.on("message", (payload) => {
        this.handleClientMessage(client, payload);
      });

      client.socket.on("close", () => {
        this.clients.delete(client);
      });

      this.send(client, { type: "status", payload: { connected: true } });
    });
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
