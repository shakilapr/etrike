import type { FastifyInstance } from "fastify";
import type { HardwareBridge } from "../bridge/types";
import type { DebugStore } from "../db/queries";
import type { StreamHub } from "../ws/stream";

export function registerSystemRoutes(
  app: FastifyInstance,
  store: DebugStore,
  bridge: HardwareBridge,
  hub: StreamHub,
  startedAt: number
): void {
  app.get("/api/status", async () => ({
    backend_online: true,
    started_at: startedAt,
    uptime_s: Math.round(Date.now() / 1000 - startedAt),
    adapter_connected: bridge.state.connected,
    esp32_connected: bridge.state.connected,
    last_status_at: bridge.state.last_status_at,
    bridge: bridge.state,
    serial: {
      port_open: bridge.state.link_open,
      path: bridge.state.path,
      baud_rate: bridge.state.baud_rate ?? 0,
      last_error: bridge.state.last_error
    },
    bus_stats: store.getStats().buses,
    websocket_clients: hub.clientCount(),
    storage: store.counts()
  }));
}
