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
      code?: string
    }
    throw new Error(body.detail || body.title || body.code || r.statusText)
  }
  return r.json() as Promise<T>
}

export type HostDriveBody = {
  speed_mmps: number
  yaw_rate_mrad_s: number
  gear: number
  period_ms?: number | null
}

export type ProfileInfo = {
  id: string
  label: string
  destination: string
  available: boolean
  reason?: string
}

export type VehicleViewBody = {
  requested_mode?: string | null
  confirmed_mode?: string | null
  requested_power?: string | null
  confirmed_power?: string | null
  estop_active?: boolean | null
  recording?: boolean | null
}

export const api = {
  status: () => json<import('./store').Status>('/status'),
  state: () =>
    json<{ sequence: number; messages: import('./store').MessageState[] }>('/state'),
  topology: () =>
    json<{ nodes: import('./store').TopologyNode[] }>('/topology'),
  history: (limit = 200) =>
    json<{
      metrics: Record<string, unknown>
      frames: Array<{
        global_sequence: number
        channel_sequence: number
        bus: string
        can_id: number
        dlc: number
        data_hex: string
        is_extended: boolean
        direction: string
        source: string
        backend_arrival_ns: number
        adapter_epoch: number
      }>
    }>(`/history?limit=${limit}`),
  protocolMessages: () =>
    json<{
      count: number
      semantic_hash: string
      instances: Array<Record<string, unknown>>
    }>('/protocol/messages'),
  profiles: () => json<{ profiles: ProfileInfo[] }>('/sessions/profiles'),
  session: () =>
    json<{ session: import('./store').SessionState }>('/sessions'),
  createSession: (profile = 'pure_software') =>
    json<{ session: import('./store').SessionState }>('/sessions', {
      method: 'POST',
      body: JSON.stringify({ profile }),
    }),
  changeProfile: (
    sessionId: string,
    profile: string,
    expected_revision: number,
    confirm = true,
  ) =>
    json<{ session: import('./store').SessionState }>(
      `/sessions/${sessionId}/profile`,
      {
        method: 'POST',
        body: JSON.stringify({ profile, expected_revision, confirm }),
      },
    ),
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
  closeSession: (sessionId: string, expected_revision: number) =>
    json<{ session: import('./store').SessionState }>(`/sessions/${sessionId}`, {
      method: 'DELETE',
      body: JSON.stringify({ expected_revision, outcome: 'stopped' }),
    }),
  vehicleView: (sessionId: string, body: VehicleViewBody) =>
    json<{ session: import('./store').SessionState }>(
      `/sessions/${sessionId}/vehicle-view`,
      {
        method: 'POST',
        body: JSON.stringify(body),
      },
    ),
  claimLease: (
    sessionId: string,
    body: {
      bus: string
      can_id: number
      owner: string
      resource?: string
      ttl_s?: number
    },
  ) =>
    json<{ lease_id: string; owner: string; bus: string; can_id: number }>(
      `/sessions/${sessionId}/leases`,
      { method: 'POST', body: JSON.stringify(body) },
    ),
  renewLease: (sessionId: string, lease_id: string, ttl_s = 5) =>
    json<{ lease_id: string; renewed: boolean }>(
      `/sessions/${sessionId}/leases/renew`,
      { method: 'POST', body: JSON.stringify({ lease_id, ttl_s }) },
    ),
  releaseLease: (sessionId: string, leaseId: string) =>
    json<{ lease_id: string; released: boolean }>(
      `/sessions/${sessionId}/leases/${leaseId}`,
      { method: 'DELETE' },
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
  injectEstop: () =>
    json<Record<string, unknown>>('/injections', {
      method: 'POST',
      body: JSON.stringify({
        bus: 'low',
        key: 'safety:safety_estop',
        values: {},
        owner: 'ui:estop',
      }),
    }),
}
