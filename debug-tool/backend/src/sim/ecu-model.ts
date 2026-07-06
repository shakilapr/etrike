import type { CanFrame } from "../types/can";

/**
 * Contract for a simulated or emulated ECU model.
 * Implementations can be native C++ (via IPC), TypeScript classes,
 * or WASM modules — the SimulationEngine doesn't care.
 */
export interface EcuModel {
  /** Human-readable identifier (e.g. "RT Gateway"). */
  readonly id: string;

  /** One-time setup. Called before start(). */
  config(params: EcuConfig): void;

  /** Begin state machine execution. */
  start(): void | Promise<void>;

  /** Feed an incoming CAN frame to this ECU. May trigger state transitions. */
  ingest(frame: CanFrame): void;

  /** Periodic state machine update. May emit frames. */
  tick(dtMs: number): CanFrame[];

  /** Register a listener for frames emitted by this ECU. */
  onFrame(callback: (frame: CanFrame) => void): void;

  /** Read current ECU state (mode, faults, health, etc.). */
  state(): EcuState;

  /** Graceful shutdown. */
  stop(): void | Promise<void>;
}

export interface EcuConfig {
  /** CAN bitrate (not used by all models). */
  bitrate?: number;
  /** Bypass flags — see architecture §14.6. */
  bypasses?: {
    epscSync?: boolean;
    sebSync?: boolean;
    mtrAbsent?: boolean;
    benchSolo?: boolean;
  };
  /** Arbitrary model-specific parameters. */
  [key: string]: unknown;
}

export interface EcuState {
  /** ECU identifier matching the model's id. */
  ecu: string;
  /** Current operating mode (MANUAL, AUTO, ESTOP). */
  mode?: string;
  /** Safety state (Normal, InternalEstop, Fault). */
  safety?: string;
  /** Steer state machine state (for RT). */
  steerState?: string;
  /** Whether this ECU considers itself healthy. */
  healthy: boolean;
  /** Active fault flags (ECU-specific bit masks). */
  faultFlags?: number;
  /** Uptime in simulation milliseconds. */
  uptimeMs: number;
  /** Arbitrary model-specific fields. */
  [key: string]: unknown;
}
