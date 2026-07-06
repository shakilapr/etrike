import { latestById } from "./can";
import { logError, logWarn, logInfo } from "./errors";

// ── ESTOP reason codes ──
const ESTOP_REASONS: Record<number, string> = {
  0: "None", 1: "Button", 2: "Heartbeat timeout", 3: "Following error",
  4: "Obstacle detected", 5: "CAN frame (0x001)", 6: "CAN bus-off", 7: "Internal fault",
};
function rLabel(v: number): string { return ESTOP_REASONS[v] ?? "code " + v; }

// ── SES fault bit names (0x202 SES_ERRINFO, bytes 0-2) ──
const SES_FAULTS: [number, string][] = [
  [0x01,"UnderVolt"],[0x02,"OverVolt"],[0x04,"CanComErr"],[0x08,"TempErr"],
  [0x10,"DomainSC"],[0x20,"DomainV"],[0x40,"DomainT"],[0x80,"TempSensor"],
  [0x100,"AngleP_OC"],[0x200,"AngleP_AF"],[0x400,"AngleS_OC"],[0x800,"AngleS_AF"],
  [0x1000,"SensorPow"],[0x2000,"Alignment"],[0x4000,"OverAngle"],[0x8000,"MtrStall"],
  [0x10000,"MtrCurt"],[0x20000,"SensorCL"],[0x40000,"TorqT1_OC"],[0x80000,"TorqT1_AF"],
  [0x100000,"TorqT2_OC"],[0x200000,"TorqT2_AF"],[0x400000,"SentAngle"],[0x800000,"MtrIdling"],
  [0x1000000,"EPROM"],
];
// ── SEB fault bit names (0x731 SEB_ERRINFO, bytes 0-2) ──
const SEB_FAULTS: [number, string][] = [
  [0x01,"UnderVolt"],[0x02,"OverVolt"],[0x04,"CanComErr"],[0x08,"TempErr"],
  [0x10,"DomainSC"],[0x20,"DomainV"],[0x40,"DomainT"],[0x80,"AngleP_OC"],
  [0x100,"AngleP_AF"],[0x200,"AngleS_OC"],[0x400,"AngleS_AF"],[0x800,"NoPreSensor"],
  [0x2000,"SensorUCL"],[0x4000,"Alignment"],[0x8000,"AngleOver"],
  [0x20000,"MtrStall"],[0x40000,"MtrDC"],[0x80000,"OilErr"],[0x100000,"InitOil"],
  [0x200000,"SentValue"],[0x400000,"MtrNoLoad"],
  [0x1000000,"PreSensorOver"],[0x2000000,"LowVoltCharging"],
];
function decodeFaults(mask: number, table: [number, string][]): string[] {
  const active: string[] = [];
  for (const [bit, name] of table) { if (mask & bit) active.push(name); }
  return active;
}

// ── Heartbeat sources ──
function missingHeartbeats($latest: Record<string, unknown>): string[] {
  const m: string[] = [];
  if (!$latest["high:0x7FC"]) m.push("HOST(0x7FC)");
  if (!$latest["high:0x7FD"]) m.push("RT(0x7FD)");
  if (!$latest["low:0x7FE"])  m.push("SYS(0x7FE)");
  return m;
}

const ESTOP_QUIET_MS = 3000;

const CD_S = 10;

