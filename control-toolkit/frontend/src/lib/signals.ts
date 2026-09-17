import type { MessageState } from '../store'

export const PROFILE_LABELS: Record<string, string> = {
  bench_test: 'Real · CANalyst Bench',
  full_vehicle: 'Real · CANalyst Vehicle',
}

/** Session profile → transport mode shown in Settings. */
export function transportModeOf(_profile?: string | undefined | null): 'real' {
  return 'real'
}

export function signalText(m: MessageState | undefined, key: string): string {
  if (!m?.signals?.[key]) return '—'
  const s = m.signals[key]
  return String(s.enum_label ?? s.engineering_value ?? '—')
}

/** Empty/null/whitespace → em dash (topbar mode/power often arrives as ""). */
export function dash(v: unknown): string {
  if (v == null) return '—'
  const s = String(v).trim()
  return s === '' ? '—' : s
}

/** NODE_STATUS enum label for node_state (INIT..UNKNOWN) — numeric fallback. */
export function nodeStateLabel(m: MessageState | undefined): string {
  const raw = m?.signals?.node_state
  const label = String(raw?.enum_label ?? '').trim().toUpperCase()
  if (label) return label
  const n = signalNum(m, 'node_state')
  return ({ 0: 'INIT', 1: 'ACQUIRE', 2: 'STANDBY', 3: 'ACTIVE', 4: 'INHIBITED', 5: 'ESTOP', 6: 'RECOVER', 15: 'UNKNOWN' })[n ?? -1] ?? ''
}

/** Persistent ESTOP latch observed from a node's NODE_STATUS frame (fresh). */
export function nodeLatchActive(messages: MessageState[], name: string): boolean {
  const m = findMsg(messages, name)
  if (!m || !frameRecent(m)) return false
  if (nodeStateLabel(m) === 'ESTOP') return true
  return signalIsOn(m, 'estop_latched') || signalIsOn(m, 'estop_active')
}

/** Req/Conf line without "— · —" style doubling when both empty. */
export function formatReqConf(req: unknown, conf: unknown): string {
  const r = dash(req)
  const c = dash(conf)
  if (r === '—' && c === '—') return '—'
  if (r === c) return r
  return `Req ${r} · Conf ${c}`
}

export function signalNum(m: MessageState | undefined, key: string): number | null {
  const v = m?.signals?.[key]?.engineering_value
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.trim() !== '' && !Number.isNaN(Number(v))) return Number(v)
  return null
}

export function findMsg(messages: MessageState[], name: string, bus?: string) {
  return messages.find((m) => m.name === name && (bus == null || m.bus === bus))
}

/** True for live/late, or recently seen event frames (cycle_ms=0 ages to missing quickly). */
export function frameRecent(m: MessageState | undefined, maxAgeMs = 3000): boolean {
  if (!m) return false
  const f = String(m.freshness || '').toLowerCase()
  if (f === 'live' || f === 'late') return true
  if (f === 'unseen') return false
  if (typeof m.age_ms === 'number' && Number.isFinite(m.age_ms) && m.age_ms <= maxAgeMs) {
    return true
  }
  return false
}

/** Decode a boolean-ish signal (0/1, true/false, active, ESTOP, …). */
export function signalIsOn(m: MessageState | undefined, key: string): boolean {
  if (!m?.signals?.[key]) return false
  const s = m.signals[key]
  const v = s.enum_label ?? s.engineering_value
  if (typeof v === 'boolean') return v
  if (typeof v === 'number' && Number.isFinite(v)) return v !== 0
  const t = String(v ?? '')
    .trim()
    .toLowerCase()
  if (!t || t === '0' || t === 'false' || t === 'clear' || t === 'off' || t === 'inactive') {
    return false
  }
  if (t === '1' || t === 'true' || t === 'active' || t === 'on' || t === 'estop') return true
  // numeric string
  const n = Number(t)
  return Number.isFinite(n) ? n !== 0 : false
}

/** RT firmware estop_reason codes (rt-esp32/src/config.h). */
export const RT_ESTOP_REASONS: Record<number, string> = {
  0: 'None',
  1: 'ESTOP Button',
  2: 'Heartbeat Loss',
  3: 'Following Error',
  4: 'Obstacle',
  5: 'CAN Frame',
  6: 'Bus Off',
  7: 'Internal',
  8: 'EGAS Mismatch',
  9: 'Stale Cmd',
  10: 'Watchdog',
}

/** Deliberately compact: this value is rendered in the fixed top health strip. */
export const RT_ESTOP_REASON_SHORT: Record<number, string> = {
  0: 'ESTOP',
  1: 'Button',
  2: 'Heartbeat',
  3: 'Steering',
  4: 'Obstacle',
  5: 'CAN frame',
  6: 'Bus-off',
  7: 'Internal',
  8: 'EGAS fault',
  9: 'Stale cmd',
  10: 'Watchdog',
}

/**
 * ESTOP is multi-source — never trust only session.estop_active (host inject latch).
 * Includes RT estop_reason, SYS flags, and bus 0x001 so UI can show *why*.
 */
