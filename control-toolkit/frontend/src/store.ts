import { create } from 'zustand'

export type MessageState = {
  bus: string
  can_id: number
  key?: string | null
  name?: string | null
  last_seen_ns?: number | null
  expected_rate_hz?: number | null
  freshness: string
  validation_status: string
  signals: Record<
    string,
    {
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
  revision: number
  wire_hash?: string | null
}

export type Status = {
  ready: boolean
  wire_hash: string
  profile: string
  version: string
  adapter: {
    identity: string
    health: string
    adapter_epoch: number
    channels: Record<string, { activity: string; rx_count: number }>
  }
  session: SessionState
  catalog: { messages: number; instances: number }
}

type AppState = {
  status: Status | null
  messages: MessageState[]
  sequence: number
  streamQuality: 'connecting' | 'live' | 'delayed' | 'lost'
  protocolMismatch: boolean
  workspace: 'overview' | 'live' | 'control'
  setStatus: (s: Status) => void
  setMessages: (msgs: MessageState[], sequence: number) => void
  setStreamQuality: (q: AppState['streamQuality']) => void
  setProtocolMismatch: (v: boolean) => void
  setWorkspace: (w: AppState['workspace']) => void
}

export const useAppStore = create<AppState>((set) => ({
  status: null,
  messages: [],
  sequence: 0,
  streamQuality: 'connecting',
  protocolMismatch: false,
  workspace: 'overview',
  setStatus: (status) => set({ status }),
  setMessages: (messages, sequence) => set({ messages, sequence }),
  setStreamQuality: (streamQuality) => set({ streamQuality }),
  setProtocolMismatch: (protocolMismatch) => set({ protocolMismatch }),
  setWorkspace: (workspace) => set({ workspace }),
}))
