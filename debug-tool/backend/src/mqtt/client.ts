import mqtt, { type MqttClient } from "mqtt";
import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import { normalizeFrame, normalizeStats, type CanFrame, type CanStats } from "../types/can";
import type { StreamHub } from "../ws/stream";

export interface MqttBridgeState {
  mqtt_connected: boolean;
  debug_esp32_online: boolean;
  uptime_s: number | null;
  last_status_at: number | null;
}

export class MqttBridge {
  public readonly state: MqttBridgeState = {
    mqtt_connected: false,
    debug_esp32_online: false,
    uptime_s: null,
    last_status_at: null
  };

  private client: MqttClient | null = null;

  constructor(
    private readonly config: AppConfig,
    private readonly store: DebugStore,
    private readonly hub: StreamHub
  ) {}

  start(): void {
    const brokerUrl = this.config.mqttUrl ?? `mqtt://${this.config.mqttHost}:${this.config.mqttPort}`;

    this.client = mqtt.connect(brokerUrl, {
      clientId: `etrike-debug-backend-${Date.now()}`,
      clean: true,
      reconnectPeriod: 3000,
      connectTimeout: 5000
    });

    this.client.on("connect", () => {
      this.state.mqtt_connected = true;

      this.client!.subscribe(
        [
          "etrike/debug/can/rx/#",
          "etrike/debug/can/stats",
          "etrike/debug/status",
          "etrike/debug/uptime",
          "etrike/debug/cmd/response"
        ],
        { qos: 1 },
        (err) => {
          if (err) {
            console.error("MQTT subscribe error:", err);
          }
        }
      );
    });

    this.client.on("close", () => {
      this.state.mqtt_connected = false;
    });

    this.client.on("error", (err) => {
      console.error("MQTT client error:", err);
    });

    this.client.on("message", (topic, payload) => {
      this.handleMessage(topic, payload);
    });
  }

  async close(): Promise<void> {
    return new Promise<void>((resolve) => {
      if (!this.client) {
        resolve();
        return;
      }

      this.client.end(false, {}, () => {
        this.state.mqtt_connected = false;
        resolve();
      });
    });
  }

  publishJson(topic: string, payload: unknown): void {
    if (!this.client || !this.state.mqtt_connected) {
      console.warn(`MQTT not connected; dropping publish to ${topic}`);
      return;
    }

    this.client.publish(topic, JSON.stringify(payload), { qos: 1 });
  }

  private handleMessage(topic: string, payload: Buffer): void {
    let parsed: unknown;
    try {
      parsed = JSON.parse(payload.toString("utf8"));
    } catch {
      return;
    }

    switch (topic) {
      case "etrike/debug/can/stats": {
        const stats = normalizeStats(parsed as Partial<CanStats> & Record<string, unknown>);
        this.store.setStats(stats);
        this.hub.broadcast({ type: "stats", payload: stats });
        break;
      }

      case "etrike/debug/status": {
        const data = parsed as Record<string, unknown>;
        this.state.debug_esp32_online = Boolean(data.online);
        this.state.last_status_at = Date.now() / 1000;
        this.hub.broadcast({ type: "status", payload: data });
        break;
      }

      case "etrike/debug/uptime": {
        const data = parsed as { uptime_s?: number };
        if (typeof data.uptime_s === "number") {
          this.state.uptime_s = data.uptime_s;
        }
        break;
      }

      case "etrike/debug/cmd/response": {
        const data = parsed as Record<string, unknown>;
        if (typeof data.request_id === "string") {
          this.store.updateInjectionResponse(data.request_id, data);
        }
        this.hub.broadcast({ type: "cmd_ack", payload: data });
        break;
      }

      default: {
        // etrike/debug/can/rx/<bus>/<id> or similar
        if (topic.startsWith("etrike/debug/can/rx/")) {
          const frame = normalizeFrame(parsed as Partial<CanFrame> & { id: string; data: number[] });
          const stored = this.store.insertFrame(frame);
          this.hub.broadcast({ type: "can_frame", payload: { ...frame, row_id: stored.row_id, ts_real: stored.ts_real } });
        }
        break;
      }
    }
  }
}
