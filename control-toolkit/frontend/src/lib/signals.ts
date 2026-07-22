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

/**
 * ESTOP is multi-source — never trust only session.estop_active (host inject latch).
 * Sources: host latch, dual-bus SAFETY_ESTOP 0x001, SYS safety/heartbeat, RT mode.
 */
export type EstopObservation = {
  hostLatch: boolean
  busHigh: boolean
  busLow: boolean
  sysReported: boolean
  rtModeEstop: boolean
  any: boolean
  /** Short chip label */
  label: string
  /** Tooltip / detail */
  detail: string
}

export function observeEstop(
  messages: MessageState[],
  ses: { estop_active?: boolean | null } | null | undefined,
): EstopObservation {
  const hostLatch = !!ses?.estop_active
  const busHigh = frameRecent(findMsg(messages, 'SAFETY_ESTOP', 'high'))
  const busLow = frameRecent(findMsg(messages, 'SAFETY_ESTOP', 'low'))
  const sysSafety = findMsg(messages, 'SYS_SAFETY_STS')
  const sysHb = findMsg(messages, 'SYS_HEARTBEAT')
  const sysDiag = findMsg(messages, 'SYS_DIAG_RPT')
  const sysReported =
    signalIsOn(sysSafety, 'estop_active') ||
    signalIsOn(sysHb, 'estop_active') ||
    signalIsOn(sysDiag, 'estop_active')
  const rtState = findMsg(messages, 'RT_STATE_RPT')
  const rtMode =
    String(rtState?.signals?.mode?.enum_label ?? rtState?.signals?.mode?.engineering_value ?? '')
      .trim()
      .toUpperCase()
  const rtModeEstop = rtMode === 'ESTOP' || rtMode === '2'

  const any = hostLatch || busHigh || busLow || sysReported || rtModeEstop

  const parts: string[] = []
  if (hostLatch) parts.push('host latch')
  if (busHigh) parts.push('0x001 high')
  if (busLow) parts.push('0x001 low')
  if (sysReported) parts.push('SYS estop_active')
  if (rtModeEstop) parts.push('RT mode ESTOP')

  let label = 'Clear'
  if (any) {
    if (hostLatch && (busHigh || busLow || sysReported || rtModeEstop)) label = 'Latch+bus'
    else if (hostLatch) label = 'Host latch'
    else if (busHigh && busLow) label = 'Bus H+L'
    else if (busHigh) label = 'Bus High'
    else if (busLow) label = 'Bus Low'
    else if (sysReported && rtModeEstop) label = 'SYS+RT'
    else if (sysReported) label = 'SYS'
    else if (rtModeEstop) label = 'RT ESTOP'
    else label = 'Active'
  }

  return {
    hostLatch,
    busHigh,
    busLow,
    sysReported,
    rtModeEstop,
    any,
    label,
    detail: any
      ? `ESTOP sources: ${parts.join(' · ')}`
      : 'No host latch, no recent 0x001 SAFETY_ESTOP on High/Low, SYS/RT not reporting ESTOP',
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