export function initFaultWatcher(): () => void {
  let lastSafetyState: number | null = null;
  let lastMotorFault = 0;
  let lastBrakeFault = false;
  let lastSesMask = 0;
  let lastSebMask = 0;
  let lastMode: number | null = null;
  let lastGear: number | null = null;
  let lastBlocked = false;
  let lastObstacleWarn = false;
  let lastSteerDiagFault = false;

  let estopActive = false;
  let estopToggleCount = 0;
  let estopToggleStart = 0;
  let estopLastReason = 0;
  let estopSummaryTimer: ReturnType<typeof setTimeout> | null = null;
  const cds: Record<string, number> = {};

  function cd(k: string): boolean {
    const n = Date.now() / 1000;
    if (n - (cds[k] ?? 0) < CD_S) return false;
    cds[k] = n; return true;
  }

  function flushEstop() {
    if (estopSummaryTimer) { clearTimeout(estopSummaryTimer); estopSummaryTimer = null; }
    if (estopToggleCount === 0) return;
    if (estopToggleCount >= 3) {
      logError("ESTOP burst: " + estopToggleCount + " cycles in " +
        ((Date.now() - estopToggleStart) / 1000).toFixed(1) + "s, cause=" + rLabel(estopLastReason));
    }
    estopToggleCount = 0; estopToggleStart = 0;
  }

  const unsubscribe = latestById.subscribe(($latest) => {
    // ═══ Gather all CAN data ═══
    const sHi = $latest["high:0x011"]?.decoded;
    const sLo = $latest["low:0x011"]?.decoded;
    const safety = (sHi ?? sLo) as Record<string, unknown> | undefined;
    const estop = safety?.estop_active === true || safety?.estop_active === 1;

    const rHi = $latest["high:0x210"]?.decoded;
    const rLo = $latest["low:0x210"]?.decoded;
    const rpt = (rHi ?? rLo) as Record<string, unknown> | undefined;
    const estopReason: number = rpt?.estop_reason !== undefined ? Number(rpt.estop_reason) : 0;
    const safetyState: number | null = rpt?.safety_state !== undefined ? Number(rpt.safety_state) : null;
    const mode: number | null = rpt?.mode !== undefined ? Number(rpt.mode) : null;

    const dHi = $latest["high:0x600"]?.decoded;
    const dLo = $latest["low:0x600"]?.decoded;
    const diag = (dHi ?? dLo) as Record<string, unknown> | undefined;
    const brakeFault = diag?.brake_fault === true || diag?.brake_fault === 1;
    const hbDiagOk = diag?.hb_ok !== false && diag?.hb_ok !== 0;

    const mHi = $latest["high:0x206"]?.decoded;
    const mLo = $latest["low:0x206"]?.decoded;
    const motor = (mHi ?? mLo) as Record<string, unknown> | undefined;
    const faultFlags: number = motor?.fault_flags !== undefined ? Number(motor.fault_flags) : 0;
    const gear: number | null = motor?.gear_state !== undefined ? Number(motor.gear_state) : null;

    const ses = $latest["low:0x202"]?.decoded as Record<string, unknown> | undefined;
    const sesMask: number = ses?.fault_mask !== undefined ? Number(ses.fault_mask) : 0;
    const sesL3 = ses?.l3_fault === true;

    const seb = $latest["low:0x731"]?.decoded as Record<string, unknown> | undefined;
    const sebMask: number = seb?.fault_mask !== undefined ? Number(seb.fault_mask) : 0;
    const sebL3 = seb?.l3_fault === true;

    const steerDiag = $latest["high:0x310"]?.decoded as Record<string, unknown> | undefined;
    const steerDiagFault = steerDiag?.SteerDiag_Fault === true;

    const obstacle = $latest["high:0x400"]?.decoded as Record<string, unknown> | undefined;
    const distMm: number = obstacle?.distance_mm !== undefined ? Number(obstacle.distance_mm) : Infinity;
    const obstacleWarn = distMm < 2000 && distMm !== 0xFFFFFFFF; // <2m and not "clear"

    const hbMissing = missingHeartbeats($latest as Record<string, unknown>);
    const hbOk = hbDiagOk && safety?.heartbeat_ok !== false && safety?.heartbeat_ok !== 0;

    // ═══ ESTOP toggle tracking ═══
    if (estop !== estopActive) {
      estopActive = estop;
      if (estop) {
        if (estopToggleCount === 0) estopToggleStart = Date.now();
        estopToggleCount++; estopLastReason = estopReason;
        if (estopSummaryTimer) clearTimeout(estopSummaryTimer);
      }
      if (!estop) {
        if (estopSummaryTimer) clearTimeout(estopSummaryTimer);
        estopSummaryTimer = setTimeout(flushEstop, ESTOP_QUIET_MS);
      }
    }

    // ═══ Brake fault ═══
    if (brakeFault !== lastBrakeFault) {
      if (brakeFault) {
        if (cd("brake_on")) {
          const sebMissing = !$latest["low:0x721"];
          if (sebMissing) {
            logError("BRAKE FAULT — SEB (brake ECU) not responding (start Simulator to suppress)");
          } else {
            logError("BRAKE FAULT — pressure sensor or actuator failure");
          }
        }
      } else {
        if (cd("brake_off")) { logInfo("Brake fault cleared"); flushEstop(); }
      }
      lastBrakeFault = brakeFault;
    }

    // ═══ Safety state ═══
    if (safetyState !== null && safetyState !== lastSafetyState) {
      const labels = ["Normal", "Internal ESTOP", "Fault"];
      const label = labels[safetyState] ?? "?" + safetyState;
      if (safetyState > 0 && cd("safety")) {
        let detail = rLabel(estopReason);
        if (estopReason === 2 && hbMissing.length > 0)
          detail = "heartbeat missing: " + hbMissing.join(", ");
        logError("Safety: " + label + " — " + detail);
      }
      lastSafetyState = safetyState;
    }

    // ═══ Mode changes (only log MANUAL↔AUTO; ESTOP is a symptom of faults) ═══
    if (mode !== null && mode !== lastMode) {
      const labels = ["MANUAL", "AUTO", "ESTOP"];
      if (mode !== 2 && cd("mode")) logInfo("Mode: " + (labels[mode] ?? "?" + mode));
      lastMode = mode;
    }

    // ═══ Gear changes ═══
    if (gear !== null && gear !== lastGear) {
      const labels = ["N", "D", "S", "R"];
      if (cd("gear")) logInfo("Gear: " + (labels[gear] ?? "?" + gear));
      lastGear = gear;
    }

    // ═══ Motor fault ═══
    if (faultFlags !== lastMotorFault && cd("motor")) {
      if (faultFlags > 0)
        logWarn("Motor fault 0x" + faultFlags.toString(16).toUpperCase().padStart(2,"0"));
      else if (lastMotorFault > 0) logInfo("Motor fault cleared");
      lastMotorFault = faultFlags;
    }

    // ═══ SES faults (detailed) ═══
    if (sesMask !== lastSesMask && cd("ses")) {
      const was = decodeFaults(lastSesMask, SES_FAULTS);
      const now = decodeFaults(sesMask, SES_FAULTS);
      const added = now.filter(f => !was.includes(f));
      const removed = was.filter(f => !now.includes(f));
      if (added.length)   logError("SES fault: " + added.join(", "));
      if (removed.length) logInfo("SES cleared: " + removed.join(", "));
      lastSesMask = sesMask;
    }

    // ═══ SEB faults (detailed) ═══
    if (sebMask !== lastSebMask && cd("seb")) {
      const was = decodeFaults(lastSebMask, SEB_FAULTS);
      const now = decodeFaults(sebMask, SEB_FAULTS);
      const added = now.filter(f => !was.includes(f));
      const removed = was.filter(f => !now.includes(f));
      if (added.length)   logError("SEB fault: " + added.join(", "));
      if (removed.length) logInfo("SEB cleared: " + removed.join(", "));
      lastSebMask = sebMask;
    }

    // ═══ Steering diag fault ═══
    if (steerDiagFault !== lastSteerDiagFault && cd("steer_diag")) {
      if (steerDiagFault) logWarn("Steering diag fault (0x310)");
      else logInfo("Steering diag fault cleared");
      lastSteerDiagFault = steerDiagFault;
    }

    // ═══ Obstacle proximity ═══
    if (obstacleWarn !== lastObstacleWarn && cd("obstacle")) {
      if (obstacleWarn) logWarn("Obstacle: " + distMm + "mm (<2m)");
      else logInfo("Obstacle cleared");
      lastObstacleWarn = obstacleWarn;
    }

    // ═══ ESTOP burst ═══
    if (estopToggleCount >= 3 && cd("estop_burst")) {
      logError("ESTOP burst: " + estopToggleCount + " cycles in " +
        ((Date.now() - estopToggleStart) / 1000).toFixed(1) + "s, cause=" + rLabel(estopLastReason));
      estopToggleCount = 0; estopToggleStart = 0;
    }

    // ═══ Mobility blockers ═══
    const blockers: string[] = [];
    if (estop)               blockers.push("ESTOP active");
    if (mode === 2)          blockers.push("Mode=ESTOP");
    if (safetyState === 1)   blockers.push("Safety=InternalEstop");
    if (safetyState === 2)   blockers.push("Safety=Fault");
    if (brakeFault)          blockers.push("Brake fault");
    if (faultFlags > 0)      blockers.push("Motor fault 0x" + faultFlags.toString(16).toUpperCase().padStart(2,"0"));
    if (sesL3)               blockers.push("SES fault");
    if (sebL3)               blockers.push("SEB fault");
    if (steerDiagFault)      blockers.push("Steer diag fault");
    if (!hbOk) {
      if (hbMissing.length) blockers.push("HB missing: " + hbMissing.join(","));
      else blockers.push("Heartbeat lost");
    }
    const blocked = blockers.length > 0;
    if (blocked !== lastBlocked && cd("mobility")) {
      if (blocked) logError("BLOCKED: " + blockers.join("; "));
      else logInfo("CLEAR — vehicle ready");
    }
    lastBlocked = blocked;
  });

  return () => {
    if (estopSummaryTimer) clearTimeout(estopSummaryTimer);
    unsubscribe();
  };
}