export type EstopObservation = {
  hostLatch: boolean
  busHigh: boolean
  busLow: boolean
  sysReported: boolean
  rtModeEstop: boolean
  rtReasonCode: number
  rtReasonLabel: string
  rtStale?: boolean
  lastKnownReasonCode?: number
  lastKnownReasonLabel?: string
  rtMode: string
  safetyState: number | null
  sysHeartbeatBad: boolean
  sysCanBad: boolean
  sysBrakeFault: boolean
  sysNodeLatch: boolean
  rtNodeLatch: boolean
  mtrNodeLatch: boolean
  rtPresent: boolean
  hasLiveTraffic: boolean
  any: boolean
  /** Short chip label */
  label: string
  /** Human causes (ordered) */
  causes: string[]
  /** Tooltip / detail */
  detail: string
}

export function observeEstop(
  messages: MessageState[],
  ses: { estop_active?: boolean | null } | null | undefined,
): EstopObservation {
  const hostLatch = !!ses?.estop_active
  const busHigh = frameRecent(findMsg(messages, 'SAFETY_ESTOP', 'high'), 5000)
  const busLow = frameRecent(findMsg(messages, 'SAFETY_ESTOP', 'low'), 5000)
  const sysSafety = findMsg(messages, 'SYS_SAFETY_STS')
  const sysHb = findMsg(messages, 'SYS_HEARTBEAT')
  const sysDiag = findMsg(messages, 'SYS_DIAG_RPT')
  const sysReported =
    (frameRecent(sysSafety) && signalIsOn(sysSafety, 'estop_active')) ||
    (frameRecent(sysHb) && signalIsOn(sysHb, 'estop_active')) ||
    (frameRecent(sysDiag) && signalIsOn(sysDiag, 'estop_active'))
  const sysHeartbeatBad =
    !!sysHb &&
    frameRecent(sysHb) &&
    sysHb.signals?.heartbeat_ok != null &&
    !signalIsOn(sysHb, 'heartbeat_ok')
  const sysCanBad =
    !!sysHb && frameRecent(sysHb) && sysHb.signals?.can_ok != null && !signalIsOn(sysHb, 'can_ok')
  const sysBrakeFault = frameRecent(sysDiag) && signalIsOn(sysDiag, 'brake_fault')

  const candidates = [
    findMsg(messages, 'RT_STATE_RPT', 'high'),
    findMsg(messages, 'RT_STATE_RPT', 'low'),
    findMsg(messages, 'RT_STATE_RPT'),
  ].filter((m): m is MessageState => !!m)
  const rtState = candidates.find((m) => frameRecent(m, 3000))
  const lastKnownRtState = candidates[0]
  const rtStale = !rtState && candidates.length > 0
  const rtPresent = !!rtState || (candidates.length > 0 && frameRecent(candidates[0], 10000))

  const rtModeRaw = rtState?.signals?.mode
  let rtMode = String(rtModeRaw?.enum_label ?? rtModeRaw?.engineering_value ?? '')
    .trim()
    .toUpperCase()
  if (!rtMode && rtModeRaw?.engineering_value != null) {
    const n = Number(rtModeRaw.engineering_value)
    if (n === 0) rtMode = 'MANUAL'
    else if (n === 1) rtMode = 'AUTO'
    else if (n === 2) rtMode = 'ESTOP'
  }
  const rtModeEstop = rtMode === 'ESTOP' || Number(rtModeRaw?.engineering_value) === 2
  const rtReasonCode = (() => {
    if (!rtState) return 0
    const n = signalNum(rtState, 'estop_reason')
    return n != null && Number.isFinite(n) ? Math.trunc(n) : 0
  })()
  const rtReasonLabel = RT_ESTOP_REASONS[rtReasonCode] ?? `unknown_${rtReasonCode}`

  const lastKnownReasonCode = (() => {
    const n = signalNum(lastKnownRtState, 'estop_reason')
    return n != null && Number.isFinite(n) ? Math.trunc(n) : 0
  })()
  const lastKnownReasonLabel = RT_ESTOP_REASONS[lastKnownReasonCode] ?? `unknown_${lastKnownReasonCode}`

  const safetyState = signalNum(rtState, 'safety_state')

  // NODE_STATUS (0x500/0x501/0x502) authoritative persistent latch.
  const sysNodeLatch = nodeLatchActive(messages, 'SYS_NODE_STATUS')
  const rtNodeLatch = nodeLatchActive(messages, 'RT_NODE_STATUS')
  const mtrNodeLatch = nodeLatchActive(messages, 'MTR_NODE_STATUS')

  const causes: string[] = []
  if (hostLatch) causes.push('Host inject latch (Clear latch = host only)')
  if (busHigh) causes.push('0x001 SAFETY_ESTOP on High')
  if (busLow) causes.push('0x001 SAFETY_ESTOP on Low')
  if (sysReported) causes.push('SYS estop_active')
  if (sysNodeLatch) causes.push('SYS NODE_STATUS latched')
  if (rtNodeLatch) causes.push('RT NODE_STATUS latched')
  if (mtrNodeLatch) causes.push('MTR NODE_STATUS latched')
  if (sysHeartbeatBad) causes.push('SYS heartbeat_ok=0')
  if (sysCanBad) causes.push('SYS can_ok=0')
  if (sysBrakeFault) causes.push('SYS brake_fault')
  if (rtReasonCode !== 0) {
    causes.push(`RT estop_reason=${rtReasonCode} (${rtReasonLabel})`)
  } else if (rtModeEstop) {
    causes.push('RT mode ESTOP (reason code 0)')
  }

  const any =
    hostLatch ||
    busHigh ||
    busLow ||
    sysReported ||
    sysNodeLatch ||
    rtNodeLatch ||
    mtrNodeLatch ||
    rtModeEstop ||
    rtReasonCode !== 0 ||
    sysHeartbeatBad ||
    sysCanBad ||
    sysBrakeFault

  const hasLiveTraffic = messages.length > 0

  let label = 'Clear'
  if (any) {
    if (rtReasonCode !== 0) {
      label = `RT · ${RT_ESTOP_REASON_SHORT[rtReasonCode] ?? `Reason ${rtReasonCode}`}`
    }
    else if (hostLatch && (busHigh || busLow || sysReported || rtModeEstop)) label = 'Latch+bus'
    else if (hostLatch) label = 'Host latch'
    else if (busHigh && busLow) label = 'Bus H+L'
    else if (busHigh) label = 'Bus High'
    else if (busLow) label = 'Bus Low'
    else if (sysReported && rtModeEstop) label = 'SYS+RT'
    else if (sysNodeLatch && rtNodeLatch && mtrNodeLatch) label = 'SYS+RT+MTR latched'
    else if (sysNodeLatch && rtNodeLatch) label = 'SYS+RT latched'
    else if (sysNodeLatch) label = 'SYS latched'
    else if (rtNodeLatch) label = 'RT latched'
    else if (mtrNodeLatch) label = 'MTR latched'
    else if (sysReported) label = 'SYS'
    else if (rtModeEstop) label = 'RT ESTOP'
    else if (sysHeartbeatBad || sysCanBad || sysBrakeFault) label = 'SYS fault'
    else label = 'Active'
  } else if (!hasLiveTraffic) {
    label = 'No signal'
  }

  return {
    hostLatch,
    busHigh,
    busLow,
    sysReported,
    rtModeEstop,
    rtReasonCode,
    rtReasonLabel,
    rtStale,
    lastKnownReasonCode,
    lastKnownReasonLabel,
    rtMode,
    safetyState,
    sysHeartbeatBad,
    sysCanBad,
    sysBrakeFault,
    sysNodeLatch,
    rtNodeLatch,
    mtrNodeLatch,
    rtPresent,
    hasLiveTraffic,
    any,
    label,
    causes,
    detail: any
      ? `ESTOP: ${causes.join(' · ')}`
      : hasLiveTraffic
        ? 'ESTOP clear — no host latch, no recent 0x001, SYS/RT not reporting ESTOP'
        : 'No bus telemetry — safety state unconfirmed',
  }
}

