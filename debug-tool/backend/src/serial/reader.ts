import { ReadlineParser, SerialPort } from "serialport";
import type { HardwareBridge, BridgeState } from "../bridge/types";
import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import { BusDetector, normalizeFrame, normalizeStats, type Bus, type CanStats } from "../types/can";
import type { CanFrame } from "../types/can";
import type { StreamHub } from "../ws/stream";

export interface SerialState extends BridgeState {
  esp32_connected: boolean;
  port_open: boolean;
  last_frame_at: number | null;
  degraded: boolean;
}

export class SerialBridge implements HardwareBridge {
  readonly state: SerialState;
  private port: SerialPort | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private busDetector = new BusDetector();
  private detectedBus: Bus = "high";
  private lastBusDetectionConfidence: "none" | "low" | "high" = "none";
  private frameCallbacks: Array<(frame: CanFrame) => void> = [];
  private opening = false;

  constructor(
    private readonly config: AppConfig,
    private readonly store: DebugStore,
    private readonly hub: StreamHub
  ) {
    this.state = {
      transport: "serial",
      adapter: "ESP32 serial bridge",
      connected: false,
      link_open: false,
      esp32_connected: false,
      port_open: false,
      path: config.serialPath,
      baud_rate: config.serialBaudRate,
      bitrate: null,
      last_status_at: null,
      last_error: null,
      last_frame_at: null,
      degraded: false
    };
  }

  onFrame(callback: (frame: CanFrame) => void): void {
    this.frameCallbacks.push(callback);
  }

  start(): void {
    if (this.opening || this.state.port_open) return;

    if (!this.config.serialPath) {
      this.state.last_error = "serial disabled";
      console.warn("[serial] serial disabled: no serial path configured");
      this.broadcastStatus();
      return;
    }

    this.port = new SerialPort({ path: this.config.serialPath, baudRate: this.config.serialBaudRate, autoOpen: false });
    const parser = this.port.pipe(new ReadlineParser({ delimiter: "\n" }));
    parser.on("data", (line: string) => this.handleLine(line));

    this.port.on("open", () => {
      this.opening = false;
      this.reconnectAttempt = 0;
      this.state.connected = true;
      this.state.link_open = true;
      this.state.port_open = true;
      this.state.esp32_connected = true;
      this.state.last_error = null;
      this.busDetector.reset();
      this.detectedBus = "high";
      this.lastBusDetectionConfidence = "none";
      this.broadcastStatus();
    });
    this.port.on("close", () => {
      this.opening = false;
      this.state.connected = false;
      this.state.link_open = false;
      this.state.port_open = false;
      this.state.esp32_connected = false;
      this.broadcastStatus();
      this.scheduleReconnect();
    });
    this.port.on("error", (error) => {
      this.opening = false;
      this.state.last_error = error.message;
      console.error(`[serial] ${error.message}`);
      this.state.connected = false;
      this.state.link_open = false;
      this.state.port_open = false;
      this.state.esp32_connected = false;
      this.broadcastStatus();
      this.scheduleReconnect();
    });
    this.opening = true;
    this.port.open((error) => {
      if (error) {
        this.opening = false;
        this.state.last_error = error.message;
        console.error(`[serial] ${error.message}`);
        this.broadcastStatus();
        this.scheduleReconnect();
      }
    });
  }

  sendCommand(command: Record<string, unknown>): void {
    if (!this.port || !this.port.isOpen || !this.port.writable) {
      throw new Error(this.state.last_error ?? "serial port is not open");
    }
    this.port.write(`${JSON.stringify(command)}\n`);
  }

  async close(): Promise<void> {
    if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
    if (!this.port || !this.port.isOpen) return;
    await new Promise<void>((resolve) => this.port?.close(() => resolve()));
  }

