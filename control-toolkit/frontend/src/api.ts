const BASE = '/api/v1'

async function json<T>(path: string, init?: RequestInit): Promise<T> {
  let r: Response
  try {
    r = await fetch(`${BASE}${path}`, {
      headers: { 'Content-Type': 'application/json', ...(init?.headers || {}) },
      ...init,
    })
  } catch {
    throw new Error(
      'Backend unreachable (network error). Start control-toolkit on port 8001 and ensure Vite proxies CTK_E2E_API.',
    )
  }
  if (!r.ok) {
    const body = (await r.json().catch(() => ({}))) as {
      detail?: string
      title?: string
      code?: string
    }
    if (
      (r.status === 502 || r.status === 503 || r.status === 504) &&
      !body.detail &&
      !body.title &&
      !body.code
    ) {
      throw new Error(
        `Backend unavailable (${r.status} ${r.statusText}). Vite proxy cannot reach the API — start uvicorn on 127.0.0.1:8001.`,
      )
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
  mode?: 'computer' | 'real' | string
}

export type TransportModeInfo = {
  id: 'computer' | 'real' | string
  label: string
  description: string
  destination: string
  available: boolean
  reason?: string | null
  adapter?: string
  profile?: string
  profiles?: string[]
}

export type PhysicalAdapterInfo = {
  kind: string
  available: boolean
  reason?: string | null
  channels?: Record<string, string>
  bitrate?: number
  device_index?: number
  usb_vid?: number
  usb_pid?: number
  usb_visible?: boolean | null
  python_can_version?: string
  backend?: string
}

export type SettingsSnapshot = {
  service: {
    title: string
    version: string
    ready: boolean
    api_prefix: string
    host: string
    port: number
    workers: number
  }
  transport: {
    modes: TransportModeInfo[]
    profiles: ProfileInfo[]
    physical_adapter: PhysicalAdapterInfo
    channel_map: Record<
      string,
      { logical: string; physical: string; bitrate: number; role: string }
    >
    active: {
      profile?: string | null
      destination?: string | null
      mode?: 'computer' | 'real' | string
    }
  }
  session: Record<string, unknown>
  adapter: {
    identity?: string
    health?: string
    adapter_epoch?: number
    capability?: Record<string, unknown>
    channels?: Record<string, unknown>
    [key: string]: unknown
  }
  protocol: {
    wire_hash: string
    semantic_hash: string
    network_hash: string
    catalog: { messages: number; instances: number }
  }
  runtime: {
    default_profile: string
    stream_heartbeat_ms: number
    latest_state_batch_hz: number
    browser_degraded_ms: number
    browser_lost_ms: number
    rx_queue_maxsize: number
    history_capacity: number
    canalyst_device_index?: number
    canalyst_bitrate?: number
    canalyst_poll_ms?: number
    canalyst_receive_timeout_ms?: number
    canalyst_reconnect_initial_ms?: number
    canalyst_reconnect_max_ms?: number
    canalyst_recovery_stability_ms?: number
    host?: string
    port?: number
    env_prefix?: string
    notes?: string
  }
  history: Record<string, number | unknown>
  control: Record<string, unknown>
  synthetic_peers: Array<Record<string, string> | string>
  diagnostics: {
    episode_count: number
    episodes: Array<Record<string, unknown>>
  }
  recording: { active: Record<string, unknown> | null }
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
  tests: () =>
    json<{ count: number; tests: Array<Record<string, unknown>> }>('/tests'),
  test: (testId: string) =>
    json<{ test: Record<string, unknown> }>(`/tests/${testId}`),
  syntheticPeers: () =>
    json<{
      available: Array<Record<string, unknown>>
      running: Array<Record<string, unknown>>
    }>('/synthetic-peers'),
  startSyntheticPeers: (names: string[]) =>
    json<{ started: Array<Record<string, unknown>> }>('/synthetic-peers/start', {
      method: 'POST',
      body: JSON.stringify({ names }),
    }),
  stopSyntheticPeers: (names?: string[]) =>
    json<{ stopped: number }>('/synthetic-peers/stop', {
      method: 'POST',
      body: JSON.stringify(names ? { names } : {}),
    }),
  profiles: () =>
    json<{
      profiles: ProfileInfo[]
      transport_modes?: TransportModeInfo[]
      physical_adapter?: PhysicalAdapterInfo
    }>('/sessions/profiles'),
  /** Aggregated live settings: transport, session, adapter, protocol, runtime, … */
  settings: () => json<SettingsSnapshot>('/settings'),
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
    // Use the backend's atomic control-safety path: it transmits on both buses,
    // latches the session ESTOP view, and releases any active motion ownership.
    return json<Record<string, unknown>>('/control/intent', {
      method: 'POST',
      body: JSON.stringify({
        sequence: Date.now(),
        source: 'ui:header',
        mode: 'estop',
        throttle: 0,
        steer: 0,
        estop: true,
      }),
    })
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