export type OverallHealth = 'healthy' | 'degraded' | 'fault' | 'offline'

export function busActivityTone(activity?: string): 'ok' | 'warn' | 'muted' | 'danger' {
  const a = (activity || '').toLowerCase()
  if (a === 'active' || a === 'rx' || a === 'tx' || a === 'live') return 'ok'
  if (a === 'idle' || a === 'quiet') return 'warn'
  if (a === 'error' || a === 'fault' || a === 'overflow') return 'danger'
  return 'muted' // unseen / —
}

export function shortHash(h: string | null | undefined, n = 12): string {
  if (!h) return '—'
  return h.length > n ? `${h.slice(0, n)}…` : h
}

/** 3-Tier Speed Pipeline Signals */
export function getSpeedPipeline(messages: MessageState[]) {
  const hostDrive = findMsg(messages, 'HOST_DRIVE_CMD')
  const rtDrive = findMsg(messages, 'RT_DRIVE_CMD')
  const sysThrottle = findMsg(messages, 'SYS_THROTTLE_STS')
  const motorFbk = findMsg(messages, 'MTR_MOTOR_FBK')
  const wheelSts = findMsg(messages, 'RT_WHEEL_SPEED_STS')

  const hostSpeed = signalNum(hostDrive, 'speed_mmps')
  const hostGear = signalText(hostDrive, 'gear')

  const rtSpeed =
    signalNum(rtDrive, 'motor_speed_mmps') ?? signalNum(rtDrive, 'speed_mmps')
  const rtGear = signalText(rtDrive, 'gear')

  const manualSpeed = signalNum(sysThrottle, 'speed_mmps')
  const mtrFbkSpeed =
    signalNum(motorFbk, 'motor_command_speed_mmps') ??
    signalNum(motorFbk, 'speed_mmps')
  const mtrGear = signalText(motorFbk, 'gear_state')
  const physicalSpeed = signalNum(wheelSts, 'measured_speed_mmps')

  return {
    hostDrive,
    rtDrive,
    sysThrottle,
    motorFbk,
    wheelSts,
    hostSpeed,
    hostGear,
    rtSpeed,
    rtGear,
    manualSpeed,
    mtrFbkSpeed,
    mtrGear,
    physicalSpeed,
  }
}

