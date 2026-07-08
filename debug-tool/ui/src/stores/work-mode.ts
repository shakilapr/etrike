import { writable } from "svelte/store";
import { getMode } from "../lib/api";
import type { WorkModeConfig } from "../lib/api";

export const workModeReady = writable(false);
export const workMode = writable<WorkModeConfig>({
  mode: "monitor",
  simulatedEcus: [],
  idSources: {},
  injectEmulatedToPhysical: false,
  bypasses: { sesSync: false, sebSync: false, mtrAbsent: false, benchSolo: false },
});

const MODE_LABELS: Record<string, string> = {
  "full-sim": "Full Simulation",
  "emulator": "Emulator",
  "hybrid": "Hybrid",
  "bench": "Bench Test",
  "monitor": "Monitor Only",
};

export function modeLabel(mode: string): string {
  return MODE_LABELS[mode] ?? mode;
}

/** Fetch the current work mode from the backend and sync the store. */
export async function initWorkMode(): Promise<void> {
  try {
    const config = await getMode();
    workMode.set(config);
  } catch {
    // Backend not yet ready; store keeps its default (monitor)
  } finally {
    workModeReady.set(true);
  }
}
