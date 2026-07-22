import type { MessageState, TopologyNode } from '../store'

/**
 * ECU presence probes for the topbar strip.
 * Status frames (not only heartbeats) mark SES/SEB connected when present on Low.
 */
export type EcuProbe = {
  node: string
  short: string
  bus: 'high' | 'low'
  can_id: number
  names?: string[]
  title: string
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
  },
  {
    node: 'RT_low',
    short: 'RT-L',
    bus: 'low',
    can_id: 0x7fd,
    names: ['RT_HEARTBEAT'],
    title: 'RT · heartbeat 0x7FD Low',
  },
  {
    node: 'SYS',
    short: 'SYS',
    bus: 'low',
    can_id: 0x7fe,
    names: ['SYS_HEARTBEAT'],
    title: 'SYS · heartbeat 0x7FE Low',
  },
  {
    node: 'MTR',
    short: 'MTR',
    bus: 'low',
    can_id: 0x206,
    names: ['MTR_MOTOR_FBK'],
    title: 'MTR · motor feedback 0x206 Low',
  },
  {
    node: 'SES',
    short: 'SBW',
    bus: 'low',
    can_id: 0x201,
    names: ['SES_STATUS'],
    title: 'Steering-by-wire (SES / EPS-C) · SES_STATUS 0x201 Low',
  },
  {
    node: 'SEB',
    short: 'BBW',
    bus: 'low',
    can_id: 0x721,
    names: ['SEB_STATUS'],
    title: 'Brake-by-wire (SEB) · SEB_STATUS 0x721 Low',
  },
]

export type EcuPresence = {
  node: string
  short: string
  bus: string
  can_id: number
  liveness: string
  title: string
}

function livenessFromFreshness(freshness?: string | null): string {
  const f = String(freshness || '').toLowerCase()
  if (f === 'live') return 'live'
  if (f === 'late') return 'late'
  if (f === 'missing' || f === 'stale') return 'missing'
  if (f === 'invalid') return 'fault'
  if (f === 'unseen' || !f) return 'offline'
  return f
}

function findProbeMessage(
  messages: MessageState[],
  probe: EcuProbe,
): MessageState | undefined {
  const byId = messages.find((m) => m.bus === probe.bus && m.can_id === probe.can_id)
  if (byId) return byId
  if (probe.names?.length) {
    return messages.find(
      (m) => m.bus === probe.bus && probe.names!.includes(m.name || ''),
    )
  }
  return undefined
}

/**
 * Always returns the full unit set. Prefers live CAN messages; falls back to topology.
 * SES/SEB appear even when the topology API omits them (old backend process).
 */
export function buildEcuPresence(
  topology: TopologyNode[],
  messages: MessageState[],
): EcuPresence[] {
  const topoByNode = new Map(topology.map((n) => [n.node, n]))

  return ECU_PROBES.map((probe) => {
    const msg = findProbeMessage(messages, probe)
    const topo = topoByNode.get(probe.node)
    let liveness = 'offline'
    if (msg) {
      liveness = livenessFromFreshness(msg.freshness)
    } else if (topo) {
      liveness = String(topo.liveness || 'offline').toLowerCase()
    }
    const age =
      msg?.age_ms != null && Number.isFinite(msg.age_ms)
        ? ` · age ${Math.round(msg.age_ms)} ms`
        : ''
    return {
      node: probe.node,
      short: probe.short,
      bus: probe.bus,
      can_id: probe.can_id,
      liveness,
      title: `${probe.title} · ${liveness}${age}`,
    }
  })
}
