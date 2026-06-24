import { ReadlineParser, SerialPort } from "serialport";
import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import { BusDetector, normalizeFrame, normalizeStats, type Bus, type CanStats } from "../types/can";
import type { StreamHub } from "../ws/stream";

export interface SerialState {
  esp32_connected: boolean;
  port_open: boolean;
  path: string | null;
  baud_rate: number;
  last_status_at: number | null;
  last_error: string | null;
}

export class SerialBridge {
  readonly state: SerialState;
  private port: SerialPort | null = null;
  private busDetector = new BusDetector();
  private detectedBus: Bus = "high";

  constructor(
    private readonly config: AppConfig,
    private readonly store: DebugStore,
    private readonly hub: StreamHub
  ) {
    this.state = {
      esp32_connected: false,
      port_open: false,
      path: config.serialPath,
      baud_rate: config.serialBaudRate,
      last_status_at: null,
      last_error: null
    };
  }

  start(): void {
    if (!this.config.serialPath) {
      this.state.last_error = "serial disabled";
      this.broadcastStatus();
      return;
    }

    this.port = new SerialPort({ path: this.config.serialPath, baudRate: this.config.serialBaudRate, autoOpen: false });
    const parser = this.port.pipe(new ReadlineParser({ delimiter: "\n" }));
    parser.on("data", (line: string) => this.handleLine(line));

    this.port.on("open", () => {
      this.state.port_open = true;
      this.state.esp32_connected = true;
      this.state.last_error = null;
      this.busDetector.reset();
      this.detectedBus = "high";
      this.broadcastStatus();
    });
    this.port.on("close", () => {
      this.state.port_open = false;
      this.state.esp32_connected = false;
      this.broadcastStatus();
    });
    this.port.on("error", (error) => {
      this.state.last_error = error.message;
      this.state.port_open = false;
      this.state.esp32_connected = false;
      this.broadcastStatus();
    });
    this.port.open((error) => {
      if (error) {
        this.state.last_error = error.message;
        this.broadcastStatus();
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
    if (!this.port || !this.port.isOpen) return;
    await new Promise<void>((resolve) => this.port?.close(() => resolve()));
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
      this.state.last_status_at = Date.now() / 1000;
      this.state.last_error = typeof message.error === "string" ? message.error : null;
      this.broadcastStatus();
      return;
    }

    if (message.type === "cmd_ack") {
      const status = typeof message.status === "string" ? message.status : "unknown";
      this.store.updateLatestInjectionStatus(status);
      this.hub.broadcast({ type: "cmd_ack", payload: message });
      return;
    }

    if (typeof message.id === "string" && Array.isArray(message.data)) {
      // Auto-detect which CAN bus this controller is connected to
      const prevDetected = this.detectedBus;
      this.detectedBus = this.busDetector.feed(message.id);

      const frame = normalizeFrame({
        ts: typeof message.ts === "number" ? message.ts : undefined,
        bus: this.detectedBus,
        id: message.id,
        name: typeof message.name === "string" ? message.name : undefined,
        dlc: typeof message.dlc === "number" ? message.dlc : undefined,
        data: message.data as number[],
        decoded: typeof message.decoded === "object" && message.decoded ? (message.decoded as Record<string, unknown>) : undefined
      });
      this.store.insertFrame(frame);
      this.hub.broadcast({ type: "can_frame", payload: frame });

      // Notify when bus detection locks in
      if (this.detectedBus !== prevDetected || this.busDetector.state.confidence === "high") {
        this.broadcastStatus();
      }
      return;
    }

    this.hub.broadcast({ type: "status", payload: { warning: "unhandled serial message", message } });
  }

  private broadcastStatus(): void {
    this.hub.broadcast({
      type: "status",
      payload: {
        ...this.state,
        bus_detection: this.busDetector.state
      }
    });
  }
}
