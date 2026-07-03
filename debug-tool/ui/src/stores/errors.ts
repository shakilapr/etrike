import { writable } from "svelte/store";

export type Severity = "info" | "warn" | "error";

export interface ErrorEntry {
  ts: number;
  message: string;
  severity: Severity;
}

export const errorLog = writable<ErrorEntry[]>([]);

function log(severity: Severity, message: string): void {
  errorLog.update((log) => [{ ts: Date.now() / 1000, message, severity }, ...log].slice(0, 100));
}

/** Green — informational: mode changes, gear changes, faults cleared, ready state */
export function logInfo(message: string): void { log("info", message); }
/** Orange — warning: non-critical faults, proximity warnings, diagnostic issues */
export function logWarn(message: string): void { log("warn", message); }
/** Red — critical: ESTOP, safety faults, L3 faults, mobility blockers */
export function logError(message: string): void { log("error", message); }

export function clearErrors(): void {
  errorLog.set([]);
}