  private reconnectAttempt = 0;
  private static readonly MAX_RECONNECT_ATTEMPTS = 10;
  private static readonly RECONNECT_BASE_MS = 1000;
  private static readonly RECONNECT_MAX_MS = 30000;

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return; // already pending
    if (this.reconnectAttempt >= SerialBridge.MAX_RECONNECT_ATTEMPTS) {
      // Cap fast backoff, switch to slow polling every 30s
      this.state.last_error = "reconnection attempts exhausted, polling every 30s";
      this.broadcastStatus();
      this.reconnectTimer = setTimeout(() => {
        this.reconnectTimer = null;
        this.reconnectAttempt = 0; // reset for fresh backoff if it comes back
        if (!this.state.connected && this.config.serialPath) {
          this.start();
        }
      }, 30000);
      return;
    }
    const delay = Math.min(
      SerialBridge.RECONNECT_BASE_MS * Math.pow(2, this.reconnectAttempt),
      SerialBridge.RECONNECT_MAX_MS
    );
    this.reconnectAttempt++;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.state.connected && this.config.serialPath) {
        this.start();
      }
    }, delay);
  }

  private handleLine(line: string): void {
    const trimmed = line.trim();
    if (!trimmed) return;

    let message: Record<string, unknown>;
    try {
      message = JSON.parse(trimmed) as Record<string, unknown>;
    } catch (error) {
      this.hub.broadcast({ type: "status", payload: { warning: "invalid serial JSON line", error: String(error) } });
      return;
    }

    if (message.type === "stats") {
      const stats: CanStats = normalizeStats(message);
      this.store.setStats(stats);
      this.hub.broadcast({ type: "stats", payload: stats });
      return;
    }

    if (message.type === "status") {
      this.state.esp32_connected = message.esp32_connected !== false;
      this.state.connected = this.state.esp32_connected;
      this.state.last_status_at = Date.now() / 1000;
      this.state.last_error = typeof message.error === "string" ? message.error : null;
      this.broadcastStatus();
      return;
    }

    if (message.type === "cmd_ack") {
      const status = typeof message.status === "string" ? message.status : "unknown";
      const correlationId = typeof message.correlation_id === "string" ? message.correlation_id : null;
      if (correlationId) this.store.updateInjectionByCorrelation(correlationId, status);
      else this.store.updateLatestInjectionStatus(status);
      this.hub.broadcast({ type: "cmd_ack", payload: message });
      return;
    }

    if (typeof message.id === "string" && Array.isArray(message.data)) {
      const prevDetected = this.detectedBus;
      const prevConfidence = this.lastBusDetectionConfidence;
      const explicitBus = message.bus === "low" || message.bus === "high" ? message.bus : null;
      this.detectedBus = explicitBus ?? this.busDetector.feed(message.id);
      const currentConfidence = this.busDetector.state.confidence;

      const frame = normalizeFrame({
        ts: typeof message.ts === "number" ? message.ts : undefined,
        bus: this.detectedBus,
        id: message.id,
        name: typeof message.name === "string" ? message.name : undefined,
        dlc: typeof message.dlc === "number" ? message.dlc : undefined,
        data: message.data as number[],
        decoded: typeof message.decoded === "object" && message.decoded ? (message.decoded as Record<string, unknown>) : undefined
      });
      this.state.last_frame_at = Date.now() / 1000;
      this.state.degraded = false;
      this.store.insertFrame(frame);
      this.hub.broadcast({ type: "can_frame", payload: frame });
      for (const callback of this.frameCallbacks) callback(frame);

      if (this.detectedBus !== prevDetected || (prevConfidence !== "high" && currentConfidence === "high")) {
        this.broadcastStatus();
      }
      this.lastBusDetectionConfidence = currentConfidence;
      return;
    }

    this.hub.broadcast({ type: "status", payload: { warning: "unhandled serial message", message } });
  }

  private broadcastStatus(): void {
    this.hub.broadcast({
      type: "status",
      payload: {
        bridge: this.state,
        ...this.state,
        adapter_connected: this.state.connected,
        esp32_connected: this.state.connected,
        bus_detection: this.busDetector.state
      }
    });
  }
}
