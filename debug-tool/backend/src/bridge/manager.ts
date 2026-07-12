import type { AppConfig } from "../config";
import type { DebugStore } from "../db/queries";
import type { StreamHub } from "../ws/stream";
import type { WriteQueue } from "../db/write-queue";
import type { HardwareBridge, BridgeState } from "./types";
import { SerialBridge } from "../serial/reader";
import { CanalystBridge } from "../canalyst/bridge";
import type { CanFrame } from "../types/can";

export class ActiveTransportManager implements HardwareBridge {
  private activeBridge: HardwareBridge | null = null;
  private onFrameCallback?: (frame: CanFrame) => void;

  constructor(
    private readonly config: AppConfig,
    private readonly store: DebugStore,
    private readonly hub: StreamHub,
    private readonly writeQueue: WriteQueue
  ) {}

  get state(): BridgeState {
    if (this.activeBridge) return this.activeBridge.state;
    return {
      transport: "disabled",
      adapter: "none",
      connected: false,
      link_open: false,
      path: null,
      baud_rate: null,
      bitrate: null,
      last_status_at: null,
      last_error: null,
    };
  }

  onFrame(callback: (frame: CanFrame) => void): void {
    this.onFrameCallback = callback;
    if (this.activeBridge && "onFrame" in this.activeBridge) {
      (this.activeBridge as any).onFrame(callback);
    }
  }

  async start(): Promise<void> {
    let effectiveTransport = this.config.canTransport;

    if (effectiveTransport === "serial") {
      // Auto-detect CANalyst-II first
      const canalyst = new CanalystBridge(this.config, this.store, this.hub, this.writeQueue);
      if (this.onFrameCallback) canalyst.onFrame?.(this.onFrameCallback);
      
      await canalyst.start();
      const detected = await canalyst.waitForConnection(3000);
      
      if (detected) {
        this.activeBridge = canalyst;
        return;
      } else {
        await canalyst.abandon?.();
        // Fallback to serial
        const serial = new SerialBridge(this.config, this.store, this.hub, this.writeQueue);
        if (this.onFrameCallback) serial.onFrame?.(this.onFrameCallback);
        this.activeBridge = serial;
        await serial.start();
        return;
      }
    } else if (effectiveTransport === "canalystii") {
      this.activeBridge = new CanalystBridge(this.config, this.store, this.hub, this.writeQueue);
    } else {
      this.activeBridge = new SerialBridge(this.config, this.store, this.hub, this.writeQueue);
    }

    if (this.onFrameCallback && "onFrame" in this.activeBridge) {
      (this.activeBridge as any).onFrame(this.onFrameCallback);
    }
    await this.activeBridge.start();
  }

  sendCommand(command: Record<string, unknown>): void {
    if (this.activeBridge) {
      this.activeBridge.sendCommand(command);
    }
  }

  async close(): Promise<void> {
    if (this.activeBridge) {
      await this.activeBridge.close();
      this.activeBridge = null;
    }
  }
}
