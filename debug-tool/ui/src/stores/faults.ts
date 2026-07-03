import { latestById } from "./can";
import { logError } from "./errors";

// ── ESTOP reason codes (from shared/can/can_protocol.h) ──
const ESTOP_REASONS: Record<number, string> = {
  0: "None",
  1: "Button",
  2: "Heartbeat timeout",
  3: "Following error",
  4: "Obstacle detected",
  5: "CAN frame (0x001)",
  6: "CAN bus-off",
  7: "Internal fault (L3)",
};
function reasonLabel(v: number): string {
  return ESTOP_REASONS[v] ?? "code " + v;
}

// ── State ──
let lastSafetyState: number | null = null;
let lastMotorFault = 0;
let lastBrakeFault = false;
let lastSesL3Fault = false;
let lastSebL3Fault = false;

// ESTOP toggle burst tracking
let estopActive = false;
let estopToggleCount = 0;
let estopToggleStart = 0;
let estopLastReason = 0;
let estopSummaryTimer: ReturnType<typeof setTimeout> | null = null;
const ESTOP_QUIET_MS = 3000; // after 3s of no toggles, emit summary

const COOLDOWN_S = 10;
const cooldowns: Record<string, number> = {};

function cooldown(key: string): boolean {
  const now = Date.now() / 1000;
  if (now - (cooldowns[key] ?? 0) < COOLDOWN_S) return false;
  cooldowns[key] = now;
  return true;
}

function flushEstopSummary() {
  if (estopSummaryTimer) { clearTimeout(estopSummaryTimer); estopSummaryTimer = null; }
  if (estopToggleCount === 0) return;
  const count = estopToggleCount;
  const reason = reasonLabel(estopLastReason);
  const duration = ((Date.now() - estopToggleStart) / 1000).toFixed(1);
  // Only log if it's actually a burst (≥3 toggles) — single cycles are
  // already explained by the root-cause fault messages above.
  if (count >= 3) {
    logError("ESTOP burst: " + count + " cycles in " + duration + "s, cause=" + reason);
  }
  estopToggleCount = 0;
  estopToggleStart = 0;
}

export function initFaultWatcher(): () => void {
  return latestById.subscribe(($latest) => {
    // ── Gather CAN data ──
    const safetyHigh = $latest["high:0x011"]?.decoded;
    const safetyLow  = $latest["low:0x011"]?.decoded;
    const safety = safetyHigh ?? safetyLow;
    const estop = safety?.estop_active === true || safety?.estop_active === 1;

    const stateRptHigh = $latest["high:0x210"]?.decoded;
    const stateRptLow  = $latest["low:0x210"]?.decoded;
    const stateRpt = stateRptHigh ?? stateRptLow;
    const estopReason: number =
      stateRpt?.estop_reason !== undefined ? Number(stateRpt.estop_reason) : 0;

    const diagHigh = $latest["high:0x600"]?.decoded;
    const diagLow  = $latest["low:0x600"]?.decoded;
    const diag = diagHigh ?? diagLow;
    const brakeFault = diag?.brake_fault === true || diag?.brake_fault === 1;

    // ── ESTOP: track toggle bursts, don't flood ──
    if (estop !== estopActive) {
      estopActive = estop;
      if (estop) {
        // ESTOP just engaged — start or continue a burst
        if (estopToggleCount === 0) estopToggleStart = Date.now();
        estopToggleCount++;
        estopLastReason = estopReason;
        // Reset quiet timer
        if (estopSummaryTimer) clearTimeout(estopSummaryTimer);
      }
      // Start/restart quiet timer: after ESTOP_QUIET_MS of stable state, emit summary
      if (!estop) {
        if (estopSummaryTimer) clearTimeout(estopSummaryTimer);
        estopSummaryTimer = setTimeout(flushEstopSummary, ESTOP_QUIET_MS);
      }
    }

    // ── Root cause: brake fault ──
    if (brakeFault !== lastBrakeFault) {
      if (brakeFault) {
        if (cooldown("brake_fault_on")) {
          logError("BRAKE FAULT - pressure sensor or actuator fault (ESTOP will be triggered)");
        }
      } else {
        if (cooldown("brake_fault_off")) {
          logError("Brake fault cleared");
          // Flush any pending ESTOP summary when brake fault clears
          flushEstopSummary();
        }
      }
      lastBrakeFault = brakeFault;
    }

    // ── Safety state ──
    const safetyState: number | null =
      stateRpt?.safety_state !== undefined ? Number(stateRpt.safety_state) : null;
    if (safetyState !== null && safetyState !== lastSafetyState) {
      const labels = ["Normal", "Internal ESTOP", "Fault"];
      const label = labels[safetyState] ?? "?" + safetyState;
      if (safetyState > 0 && cooldown("safety_state")) {
        logError("Safety state: " + label + " (reason: " + reasonLabel(estopReason) + ")");
      }
      lastSafetyState = safetyState;
    }

    // ── Motor fault ──
    const motorHigh = $latest["high:0x206"]?.decoded;
    const motorLow  = $latest["low:0x206"]?.decoded;
    const motor = motorHigh ?? motorLow;
    const faultFlags: number = motor?.fault_flags !== undefined ? Number(motor.fault_flags) : 0;
    if (faultFlags !== lastMotorFault && cooldown("motor_fault")) {
      if (faultFlags > 0) {
        logError("Motor fault flags=0x" + faultFlags.toString(16).padStart(2, "0").toUpperCase());
      } else if (lastMotorFault > 0) {
        logError("Motor faults cleared");
      }
      lastMotorFault = faultFlags;
    }

    // ── SES L3 fault (0x202) ──
    const sesFault = $latest["low:0x202"]?.decoded;
    const sesL3 = sesFault?.l3_fault === true;
    if (sesL3 && !lastSesL3Fault && cooldown("ses_fault")) {
      logError("SES steering L3 fault (mask=" + (sesFault?.fault_mask_hex ?? "?") + ")");
    }
    lastSesL3Fault = sesL3;

    // ── SEB L3 fault (0x731) ──
    const sebFault = $latest["low:0x731"]?.decoded;
    const sebL3 = sebFault?.l3_fault === true;
    if (sebL3 && !lastSebL3Fault && cooldown("seb_fault")) {
      logError("SEB brake-by-wire L3 fault (mask=" + (sebFault?.fault_mask_hex ?? "?") + ")");
    }
    lastSebL3Fault = sebL3;
  });
}