/** 3-Tier Steering Pipeline Signals */
export function getSteeringPipeline(messages: MessageState[]) {
  const hostDrive = findMsg(messages, 'HOST_DRIVE_CMD')
  const hostSteer = findMsg(messages, 'HOST_STEER_CMD')
  const rtSesReq = findMsg(messages, 'VCU_SES_REQ')
  const rtState = findMsg(messages, 'RT_STATE_RPT')
  const sesStatus = findMsg(messages, 'SES_STATUS')
  const steerDiag = findMsg(messages, 'STEER_DIAG')

  const hostYawRate = signalNum(hostDrive, 'yaw_rate_mrad_s')
  const hostAngleRaw = signalNum(hostSteer, 'steer_angle_0_1deg')
  const hostSteerDeg = hostAngleRaw != null ? hostAngleRaw * 0.1 : null

  const rtAngleRaw = signalNum(rtSesReq, 'target_angle_raw')
  const rtTargetAngleDeg =
    rtAngleRaw != null
      ? (rtAngleRaw >= 10000 ? (rtAngleRaw - 30000) * 0.1 : rtAngleRaw * 0.1)
      : null
  const rtTargetSlewRate = signalNum(rtSesReq, 'target_speed_raw')
  const rtSteerStateCode = signalNum(rtState, 'steer_state')
  const steerStateLabels: Record<number, string> = {
    0: 'BOOT_WAIT',
    1: 'LISTEN_SYNC',
    2: 'ACTIVE',
    3: 'RAMP_TO_ZERO',
    4: 'HOLD_SILENT',
    5: 'FAULT',
  }
  const rtSteerState =
    rtSteerStateCode != null
      ? steerStateLabels[rtSteerStateCode] ?? `STATE_${rtSteerStateCode}`
      : '—'

  const sesAngleDeg =
    signalNum(sesStatus, 'angle_deg') ??
    signalNum(sesStatus, 'steer_angle_deg') ??
    signalNum(sesStatus, 'angle') ??
    (signalNum(sesStatus, 'steering_angle_raw') != null
      ? (signalNum(sesStatus, 'steering_angle_raw')! * 0.1 - 3000.0)
      : null)
  const sesTorqueNm =
    signalNum(sesStatus, 'torque_nm') ??
    signalNum(sesStatus, 'steering_torque_raw')
  const sesModeCode = signalNum(sesStatus, 'control_mode')
  const sesMode = sesModeCode === 1 ? 'AUTO' : sesModeCode === 0 ? 'MANUAL' : '—'
  const sesErrorCode = signalNum(sesStatus, 'error_status')
  const diagAngle = signalNum(steerDiag, 'angle_0_1deg')

  return {
    hostDrive,
    hostSteer,
    rtSesReq,
    rtState,
    sesStatus,
    steerDiag,
    hostYawRate,
    hostSteerDeg,
    rtTargetAngleDeg,
    rtTargetSlewRate,
    rtSteerState,
    sesAngleDeg,
    sesTorqueNm,
    sesMode,
    sesErrorCode,
    diagAngle,
  }
}

/** 3-Tier Brake Pipeline Signals */
export function getBrakePipeline(messages: MessageState[]) {
  const hostBrake = findMsg(messages, 'HOST_BRAKE_REQ')
  const rtBrake = findMsg(messages, 'RT_BRAKE_CMD')
  const sysSebReq = findMsg(messages, 'VCU_SEB_REQ')
  const sebStatus = findMsg(messages, 'SEB_STATUS')
  const brakeDiag = findMsg(messages, 'BRAKE_DIAG')

  const hostPressureKpa = signalNum(hostBrake, 'brake_pressure_kpa')
  const rtPressureKpa = signalNum(rtBrake, 'brake_pressure_kpa')

  const sebReqPressureRaw = signalNum(sysSebReq, 'pressure_request_raw')
  const sebReqStrokeRaw = signalNum(sysSebReq, 'stroke_request_raw')
  const sysPressureKpa =
    signalNum(sysSebReq, 'brake_pressure_kpa') ??
    (sebReqPressureRaw != null ? sebReqPressureRaw * 50 : null)
  const sysStrokeMm =
    signalNum(sysSebReq, 'stroke_mm') ??
    (sebReqStrokeRaw != null ? sebReqStrokeRaw * 0.05 : null)
  const sysControlMode = signalNum(sysSebReq, 'control_mode') // 0=Stroke, 1=Pressure

  const sebFbkPressureRaw = signalNum(sebStatus, 'pressure_value_raw')
  const sebFbkStrokeRaw = signalNum(sebStatus, 'stroke_value_raw')
  const actualPressureKpa =
    signalNum(sebStatus, 'pressure_kpa') ??
    (sebFbkPressureRaw != null ? sebFbkPressureRaw * 50 : null) ??
    signalNum(brakeDiag, 'pressure_raw')
  const actualStrokeMm =
    signalNum(sebStatus, 'stroke_mm') ??
    (sebFbkStrokeRaw != null ? sebFbkStrokeRaw * 0.05 : null)

  const compositeBrakeKpa =
    actualPressureKpa ??
    sysPressureKpa ??
    rtPressureKpa ??
    hostPressureKpa

  return {
    hostBrake,
    rtBrake,
    sysSebReq,
    sebStatus,
    brakeDiag,
    hostPressureKpa,
    rtPressureKpa,
    sysPressureKpa,
    sysStrokeMm,
    sysControlMode,
    actualPressureKpa,
    actualStrokeMm,
    compositeBrakeKpa,
  }
}

