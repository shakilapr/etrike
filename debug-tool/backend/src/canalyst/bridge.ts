import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { existsSync } from "node:fs";
import { createInterface } from "node:readline";
import { resolve } from "node:path";
import type { HardwareBridge, BridgeState } from "../bridge/types";
import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import { normalizeFrame, normalizeStats, BusDetector, type CanFrame, type CanStats } from "../types/can";
import type { StreamHub } from "../ws/stream";
import type { WriteQueue } from "../db/write-queue";

export class CanalystBridge implements HardwareBridge {
  readonly state: BridgeState;
  private process: ChildProcessWithoutNullStreams | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private busDetector = new BusDetector();
  private frameCallbacks: Array<(frame: CanFrame) => void> = [];
  private reconnectSuppressed = false;

  constructor(
    private readonly config: AppConfig,
    private readonly store: DebugStore,
    private readonly hub: StreamHub,
    private readonly writeQueue: WriteQueue
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
      last_error: null,
      last_frame_at: null,
      degraded: false
    };
  }

  onFrame(callback: (frame: CanFrame) => void): void {
    this.frameCallbacks.push(callback);
  }

  start(): void {
    this.reconnectSuppressed = false;
    if (this.process || this.state.link_open) return;

    const scriptPath = resolve(__dirname, "../../canalystii_bridge.py");
    if (!existsSync(scriptPath)) {
      this.state.last_error = `CANalyst-II bridge script not found: ${scriptPath}`;
      this.broadcastStatus();
      return;
    }

    this.spawnProcess(scriptPath);
  }

  /**
   * Returns a promise that resolves to true when the bridge reports
   * adapter_connected, or false if the process exits with an error
   * before connecting (e.g. device not found).  Times out after `timeoutMs`.
   */
  waitForConnection(timeoutMs = 3000): Promise<boolean> {
    return new Promise((resolve) => {
      const done = (result: boolean) => {
        clearTimeout(timer);
        resolve(result);
      };
      const timer = setTimeout(() => {
        // Timed out — bridge is still running but hasn't reported status.
        // Treat as connected since the process is alive (device may be slow).
        resolve(this.state.connected);
      }, timeoutMs);

      const check = () => {
        if (this.state.connected) { done(true); return; }
        // If the process died without connecting, it's a hard failure
        if (this.process == null && !this.state.connected) { done(false); return; }
      };

      // Poll every 200ms until connected or process exits
      const interval = setInterval(() => {
        check();
        if (this.process == null || this.state.connected) {
          clearInterval(interval);
        }
      }, 200);
    });
  }

  /** Closes the bridge and abandons reconnect — used when falling back to another transport. */
  async abandon(): Promise<void> {
    if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
    this.reconnectSuppressed = true;
    this.reconnectAttempt = 0;
    await this.close();
  }

  private spawnProcess(scriptPath: string): void {
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
    const child = this.process;

    this.state.link_open = true;
    this.state.last_error = null;
    this.reconnectAttempt = 0;
    this.broadcastStatus();

    createInterface({ input: child.stdout }).on("line", (line) => this.handleLine(line));
    createInterface({ input: child.stderr }).on("line", (line) => {
      const trimmed = line.trim();
      if (!trimmed) return;
      if (trimmed.startsWith("[INFO]") || trimmed.startsWith("[WARN]") || trimmed.startsWith("DEBUG:")) return;
      this.state.last_error = trimmed;
      this.hub.broadcast({ type: "status", payload: { bridge: { ...this.state }, warning: "canalystii stderr", error: trimmed } });
    });

    child.on("error", (error) => {
      this.state.connected = false;
      this.state.link_open = false;
      this.state.last_error = error.message;
      this.broadcastStatus();
    });
    child.on("exit", (code, signal) => {
      if (this.process === child) this.process = null;
      this.state.connected = false;
      this.state.link_open = false;
      this.state.last_error = code === 0 ? null : `CANalyst-II bridge exited code=${code ?? "null"} signal=${signal ?? "null"}`;
      this.broadcastStatus();
      if (code !== 0 && !this.reconnectSuppressed) this.scheduleReconnect();
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

  private reconnectAttempt = 0;
  private static readonly MAX_RECONNECT_ATTEMPTS = 10;
  private static readonly RECONNECT_BASE_MS = 1000;
  private static readonly RECONNECT_MAX_MS = 30000;

  private scheduleReconnect(): void {
    if (this.reconnectTimer || this.reconnectAttempt >= CanalystBridge.MAX_RECONNECT_ATTEMPTS) {
      if (this.reconnectAttempt >= CanalystBridge.MAX_RECONNECT_ATTEMPTS) {
        this.state.last_error = "max reconnection attempts reached";
        this.broadcastStatus();
      }
      return;
    }
    const delay = Math.min(
      CanalystBridge.RECONNECT_BASE_MS * Math.pow(2, this.reconnectAttempt),
      CanalystBridge.RECONNECT_MAX_MS
    );
    this.reconnectAttempt++;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.state.connected) this.start();
    }, delay);
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
      this.state.last_frame_at = Date.now() / 1000;
      this.state.degraded = false;
      this.writeQueue.enqueue(frame);
      this.busDetector.feed(frame.id);
      this.hub.broadcast({ type: "can_frame", payload: frame });
      for (const callback of this.frameCallbacks) callback(frame);
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
