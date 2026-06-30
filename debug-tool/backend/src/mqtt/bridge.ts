import { createServer, type Server as NetServer } from "net";
import type { HardwareBridge, BridgeState } from "../bridge/types";
import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import { BusDetector, normalizeFrame, normalizeStats, type CanStats } from "../types/can";
import type { StreamHub } from "../ws/stream";

// aedes is ESM-only; tsx (runtime) handles ESM fine, but tsc needs help.
// We use require() at runtime via eval to keep tsc happy.
function loadAedes(): any {
  // aedes is ESM; in CJS with tsx:
  // mod.Aedes — the class with static createBroker() (new API)
  // mod.default — the legacy constructor (old API)
  // eslint-disable-next-line @typescript-eslint/no-var-requires
  const mod = require("aedes");
  return mod.Aedes ?? mod.default ?? mod;
}

export class MqttBridge implements HardwareBridge {
  readonly state: BridgeState;
  private broker: InstanceType<ReturnType<typeof loadAedes>> | null = null;
  private server: NetServer | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private reconnectAttempt = 0;
  private busDetector = new BusDetector();
  private static readonly MAX_RECONNECT_ATTEMPTS = 10;
  private static readonly RECONNECT_BASE_MS = 1000;
  private static readonly RECONNECT_MAX_MS = 30000;

  constructor(
    private readonly config: AppConfig,
    private readonly store: DebugStore,
    private readonly hub: StreamHub
  ) {
    this.state = {
      transport: "mqtt",
      adapter: "MQTT broker",
      connected: false,
      link_open: false,
      path: `mqtt://127.0.0.1:${config.mqttPort}`,
      baud_rate: null,
      bitrate: null,
      last_status_at: null,
      last_error: null,
      bus_detection: { detected: false, bus: "high", confidence: "none", highHits: 0, lowHits: 0 },
    };
  }

  async start(): Promise<void> {
    try {
      const AedesCtor = loadAedes();
      // New aedes API (v1+): Aedes.createBroker() static factory
      const aedes = typeof AedesCtor.createBroker === "function"
        ? await AedesCtor.createBroker()
        : new AedesCtor();
      this.broker = aedes;

      this.server = createServer(aedes.handle);

      this.server.listen(this.config.mqttPort, "127.0.0.1", () => {
        this.state.connected = true;
        this.state.link_open = true;
        this.state.last_error = null;
        this.reconnectAttempt = 0;
        this.broadcastStatus();
      });

      this.server.on("error", (error: Error) => {
        this.state.last_error = error.message;
        this.state.connected = false;
        this.state.link_open = false;
        this.broadcastStatus();
        this.scheduleReconnect();
      });

      // Subscribe to ESP32/simulator topics
      aedes.on("publish", (packet: any, client: any) => {
        if (!packet || !packet.topic) return;
        if (client && client.id === aedes.id) return; // skip own publishes

        try {
          const message = JSON.parse(packet.payload.toString("utf8")) as Record<string, unknown>;

          if (packet.topic.startsWith("etrike/debug/can/rx/")) {
            this.handleCanFrame(message);
          } else if (packet.topic === "etrike/debug/can/stats") {
            this.handleStats(message);
          } else if (packet.topic === "etrike/debug/status") {
            this.handleStatus(message);
          } else if (packet.topic === "etrike/debug/cmd/response") {
            this.handleCmdAck(message);
          }
        } catch {
          this.hub.broadcast({ type: "status", payload: { warning: "invalid MQTT JSON", topic: packet.topic } });
        }
      });

      aedes.on("client", () => this.broadcastStatus());
      aedes.on("clientDisconnect", () => this.broadcastStatus());
    } catch (error) {
      this.state.last_error = error instanceof Error ? error.message : String(error);
      this.broadcastStatus();
    }
  }

  sendCommand(command: Record<string, unknown>): void {
    if (!this.broker) throw new Error("MQTT broker is not running");
    const topic = "etrike/debug/cmd/send";
    const payload = Buffer.from(JSON.stringify(command), "utf8");
    (this.broker as any).publish({ topic, payload, qos: 1, retain: false });
  }

  async close(): Promise<void> {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.server) {
      await new Promise<void>((resolve) => {
        this.server!.close(() => resolve());
      });
      this.server = null;
    }
    this.broker = null;
  }

  private handleCanFrame(message: Record<string, unknown>): void {
    if (typeof message.id !== "string" || !Array.isArray(message.data)) return;

    const explicitBus = message.bus === "low" || message.bus === "high" ? message.bus : null;
    const detectedBus = explicitBus ?? this.busDetector.feed(message.id);

    const frame = normalizeFrame({
      ts: typeof message.ts === "number" ? message.ts : undefined,
      bus: detectedBus as "high" | "low",
      id: message.id,
      name: typeof message.name === "string" ? message.name : undefined,
      dlc: typeof message.dlc === "number" ? message.dlc : undefined,
      data: message.data as number[],
      decoded: typeof message.decoded === "object" && message.decoded ? (message.decoded as Record<string, unknown>) : undefined,
    });
    this.store.insertFrame(frame);
    this.hub.broadcast({ type: "can_frame", payload: frame });
  }

  private handleStats(message: Record<string, unknown>): void {
    const stats: CanStats = normalizeStats(message);
    this.store.setStats(stats);
    this.hub.broadcast({ type: "stats", payload: stats });
  }

  private handleStatus(message: Record<string, unknown>): void {
    const connected = message.adapter_connected !== false && message.online !== false;
    this.state.connected = connected;
    this.state.link_open = connected;
    this.state.last_status_at = Date.now() / 1000;
    this.state.last_error = typeof message.error === "string" ? message.error : null;
    this.broadcastStatus();
  }

  private handleCmdAck(message: Record<string, unknown>): void {
    const status = typeof message.status === "string" ? message.status : "unknown";
    const correlationId = typeof message.correlation_id === "string" ? message.correlation_id : undefined;
    if (correlationId) {
      this.store.updateInjectionByCorrelation(correlationId, status);
    } else {
      this.store.updateLatestInjectionStatus(status);
    }
    this.hub.broadcast({ type: "cmd_ack", payload: message });
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer || this.reconnectAttempt >= MqttBridge.MAX_RECONNECT_ATTEMPTS) {
      if (this.reconnectAttempt >= MqttBridge.MAX_RECONNECT_ATTEMPTS) {
        this.state.last_error = "max reconnection attempts reached";
        this.broadcastStatus();
      }
      return;
    }
    const delay = Math.min(
      MqttBridge.RECONNECT_BASE_MS * Math.pow(2, this.reconnectAttempt),
      MqttBridge.RECONNECT_MAX_MS
    );
    this.reconnectAttempt++;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.state.connected) this.start();
    }, delay);
  }

  private broadcastStatus(): void {
    this.hub.broadcast({
      type: "status",
      payload: {
        bridge: this.state,
        adapter_connected: this.state.connected,
        esp32_connected: this.state.connected,
        bus_detection: this.busDetector.state,
        serial: {
          port_open: this.state.link_open,
          path: this.state.path,
          baud_rate: this.state.baud_rate,
          last_error: this.state.last_error,
        },
      },
    });
  }
}