/** 3-Tier Authority & Power/Safety Pipeline Signals */
export function getAuthorityPipeline(messages: MessageState[]) {
  const hmiMode = findMsg(messages, 'HMI_MODE_REQ')
  const hmiPwr = findMsg(messages, 'HMI_PWR_REQ')
  const hostLight = findMsg(messages, 'HOST_LIGHT_CMD')
  const rtState = findMsg(messages, 'RT_STATE_RPT')
  const sysMode = findMsg(messages, 'SYS_MODE_CMD')
  const sysPwr = findMsg(messages, 'SYS_PWR_CMD')
  const sysSafety = findMsg(messages, 'SYS_SAFETY_STS')

  const hmiReqMode = signalText(hmiMode, 'req_mode')
  const hmiReqStart = signalText(hmiPwr, 'req_start')
  const rtReportedMode = signalText(rtState, 'mode')
  const sysCommandedMode = signalText(sysMode, 'mode')
  const sysCommandedPower = signalText(sysPwr, 'power_state')

  const safetyEstop = signalIsOn(sysSafety, 'estop_active')
  const lightBrake = signalIsOn(sysSafety, 'light_brake')
  const lightHead = signalIsOn(sysSafety, 'light_head')
  const lightLeft = signalIsOn(sysSafety, 'light_left')
  const lightRight = signalIsOn(sysSafety, 'light_right')

  return {
    hmiMode,
    hmiPwr,
    hostLight,
    rtState,
    sysMode,
    sysPwr,
    sysSafety,
    hmiReqMode,
    hmiReqStart,
    rtReportedMode,
    sysCommandedMode,
    sysCommandedPower,
    safetyEstop,
    lightBrake,
    lightHead,
    lightLeft,
    lightRight,
  }
}

/** Confirmed vehicle gear from CAN signals (MTR feedback, RT drive cmd, Host drive cmd). Disconnected -> '—' */
export function getVehicleGear(messages: MessageState[]): string {
  const mtrFbk = findMsg(messages, 'MTR_MOTOR_FBK')
  const rtDrive = findMsg(messages, 'RT_DRIVE_CMD')
  const hostDrive = findMsg(messages, 'HOST_DRIVE_CMD')

  if (mtrFbk && frameRecent(mtrFbk)) {
    const g = signalText(mtrFbk, 'gear_state')
    if (g && g !== '—') return g
  }
  if (rtDrive && frameRecent(rtDrive)) {
    const g = signalText(rtDrive, 'gear')
    if (g && g !== '—') return g
  }
  if (hostDrive && frameRecent(hostDrive)) {
    const g = signalText(hostDrive, 'gear')
    if (g && g !== '—') return g
  }
  return '—'
}

export type ControllerModes = {
  sys: string
  rt: string
  mtr: string
  hmi?: string
}

/** Drive mode of each controller (SYS, RT, MTR) from real CAN messages */
export function getControllerModes(messages: MessageState[]): ControllerModes {
  const sysMode = findMsg(messages, 'SYS_MODE_CMD')
  const sysDiag = findMsg(messages, 'SYS_DIAG_RPT')
  const sysHb = findMsg(messages, 'SYS_HEARTBEAT')
  const rtState = findMsg(messages, 'RT_STATE_RPT')
  const mtrFbk = findMsg(messages, 'MTR_MOTOR_FBK')
  const mtrStatus = findMsg(messages, 'MTR_NODE_STATUS')
  const hmiMode = findMsg(messages, 'HMI_MODE_REQ')

  let sys = '—'
  if (sysMode && frameRecent(sysMode)) {
    const m = signalText(sysMode, 'mode')
    if (m && m !== '—') sys = m.toUpperCase()
  } else if (sysDiag && frameRecent(sysDiag)) {
    const m = signalText(sysDiag, 'mode')
    if (m && m !== '—') sys = m.toUpperCase()
  } else if (sysHb && frameRecent(sysHb)) {
    const auto = sysHb.signals?.mode_auto?.engineering_value
    sys = auto === 1 || auto === '1' || String(auto).toLowerCase() === 'true' ? 'AUTO' : 'MANUAL'
  }

  let rt = '—'
  if (rtState && frameRecent(rtState)) {
    const m = signalText(rtState, 'mode')
    if (m && m !== '—') rt = m.toUpperCase()
  }

  let mtr = '—'
  const isMtrLive =
    (mtrFbk && frameRecent(mtrFbk)) || (mtrStatus && frameRecent(mtrStatus))
  if (isMtrLive) {
    const estop = mtrStatus?.signals?.estop_active?.engineering_value === 1
    const fault = (signalNum(mtrFbk, 'fault_flags') ?? 0) > 0
    if (estop) mtr = 'ESTOP'
    else if (fault) mtr = 'FAULT'
    else if (sys !== '—') mtr = sys
    else mtr = 'READY'
  }

  let hmi = '—'
  if (hmiMode && frameRecent(hmiMode)) {
    const m = signalText(hmiMode, 'req_mode')
    if (m && m !== '—') hmi = m.toUpperCase()
  }

  return { sys, rt, mtr, hmi }
}

export type VehiclePowerInfo = {
  state: 'ON' | 'OFF' | '—'
  detail: string
}

/** Vehicle power state from SYS_PWR_CMD, HMI_PWR_REQ, and session */
export function getVehiclePower(
  messages: MessageState[],
  ses?: { confirmed_power?: string | null; requested_power?: string | null } | null,
): VehiclePowerInfo {
  const sysPwr = findMsg(messages, 'SYS_PWR_CMD')
  const hmiPwr = findMsg(messages, 'HMI_PWR_REQ')

  const sysLive = sysPwr && frameRecent(sysPwr)
  const hmiLive = hmiPwr && frameRecent(hmiPwr)

  const sysVal = sysLive ? signalText(sysPwr, 'power_state') : null
  const hmiVal = hmiLive ? signalText(hmiPwr, 'req_start') : null

  if (sysVal === 'ON' || hmiVal === 'ON' || ses?.confirmed_power === 'ON') {
    return { state: 'ON', detail: 'Drive power energized (SYS/HMI ON)' }
  }
  if (
    sysVal === 'OFF' ||
    hmiVal === 'OFF' ||
    ses?.confirmed_power === 'OFF' ||
    ses?.requested_power === 'OFF'
  ) {
    return { state: 'OFF', detail: 'Drive power isolated' }
  }
  return { state: '—', detail: 'No power telemetry active' }
}

