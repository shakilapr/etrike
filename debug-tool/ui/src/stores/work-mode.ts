import { writable } from "svelte/store";
import { getMode, getOperationalState } from "../lib/api";
import type { WorkModeConfig, OperationalState } from "../lib/api";

export const workModeReady = writable(false);
export const workMode = writable<WorkModeConfig>({
  mode: "monitor",
  simulatedEcus: [],
  idSources: {},
  injectEmulatedToPhysical: false,
  bypasses: { sesSync: false, sebSync: false, mtrAbsent: false, benchSolo: false },
});

export const operationalState = writable<OperationalState>({
  mode: "offline",
  arm: "disarmed",
  revision: "0",
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
    const [config, opState] = await Promise.all([
      getMode(),
      getOperationalState().catch(() => null)
    ]);
    workMode.set(config);
    if (opState) {
      operationalState.set(opState);
    }
  } catch {
    // Backend not yet ready; store keeps its default (monitor)
  } finally {
    workModeReady.set(true);
  }
}
