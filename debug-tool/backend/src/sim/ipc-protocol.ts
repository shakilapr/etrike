/**
 * JSON-Lines protocol for native ECU model IPC.
 * Same pattern as the CANalyst-II Python bridge: stdin/stdout JSON Lines.
 *
 * Direction legend:
 *   →  Backend (Node.js) sends to native process stdin
 *   ←  Native process writes to stdout, received by backend
 */

// ── Backend → Native ──────────────────────────────────────────────────

export interface IpcFrameIn {
  type: "frame";
  bus: "high" | "low";
  id: string;
  dlc: number;
  data: number[];
  ts?: number;
}

export interface IpcConfig {
  type: "config";
  bypass_epsc_sync?: boolean;
  bypass_seb_sync?: boolean;
  bypass_mtr_absent?: boolean;
  bench_solo?: boolean;
}

export interface IpcTick {
  type: "tick";
  dt_ms: number;
}

export type IpcToNative = IpcFrameIn | IpcConfig | IpcTick;

// ── Native → Backend ──────────────────────────────────────────────────

export interface IpcFrameOut {
  type: "frame";
  bus: "high" | "low";
  id: string;
  dlc: number;
  data: number[];
  name?: string;
  ts?: number;
}

export interface IpcState {
  type: "state";
  ecu: string;
  mode?: string;
  safety?: string;
  steer_state?: string;
  healthy: boolean;
  fault_flags?: number;
  uptime_ms: number;
}

export interface IpcError {
  type: "error";
  message: string;
}

export type IpcFromNative = IpcFrameOut | IpcState | IpcError;

// ── Helpers ───────────────────────────────────────────────────────────

/** Serialize a message to a JSON line for the native process stdin. */
export function encodeIpcMessage(msg: IpcToNative): string {
  return JSON.stringify(msg) + "\n";
}

/** Try to parse a line from the native process stdout. */
export function decodeIpcMessage(line: string): IpcFromNative | null {
  const trimmed = line.trim();
  if (!trimmed) return null;
  try {
    return JSON.parse(trimmed) as IpcFromNative;
  } catch {
    return null;
  }
}
