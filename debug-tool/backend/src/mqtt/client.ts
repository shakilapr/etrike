import mqtt, { type MqttClient } from "mqtt";
import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import { normalizeCanId, normalizeFrame, type CanStats } from "../types/can";
import type { StreamHub } from "../ws/stream";

export interface DeviceState {
  debug_esp32_online: boolean;
  last_status_at: number | null;
  mqtt_connected: boolean;
  uptime_s: number | null;
}

export class MqttBridge {
  readonly state: DeviceState = {
    debug_esp32_online: false,
    last_status_at: null,
    mqtt_connected: false,
    uptime_s: null
  };

  private client: MqttClient | null = null;

  constructor(
    private readonly config: AppConfig,
    private readonly store: DebugStore,
    private readonly hub: StreamHub
  ) {}

  start(): void {
    this.client = mqtt.connect(this.config.mqttUrl, {
      clientId: `etrike-debug-backend-${process.pid}`,
      reconnectPeriod: 1000
    });

    this.client.on("connect", () => {
      this.state.mqtt_connected = true;
      this.client?.subscribe([
        "etrike/debug/can/rx/#",
        "etrike/debug/can/stats",
        "etrike/debug/status",
        "etrike/debug/uptime",
        "etrike/debug/cmd/response"
      ]);
      this.broadcastStatus();
    });

    this.client.on("offline", () => {
      this.state.mqtt_connected = false;
      this.broadcastStatus();
    });

    this.client.on("close", () => {
      this.state.mqtt_connected = false;
      this.broadcastStatus();
    });

    this.client.on("message", (topic, payload) => this.handleMessage(topic, payload));
  }

  publishJson(topic: string, payload: Record<string, unknown>): void {
    if (!this.client?.connected) {
      throw new Error("MQTT bridge is not connected");
    }
    this.client.publish(topic, JSON.stringify(payload), { qos: 0 });
  }

  async close(): Promise<void> {
    if (!this.client) return;
    await new Promise<void>((resolve) => this.client?.end(false, {}, () => resolve()));
  }

  private handleMessage(topic: string, payload: Buffer): void {
    if (topic.startsWith("etrike/debug/can/rx/")) {
      this.handleCanFrame(topic, payload);
      return;
    }

    if (topic === "etrike/debug/can/stats") {
      this.handleStats(payload);
      return;
    }

    if (topic === "etrike/debug/status") {
      const status = payload.toString("utf8").trim().toLowerCase();
      this.state.debug_esp32_online = status === "online";
      this.state.last_status_at = Date.now() / 1000;
      this.broadcastStatus();
      return;
    }

    if (topic === "etrike/debug/uptime") {
      const uptime = Number(payload.toString("utf8"));
      this.state.uptime_s = Number.isFinite(uptime) ? uptime : null;
      this.broadcastStatus();
      return;
    }

    if (topic === "etrike/debug/cmd/response") {
      this.handleCommandResponse(payload);
    }
  }

  private handleCanFrame(topic: string, payload: Buffer): void {
    try {
      const parsed = JSON.parse(payload.toString("utf8")) as {
        ts?: number;
        id?: string;
        name?: string;
        dlc?: number;
        data?: number[];
        decoded?: Record<string, unknown>;
      };
      const id = normalizeCanId(parsed.id ?? topic.split("/").at(-1) ?? "");
      const frame = normalizeFrame({
        ts: parsed.ts,
        id,
        name: parsed.name,
        dlc: parsed.dlc,
        data: parsed.data ?? [],
        decoded: parsed.decoded
      });
      this.store.insertFrame(frame);
      this.hub.broadcast({ type: "can_frame", payload: frame });
    } catch (error) {
      this.hub.broadcast({
        type: "status",
        payload: { warning: "invalid CAN MQTT payload", error: String(error) }
      });
    }
  }

  private handleStats(payload: Buffer): void {
    try {
      const stats = JSON.parse(payload.toString("utf8")) as CanStats;
      this.store.setStats(stats);
      this.hub.broadcast({ type: "stats", payload: stats });
    } catch (error) {
      this.hub.broadcast({
        type: "status",
        payload: { warning: "invalid stats MQTT payload", error: String(error) }
      });
    }
  }

  private handleCommandResponse(payload: Buffer): void {
    try {
      const response = JSON.parse(payload.toString("utf8")) as Record<string, unknown>;
      const requestId = typeof response.request_id === "string" ? response.request_id : "";
      if (requestId) {
        this.store.updateInjectionResponse(requestId, response);
      }
      this.hub.broadcast({ type: "cmd_ack", payload: response });
    } catch (error) {
      this.hub.broadcast({
        type: "cmd_ack",
        payload: { status: "error", error: String(error) }
      });
    }
  }

  private broadcastStatus(): void {
    this.hub.broadcast({ type: "status", payload: this.state });
  }
}
