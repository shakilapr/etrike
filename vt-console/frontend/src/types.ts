// Mirrors vt-console/backend/vtc/models/*.py. Keep in sync by hand (no OpenAPI
// codegen for this read-only MVP — see workplan.md Phase 4 scope note).

export type Bus = 'high' | 'low'

export type Freshness =
  | 'unseen'
  | 'live'
  | 'late'
  | 'missing'
  | 'invalid'
  | 'frozen'
  | 'recovering'

export interface SignalValue {
  raw_value: number | null
  engineering_value: number | string | null
  unit: string | null
  enum_label: string | null
  valid: boolean
}

export interface MessageState {
  bus: Bus
  can_id: number
  key: string | null
  name: string | null
  last_seen_ns: number | null
  observed_rate_hz: number | null
  expected_rate_hz: number | null
  freshness: Freshness
  validation_status: string | null
  signals: Record<string, SignalValue>
}

export interface LatestStateSnapshot {
  sequence: number
  version: number
  wire_hash: string
  messages: MessageState[]
}

// Tri-state: true/false/null (null = Unknown — never render as false).
export interface Capability {
  hw_timestamps: boolean | null
  tx_echo: boolean | null
  listen_only: boolean | null
  bus_off_reporting: boolean | null
  tec_rec_reporting: boolean | null
}

export type AdapterHealth =
  | 'absent'
  | 'opening'
  | 'open'
  | 'active'
  | 'quiet'
  | 'degraded'
  | 'recovering'
  | 'closed'

export type ChannelActivity = 'unseen' | 'active' | 'quiet'

export interface ChannelState {
  channel: string
  activity: ChannelActivity
  last_rx_ns: number | null
  rx_count: number
  tx_count: number
  rx_overflow: number
  queue_high_water: number
}

export interface AdapterStatus {
  identity: string
  health: AdapterHealth
  adapter_epoch: number
  capability: Capability
  channels: Record<string, ChannelState>
}

export type Profile = 'full_vehicle' | 'bench_test' | 'pure_software'
export type SessionPhase =
  | 'stopped'
  | 'preparing'
  | 'listening'
  | 'running'
  | 'stopping'
  | 'completed'
  | 'failed'
  | 'inconclusive'
export type BenchTxState = 'disabled' | 'enabled'

export interface SessionState {
  profile: Profile
  phase: SessionPhase
  bench_tx: BenchTxState
  session_id: string | null
  test_session_id: string | null
  revision: number
  adapter_epoch: number | null
  wire_hash: string | null
  destination: 'virtual' | 'physical'
  capabilities: string[]
  leases: string[]
}

export interface StatusResponse {
  service: string
  version: string
  ready: boolean
  wire_hash: string
  profile: Profile
  catalog: { messages: number; instances: number }
  adapter: AdapterStatus
  session: SessionState
}

export interface ProfileOption {
  id: Profile
  label: string
  destination: 'virtual' | 'physical'
  available: boolean
  reason?: string
}

// One physical instance of a canonical message, as served by
// GET /api/v1/protocol/messages.
export interface ProtocolInstance {
  key: string
  name: string
  bus: Bus
  id: number
  frame_format: string
  sender: string
  receivers: string[]
  cycle_ms: number | null
  semantics: string
}

export interface ProtocolMessagesResponse {
  wire_hash: string
  count: number
  instances: ProtocolInstance[]
}

// WebSocket frames (see vtc/api/stream.py — full snapshots, not deltas).
export interface WsHello {
  type: 'hello'
  wire_hash: string
  server_time_ns: number
}

export interface WsState {
  type: 'state'
  batch_seq: number
  version: number
  sequence: number
  messages: MessageState[]
}

export interface WsHeartbeat {
  type: 'heartbeat'
  batch_seq: number
  server_time_ns: number
}

export interface WsEvent {
  type: 'event'
  batch_seq: number
  event: Record<string, unknown>
}

export type WsFrame = WsHello | WsState | WsHeartbeat | WsEvent

export type StreamQuality = 'connecting' | 'live' | 'delayed' | 'lost'
