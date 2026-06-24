import type { FastifyInstance } from "fastify";
import type { DebugStore } from "../db/queries";
import type { SerialBridge } from "../serial/reader";
import type { StreamHub } from "../ws/stream";

export function registerSystemRoutes(
  app: FastifyInstance,
  store: DebugStore,
  serial: SerialBridge,
  hub: StreamHub,
  startedAt: number
): void {
  app.get("/api/status", async () => ({
    backend_online: true,
    started_at: startedAt,
    uptime_s: Math.round(Date.now() / 1000 - startedAt),
    esp32_connected: serial.state.esp32_connected,
    last_status_at: serial.state.last_status_at,
    serial: {
      port_open: serial.state.port_open,
      path: serial.state.path,
      baud_rate: serial.state.baud_rate,
      last_error: serial.state.last_error
    },
    bus_stats: store.getStats().buses,
    websocket_clients: hub.clientCount(),
    storage: store.counts()
  }));
}
