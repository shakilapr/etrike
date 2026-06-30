import { writable } from "svelte/store";

export interface ErrorEntry {
  ts: number; // seconds since epoch
  message: string;
}

export const errorLog = writable<ErrorEntry[]>([]);

export function logError(message: string): void {
  errorLog.update((log) => [{ ts: Date.now() / 1000, message }, ...log].slice(0, 50));
}

export function clearErrors(): void {
  errorLog.set([]);
}
