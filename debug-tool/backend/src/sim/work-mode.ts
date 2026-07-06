/**
 * Work mode configuration types and defaults.
 * See architecture §14.6 for the full WorkModeConfig specification.
 */
import { z } from "zod";

export const WORK_MODES = ["full-sim", "emulator", "hybrid", "bench", "monitor"] as const;
export type WorkMode = (typeof WORK_MODES)[number];

export const ECU_IDS = ["host", "rt", "sys", "mtr", "epsc", "seb"] as const;
export type EcuId = (typeof ECU_IDS)[number];

export const FRAME_SOURCES = ["physical", "emulated", "simulated", "*"] as const;
export type FrameSourceRef = (typeof FRAME_SOURCES)[number];

export interface WorkModeConfig {
  mode: WorkMode;
  /** Which ECUs run as software models (Full Sim: all 6, Emulator: missing only, Hybrid: missing only, Bench: none) */
  simulatedEcus: EcuId[];
  /** Per-ID source routing. "*" = auto-detect from ECU presence; explicit entries override. */
  idSources: Record<string, FrameSourceRef>;
  /** Whether emulated frames are injected onto the physical CAN bus. */
  injectEmulatedToPhysical: boolean;
  /** Bench-test bypass flags. */
  bypasses: {
    epscSync: boolean;
    sebSync: boolean;
    mtrAbsent: boolean;
    benchSolo: boolean;
  };
  /** Scenario to run on start (optional). */
  scenario?: string;
  /** Model backend: "ts" = TypeScript (always works), "native" = C++ via IPC (faster, needs sim-engine-native built). */
  modelBackend?: "ts" | "native";
}

/** Validation schema for WorkModeConfig. */
export const workModeConfigSchema = z.object({
  mode: z.enum(WORK_MODES),
  simulatedEcus: z.array(z.enum(ECU_IDS)).default([]),
  idSources: z.record(z.enum(FRAME_SOURCES)).default({}),
  injectEmulatedToPhysical: z.boolean().default(false),
  bypasses: z.object({
    epscSync: z.boolean().default(false),
    sebSync: z.boolean().default(false),
    mtrAbsent: z.boolean().default(false),
    benchSolo: z.boolean().default(false),
  }).default({}),
  scenario: z.string().optional(),
});

/** Default config per mode. */
export const MODE_DEFAULTS: Record<WorkMode, WorkModeConfig> = {
  "full-sim": {
    mode: "full-sim",
    simulatedEcus: ["host", "rt", "sys", "mtr", "epsc", "seb"],
    idSources: {},
    injectEmulatedToPhysical: false,
    bypasses: { epscSync: true, sebSync: true, mtrAbsent: false, benchSolo: false },
  },
  "emulator": {
    mode: "emulator",
    simulatedEcus: [],
    idSources: { "*": "*" },
    injectEmulatedToPhysical: false,
    bypasses: { epscSync: false, sebSync: false, mtrAbsent: false, benchSolo: false },
  },
  "hybrid": {
    mode: "hybrid",
    simulatedEcus: [],
    idSources: { "*": "*" },
    injectEmulatedToPhysical: true,
    bypasses: { epscSync: false, sebSync: false, mtrAbsent: false, benchSolo: false },
  },
  "bench": {
    mode: "bench",
    simulatedEcus: [],
    idSources: {},
    injectEmulatedToPhysical: false,
    bypasses: { epscSync: true, sebSync: true, mtrAbsent: true, benchSolo: true },
  },
  "monitor": {
    mode: "monitor",
    simulatedEcus: [],
    idSources: {},
    injectEmulatedToPhysical: false,
    bypasses: { epscSync: false, sebSync: false, mtrAbsent: false, benchSolo: false },
  },
};
