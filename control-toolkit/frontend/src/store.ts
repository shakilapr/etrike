import { create } from 'zustand'

export type MessageState = {
  bus: string
  can_id: number
  key?: string | null
  name?: string | null
  last_seen_ns?: number | null
  age_ms?: number | null
  observed_rate_hz?: number | null
  expected_rate_hz?: number | null
  freshness: string
  validation_status: string
  signals: Record<
    string,
    {
      raw_value?: number | null
      engineering_value: number | string | null
      unit?: string | null
      enum_label?: string | null
      valid: boolean
    }
  >
}

export type SessionState = {
  profile: string
  phase: string
  bench_tx: string
  session_id: string | null
  test_session_id?: string | null
  revision: number
  wire_hash?: string | null
  semantic_hash?: string | null
  destination?: string
  requested_mode?: string | null
  confirmed_mode?: string | null
  requested_power?: string | null
  confirmed_power?: string | null
  estop_active?: boolean | null
  recording?: boolean
  leases?: string[]
  jobs?: string[]
}

export type TopologyNode = {
  node: string
  bus: string
  can_id: number
  liveness: string
  freshness: string
  validation_status?: string | null
}

export type Status = {
  ready: boolean
  wire_hash: string
  semantic_hash?: string
  network_hash?: string
  profile: string
  version: string
  adapter: {
    identity: string
    health: string
    adapter_epoch: number
    worker_alive?: boolean | null
    worker_heartbeat_ns?: number | null
    retry_count?: number
    last_error?: string | null
    device_index?: number | null
    bitrate?: number | null
    channel_map?: Record<string, number>
    channels: Record<
      string,
      {
        activity: string
        rx_count: number
        tx_count?: number
        rx_overflow?: number
        rx_invalid?: number
        last_error?: string | null
      }
    >
  }
  /** Physical/virtual link view (Real may be disconnected). */
  link?: {
    mode: string
    destination?: string
    connected: boolean
    health: string
    detail?: string | null
  }
  session: SessionState
  catalog: { messages: number; instances: number }
}

export type Workspace =
  | 'overview'
  | 'network'
  | 'live'
  | 'control'
  | 'preview'
  | 'bench'
  | 'inject'
  | 'dictionary'
  | 'diagnostics'
  | 'logs'
  | 'settings'

export type Activity = 'explorer' | 'control' | 'monitor'
export type ControlMethod = 'high' | 'low' | 'mtr' | 'hmi'

type AppState = {
  status: Status | null
  messages: MessageState[]
  topology: TopologyNode[]
  sequence: number
  streamQuality: 'connecting' | 'live' | 'delayed' | 'lost' | 'dropping'
  reconnectAttempts: number
  protocolMismatch: boolean
  workspace: Workspace
  activity: Activity
  controlMethod: ControlMethod
  liveFilter: string
  selectedMessageKey: string | null
  setStatus: (s: Status) => void
  setMessages: (msgs: MessageState[], sequence: number) => void
  setTopology: (nodes: TopologyNode[]) => void
  setStreamQuality: (q: AppState['streamQuality']) => void
  setReconnectAttempts: (n: number) => void
  setProtocolMismatch: (v: boolean) => void
  setWorkspace: (w: Workspace) => void
  setActivity: (activity: Activity) => void
  setControlMethod: (method: ControlMethod) => void
  setLiveFilter: (f: string) => void
  setSelectedMessageKey: (k: string | null) => void
  /** Drop Live CAN selection + message ghosts (mode/transport switch). */
  clearLiveView: () => void
}

export const useAppStore = create<AppState>((set) => ({
  status: null,
  messages: [],
  topology: [],
  sequence: 0,
  streamQuality: 'connecting',
  reconnectAttempts: 0,
  protocolMismatch: false,
  workspace: 'overview',
  activity: 'explorer',
  controlMethod: 'high',
  liveFilter: '',
  selectedMessageKey: null,
  setStatus: (status) =>
    set((prev) => {
      const prevProfile = prev.status?.session?.profile
      const nextProfile = status.session?.profile
      const prevDest = prev.status?.session?.destination
      const nextDest = status.session?.destination
      const modeChanged =
        (prevProfile != null && nextProfile != null && prevProfile !== nextProfile) ||
        (prevDest != null && nextDest != null && prevDest !== nextDest)
      // Never keep previous mode's channel activity when adapter identity changes
      // (virtual → none/absent, etc.).
      if (modeChanged) {
        return {
          status,
          messages: [],
          topology: [],
          selectedMessageKey: null,
        }
      }
      return { status }
    }),
  setMessages: (messages, sequence) =>
    set((s) => {
      const keep =
        !!s.selectedMessageKey &&
        messages.some(
          (m) =>
            m.key === s.selectedMessageKey ||
            `${m.bus}-${m.can_id}` === s.selectedMessageKey,
        )
      return {
        messages,
        sequence,
        selectedMessageKey: keep ? s.selectedMessageKey : null,
      }
    }),
  setTopology: (topology) => set({ topology }),
  setStreamQuality: (streamQuality) => set({ streamQuality }),
  setReconnectAttempts: (reconnectAttempts) => set({ reconnectAttempts }),
  setProtocolMismatch: (protocolMismatch) => set({ protocolMismatch }),
  setWorkspace: (workspace) => set({ workspace }),
  setActivity: (activity) => set({ activity }),
  setControlMethod: (controlMethod) => set({ controlMethod }),
  setLiveFilter: (liveFilter) => set({ liveFilter }),
  setSelectedMessageKey: (selectedMessageKey) => set({ selectedMessageKey }),
  clearLiveView: () => set({ messages: [], topology: [], selectedMessageKey: null }),
}))
