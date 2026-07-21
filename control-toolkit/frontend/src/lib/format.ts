/** Format age in ms for dense live tables. */
export function formatAge(age?: number | null): string {
  if (age == null || !Number.isFinite(age)) return '—'
  const ms = Math.max(0, age)
  if (ms < 1000) return `${Math.round(ms)} ms`
  return `${(ms / 1000).toFixed(1)} s`
}

/** CAN ID as uppercase hex with 0x prefix. */
export function hexId(id: number): string {
  return `0x${id.toString(16).toUpperCase()}`
}