export type IndicatorStatus = 'ok' | 'warn' | 'danger' | 'info' | 'muted'

export type DiagnosticItem = {
  id: string
  label: string
  status: IndicatorStatus
  valueText?: string
  tooltip: string
}

/** Comprehensive Diagnostic Indicators for Quick-Check Strip and In-Card Badges */
export function getDiagnosticIndicators(messages: MessageState[]) {
  const sesStatus = findMsg(messages, 'SES_STATUS')
  const sebStatus = findMsg(messages, 'SEB_STATUS')
  const rtDiag = findMsg(messages, 'RT_DIAG_RPT')
  const sysPwr = findMsg(messages, 'SYS_PWR_CMD')
  const sysDiag = findMsg(messages, 'SYS_DIAG_RPT')
  const hostObs = findMsg(messages, 'HOST_OBSTACLE_DIST')
  const rtDrive = findMsg(messages, 'RT_DRIVE_CMD')
  const mtrFbk = findMsg(messages, 'MTR_MOTOR_FBK')
  const hostSteer = findMsg(messages, 'HOST_STEER_CMD')
  const rtSesReq = findMsg(messages, 'VCU_SES_REQ')
  const wheelSts = findMsg(messages, 'RT_WHEEL_SPEED_STS')
  const sysSafety = findMsg(messages, 'SYS_SAFETY_STS')

  // Actuator message presence
  const sesHasMsg = sesStatus != null && frameRecent(sesStatus)
  const sebHasMsg = sebStatus != null && frameRecent(sebStatus)
  const rtDiagHasMsg = rtDiag != null && frameRecent(rtDiag)
  const sysPwrHasMsg = sysPwr != null && frameRecent(sysPwr)
  const sysDiagHasMsg = sysDiag != null && frameRecent(sysDiag)
  const hostObsHasMsg = hostObs != null && frameRecent(hostObs)
  const rtDriveHasMsg = rtDrive != null && frameRecent(rtDrive)
  const mtrFbkHasMsg = mtrFbk != null && frameRecent(mtrFbk)
  const hostSteerHasMsg = hostSteer != null && frameRecent(hostSteer)
  const rtSesReqHasMsg = rtSesReq != null && frameRecent(rtSesReq)
  const wheelStsHasMsg = wheelSts != null && frameRecent(wheelSts)
  const sysSafetyHasMsg = sysSafety != null && frameRecent(sysSafety)

  // SES Error Level (0x201 error_status)
  const sesErrLevel = sesHasMsg ? signalNum(sesStatus, 'error_status') : null
  const sesAligned = sesHasMsg
    ? signalIsOn(sesStatus, 'ses_aligned') ||
      signalIsOn(sesStatus, 'aligned') ||
      signalIsOn(sesStatus, 'is_aligned')
    : null

  // SEB Error Level (0x721 error_status: 0=no fault, 1=L1, 2=L2, 3=L3)
  const sebErrLevel = sebHasMsg ? signalNum(sebStatus, 'error_status') : null
  const sebAligned = sebHasMsg
    ? signalIsOn(sebStatus, 'seb_aligned') ||
      signalIsOn(sebStatus, 'aligned') ||
      signalIsOn(sebStatus, 'is_aligned')
    : null

  // SEB Fallback State (0x620 brake_fallback_state: 0=NORMAL, 1=SYS_DEGRADED, 2=EMERGENCY_FALLBACK)
  const sebFallbackCode = rtDiagHasMsg ? signalNum(rtDiag, 'brake_fallback_state') : null

  // MTR Contactor (0x113 power_state)
  const mtrPowerState = sysPwrHasMsg ? signalText(sysPwr, 'power_state') : null
  const mtrContactorClosed =
    mtrPowerState != null && (mtrPowerState === 'ON' || mtrPowerState === '1')
      ? true
      : mtrPowerState != null && mtrPowerState !== '—'
        ? false
        : null

  // Brake lever engaged (0x600 brake_engaged)
  const leverEngaged = sysDiagHasMsg ? signalIsOn(sysDiag, 'brake_engaged') : null

  // EGAS Level 2 Consistency (RT_DRIVE_CMD 0x204 speed vs MTR_MOTOR_FBK 0x206 speed)
  const rtSpd = rtDriveHasMsg
    ? (signalNum(rtDrive, 'motor_speed_mmps') ?? signalNum(rtDrive, 'speed_mmps'))
    : null
  const mtrSpd = mtrFbkHasMsg
    ? (signalNum(mtrFbk, 'motor_command_speed_mmps') ?? signalNum(mtrFbk, 'speed_mmps'))
    : null
  const egasDiff =
    rtSpd != null && mtrSpd != null ? Math.abs(mtrSpd - rtSpd) : null
  const egasConsistent = egasDiff != null ? egasDiff <= 250 : null

  // Obstacle distance (0x400 distance_mm)
  const obsDistMm = hostObsHasMsg ? signalNum(hostObs, 'distance_mm') : null

  // CAN Controllers & Errors
  const twaiTec = sysDiagHasMsg ? signalNum(sysDiag, 'tec') : null
  const twaiRec = sysDiagHasMsg ? signalNum(sysDiag, 'rec') : null
  const twaiRxOverflow = sysDiagHasMsg ? signalNum(sysDiag, 'rx_overflow') : null

  const mcpTec = rtDiagHasMsg ? signalNum(rtDiag, 'mcp_tec') : null
  const mcpRec = rtDiagHasMsg ? signalNum(rtDiag, 'mcp_rec') : null
  const mcpBusOff = rtDiagHasMsg ? signalIsOn(rtDiag, 'mcp_bus_off') : null
  const mcpRecovering = rtDiagHasMsg ? signalIsOn(rtDiag, 'mcp_recovering') : null

  // Relays
  const headlamp = sysSafetyHasMsg ? signalIsOn(sysSafety, 'light_head') : null
  const brakelamp = sysSafetyHasMsg ? signalIsOn(sysSafety, 'light_brake') : null
  const turnLeft = sysSafetyHasMsg ? signalIsOn(sysSafety, 'light_left') : null
  const turnRight = sysSafetyHasMsg ? signalIsOn(sysSafety, 'light_right') : null

  // In-card signals
  const hostAngleValid = hostSteerHasMsg ? signalIsOn(hostSteer, 'angle_valid') : null
  const rtCtrlEn = rtSesReqHasMsg ? signalIsOn(rtSesReq, 'control_enable') : null
  const rtAlignEn = rtSesReqHasMsg ? signalIsOn(rtSesReq, 'alignment_enable') : null
  const sebAutoBrake = sebHasMsg ? signalIsOn(sebStatus, 'auto_brake_status') : null
  const mtrFaultFlags = mtrFbkHasMsg ? signalNum(mtrFbk, 'fault_flags') : null
  const rawWheelState = wheelStsHasMsg ? signalText(wheelSts, 'sensor_state') : null
  const wheelSensorState =
    rawWheelState && rawWheelState !== '—' ? rawWheelState : null

  return {
    // Pod 1: Actuator Sync & Calibration
    actuators: [
      {
        id: 'ses-align',
        label: 'SES Aligned',
        status: sesAligned === null ? 'muted' : sesAligned ? 'ok' : 'warn',
        valueText:
          sesAligned === null ? '—' : sesAligned ? 'Calibrated' : 'Syncing',
        tooltip:
          'SES SES Steer-by-Wire zero-point calibration status (0x201 SES_STATUS)',
      },
      {
        id: 'ses-err',
        label: 'SES Health',
        status:
          sesErrLevel == null
            ? 'muted'
            : sesErrLevel === 0
              ? 'ok'
              : sesErrLevel === 1
                ? 'warn'
                : 'danger',
        valueText:
          sesErrLevel == null
            ? '—'
            : sesErrLevel === 0
              ? 'Normal'
              : `L${sesErrLevel}`,
        tooltip:
          'SES SES Actuator Fault Level (0=Normal, 1=L1 Warning, 2=L2 Degraded, 3=L3 Fault) (0x201)',
      },
      {
        id: 'seb-align',
        label: 'SEB Aligned',
        status: sebAligned === null ? 'muted' : sebAligned ? 'ok' : 'warn',
        valueText:
          sebAligned === null ? '—' : sebAligned ? 'Calibrated' : 'Syncing',
        tooltip:
          'SEB Brake-by-Wire stroke calibration status (0x721 SEB_STATUS)',
      },
      {
        id: 'seb-fallback',
        label: 'SEB Fallback',
        status:
          sebFallbackCode == null
            ? 'muted'
            : sebFallbackCode === 0
              ? 'ok'
              : sebFallbackCode === 1
                ? 'warn'
                : 'danger',
        valueText:
          sebFallbackCode == null
            ? '—'
            : sebFallbackCode === 0
              ? 'Normal'
              : sebFallbackCode === 1
                ? 'Degraded'
                : 'Fallback',
        tooltip:
          'SEB Brakes Fallback Authority (0=NORMAL SYS authority, 1=SYS_DEGRADED, 2=RT EMERGENCY_FALLBACK) (0x620)',
      },
      {
        id: 'mtr-contactor',
        label: 'MTR Contactor',
        status: mtrContactorClosed == null ? 'muted' : mtrContactorClosed ? 'ok' : 'muted',
        valueText:
          mtrContactorClosed == null
            ? '—'
            : mtrContactorClosed
              ? 'Closed (ON)'
              : 'Open (Safe)',
        tooltip:
          'MTR Motor 12V Main Power Relay / Contactor Command (0x113 SYS_PWR_CMD)',
      },
    ] as DiagnosticItem[],

    // Pod 2: Safety Interlocks
    interlocks: [
      {
        id: 'brake-lever',
        label: 'Brake Lever',
        status: leverEngaged == null ? 'muted' : leverEngaged ? 'warn' : 'ok',
        valueText:
          leverEngaged == null
            ? '—'
            : leverEngaged
              ? 'Pulled (Override)'
              : 'Released',
        tooltip:
          'Handlebar physical brake lever sensor (SYS GPIO2 active-low pull)',
      },
      {
        id: 'egas-l2',
        label: 'EGAS L2',
        status:
          egasConsistent == null
            ? 'muted'
            : egasConsistent
              ? 'ok'
              : 'danger',
        valueText:
          egasDiff == null
            ? '—'
            : egasConsistent
              ? `Δ ${egasDiff.toFixed(0)} mm/s`
              : `FAULT Δ ${egasDiff.toFixed(0)}`,
        tooltip:
          'EGAS Level 2 speed consistency check (RT 0x204 command vs MTR 0x206 echo)',
      },
      {
        id: 'obstacle-dist',
        label: 'Obstacle Guard',
        status:
          obsDistMm == null
            ? 'muted'
            : obsDistMm > 3000
              ? 'ok'
              : obsDistMm > 300
                ? 'warn'
                : 'danger',
        valueText:
          obsDistMm == null
            ? '—'
            : obsDistMm >= 4000
              ? '> 4.0 m'
              : `${(obsDistMm / 1000).toFixed(1)} m`,
        tooltip:
          'Perception obstacle collision distance (0x400 HOST_OBSTACLE_DIST, <0.3m triggers stop)',
      },
    ] as DiagnosticItem[],

    // Pod 3: CAN Controllers & Errors
    canControllers: [
      {
        id: 'twai-tec-rec',
        label: 'Low TWAI Errors',
        status:
          twaiTec == null || twaiRec == null
            ? 'muted'
            : twaiTec === 0 && twaiRec === 0
              ? 'ok'
              : twaiTec < 128 && twaiRec < 128
                ? 'warn'
                : 'danger',
        valueText:
          twaiTec == null || twaiRec == null
            ? '—'
            : `TEC ${twaiTec} · REC ${twaiRec}`,
        tooltip:
          'ESP32-S3 Low CAN TWAI Hardware Transmit & Receive Error Counters (0x600 SYS_DIAG_RPT)',
      },
      {
        id: 'twai-rx-ovf',
        label: 'TWAI RX Queue',
        status:
          twaiRxOverflow == null
            ? 'muted'
            : twaiRxOverflow === 0
              ? 'ok'
              : 'warn',
        valueText:
          twaiRxOverflow == null
            ? '—'
            : twaiRxOverflow === 0
              ? '0 ovf'
              : `${twaiRxOverflow} dropped`,
        tooltip:
          'FreeRTOS TWAI RX Queue overflow count (0x600 SYS_DIAG_RPT)',
      },
      {
        id: 'mcp-tec-rec',
        label: 'High MCP Errors',
        status:
          mcpTec == null || mcpRec == null
            ? 'muted'
            : mcpBusOff
              ? 'danger'
              : mcpTec === 0 && mcpRec === 0
                ? 'ok'
                : 'warn',
        valueText:
          mcpTec == null || mcpRec == null
            ? '—'
            : mcpBusOff
              ? 'BUS-OFF'
              : `TEC ${mcpTec} · REC ${mcpRec}`,
        tooltip:
          'MCP2515 SPI High CAN Transmit & Receive Error Counters (0x620 RT_DIAG_RPT)',
      },
      {
        id: 'mcp-recovery',
        label: 'MCP Bus-Off',
        status:
          mcpBusOff == null || mcpRecovering == null
            ? 'muted'
            : mcpBusOff
              ? 'danger'
              : mcpRecovering
                ? 'warn'
                : 'ok',
        valueText:
          mcpBusOff == null || mcpRecovering == null
            ? '—'
            : mcpBusOff
              ? 'Bus Off'
              : mcpRecovering
                ? 'Recovering'
                : 'Active',
        tooltip:
          'MCP2515 SPI High CAN Bus-Off recovery state (0x620 RT_DIAG_RPT)',
      },
    ] as DiagnosticItem[],

    // Pod 4: Relays & Lighting
    relays: [
      {
        id: 'relay-headlight',
        label: 'Headlight',
        status: headlamp == null ? 'muted' : headlamp ? 'info' : 'muted',
        valueText: headlamp == null ? '—' : headlamp ? 'ON' : 'OFF',
        tooltip:
          'SYS 12V Headlight relay output GPIO10 (0x011 SYS_SAFETY_STS)',
      },
      {
        id: 'relay-brake',
        label: 'Brake Light',
        status: brakelamp == null ? 'muted' : brakelamp ? 'warn' : 'muted',
        valueText: brakelamp == null ? '—' : brakelamp ? 'ON' : 'OFF',
        tooltip:
          'SYS 12V Brake Light relay output GPIO21 (0x011 SYS_SAFETY_STS)',
      },
      {
        id: 'relay-turn-l',
        label: 'Turn L',
        status: turnLeft == null ? 'muted' : turnLeft ? 'info' : 'muted',
        valueText: turnLeft == null ? '—' : turnLeft ? 'BLINK' : 'OFF',
        tooltip:
          'SYS 12V Left turn signal relay GPIO18 (0x011 SYS_SAFETY_STS)',
      },
      {
        id: 'relay-turn-r',
        label: 'Turn R',
        status: turnRight == null ? 'muted' : turnRight ? 'info' : 'muted',
        valueText: turnRight == null ? '—' : turnRight ? 'BLINK' : 'OFF',
        tooltip:
          'SYS 12V Right turn signal relay GPIO19 (0x011 SYS_SAFETY_STS)',
      },
    ] as DiagnosticItem[],

    // In-card contextual items
    inCard: {
      hostAngleValid,
      rtCtrlEn,
      rtAlignEn,
      sebAutoBrake,
      mtrFaultFlags,
      wheelSensorState,
      sesAligned,
      sebAligned,
      sesErrLevel,
      sebErrLevel,
      mtrContactorClosed,
      egasConsistent,
      leverEngaged,
    },
  }
}
