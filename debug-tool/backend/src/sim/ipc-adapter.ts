import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { createInterface } from "node:readline";
import { existsSync } from "node:fs";
import { resolve } from "node:path";
import type { CanFrame } from "../types/can";
import type { EcuModel, EcuConfig, EcuState } from "./ecu-model";
import { encodeIpcMessage, decodeIpcMessage, type IpcToNative, type IpcFromNative } from "./ipc-protocol";

/**
 * Runs a native ECU simulation process (compiled from firmware C++ source)
 * and communicates via stdin/stdout JSON-Lines. Implements the EcuModel
 * interface so the SimulationEngine doesn't care about IPC vs. TypeScript.
 *
 * Same pattern as CanalystBridge — the native process is a child that reads
 * frames/commands from stdin and writes response frames/state to stdout.
 */
export class IpcEngineAdapter implements EcuModel {
  readonly id: string;
  private process: ChildProcessWithoutNullStreams | null = null;
  private frameCallbacks: Array<(frame: CanFrame) => void> = [];
  private stateCallbacks: Array<(state: EcuState) => void> = [];
  private currentState: EcuState;
  private binaryPath: string;

  constructor(ecuId: string, binaryPath?: string) {
    this.id = ecuId;
    this.binaryPath = binaryPath ?? IpcEngineAdapter.defaultBinaryPath();
    this.currentState = { ecu: ecuId, healthy: false, uptimeMs: 0 };
  }

  /** Resolve the default path to sim-engine-native executable. */
  static defaultBinaryPath(): string {
    const ext = process.platform === "win32" ? ".exe" : "";
    // Look relative to the native-test build directory
    const candidates = [
      resolve(__dirname, "../../../native-test/build3/sim_engine_native" + ext),
      resolve(__dirname, "../../../../native-test/build3/sim_engine_native" + ext),
    ];
    for (const candidate of candidates) {
      if (existsSync(candidate)) return candidate;
    }
    return candidates[0]; // return first candidate even if not found — caller handles error
  }

  config(_params: EcuConfig): void {
    if (this.process && this.process.stdin.writable) {
      this.process.stdin.write(encodeIpcMessage({
        type: "config",
        bypass_epsc_sync: _params.bypasses?.epscSync,
        bypass_seb_sync: _params.bypasses?.sebSync,
        bypass_mtr_absent: _params.bypasses?.mtrAbsent,
        bench_solo: _params.bypasses?.benchSolo,
      }));
    }
  }

  start(): void {
    if (this.process) return;

    if (!existsSync(this.binaryPath)) {
      throw new Error(`sim-engine-native not found at ${this.binaryPath}. ` +
        `Build it with: cmake --build native-test/build3 --target sim_engine_native`);
    }

    this.process = spawn(this.binaryPath, [], {
      stdio: ["pipe", "pipe", "pipe"],
    });

    createInterface({ input: this.process.stdout }).on("line", (line: string) => {
      const msg = decodeIpcMessage(line);
      if (!msg) return;

      if (msg.type === "frame") {
        const frame: CanFrame = {
          ts: msg.ts ?? Date.now() / 1000,
          bus: msg.bus,
          id: msg.id,
          name: msg.name ?? "UNKNOWN",
          dlc: msg.dlc,
          data: msg.data,
          decoded: {},
        };
        for (const cb of this.frameCallbacks) {
          try { cb(frame); } catch { /* don't let one broken callback break the adapter */ }
        }
      } else if (msg.type === "state") {
        this.currentState = {
          ecu: msg.ecu,
          mode: msg.mode,
          safety: msg.safety,
          steerState: msg.steer_state,
          healthy: msg.healthy,
          faultFlags: msg.fault_flags,
          uptimeMs: msg.uptime_ms,
        };
        for (const cb of this.stateCallbacks) {
          try { cb(this.currentState); } catch { /* */ }
        }
      }
    });

    this.process.stderr?.on("data", (data: Buffer) => {
      // Forward stderr to console for debugging (physics debug logs, etc.)
      const text = data.toString("utf8").trim();
      if (text) console.log(`[sim-engine:${this.id}] ${text}`);
    });

    this.process.on("error", (error) => {
      console.error(`[sim-engine:${this.id}] process error:`, error.message);
    });

    this.process.on("exit", (code, signal) => {
      console.log(`[sim-engine:${this.id}] exited code=${code} signal=${signal}`);
      this.currentState.healthy = false;
      this.process = null;
    });

    this.currentState.healthy = true;
  }

  ingest(frame: CanFrame): void {
    if (!this.process || !this.process.stdin.writable) return;
    const msg: IpcToNative = {
      type: "frame",
      bus: frame.bus,
      id: frame.id,
      dlc: frame.dlc,
      data: frame.data,
      ts: frame.ts,
    };
    this.process.stdin.write(encodeIpcMessage(msg));
  }

  tick(dtMs: number): CanFrame[] {
    if (!this.process || !this.process.stdin.writable) return [];
    // Collect frames synchronously is not possible with async IPC.
    // Instead, frames arrive via the stdout listener and are pushed
    // to registered callbacks. The SimulationEngine should use onFrame().
    this.process.stdin.write(encodeIpcMessage({ type: "tick", dt_ms: dtMs }));
    return []; // frames arrive asynchronously via onFrame callback
  }

  onFrame(callback: (frame: CanFrame) => void): void {
    this.frameCallbacks.push(callback);
  }

  /** Register a state update listener. */
  onState(callback: (state: EcuState) => void): void {
    this.stateCallbacks.push(callback);
  }

  state(): EcuState {
    return { ...this.currentState };
  }

  async stop(): Promise<void> {
    if (!this.process) return;
    const child = this.process;
    this.process = null;
    this.currentState.healthy = false;
    await new Promise<void>((resolveClose) => {
      child.once("exit", () => resolveClose());
      child.kill();
      setTimeout(resolveClose, 1000).unref();
    });
  }
}
