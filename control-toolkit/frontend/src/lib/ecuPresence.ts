import type { MessageState, TopologyNode } from '../store'

/**
 * ECU presence probes for the topbar strip.
 * Status frames (not only heartbeats) mark SES/SEB connected when present on Low.
 *
 * Lamp colors (Topbar):
 *  - green  = live and clean
 *  - yellow = late, or live/late with errors/faults
 *  - red    = offline / missing / dead
 *  - muted  = unknown
 */
export type EcuProbe = {
  node: string
  short: string
  bus: 'high' | 'low'
  can_id: number
  names?: string[]
  title: string
  /**
   * Fault detection while the unit is still present.
   * - `onIfTruthy`: non-zero / true = error
   * - `onIfFalsy`: false / 0 = error (health flags)
   * Companions: extra frames that report faults for this ECU.
   */
  faults?: {
    onIfTruthy?: string[]
    onIfFalsy?: string[]
    companions?: Array<{
      bus: 'high' | 'low'
      can_id: number
      names?: string[]
      onIfTruthy?: string[]
      onIfFalsy?: string[]
      /** Any live companion frame counts as degraded (opaque err bitmap). */
      anyLive?: boolean
    }>
  }
}

export const ECU_PROBES: readonly EcuProbe[] = [
  {
    node: 'Host',
    short: 'Host',
    bus: 'high',
    can_id: 0x7fc,
    names: ['HOST_HEARTBEAT'],
    title: 'Host · heartbeat 0x7FC High',
  },
  {
    node: 'RT_high',
    short: 'RT-H',
    bus: 'high',
    can_id: 0x7fd,
    names: ['RT_HEARTBEAT'],
    title: 'RT · heartbeat 0x7FD High',
    faults: {
      companions: [
        {
          bus: 'high',
          can_id: 0x210,
          names: ['RT_STATE_RPT'],
          onIfTruthy: ['estop_reason', 'rx_overflow'],
        },
      ],
    },
  },
  {
    node: 'RT_low',
    short: 'RT-L',
    bus: 'low',
    can_id: 0x7fd,
    names: ['RT_HEARTBEAT'],
    title: 'RT · heartbeat 0x7FD Low',
    faults: {
      companions: [
        {
          bus: 'low',
          can_id: 0x210,
          names: ['RT_STATE_RPT'],
          onIfTruthy: ['estop_reason', 'rx_overflow'],
        },
      ],
    },
  },
  {
    node: 'SYS',
    short: 'SYS',
    bus: 'low',
    can_id: 0x7fe,
    names: ['SYS_HEARTBEAT'],
    title: 'SYS · heartbeat 0x7FE Low',
    faults: {
      onIfTruthy: ['estop_active'],
      onIfFalsy: [
        'heartbeat_ok',
        'can_ok',
        'task_safety_ok',
        'task_brake_ok',
        'task_dispatch_ok',
        'task_can_tx_ok',
      ],
      companions: [
        {
          bus: 'low',
          can_id: 0x11,
          names: ['SYS_SAFETY_STS'],
          onIfTruthy: ['estop_active'],
          onIfFalsy: ['heartbeat_ok'],
        },
        {
          bus: 'low',
          can_id: 0x600,
          names: ['SYS_DIAG_RPT'],
          onIfTruthy: ['brake_fault', 'estop_active', 'rx_overflow'],
          onIfFalsy: ['heartbeat_ok'],
        },
      ],
    },
  },
  {
    node: 'MTR',
    short: 'MTR',
    bus: 'low',
    can_id: 0x206,
    names: ['MTR_MOTOR_FBK'],
    title: 'MTR · motor feedback 0x206 Low',
    faults: {
      onIfTruthy: ['fault_flags'],
    },
  },
  {
    node: 'SES',
    short: 'SBW',
    bus: 'low',
    can_id: 0x201,
    names: ['SES_STATUS'],
    title: 'Steering-by-wire (SES / EPS-C) · SES_STATUS 0x201 Low',
    faults: {
      onIfTruthy: ['error_status'],
      companions: [
        {
          bus: 'low',
          can_id: 0x202,
          names: ['SES_ERR_INFO', 'SES_ErrInfo'],
          anyLive: true,
          onIfTruthy: [
            'can_com_err',
            'ecu_temp_err',
            'ecu_under_volt',
            'ecu_over_volt',
            'domain_v',
            'domain_t',
            'alignment_fault',
            'over_angle',
            'str_mtr_stall',
            'mtr_curt_fault',
          ],
        },
      ],
    },
  },
  {
    node: 'SEB',
    short: 'BBW',
    bus: 'low',
    can_id: 0x721,
    names: ['SEB_STATUS'],
    title: 'Brake-by-wire (SEB) · SEB_STATUS 0x721 Low',
    faults: {
      onIfTruthy: ['error_status'],
      companions: [
        {
          bus: 'low',
          can_id: 0x731,
          names: ['SEB_ERR_INFO', 'SEB_ErrInfo'],
          anyLive: true,
          onIfTruthy: ['raw'],
        },
      ],
    },
  },
]

