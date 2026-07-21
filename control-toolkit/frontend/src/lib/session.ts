import { api } from '../api'
import type { Status } from '../store'

export async function activateTransportProfile(profile: string): Promise<Status> {
  if (profile === 'bench_test' || profile === 'full_vehicle') {
    const profiles = await api.profiles()
    if (!profiles.physical_adapter?.available) {
      throw new Error(
        profiles.physical_adapter?.reason ||
          'CANalyst-II is unavailable. Connect USB and retry; Computer remains active.',
      )
    }
  }
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
  }
  return api.status()
}
