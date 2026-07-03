import { writable, get } from "svelte/store";

// Survives tab switches — component can mount/unmount without losing state.
export const simMasterOn = writable(false);
export const simEnabled = writable(new Set<string>());
export const simRunning = writable(new Set<string>());

export function simHas(key: string): boolean {
  return get(simEnabled).has(key);
}
export function simIsRunning(key: string): boolean {
  return get(simRunning).has(key);
}

export function simToggle(key: string) {
  simEnabled.update((s) => {
    const next = new Set(s);
    if (next.has(key)) next.delete(key);
    else next.add(key);
    return next;
  });
}

export function simAddRunning(key: string) {
  simRunning.update((s) => new Set(s).add(key));
}
export function simRemoveRunning(key: string) {
  simRunning.update((s) => {
    const next = new Set(s);
    next.delete(key);
    return next;
  });
}
export function simClearRunning() {
  simRunning.set(new Set());
}
