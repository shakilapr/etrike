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
  protocolDictionary: () =>
    json<{
      count: number
      signal_count: number
      wire_hash: string
      semantic_hash: string
      source: string
      messages: Array<Record<string, unknown>>
    }>('/protocol/dictionary'),
  refreshDictionary: () =>
    json<{
      count: number
      signal_count: number
      wire_hash: string
      semantic_hash: string
      source: string
      refreshed?: boolean
      messages: Array<Record<string, unknown>>
    }>('/protocol/dictionary/refresh', {
      method: 'POST',
      body: '{}',
    }),
  protocolLayout: (bus: string, canId: number | string) =>
    json<{
      key: string
      name: string
      bus: string
      can_id: number
      bit_grid: {
        dlc: number
        byte_order: string
        endian_label: string
        fields: Array<{
          key: string
          byte: number
          bit: number
          bits: number
          min?: number | null
          max?: number | null
          unit?: string | null
          enum?: Record<string, string> | null
        }>
        rows: Array<{
          byte: number
          bits: Array<{ bit: number; field: string | null }>
        }>
      }
      live: {
        freshness: string
        validation_status?: string | null
        signals: Record<
          string,
          {
            engineering_value: unknown
            enum_label?: string | null
            unit?: string | null
            valid?: boolean
          }
        >
      } | null
    }>(`/protocol/messages/${bus}/${typeof canId === 'number' ? `0x${canId.toString(16)}` : canId}/layout`),
  evidence: (id: string, limit = 100) =>
    json<{
      evidence_id: string
      kind: string
      frame_total: number
      evidence_quality: string
      frames: Array<Record<string, unknown>>
    }>(`/evidence/${id}?limit=${limit}`),
  runTest: (body: {
    name: string
    stimulus: Record<string, unknown>
    expect: Record<string, unknown>
  }) =>
    json<{ test: Record<string, unknown> }>('/tests', {
      method: 'POST',
      body: JSON.stringify(body),
    }),
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
  injectEstop: async () => {
    // Dual-bus ESTOP matches firmware bridge (network.yaml high↔low same_frame).
    const high = await json<Record<string, unknown>>('/injections', {
      method: 'POST',
      body: JSON.stringify({
        bus: 'high',
        key: 'safety:safety_estop',
        values: {},
        owner: 'ui:estop',
      }),
    })
    try {
      await json<Record<string, unknown>>('/injections', {
        method: 'POST',
        body: JSON.stringify({
          bus: 'low',
          key: 'safety:safety_estop',
          values: {},
          owner: 'ui:estop',
        }),
      })
    } catch {
      /* low may conflict if ownership shared; high already sent */
    }
    return high
  },
  controlStatus: () =>
    json<{ control: Record<string, unknown> }>('/control/status'),
  controlIntent: (body: {
    sequence: number
    source?: string
    mode?: string
    throttle: number
    steer: number
    gear?: number | null
    hard_brake?: boolean
    estop?: boolean
  }) =>
    json<{ control: Record<string, unknown>; estop?: unknown }>(
      '/control/intent',
      { method: 'POST', body: JSON.stringify(body) },
    ),
  controlRelease: (reason = 'client_release') =>
    json<{ control: Record<string, unknown> }>('/control/release', {
      method: 'POST',
      body: JSON.stringify({ reason }),
    }),
  controlDirect: (body: {
    channel: 'motor' | 'steering' | 'brake'
    enabled: boolean
    values?: Record<string, unknown>
    period_ms?: number | null
  }) =>
    json<{ control: Record<string, unknown> }>('/control/direct', {
      method: 'POST',
      body: JSON.stringify(body),
    }),
  events: (limit = 50) =>
    json<{ count: number; events: Array<Record<string, unknown>> }>(
      `/events?limit=${limit}`,
    ),
  episodes: () =>
    json<{ count: number; episodes: Array<Record<string, unknown>> }>('/episodes'),
  logs: (opts?: {
    limit?: number
    category?: string
    severity?: string
    q?: string
  }) => {
    const p = new URLSearchParams()
    p.set('limit', String(opts?.limit ?? 200))
    if (opts?.category) p.set('category', opts.category)
    if (opts?.severity) p.set('severity', opts.severity)
    if (opts?.q) p.set('q', opts.q)
    return json<{
      count: number
      stats: Record<string, unknown>
      logs: Array<Record<string, unknown>>
    }>(`/logs?${p.toString()}`)
  },
  clearLogs: () =>
    json<{ cleared: number }>('/logs', { method: 'DELETE' }),
  recordings: () =>
    json<{
      active: Record<string, unknown> | null
      recordings: Array<Record<string, unknown>>
    }>('/recordings'),
  startRecording: () =>
    json<{ recording: Record<string, unknown> }>('/recordings', {
      method: 'POST',
      body: '{}',
    }),
  stopRecording: (id: string) =>
    json<{ recording: Record<string, unknown> }>(`/recordings/${id}`, {
      method: 'DELETE',
    }),
  hmiMode: (req_mode: number, enabled = true) =>
    json<Record<string, unknown>>('/hmi/mode', {
      method: 'POST',
      body: JSON.stringify({ req_mode, enabled }),
    }),
  hmiPower: (req_start: number, enabled = true) =>
    json<Record<string, unknown>>('/hmi/power', {
      method: 'POST',
      body: JSON.stringify({ req_start, enabled }),
    }),
}