export type EcuPresence = {
  node: string
  short: string
  bus: string
  can_id: number
  /** offline | missing | late | live | degraded | fault */
  liveness: string
  /** Human reasons when degraded/fault (tooltip). */
  issues: string[]
  title: string
}

function freshnessKey(freshness?: string | null): string {
  return String(freshness || '').toLowerCase()
}

function isPresentFreshness(f: string): boolean {
  return f === 'live' || f === 'late' || f === 'invalid' || f === 'recovering'
}

function findMessage(
  messages: MessageState[],
  bus: string,
  can_id: number,
  names?: string[],
): MessageState | undefined {
  const byId = messages.find((m) => m.bus === bus && m.can_id === can_id)
  if (byId) return byId
  if (names?.length) {
    return messages.find((m) => m.bus === bus && names.includes(m.name || ''))
  }
  return undefined
}

function signalEng(msg: MessageState, key: string): unknown {
  const s = msg.signals?.[key]
  if (!s) return undefined
  if (s.enum_label != null && s.enum_label !== '') return s.enum_label
  return s.engineering_value
}

function isTruthyFaultValue(v: unknown): boolean {
  if (v == null) return false
  if (typeof v === 'boolean') return v
  if (typeof v === 'number') return Number.isFinite(v) && v !== 0
  const t = String(v).trim().toLowerCase()
  if (!t || t === '0' || t === 'false' || t === 'ok' || t === 'none' || t === 'clear' || t === 'off') {
    return false
  }
  if (t === '1' || t === 'true' || t === 'active' || t === 'on' || t === 'fault' || t === 'error') {
    return true
  }
  const n = Number(t)
  return Number.isFinite(n) ? n !== 0 : true
}

function isFalsyHealthValue(v: unknown): boolean {
  if (v == null) return false // unknown → don't flag
  if (typeof v === 'boolean') return !v
  if (typeof v === 'number') return Number.isFinite(v) && v === 0
  const t = String(v).trim().toLowerCase()
  return t === '0' || t === 'false' || t === 'off' || t === 'fail' || t === 'bad' || t === 'no'
}

function collectSignalIssues(
  msg: MessageState,
  onIfTruthy?: string[],
  onIfFalsy?: string[],
): string[] {
  const issues: string[] = []
  for (const key of onIfTruthy || []) {
    if (!(key in (msg.signals || {}))) continue
    if (isTruthyFaultValue(signalEng(msg, key))) issues.push(`${key}=${String(signalEng(msg, key))}`)
  }
  for (const key of onIfFalsy || []) {
    if (!(key in (msg.signals || {}))) continue
    if (isFalsyHealthValue(signalEng(msg, key))) issues.push(`${key}=bad`)
  }
  // Invalid decoded fields on a present frame
  for (const [key, sig] of Object.entries(msg.signals || {})) {
    if (sig && sig.valid === false) issues.push(`${key}:invalid`)
  }
  return issues
}

function rtModeEstop(msg: MessageState): boolean {
  const mode = String(
    msg.signals?.mode?.enum_label ?? msg.signals?.mode?.engineering_value ?? '',
  )
    .trim()
    .toUpperCase()
  return mode === 'ESTOP' || mode === '2'
}

