export type BridgeTransport = "serial" | "canalystii" | "mqtt" | "disabled";

export interface BusDetectionState {
  detected: boolean;
  bus: "high" | "low";
  confidence: "none" | "low" | "high";
  highHits: number;
  lowHits: number;
  _ts?: number;  // Date.now() when last updated (for staleness check in UI)
}

export interface BridgeState {
  transport: BridgeTransport;
  adapter: string;
  connected: boolean;
  link_open: boolean;
  path: string | null;
  baud_rate: number | null;
  bitrate: number | null;
  last_status_at: number | null;
  last_error: string | null;
  bus_detection?: BusDetectionState;
}

export interface HardwareBridge {
  readonly state: BridgeState;
  start(): void | Promise<void>;
  sendCommand(command: Record<string, unknown>): void;
  close(): Promise<void>;
}
