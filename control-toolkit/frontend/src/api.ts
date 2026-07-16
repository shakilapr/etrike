const BASE = '/api/v1'

async function json<T>(path: string, init?: RequestInit): Promise<T> {
  const r = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json', ...(init?.headers || {}) },
    ...init,
  })
  if (!r.ok) {
    const body = (await r.json().catch(() => ({}))) as {
      detail?: string
      title?: string
    }
    throw new Error(body.detail || body.title || r.statusText)
  }
  return r.json() as Promise<T>
}

export type HostDriveBody = {
  speed_mmps: number
  yaw_rate_mrad_s: number
  gear: number
  period_ms?: number | null
}

export const api = {
  status: () => json<import('./store').Status>('/status'),
  state: () =>
    json<{ sequence: number; messages: import('./store').MessageState[] }>('/state'),
  createSession: (profile = 'pure_software') =>
    json<{ session: import('./store').SessionState }>('/sessions', {
      method: 'POST',
      body: JSON.stringify({ profile }),
    }),
  setBenchTx: (sessionId: string, enabled: boolean, expected_revision: number) =>
    json<{ session: import('./store').SessionState }>(
      `/sessions/${sessionId}/bench-tx`,
      {
        method: 'POST',
        body: JSON.stringify({ enabled, expected_revision }),
      },
    ),
  stopAll: (sessionId: string, expected_revision: number) =>
    json<{ session: import('./store').SessionState }>(
      `/sessions/${sessionId}/stop-all`,
      {
        method: 'POST',
        body: JSON.stringify({ expected_revision }),
      },
    ),
  hostDrive: (body: HostDriveBody) =>
    json<Record<string, unknown>>('/analysis/host-drive', {
      method: 'POST',
      body: JSON.stringify(body),
    }),
  stopAnalysis: () =>
    json<{ ok: boolean; stopped: number }>('/analysis/stop', {
      method: 'POST',
      body: '{}',
    }),
}
