import type { Freshness, MessageState, ProtocolInstance } from './types'

export interface NodeSummary {
  node: string
  instanceCount: number
  live: number
  late: number
  missing: number
  invalid: number
  unseen: number
  // Worst-case rollup across this node's expected instances. §4.5 also names
  // Offline/Simulated/Unknown-traffic/Fault states — those need a per-frame
  // `source` field the backend doesn't expose yet (MessageState has no
  // provenance field; that lives on the raw envelope, not the latest-value
  // API). Deferred to whichever later phase adds a source-aware endpoint.
  worst: Freshness
}

// Worst-to-best. `unseen` ranks alongside `missing` (never observed is at
// least as bad as "was live, now overdue"); `frozen` (counter stalled despite
// frames arriving) ranks near `invalid` since it hides a real fault behind
// apparently-live traffic.
const SEVERITY: Freshness[] = ['invalid', 'frozen', 'missing', 'unseen', 'late', 'recovering', 'live']

function worstOf(a: Freshness, b: Freshness): Freshness {
  return SEVERITY.indexOf(a) <= SEVERITY.indexOf(b) ? a : b
}

// Groups the protocol catalog's declared senders against live message
// freshness, entirely client-side (no backend topology endpoint exists yet —
// see vtc/state/topology.py, an intentionally-empty stub).
export function buildTopology(catalog: ProtocolInstance[], messages: MessageState[]): NodeSummary[] {
  const freshnessByKey = new Map<string, Freshness>()
  for (const m of messages) {
    if (m.key) freshnessByKey.set(`${m.key}:${m.bus}:${m.can_id}`, m.freshness)
  }

  const byNode = new Map<string, NodeSummary>()
  for (const inst of catalog) {
    const existing = byNode.get(inst.sender) ?? {
      node: inst.sender,
      instanceCount: 0,
      live: 0,
      late: 0,
      missing: 0,
      invalid: 0,
      unseen: 0,
      worst: 'live' as Freshness,
    }
    existing.instanceCount++
    const fresh = freshnessByKey.get(`${inst.key}:${inst.bus}:${inst.id}`) ?? 'unseen'
    // Tally buckets are a simplified 5-way view for the summary line; frozen
    // counts as late (visibly not-quite-live) rather than live, recovering
    // counts as late too (not fully live yet). `worst` (below) still tracks
    // the full 7-state severity precisely for the badge.
    const bucket = fresh === 'frozen' || fresh === 'recovering' ? 'late' : fresh
    existing[bucket]++
    existing.worst = worstOf(existing.worst, fresh)
    byNode.set(inst.sender, existing)
  }

  return [...byNode.values()].sort((a, b) => a.node.localeCompare(b.node))
}
