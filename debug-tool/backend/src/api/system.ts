import type { FastifyInstance } from "fastify";
import type { HardwareBridge } from "../bridge/types";
import type { DebugStore } from "../db/queries";
import type { StreamHub } from "../ws/stream";

export function registerSystemRoutes(
  app: FastifyInstance,
  store: DebugStore,
  bridge: HardwareBridge,
  hub: StreamHub,
  startedAt: number,
  shutdown?: () => Promise<void>
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
    bus_detection: bridge.state.bus_detection ?? { highHits: 0, lowHits: 0, confidence: "none" },
    bus_stats: store.getStats().buses,
    websocket_clients: hub.clientCount(),
    storage: store.counts()
  }));

  app.post("/api/system/stop", async () => {
    if (shutdown) {
      // Graceful shutdown after a short delay to allow the response to be sent
      setTimeout(() => { void shutdown(); }, 100);
    }
    return { ok: true };
  });

  app.post("/api/system/restart", async () => {
    if (shutdown) {
      // Restart: shutdown and exit so a process manager (or user) restarts it
      setTimeout(() => { shutdown().then(() => process.exit(0)).catch(() => process.exit(1)); }, 100);
    }
    return { ok: true };
  });
}
