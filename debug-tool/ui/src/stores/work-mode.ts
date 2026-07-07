import { writable } from "svelte/store";
import type { WorkModeConfig } from "../lib/api";

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
