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
      bus_detection: bridge.state.bus_detection ?? { detected: false, bus: "high", confidence: "none", highHits: 0, lowHits: 0 },
      bus_stats: (await store.getStats()).buses,
      stats_updated_at: await store.getStatsUpdatedAt(),
      websocket_clients: hub.clientCount(),
      storage: await store.counts(),
      queues: {
        ui: hub.getMetrics(),
        db: app.ctx.writeQueue.getMetrics()
      }
  }));

  app.post("/api/system/stop", async () => {
    try {
      await app.ctx.stateMachine.disarm();
      await bridge.close();
      return { ok: true, action: "bridge closed" };
    } catch (error) {
      return { ok: false, error: error instanceof Error ? error.message : String(error) };
    }
  });

  app.post("/api/system/restart", async () => {
    try {
      await app.ctx.stateMachine.disarm();
      await bridge.close();
      await bridge.start();
      return { ok: true, action: "bridge restarted" };
    } catch (error) {
      return { ok: false, error: error instanceof Error ? error.message : String(error) };
    }
  });

  app.post("/api/system/shutdown", async () => {
    await app.ctx.stateMachine.disarm();
    if (shutdown) {
      setTimeout(() => { void shutdown(); }, 200);
    }
    return { ok: true, action: "shutting down" };
  });
}
