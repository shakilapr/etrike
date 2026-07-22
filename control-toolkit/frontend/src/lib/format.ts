/** Format age in ms for dense live tables. */
export function formatAge(age?: number | null): string {
  if (age == null || !Number.isFinite(age)) return '—'
  const ms = Math.max(0, age)
  if (ms < 1000) return `${Math.round(ms)} ms`
  return `${(ms / 1000).toFixed(1)} s`
}

/**
 * Age vs expected period (from expected_rate_hz):
 *  - ok:    age ≤ 1× period
 *  - late:  age > 1× period  (orange — more visible than pale yellow)
 *  - stale: age > 2× period  (red)
 *  - unknown: no age or no expected rate
 */
export type AgeTone = 'ok' | 'late' | 'stale' | 'unknown'

export function ageTone(
  ageMs?: number | null,
  expectedRateHz?: number | null,
): AgeTone {
  if (ageMs == null || !Number.isFinite(ageMs)) return 'unknown'
  if (expectedRateHz == null || !Number.isFinite(expectedRateHz) || expectedRateHz <= 0) {
    return 'unknown'
  }
  const periodMs = 1000 / expectedRateHz
  const age = Math.max(0, ageMs)
  if (age > 2 * periodMs) return 'stale'
  if (age > periodMs) return 'late'
  return 'ok'
}

/** CAN ID as uppercase hex with 0x prefix. */
export function hexId(id: number): string {
  return `0x${id.toString(16).toUpperCase()}`
}
