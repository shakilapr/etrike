import { api } from '../api'
import type { Status } from '../store'

/**
 * Activate Computer (virtual) or Real (physical destination) profile.
 *
 * Real may be entered without a CANalyst — backend opens absent link and TX
 * stays blocked until the adapter is connected.
 */
export async function activateTransportProfile(profile: string): Promise<Status> {
  const current = await api.status()
  if (current.session?.session_id && current.session.profile !== profile) {
    await api.changeProfile(
      current.session.session_id,
      profile,
      current.session.revision,
      true,
    )
  } else if (!current.session?.session_id) {
    await api.createSession(profile)
  } else if (current.session.profile === profile) {
    // Already on this profile — refresh only.
    return api.status()
  }
  return api.status()
}

/** Human-readable physical link for topbar / settings. */
export function linkLabelFromStatus(status: Status | null | undefined): {
  label: string
  tone: 'ok' | 'warn' | 'danger' | 'muted'
  detail: string
} {
  const mode =
    status?.link?.mode ||
    (status?.session?.destination === 'physical' ||
    status?.session?.profile === 'bench_test' ||
    status?.session?.profile === 'full_vehicle'
      ? 'real'
      : 'computer')
  const health = (status?.link?.health || status?.adapter?.health || '—').toLowerCase()
  const connected =
    status?.link?.connected ??
    ['open', 'active', 'quiet', 'degraded', 'recovering'].includes(health)

  if (mode === 'computer') {
    if (health === 'open' || health === 'active' || health === 'quiet' || connected) {
      return { label: 'Virtual', tone: 'ok', detail: 'Computer · dual virtual CAN' }
    }
    return { label: 'Virtual', tone: 'muted', detail: 'Computer mode' }
  }

  // Real mode
  if (connected && (health === 'open' || health === 'active' || health === 'quiet')) {
    return { label: 'Connected', tone: 'ok', detail: 'CANalyst-II link open' }
  }
  if (health === 'degraded' || health === 'recovering') {
    return {
      label: health === 'recovering' ? 'Recovering' : 'Degraded',
      tone: 'warn',
      detail: status?.adapter?.last_error || status?.link?.detail || 'Physical link unstable',
    }
  }
  return {
    label: 'No connection',
    tone: 'danger',
    detail:
      status?.adapter?.last_error ||
      status?.link?.detail ||
      'Real mode — CANalyst-II not connected',
  }
}
