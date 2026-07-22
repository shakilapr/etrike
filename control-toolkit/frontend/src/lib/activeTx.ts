/**
 * Active TX (host-side CAN output) helpers.
 * Blue = messages we manipulate and send via CANalyst / virtual TX gate.
 * Green stays for ECU bus liveness (RX presence), not our TX.
 */
import { hexId } from './format'

export type ActiveJob = {
  job_id: string
  bus: string
  key?: string | null
  can_id?: number | null
  values: Record<string, unknown>
  period_ms: number
  owner: string
  counter_field?: string | null
  missed: number
  last_result?: string | null
}

/** Client-side pause snapshot (backend has cancel only — resume re-injects). */
export type PausedJob = {
  pause_id: string
  bus: string
  key?: string | null
  can_id?: number | null
  values: Record<string, unknown>
  period_ms: number
  counter_field?: string | null
  name: string
}

export type CatalogMsg = {
  canonicalKey: string
  name: string
  can_id: number
  bus: string
}

export function jobLabel(job: Pick<ActiveJob, 'key' | 'can_id'>, catalog: CatalogMsg[]): string {
  if (job.key) {
    const m = catalog.find((x) => x.canonicalKey === job.key)
    if (m?.name) return m.name
    const short = job.key.includes(':') ? job.key.split(':').pop() : job.key
    return String(short || job.key).toUpperCase()
  }
  if (job.can_id != null) return hexId(job.can_id)
  return 'JOB'
}

export function jobCanIdNum(
  job: Pick<ActiveJob, 'key' | 'can_id' | 'bus'>,
  catalog: CatalogMsg[],
): number | null {
  if (job.can_id != null && Number.isFinite(job.can_id)) return job.can_id
  if (job.key) {
    const m = catalog.find(
      (x) =>
        x.canonicalKey === job.key &&
        (!job.bus || x.bus === job.bus),
    )
    if (m) return m.can_id
    const any = catalog.find((x) => x.canonicalKey === job.key)
    if (any) return any.can_id
  }
  return null
}

export function jobCanIdText(
  job: Pick<ActiveJob, 'key' | 'can_id' | 'bus'>,
  catalog: CatalogMsg[],
): string {
  const n = jobCanIdNum(job, catalog)
  return n != null ? hexId(n) : '—'
}

/** Stable `bus:can_id` keys for messages currently owned by host TX jobs. */
export function hostTxKeySet(
  jobs: ActiveJob[],
  catalog: CatalogMsg[] = [],
): Set<string> {
  const set = new Set<string>()
  for (const j of jobs) {
    const id = jobCanIdNum(j, catalog)
    if (id != null) set.add(`${j.bus}:${id}`)
  }
  return set
}

export function isHostTxFrame(opts: {
  bus: string
  can_id: number
  direction?: string | null
  source?: string | null
  hostKeys?: Set<string>
}): boolean {
  const dir = String(opts.direction || '').toLowerCase()
  const src = String(opts.source || '').toLowerCase()
  if (dir === 'tx') return true
  if (src === 'injection' || src === 'synthetic') return true
  if (opts.hostKeys?.has(`${opts.bus}:${opts.can_id}`)) return true
  return false
}
