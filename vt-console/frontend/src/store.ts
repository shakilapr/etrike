import { create } from 'zustand'
import type { MessageState, ProtocolInstance, SessionState, StatusResponse, StreamQuality } from './types'

// Later phases (Control, Bench, Preview, Dictionary, Diagnostics...) add their
// own workspaces — deliberately not modeled here yet (workplan Phase 4 scope).
export type Workspace = 'overview' | 'network' | 'live'

interface AppState {
  status: StatusResponse | null
  session: SessionState | null
  messages: MessageState[]
  sequence: number
  catalog: ProtocolInstance[]
  catalogWireHash: string | null

  streamQuality: StreamQuality
  reconnectAttempts: number
  protocolMismatch: boolean
  helloWireHash: string | null
  // client_now_ms - server_time_ns/1e6, from the last hello/heartbeat. Rough
  // transport-delay estimate (workplan §4.3) — not a calibrated NTP-style sync.
  clockOffsetMs: number | null

  workspace: Workspace
  liveFilter: string

  setStatus: (s: StatusResponse) => void
  setSession: (s: SessionState) => void
  setMessages: (msgs: MessageState[], sequence: number) => void
  setCatalog: (instances: ProtocolInstance[], wireHash: string) => void
  setStreamQuality: (q: StreamQuality) => void
  setReconnectAttempts: (n: number) => void
  setHelloWireHash: (h: string) => void
  setClockOffsetMs: (ms: number) => void
  setWorkspace: (w: Workspace) => void
  setLiveFilter: (f: string) => void
}

export const useAppStore = create<AppState>((set, get) => ({
  status: null,
  session: null,
  messages: [],
  sequence: 0,
  catalog: [],
  catalogWireHash: null,

  streamQuality: 'connecting',
  reconnectAttempts: 0,
  protocolMismatch: false,
  helloWireHash: null,
  clockOffsetMs: null,

  workspace: 'overview',
  liveFilter: '',

  setStatus: (status) => {
    set({ status, session: status.session })
    const hello = get().helloWireHash
    if (hello) set({ protocolMismatch: hello !== status.wire_hash })
  },
  setSession: (session) => set({ session }),
  setMessages: (messages, sequence) => set({ messages, sequence }),
  setCatalog: (catalog, catalogWireHash) => set({ catalog, catalogWireHash }),
  setStreamQuality: (streamQuality) => set({ streamQuality }),
  setReconnectAttempts: (reconnectAttempts) => set({ reconnectAttempts }),
  setHelloWireHash: (helloWireHash) => {
    set({ helloWireHash })
    const status = get().status
    if (status) set({ protocolMismatch: helloWireHash !== status.wire_hash })
  },
  setClockOffsetMs: (clockOffsetMs) => set({ clockOffsetMs }),
  setWorkspace: (workspace) => set({ workspace }),
  setLiveFilter: (liveFilter) => set({ liveFilter }),
}))
