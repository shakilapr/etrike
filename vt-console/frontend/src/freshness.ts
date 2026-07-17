import type { Freshness } from './types'

export const FRESHNESS_LABEL: Record<Freshness, string> = {
  unseen: 'Unseen',
  live: 'Live',
  late: 'Late',
  missing: 'Missing',
  invalid: 'Invalid',
  frozen: 'Frozen',
  recovering: 'Recovering',
}

// Tailwind text/bg utility pairs — kept centralized so every workspace agrees
// on what "Late" looks like (architecture §17: consistent status language).
export const FRESHNESS_CLASS: Record<Freshness, string> = {
  unseen: 'text-text-faint bg-surface-hover',
  live: 'text-live bg-live/10',
  late: 'text-late bg-late/10',
  missing: 'text-missing bg-missing/10',
  invalid: 'text-invalid bg-invalid/10',
  frozen: 'text-frozen bg-frozen/10',
  recovering: 'text-recovering bg-recovering/10',
}

export function formatAgeMs(ageMs: number | null): string {
  if (ageMs === null || ageMs < 0) return '—'
  if (ageMs < 1000) return `${Math.round(ageMs)}ms`
  if (ageMs < 60_000) return `${(ageMs / 1000).toFixed(1)}s`
  return `${Math.floor(ageMs / 60_000)}m`
}

export function ageMsFromLastSeen(lastSeenNs: number | null, nowMs: number, clockOffsetMs: number | null): number | null {
  if (lastSeenNs === null) return null
  // last_seen_ns and the WS hello/heartbeat's server_time_ns are both
  // time.monotonic_ns() on the backend, so clockOffsetMs (captured at the
  // last hello/heartbeat) maps last_seen_ns onto the client's clock exactly,
  // not approximately — the only drift is normal staleness of that sample
  // between heartbeats. No offset yet (stream not connected) -> Unknown.
  if (clockOffsetMs === null) return null
  const lastSeenMs = lastSeenNs / 1e6 + clockOffsetMs
  return nowMs - lastSeenMs
}
