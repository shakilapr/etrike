import type { FastifyInstance } from "fastify";
import type { DebugStore } from "../db/queries";
import type { MqttBridge } from "../mqtt/client";
import type { StreamHub } from "../ws/stream";

export function registerSystemRoutes(
  app: FastifyInstance,
  store: DebugStore,
  bridge: MqttBridge,
  hub: StreamHub,
  startedAt: number
): void {
  app.get("/api/status", async () => ({
    backend_online: true,
    started_at: startedAt,
    uptime_s: Math.round(Date.now() / 1000 - startedAt),
    debug_esp32_online: bridge.state.debug_esp32_online,
    debug_esp32_uptime_s: bridge.state.uptime_s,
    last_status_at: bridge.state.last_status_at,
    mqtt_connected: bridge.state.mqtt_connected,
    websocket_clients: hub.clientCount(),
    storage: store.counts()
  }));
}
