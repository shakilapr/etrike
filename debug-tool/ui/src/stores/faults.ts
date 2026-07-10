import { ID_HOST_HEARTBEAT, ID_RT_HEARTBEAT, ID_SYS_HEARTBEAT, ID_SYS_SAFETY_STS, ID_RT_STATE_RPT, ID_SYS_DIAG_RPT, ID_MTR_MOTOR_FBK, ID_STEER_DIAG, ID_HOST_OBSTACLE_DIST, ID_SEB_STATUS, ID_SES_ErrInfo, ID_SEB_ErrInfo, SIG_SYS_MODE_CMD_MODE, SIG_RT_DRIVE_CMD_GEAR, ESTOP_REASONS, SES_FAULTS, SEB_FAULTS, decodeFaults } from "@etrike/debug-shared";
import { latestById } from "./can";
import { logError, logWarn, logInfo } from "./errors";


function rLabel(v: number): string { return ESTOP_REASONS[v] ?? "code " + v; }

// ── Heartbeat sources ──
function missingHeartbeats($latest: Record<string, unknown>): string[] {
  const m: string[] = [];
  if (!$latest[`high:${ID_HOST_HEARTBEAT}`]) m.push("HOST(0x7FC)");
  if (!$latest[`high:${ID_RT_HEARTBEAT}`]) m.push("RT(0x7FD)");
  if (!$latest[`low:${ID_SYS_HEARTBEAT}`])  m.push("SYS(0x7FE)");
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
    const sHi = $latest[`high:${ID_SYS_SAFETY_STS}`]?.decoded;
    const sLo = $latest[`low:${ID_SYS_SAFETY_STS}`]?.decoded;
    const safety = (sHi ?? sLo) as Record<string, unknown> | undefined;
    const estop = safety?.estop_active === true || safety?.estop_active === 1;

    const rHi = $latest[`high:${ID_RT_STATE_RPT}`]?.decoded;
    const rLo = $latest[`low:${ID_RT_STATE_RPT}`]?.decoded;
    const rpt = (rHi ?? rLo) as Record<string, unknown> | undefined;
    const estopReason: number = rpt?.estop_reason !== undefined ? Number(rpt.estop_reason) : 0;
    const safetyState: number | null = rpt?.safety_state !== undefined ? Number(rpt.safety_state) : null;
    const mode: number | null = rpt?.mode !== undefined ? Number(rpt.mode) : null;

    const dHi = $latest[`high:${ID_SYS_DIAG_RPT}`]?.decoded;
    const dLo = $latest[`low:${ID_SYS_DIAG_RPT}`]?.decoded;
    const diag = (dHi ?? dLo) as Record<string, unknown> | undefined;
    const brakeFault = diag?.brake_fault === true || diag?.brake_fault === 1;
    const hbDiagOk = diag?.hb_ok !== false && diag?.hb_ok !== 0;

    const mHi = $latest[`high:${ID_MTR_MOTOR_FBK}`]?.decoded;
    const mLo = $latest[`low:${ID_MTR_MOTOR_FBK}`]?.decoded;
    const motor = (mHi ?? mLo) as Record<string, unknown> | undefined;
    const faultFlags: number = motor?.fault_flags !== undefined ? Number(motor.fault_flags) : 0;
    const gear: number | null = motor?.gear_state !== undefined ? Number(motor.gear_state) : null;

    const ses = $latest[`low:${ID_SES_ErrInfo}`]?.decoded as Record<string, unknown> | undefined;
    const sesMask: number = ses?.fault_mask !== undefined ? Number(ses.fault_mask) : 0;
    const sesL3 = ses?.l3_fault === true;

    const seb = $latest[`low:${ID_SEB_ErrInfo}`]?.decoded as Record<string, unknown> | undefined;
    const sebMask: number = seb?.fault_mask !== undefined ? Number(seb.fault_mask) : 0;
    const sebL3 = seb?.l3_fault === true;

    const steerDiag = $latest[`high:${ID_STEER_DIAG}`]?.decoded as Record<string, unknown> | undefined;
    const steerDiagFault = steerDiag?.SteerDiag_Fault === true;

    const obstacle = $latest[`high:${ID_HOST_OBSTACLE_DIST}`]?.decoded as Record<string, unknown> | undefined;
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
          const sebMissing = !$latest[`low:${ID_SEB_STATUS}`];
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
      if (mode !== 2 && cd(SIG_SYS_MODE_CMD_MODE)) logInfo("Mode: " + (labels[mode] ?? "?" + mode));
      lastMode = mode;
    }

    // ═══ Gear changes ═══
    if (gear !== null && gear !== lastGear) {
      const labels = ["N", "D", "S", "R"];
      if (cd(SIG_RT_DRIVE_CMD_GEAR)) logInfo("Gear: " + (labels[gear] ?? "?" + gear));
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
