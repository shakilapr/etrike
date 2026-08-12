/**
 * Simulation-specific types.
 *
 * These extend the debug-tool's CAN types rather than duplicating them.
 * For now we define standalone types to avoid cross-package import
 * complexity; a future shared package can unify them.
 */

// ── Node identity ──────────────────────────────────────────────────

export type SimNodeId = "host" | "rt" | "sys" | "mtr" | "epsc" | "seb";

export type BusId = "high" | "low";

// ── Simulation frame ───────────────────────────────────────────────

/** A CAN frame inside the simulation, with source metadata. */
export interface SimFrame {
  /** Simulation time of transmission (ms). */
  simTimeMs: number;
  /** Which bus this frame was sent on. */
  bus: BusId;
  /** CAN identifier as a hex string, e.g. "0x300". */
  canId: string;
  /** Human-readable message name. */
  name: string;
  /** Data length code (0–8). */
  dlc: number;
  /** Payload bytes (0–255 each). */
  data: number[];
  /** Which simulated ECU sent this frame. */
  sender: SimNodeId;
  /** Decoded signal values (filled by decode later). */
  decoded?: Record<string, unknown>;
}

// ── Bus statistics ─────────────────────────────────────────────────

export interface BusStats {
  active: boolean;
  total: number;
  fps: number;
  loadPct: number;
  tec: number;
  rec: number;
  byId: Record<string, number>;
}

// ── Drive command (internal setpoint) ──────────────────────────────

export interface DriveCommand {
  speedMmps: number;       // mm/s, range [-500, 3000]
  yawRateMradS: number;    // mrad/s, range [-3000, 3000]
  gear: number;            // 0=N, 1=D, 2=S, 3=R
}

export interface ResolvedSetpoint {
  motorSpeedMmps: number;
  steerAngleDeg: number;
  gear: number;
}

// ── Simulation configuration ───────────────────────────────────────

export interface SimConfig {
  /** Tick resolution in ms (default 1). */
  tickMs: number;
  /** Clock speed multiplier (0=step, 1=real-time, >1=fast). */
  speed: number;
  /** Initial mode: "manual" | "auto" | "estop". */
  initialMode: "manual" | "auto" | "estop";
  /** Vehicle plant parameters. */
  plant: PlantConfig;
  /** Host drive cycle (empty = no Host commands). */
  hostDriveCycle: DriveCycleStep[];
  /** Fault injection schedule. */
  faults: FaultSpec[];
}

export interface PlantConfig {
  wheelbaseMm: number;
  maxSpeedMmps: number;
  maxSteeringDeg: number;
  steerLagMs: number;
  brakeDecelMmps2PerMm: number;
}

export interface DriveCycleStep {
  speedMmps: number;
  yawRateMradS: number;
  gear: number;
  durationMs: number;
  /** Direct Host steering angle in signed 0.1° units (right positive). */
  steerAngle01deg?: number;
}

// ── Fault injection ────────────────────────────────────────────────

export interface FaultSpec {
  /** Simulation time (ms) to activate this fault. */
  atMs: number;
  /** Type of fault. */
  type: FaultType;
  /** Target CAN ID (for drop/corrupt). */
  canId?: string;
  /** Target bus. */
  bus?: BusId;
  /** For corrupt: byte index and XOR mask. */
  byteIndex?: number;
  xorMask?: number;
  /** For freezeHeartbeat: which ECU's heartbeat to stop. */
  target?: SimNodeId;
  /** For triggerEstop: just the event. */
  /** For setEstopGpio: pressed or released. */
  pressed?: boolean;
}

export type FaultType =
  | "dropMessage"
  | "corruptMessage"
  | "freezeHeartbeat"
  | "triggerEstop"
  | "setEstopGpio"
  | "setBrakeLever"
  | "setModeButton";

// ── Simulation result ──────────────────────────────────────────────

export interface SimulationResult {
  durationMs: number;
  totalFrames: number;
  violations: SafetyViolation[];
  validationErrors: ValidationError[];
  highBus: BusStats;
  lowBus: BusStats;
  plantFinalSpeedMmps: number;
  plantFinalSteerDeg: number;
  plantMaxSteerDeg: number;
  plantFinalBrakeStrokeMm: number;
}

export interface SafetyViolation {
  timeMs: number;
  type: string;
  description: string;
}

export interface ValidationError {
  timeMs: number;
  canId: string;
  bus: BusId;
  error: string;
}
