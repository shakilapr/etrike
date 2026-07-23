import type { MessageState } from '../store'

export const PROFILE_LABELS: Record<string, string> = {
  pure_software: 'Computer · Virtual',
  bench_test: 'Real · CANalyst Bench',
  full_vehicle: 'Real · CANalyst Vehicle',
}

/** Session profile → transport mode shown in Settings toggle. */
export function transportModeOf(profile: string | undefined | null): 'computer' | 'real' {
  if (profile === 'bench_test' || profile === 'full_vehicle') return 'real'
  return 'computer'
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
  rtMode: string
  safetyState: number | null
  sysHeartbeatBad: boolean
  sysCanBad: boolean
  sysBrakeFault: boolean
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
    signalIsOn(sysSafety, 'estop_active') ||
    signalIsOn(sysHb, 'estop_active') ||
    signalIsOn(sysDiag, 'estop_active')
  const sysHeartbeatBad =
    !!sysHb &&
    frameRecent(sysHb) &&
    sysHb.signals?.heartbeat_ok != null &&
    !signalIsOn(sysHb, 'heartbeat_ok')
  const sysCanBad =
    !!sysHb && frameRecent(sysHb) && sysHb.signals?.can_ok != null && !signalIsOn(sysHb, 'can_ok')
  const sysBrakeFault = signalIsOn(sysDiag, 'brake_fault')

  const rtState =
    findMsg(messages, 'RT_STATE_RPT', 'high') ||
    findMsg(messages, 'RT_STATE_RPT', 'low') ||
    findMsg(messages, 'RT_STATE_RPT')
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
    const n = signalNum(rtState, 'estop_reason')
    return n != null && Number.isFinite(n) ? Math.trunc(n) : 0
  })()
  const rtReasonLabel = RT_ESTOP_REASONS[rtReasonCode] ?? `unknown_${rtReasonCode}`
  const safetyState = signalNum(rtState, 'safety_state')

  const causes: string[] = []
  if (hostLatch) causes.push('Host inject latch (Clear latch = host only)')
  if (busHigh) causes.push('0x001 SAFETY_ESTOP on High')
  if (busLow) causes.push('0x001 SAFETY_ESTOP on Low')
  if (sysReported) causes.push('SYS estop_active')
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
    rtModeEstop ||
    rtReasonCode !== 0 ||
    sysHeartbeatBad ||
    sysCanBad ||
    sysBrakeFault

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
    else if (sysReported) label = 'SYS'
    else if (rtModeEstop) label = 'RT ESTOP'
    else if (sysHeartbeatBad || sysCanBad || sysBrakeFault) label = 'SYS fault'
    else label = 'Active'
  }

  return {
    hostLatch,
    busHigh,
    busLow,
    sysReported,
    rtModeEstop,
    rtReasonCode,
    rtReasonLabel,
    rtMode,
    safetyState,
    sysHeartbeatBad,
    sysCanBad,
    sysBrakeFault,
    any,
    label,
    causes,
    detail: any
      ? `ESTOP: ${causes.join(' · ')}`
      : 'ESTOP clear — no host latch, no recent 0x001, SYS/RT not reporting ESTOP',
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
