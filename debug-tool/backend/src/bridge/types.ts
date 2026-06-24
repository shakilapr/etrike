export type BridgeTransport = "serial" | "canalystii" | "disabled";

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
}

export interface HardwareBridge {
  readonly state: BridgeState;
  start(): void;
  sendCommand(command: Record<string, unknown>): void;
  close(): Promise<void>;
}