function detectIssues(
  probe: EcuProbe,
  messages: MessageState[],
  presence: MessageState | undefined,
): string[] {
  const issues: string[] = []
  if (presence) {
    const vs = String(presence.validation_status || '').toLowerCase()
    if (vs && vs !== 'ok' && vs !== 'unknown' && vs !== 'unknown_id') {
      issues.push(`validation=${presence.validation_status}`)
    }
    if (freshnessKey(presence.freshness) === 'invalid') {
      issues.push('decode invalid')
    }
    issues.push(
      ...collectSignalIssues(
        presence,
        probe.faults?.onIfTruthy,
        probe.faults?.onIfFalsy,
      ),
    )
  }

  for (const c of probe.faults?.companions || []) {
    const m = findMessage(messages, c.bus, c.can_id, c.names)
    if (!m) continue
    const f = freshnessKey(m.freshness)
    if (!isPresentFreshness(f) && f !== 'live' && f !== 'late') continue

    if (c.anyLive && (f === 'live' || f === 'late')) {
      // Opaque err frames: only flag if signal checks fire or raw non-zero
      const sigIssues = collectSignalIssues(m, c.onIfTruthy, c.onIfFalsy)
      if (sigIssues.length) {
        issues.push(`${m.name || c.can_id}:${sigIssues.join(',')}`)
      } else if (c.onIfTruthy?.includes('raw')) {
        // raw blob — skip unless non-empty non-zero
        const raw = signalEng(m, 'raw')
        if (raw != null && String(raw) !== '' && String(raw) !== '0') {
          issues.push(`${m.name || 'err'}:active`)
        }
      }
    } else {
      const sigIssues = collectSignalIssues(m, c.onIfTruthy, c.onIfFalsy)
      if (sigIssues.length) issues.push(`${m.name || c.can_id}:${sigIssues.join(',')}`)
    }

    // RT mode ESTOP is a clear live fault
    if ((m.name || '') === 'RT_STATE_RPT' && rtModeEstop(m)) {
      issues.push('RT mode ESTOP')
    }
  }

  // de-dupe
  return [...new Set(issues)]
}

function livenessFromFreshness(freshness?: string | null): string {
  const f = freshnessKey(freshness)
  if (f === 'live') return 'live'
  if (f === 'late') return 'late'
  if (f === 'missing' || f === 'stale') return 'missing'
  if (f === 'invalid') return 'fault'
  if (f === 'unseen' || !f) return 'offline'
  return f
}

/**
 * Always returns the full unit set. Prefers live CAN messages; falls back to topology.
 * Live + errors → degraded (yellow). Dead/missing → offline/missing (red).
 */
export function buildEcuPresence(
  topology: TopologyNode[],
  messages: MessageState[],
): EcuPresence[] {
  const topoByNode = new Map(topology.map((n) => [n.node, n]))

  return ECU_PROBES.map((probe) => {
    const msg = findMessage(messages, probe.bus, probe.can_id, probe.names)
    const topo = topoByNode.get(probe.node)

    let base = 'offline'
    if (msg) {
      base = livenessFromFreshness(msg.freshness)
    } else if (topo) {
      base = String(topo.liveness || 'offline').toLowerCase()
    }

    const issues =
      base === 'offline' || base === 'missing' || base === 'unseen'
        ? []
        : detectIssues(probe, messages, msg)

    // Topology may already report fault
    if (topo && String(topo.liveness || '').toLowerCase() === 'fault' && base !== 'offline') {
      if (!issues.includes('topology fault')) issues.push('topology fault')
    }
    if (topo?.validation_status) {
      const vs = String(topo.validation_status).toLowerCase()
      if (vs && vs !== 'ok' && vs !== 'unknown' && vs !== 'unknown_id') {
        if (!issues.some((i) => i.startsWith('validation'))) {
          issues.push(`validation=${topo.validation_status}`)
        }
      }
    }

    let liveness = base
    // Present with problems → yellow (degraded), not green.
    if ((base === 'live' || base === 'late' || base === 'fault') && issues.length > 0) {
      liveness = 'degraded'
    } else if (base === 'fault' && issues.length === 0) {
      // invalid decode without extra issues still yellow while frames arrive
      liveness = msg && isPresentFreshness(freshnessKey(msg.freshness)) ? 'degraded' : 'fault'
    }

    const age =
      msg?.age_ms != null && Number.isFinite(msg.age_ms)
        ? ` · age ${Math.round(msg.age_ms)} ms`
        : ''
    const issueTxt = issues.length ? ` · ${issues.slice(0, 4).join('; ')}` : ''

    return {
      node: probe.node,
      short: probe.short,
      bus: probe.bus,
      can_id: probe.can_id,
      liveness,
      issues,
      title: `${probe.title} · ${liveness}${age}${issueTxt}`,
    }
  })
}
