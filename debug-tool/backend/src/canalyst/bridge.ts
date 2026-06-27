import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { existsSync } from "node:fs";
import { createInterface } from "node:readline";
import { resolve } from "node:path";
import type { HardwareBridge, BridgeState } from "../bridge/types";
import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import { normalizeFrame, normalizeStats, BusDetector, type CanStats } from "../types/can";
import type { StreamHub } from "../ws/stream";

export class CanalystBridge implements HardwareBridge {
  readonly state: BridgeState;
  private process: ChildProcessWithoutNullStreams | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private busDetector = new BusDetector();

  constructor(
    private readonly config: AppConfig,
    private readonly store: DebugStore,
    private readonly hub: StreamHub
  ) {
    this.state = {
      transport: "canalystii",
      adapter: "CANalyst-II",
      connected: false,
      link_open: false,
      path: null,
      baud_rate: null,
      bitrate: config.canalystBitrate,
      last_status_at: null,
      last_error: null
    };
  }

  start(): void {
    const scriptPath = resolve(__dirname, "../../canalystii_bridge.py");
    if (!existsSync(scriptPath)) {
      this.state.last_error = `CANalyst-II bridge script not found: ${scriptPath}`;
      this.broadcastStatus();
      return;
    }

    this.process = spawn(this.config.canalystPython, [scriptPath], {
      cwd: process.cwd(),
      env: {
        ...process.env,
        CANALYST_BITRATE: String(this.config.canalystBitrate),
        CANALYST_POLL_MS: String(this.config.canalystPollMs),
        CANALYST_DEVICE_INDEX: String(this.config.canalystDeviceIndex),
        CANALYST_CH0_BUS: this.config.canalystChannel0Bus,
        CANALYST_CH1_BUS: this.config.canalystChannel1Bus
      },
      stdio: ["pipe", "pipe", "pipe"]
    });

    this.state.link_open = true;
    this.state.last_error = null;
    this.broadcastStatus();

    createInterface({ input: this.process.stdout }).on("line", (line) => this.handleLine(line));
    createInterface({ input: this.process.stderr }).on("line", (line) => {
      this.state.last_error = line;
      this.hub.broadcast({ type: "status", payload: { bridge: this.state, warning: "canalystii stderr", error: line } });
    });

    this.process.on("error", (error) => {
      this.state.connected = false;
      this.state.link_open = false;
      this.state.last_error = error.message;
      this.broadcastStatus();
    });
    this.process.on("exit", (code, signal) => {
      this.state.connected = false;
      this.state.link_open = false;
      this.state.last_error = code === 0 ? null : `CANalyst-II bridge exited code=${code ?? "null"} signal=${signal ?? "null"}`;
      this.broadcastStatus();
      if (code !== 0) this.scheduleReconnect(); // respawn on crash
    });
  }

  sendCommand(command: Record<string, unknown>): void {
    if (!this.process || !this.process.stdin.writable || !this.state.link_open) {
      throw new Error(this.state.last_error ?? "CANalyst-II bridge is not open");
    }
    this.process.stdin.write(`${JSON.stringify(command)}\n`);
  }

  async close(): Promise<void> {
    if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
    if (!this.process) return;
    const child = this.process;
    this.process = null;
    await new Promise<void>((resolveClose) => {
      child.once("exit", () => resolveClose());
      child.kill();
      setTimeout(resolveClose, 1000).unref();
    });
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.state.connected) this.start();
    }, 2000);
  }

  private handleLine(line: string): void {
    const trimmed = line.trim();
    if (!trimmed) return;

    let message: Record<string, unknown>;
    try {
      message = JSON.parse(trimmed) as Record<string, unknown>;
    } catch (error) {
      this.hub.broadcast({ type: "status", payload: { warning: "invalid CANalyst-II JSON line", error: String(error) } });
      return;
    }

    if (message.type === "stats") {
      const stats: CanStats = normalizeStats(message);
      this.store.setStats(stats);
      this.hub.broadcast({ type: "stats", payload: stats });
      return;
    }

    if (message.type === "status") {
      const connected = message.adapter_connected !== false && message.online !== false;
      this.state.connected = connected;
      this.state.link_open = connected;
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

    if ((message.bus === "high" || message.bus === "low") && typeof message.id === "string" && Array.isArray(message.data)) {
      const frame = normalizeFrame({
        ts: typeof message.ts === "number" ? message.ts : undefined,
        bus: message.bus,
        id: message.id,
        name: typeof message.name === "string" ? message.name : undefined,
        dlc: typeof message.dlc === "number" ? message.dlc : undefined,
        data: message.data as number[],
        decoded: typeof message.decoded === "object" && message.decoded ? (message.decoded as Record<string, unknown>) : undefined
      });
      this.store.insertFrame(frame);
      this.busDetector.feed(frame.id);
      this.hub.broadcast({ type: "can_frame", payload: frame });
      return;
    }

    this.hub.broadcast({ type: "status", payload: { warning: "unhandled CANalyst-II message", message } });
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
          last_error: this.state.last_error
        }
      }
    });
  }
}
