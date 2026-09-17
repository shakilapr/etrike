import { api } from '../api'
import { useAppStore, type MessageState } from '../store'
import {
  getSteeringPipeline,
  getSpeedPipeline,
  getBrakePipeline,
  getAuthorityPipeline,
  observeEstop,
  signalIsOn,
  signalNum,
  signalText,
  findMsg,
} from './signals'
import { evaluateActivationGates } from './activationGates'
import {
  parseDiagEventReport,
  parseEstopResetRsp,
  parseNodeStatus,
} from './canProtocolRules'

export type SteeringSample = {
  timestamp_ms: number
  rel_time_ms: number
  ses_actual_deg: number | null
  rt_target_deg: number | null
  host_steer_deg: number | null
  host_yaw_rate: number | null
  ses_torque_nm: number | null
  ses_mode: string
  rt_control_enable: boolean | null
  driver_override_active: boolean
  is_manual_mode: boolean
}

export type HeartbeatSample = {
  timestamp_ms: number
  rel_time_ms: number
  controllers: Array<{
    node: string
    can_id_hex: string
    can_id_dec: number
    bus: string
    liveness: string
    freshness: string
    last_seen_age_ms?: number | null
  }>
  worker_alive: boolean | null
}

export type ModeSample = {
  timestamp_ms: number
  rel_time_ms: number
  confirmed_mode: string
  requested_mode: string
  sys_cmd_mode: string
  rt_reported_mode: string
  ses_control_mode: string
  is_manual_mode: boolean
  estop_active: boolean
}

export type CanRawFrame = {
  global_sequence: number
  bus: string
  can_id: number
  can_id_hex: string
  dlc: number
  data_hex: string
  direction: string
  backend_arrival_ns: number
}

export type CanIdInventoryEntry = {
  can_id_hex: string
  can_id_dec: number
  bus: 'high' | 'low'
  message_name: string
  sender: string
  receivers: string[]
  expected_cycle_ms: number
  status_in_10s: 'ACTIVE' | 'INACTIVE'
  observed_hz: number | null
  frames_in_10s: number
  last_seen_age_ms: number | null
  last_payload_hex: string | null
  signals: Record<string, {
    value: unknown
    unit: string | null
    enum_label?: string | null
    valid?: boolean
  }>
}

export type CanBusSummary = {
  bus_name: 'high' | 'low'
  channel: 'CH0' | 'CH1'
  bitrate_kbps: number
  total_catalog_can_ids: number
  active_can_ids_count: number
  inactive_can_ids_count: number
  total_frames_observed: number
  can_ids: CanIdInventoryEntry[]
}

export type CanBusInventory = {
  high_bus: CanBusSummary
  low_bus: CanBusSummary
}

export type DiagnosticExportBundle = {
  export_metadata: {
    format: string
    version: string
    exported_at_iso: string
    exported_at_unix_ms: number
    capture_type: 'live_10s_record' | 'instant_10s_buffer'
    capture_duration_ms: number
    vehicle_profile: string
    transport_mode: string
    destination: string
    wire_hash: string
    semantic_hash?: string
  }
  llm_diagnostic_context: {
    executive_summary: string
    vehicle_mode_analysis: {
      overall_mode: string
      was_in_manual_mode: boolean
      manual_mode_evidence: string[]
      mode_timeline: ModeSample[]
      estop_active: boolean
      estop_cause_summary?: string
    }
    steering_analysis: {
      subsystem_summary: string
      is_manual_steering: boolean
      initial_ses_angle_deg: number | null
      final_ses_angle_deg: number | null
      min_ses_angle_deg: number | null
      max_ses_angle_deg: number | null
      total_angle_change_deg: number | null
      max_following_error_deg: number | null
      sample_count: number
      anomalies_detected: string[]
      steering_samples: SteeringSample[]
    }
    heartbeat_analysis: {
      summary: string
      all_controllers_healthy: boolean
      controllers_online_count: number
      total_controllers_count: number
      nodes: Array<{
        node: string
        can_id_hex: string
        can_id_dec: number
        bus: string
        liveness: string
        freshness: string
        observed_hz?: number | null
        expected_hz?: number | null
      }>
      heartbeat_timeline: HeartbeatSample[]
    }
    activation_gates_analysis: {
      all_gates_cleared: boolean
      cleared_count: number
      total_count: number
      inhibitors: string[]
      gates: Array<{
        id: string
        controller: string
        cleared: boolean
        reason: string
        blockers: string[]
        required_can_ids: string[]
      }>
    }
    can_bus_inventory: CanBusInventory
    can_ids_inventory: CanIdInventoryEntry[]
    protocol_diagnostics: {
      active_diagnostic_events: Array<{
        can_id_hex: string
        diag_id_hex: string
        diag_key: string
        reporter: string
        subsystem: string
        severity: string
        reaction: string
        state: string
        is_root_estop_cause: boolean
        decoded_snapshot: string
        description: string
      }>
      sys_estop_blockers: string[]
      ecu_node_states: Record<string, { state: string; block_mask: string; blockers: string[] }>
      can_bus_health: {
        low_can_twai: { tec?: number; rec?: number }
        high_can_mcp: { tec?: number; rec?: number; bus_off?: boolean }
      }
    }
    llm_prompt_markdown: string
  }
  raw_telemetry: {
    state_snapshots: Array<{
      rel_time_ms: number
      message_count: number
      speed: {
        host_speed_mmps: number | null
        rt_speed_mmps: number | null
        mtr_fbk_speed_mmps: number | null
        wheel_speed_mmps: number | null
      }
      steering: {
        ses_angle_deg: number | null
        rt_target_deg: number | null
        host_steer_deg: number | null
        yaw_rate: number | null
        torque_nm: number | null
      }
      brake: {
        host_pressure_kpa: number | null
        rt_pressure_kpa: number | null
        actual_pressure_kpa: number | null
        actual_stroke_mm: number | null
      }
    }>
    can_frames_by_bus: {
      high: CanRawFrame[]
      low: CanRawFrame[]
    }
    can_frames: CanRawFrame[]
    audit_logs: Array<Record<string, unknown>>
    latest_messages: MessageState[]
  }
}

// ── Canonical CAN Protocol ID Catalogs (protocol/contracts/*.yaml) ──
export type CanonicalCanCatalogItem = {
  can_id_hex: string
  can_id_dec: number
  bus: 'high' | 'low'
  message_name: string
  sender: string
  receivers: string[]
  cycle_ms: number
}

export const CAN_CATALOG_HIGH: CanonicalCanCatalogItem[] = [
  { can_id_hex: '0x001', can_id_dec: 1, bus: 'high', message_name: 'SAFETY_ESTOP', sender: 'Any', receivers: ['Host', 'RT'], cycle_ms: 0 },
  { can_id_hex: '0x011', can_id_dec: 17, bus: 'high', message_name: 'SYS_SAFETY_STS', sender: 'SYS', receivers: ['Host'], cycle_ms: 200 },
  { can_id_hex: '0x111', can_id_dec: 273, bus: 'high', message_name: 'HMI_MODE_REQ', sender: 'Host', receivers: ['RT'], cycle_ms: 1000 },
  { can_id_hex: '0x112', can_id_dec: 274, bus: 'high', message_name: 'HMI_PWR_REQ', sender: 'Host', receivers: ['RT'], cycle_ms: 1000 },
  { can_id_hex: '0x114', can_id_dec: 276, bus: 'high', message_name: 'HOST_ESTOP_RESET_REQ', sender: 'Host', receivers: ['RT'], cycle_ms: 0 },
  { can_id_hex: '0x115', can_id_dec: 277, bus: 'high', message_name: 'SYS_ESTOP_RESET_RSP', sender: 'SYS', receivers: ['Host'], cycle_ms: 0 },
  { can_id_hex: '0x120', can_id_dec: 288, bus: 'high', message_name: 'SYS_THROTTLE_STS', sender: 'MTR', receivers: ['RT', 'Host'], cycle_ms: 10 },
  { can_id_hex: '0x121', can_id_dec: 289, bus: 'high', message_name: 'RT_MOTION_RPT', sender: 'RT', receivers: ['Host'], cycle_ms: 10 },
  { can_id_hex: '0x122', can_id_dec: 290, bus: 'high', message_name: 'RT_WHEEL_SPEED_STS', sender: 'RT', receivers: ['Host', 'SYS'], cycle_ms: 100 },
  { can_id_hex: '0x206', can_id_dec: 518, bus: 'high', message_name: 'MTR_MOTOR_FBK', sender: 'MTR', receivers: ['RT', 'SYS', 'Host'], cycle_ms: 20 },
  { can_id_hex: '0x210', can_id_dec: 528, bus: 'high', message_name: 'RT_STATE_RPT', sender: 'RT', receivers: ['Host', 'SYS'], cycle_ms: 100 },
  { can_id_hex: '0x220', can_id_dec: 544, bus: 'high', message_name: 'RT_PID_RPT', sender: 'RT', receivers: ['Host'], cycle_ms: 100 },
  { can_id_hex: '0x300', can_id_dec: 768, bus: 'high', message_name: 'HOST_DRIVE_CMD', sender: 'Host', receivers: ['RT'], cycle_ms: 10 },
  { can_id_hex: '0x301', can_id_dec: 769, bus: 'high', message_name: 'HOST_BRAKE_REQ', sender: 'Host', receivers: ['RT'], cycle_ms: 0 },
  { can_id_hex: '0x302', can_id_dec: 770, bus: 'high', message_name: 'HOST_LIGHT_CMD', sender: 'Host', receivers: ['RT', 'SYS'], cycle_ms: 0 },
  { can_id_hex: '0x303', can_id_dec: 771, bus: 'high', message_name: 'HOST_STEER_CMD', sender: 'Host', receivers: ['RT'], cycle_ms: 10 },
  { can_id_hex: '0x310', can_id_dec: 784, bus: 'high', message_name: 'STEER_DIAG', sender: 'RT', receivers: ['Host'], cycle_ms: 100 },
  { can_id_hex: '0x311', can_id_dec: 785, bus: 'high', message_name: 'BRAKE_DIAG', sender: 'RT', receivers: ['Host'], cycle_ms: 100 },
  { can_id_hex: '0x400', can_id_dec: 1024, bus: 'high', message_name: 'HOST_OBSTACLE_DIST', sender: 'Host', receivers: ['RT'], cycle_ms: 100 },
  { can_id_hex: '0x500', can_id_dec: 1280, bus: 'high', message_name: 'SYS_NODE_STATUS', sender: 'SYS', receivers: ['Host'], cycle_ms: 200 },
  { can_id_hex: '0x501', can_id_dec: 1281, bus: 'high', message_name: 'RT_NODE_STATUS', sender: 'RT', receivers: ['SYS', 'Host'], cycle_ms: 100 },
  { can_id_hex: '0x502', can_id_dec: 1282, bus: 'high', message_name: 'MTR_NODE_STATUS', sender: 'MTR', receivers: ['Host'], cycle_ms: 20 },
  { can_id_hex: '0x600', can_id_dec: 1536, bus: 'high', message_name: 'SYS_DIAG_RPT', sender: 'SYS', receivers: ['RT', 'Host'], cycle_ms: 1000 },
  { can_id_hex: '0x601', can_id_dec: 1537, bus: 'high', message_name: 'SYS_DIAG_EVENT_RPT', sender: 'SYS', receivers: ['RT', 'Host'], cycle_ms: 0 },
  { can_id_hex: '0x620', can_id_dec: 1568, bus: 'high', message_name: 'RT_DIAG_RPT', sender: 'RT', receivers: ['Host'], cycle_ms: 1000 },
  { can_id_hex: '0x621', can_id_dec: 1569, bus: 'high', message_name: 'RT_DIAG_EVENT_RPT', sender: 'RT', receivers: ['Host'], cycle_ms: 0 },
  { can_id_hex: '0x631', can_id_dec: 1585, bus: 'high', message_name: 'MTR_DIAG_EVENT_RPT', sender: 'MTR', receivers: ['RT', 'Host'], cycle_ms: 0 },
  { can_id_hex: '0x7FC', can_id_dec: 2044, bus: 'high', message_name: 'HOST_HEARTBEAT', sender: 'Host', receivers: ['RT'], cycle_ms: 500 },
  { can_id_hex: '0x7FD', can_id_dec: 2045, bus: 'high', message_name: 'RT_HEARTBEAT', sender: 'RT', receivers: ['Host', 'SYS'], cycle_ms: 500 },
]

export const CAN_CATALOG_LOW: CanonicalCanCatalogItem[] = [
  { can_id_hex: '0x001', can_id_dec: 1, bus: 'low', message_name: 'SAFETY_ESTOP', sender: 'Any', receivers: ['SYS', 'MTR', 'DCDC', 'RT'], cycle_ms: 0 },
  { can_id_hex: '0x011', can_id_dec: 17, bus: 'low', message_name: 'SYS_SAFETY_STS', sender: 'SYS', receivers: ['RT', 'MTR'], cycle_ms: 200 },
  { can_id_hex: '0x110', can_id_dec: 272, bus: 'low', message_name: 'SYS_MODE_CMD', sender: 'SYS', receivers: ['RT', 'MTR'], cycle_ms: 100 },
  { can_id_hex: '0x111', can_id_dec: 273, bus: 'low', message_name: 'HMI_MODE_REQ', sender: 'Host', receivers: ['SYS'], cycle_ms: 1000 },
  { can_id_hex: '0x112', can_id_dec: 274, bus: 'low', message_name: 'HMI_PWR_REQ', sender: 'Host', receivers: ['SYS'], cycle_ms: 1000 },
  { can_id_hex: '0x113', can_id_dec: 275, bus: 'low', message_name: 'SYS_PWR_CMD', sender: 'SYS', receivers: ['MTR'], cycle_ms: 100 },
  { can_id_hex: '0x114', can_id_dec: 276, bus: 'low', message_name: 'HOST_ESTOP_RESET_REQ', sender: 'Host', receivers: ['SYS'], cycle_ms: 0 },
  { can_id_hex: '0x115', can_id_dec: 277, bus: 'low', message_name: 'SYS_ESTOP_RESET_RSP', sender: 'SYS', receivers: ['RT'], cycle_ms: 0 },
  { can_id_hex: '0x120', can_id_dec: 288, bus: 'low', message_name: 'SYS_THROTTLE_STS', sender: 'MTR', receivers: ['RT', 'Host'], cycle_ms: 10 },
  { can_id_hex: '0x122', can_id_dec: 290, bus: 'low', message_name: 'RT_WHEEL_SPEED_STS', sender: 'RT', receivers: ['SYS'], cycle_ms: 100 },
  { can_id_hex: '0x169', can_id_dec: 361, bus: 'low', message_name: 'VCU_SES_REQ', sender: 'RT', receivers: ['SES'], cycle_ms: 20 },
  { can_id_hex: '0x201', can_id_dec: 513, bus: 'low', message_name: 'SES_STATUS', sender: 'SES', receivers: ['RT'], cycle_ms: 10 },
  { can_id_hex: '0x202', can_id_dec: 514, bus: 'low', message_name: 'SES_ERR_INFO', sender: 'SES', receivers: ['RT'], cycle_ms: 100 },
  { can_id_hex: '0x203', can_id_dec: 515, bus: 'low', message_name: 'SES_VERSION', sender: 'SES', receivers: ['RT'], cycle_ms: 1000 },
  { can_id_hex: '0x204', can_id_dec: 516, bus: 'low', message_name: 'RT_DRIVE_CMD', sender: 'RT', receivers: ['SYS', 'MTR'], cycle_ms: 10 },
  { can_id_hex: '0x205', can_id_dec: 517, bus: 'low', message_name: 'RT_BRAKE_CMD', sender: 'RT', receivers: ['SYS'], cycle_ms: 20 },
  { can_id_hex: '0x206', can_id_dec: 518, bus: 'low', message_name: 'MTR_MOTOR_FBK', sender: 'MTR', receivers: ['RT', 'SYS', 'Host'], cycle_ms: 20 },
  { can_id_hex: '0x210', can_id_dec: 528, bus: 'low', message_name: 'RT_STATE_RPT', sender: 'RT', receivers: ['Host', 'SYS'], cycle_ms: 100 },
  { can_id_hex: '0x302', can_id_dec: 770, bus: 'low', message_name: 'HOST_LIGHT_CMD', sender: 'Host', receivers: ['RT', 'SYS'], cycle_ms: 0 },
  { can_id_hex: '0x500', can_id_dec: 1280, bus: 'low', message_name: 'SYS_NODE_STATUS', sender: 'SYS', receivers: ['RT'], cycle_ms: 200 },
  { can_id_hex: '0x501', can_id_dec: 1281, bus: 'low', message_name: 'RT_NODE_STATUS', sender: 'RT', receivers: ['SYS', 'Host'], cycle_ms: 20 },
  { can_id_hex: '0x502', can_id_dec: 1282, bus: 'low', message_name: 'MTR_NODE_STATUS', sender: 'MTR', receivers: ['RT', 'SYS', 'Host'], cycle_ms: 20 },
  { can_id_hex: '0x600', can_id_dec: 1536, bus: 'low', message_name: 'SYS_DIAG_RPT', sender: 'SYS', receivers: ['RT', 'Host'], cycle_ms: 1000 },
  { can_id_hex: '0x601', can_id_dec: 1537, bus: 'low', message_name: 'SYS_DIAG_EVENT_RPT', sender: 'SYS', receivers: ['RT'], cycle_ms: 0 },
  { can_id_hex: '0x631', can_id_dec: 1585, bus: 'low', message_name: 'MTR_DIAG_EVENT_RPT', sender: 'MTR', receivers: ['RT'], cycle_ms: 0 },
  { can_id_hex: '0x6FA', can_id_dec: 1786, bus: 'low', message_name: 'SES_TEST', sender: 'SES', receivers: ['RT'], cycle_ms: 10 },
  { can_id_hex: '0x6FB', can_id_dec: 1787, bus: 'low', message_name: 'SEB_TEST', sender: 'SEB', receivers: ['SYS', 'RT'], cycle_ms: 10 },
  { can_id_hex: '0x721', can_id_dec: 1825, bus: 'low', message_name: 'SEB_STATUS', sender: 'SEB', receivers: ['SYS', 'RT'], cycle_ms: 10 },
  { can_id_hex: '0x731', can_id_dec: 1841, bus: 'low', message_name: 'SEB_ERR_INFO', sender: 'SEB', receivers: ['SYS'], cycle_ms: 100 },
  { can_id_hex: '0x741', can_id_dec: 1857, bus: 'low', message_name: 'SEB_VERSION', sender: 'SEB', receivers: ['SYS'], cycle_ms: 1000 },
  { can_id_hex: '0x7B9', can_id_dec: 1977, bus: 'low', message_name: 'VCU_SEB_REQ', sender: 'SYS', receivers: ['SEB'], cycle_ms: 20 },
  { can_id_hex: '0x7FD', can_id_dec: 2045, bus: 'low', message_name: 'RT_HEARTBEAT', sender: 'RT', receivers: ['Host', 'SYS'], cycle_ms: 500 },
  { can_id_hex: '0x7FE', can_id_dec: 2046, bus: 'low', message_name: 'SYS_HEARTBEAT', sender: 'SYS', receivers: ['RT'], cycle_ms: 100 },
]

function buildCanBusSummary(
  catalog: CanonicalCanCatalogItem[],
  bus: 'high' | 'low',
  channel: 'CH0' | 'CH1',
  bitrateKbps: number,
  busFrames: CanRawFrame[],
  storeMessages: MessageState[],
  durationMs: number,
): CanBusSummary {
  const storeMsgMap = new Map<number, MessageState>()
  for (const m of storeMessages) {
    if (m.bus === bus || m.bus === channel.toLowerCase()) {
      storeMsgMap.set(m.can_id, m)
    }
  }

  const framesByCanId = new Map<number, CanRawFrame[]>()
  for (const f of busFrames) {
    let arr = framesByCanId.get(f.can_id)
    if (!arr) {
      arr = []
      framesByCanId.set(f.can_id, arr)
    }
    arr.push(f)
  }

  const catalogIdSet = new Set<number>()
  const entries: CanIdInventoryEntry[] = []

  for (const cat of catalog) {
    catalogIdSet.add(cat.can_id_dec)
    const matchingFrames = framesByCanId.get(cat.can_id_dec) || []
    const matchingMsg =
      storeMsgMap.get(cat.can_id_dec) ??
      storeMessages.find((m) => m.can_id === cat.can_id_dec && (m.bus === bus || m.bus === channel.toLowerCase())) ??
      storeMessages.find((m) => m.can_id === cat.can_id_dec)

    const frameCount = matchingFrames.length
    const lastFrame = matchingFrames.length > 0 ? matchingFrames[matchingFrames.length - 1] : null
    const isActive = frameCount > 0 || (matchingMsg?.age_ms != null && matchingMsg.age_ms <= durationMs)

    let observedHz: number | null = null
    if (frameCount > 0 && durationMs > 0) {
      observedHz = Number((frameCount / (durationMs / 1000)).toFixed(1))
    } else if (matchingMsg?.observed_rate_hz != null && matchingMsg.observed_rate_hz > 0) {
      observedHz = matchingMsg.observed_rate_hz
    }

    const lastPayloadHex = lastFrame?.data_hex ?? null
    const lastSeenAgeMs = matchingMsg?.age_ms ?? null

    const signals: CanIdInventoryEntry['signals'] = {}
    if (matchingMsg?.signals) {
      for (const [sigKey, sigVal] of Object.entries(matchingMsg.signals)) {
        signals[sigKey] = {
          value: sigVal.engineering_value,
          unit: sigVal.unit ?? null,
          enum_label: sigVal.enum_label ?? null,
          valid: sigVal.valid,
        }
      }
    }

    entries.push({
      can_id_hex: cat.can_id_hex,
      can_id_dec: cat.can_id_dec,
      bus,
      message_name: cat.message_name,
      sender: cat.sender,
      receivers: cat.receivers,
      expected_cycle_ms: cat.cycle_ms,
      status_in_10s: isActive ? 'ACTIVE' : 'INACTIVE',
      observed_hz: observedHz,
      frames_in_10s: frameCount,
      last_seen_age_ms: lastSeenAgeMs,
      last_payload_hex: lastPayloadHex,
      signals,
    })
  }

  // Include dynamic / observed frames on this bus that were not in catalog
  for (const [canId, matchingFrames] of framesByCanId.entries()) {
    if (!catalogIdSet.has(canId)) {
      const matchingMsg =
        storeMsgMap.get(canId) ??
        storeMessages.find((m) => m.can_id === canId && (m.bus === bus || m.bus === channel.toLowerCase())) ??
        storeMessages.find((m) => m.can_id === canId)
      const lastFrame = matchingFrames[matchingFrames.length - 1]
      const frameCount = matchingFrames.length
      const observedHz = durationMs > 0 ? Number((frameCount / (durationMs / 1000)).toFixed(1)) : null

      const signals: CanIdInventoryEntry['signals'] = {}
      if (matchingMsg?.signals) {
        for (const [sigKey, sigVal] of Object.entries(matchingMsg.signals)) {
          signals[sigKey] = {
            value: sigVal.engineering_value,
            unit: sigVal.unit ?? null,
            enum_label: sigVal.enum_label ?? null,
            valid: sigVal.valid,
          }
        }
      }

      entries.push({
        can_id_hex: `0x${canId.toString(16).toUpperCase().padStart(3, '0')}`,
        can_id_dec: canId,
        bus,
        message_name: matchingMsg?.name ?? 'DYNAMIC_UNCATALOGED',
        sender: 'UNKNOWN',
        receivers: [],
        expected_cycle_ms: 0,
        status_in_10s: 'ACTIVE',
        observed_hz: observedHz,
        frames_in_10s: frameCount,
        last_seen_age_ms: matchingMsg?.age_ms ?? null,
        last_payload_hex: lastFrame?.data_hex ?? null,
        signals,
      })
    }
  }

  // Sort by numeric CAN ID
  entries.sort((a, b) => a.can_id_dec - b.can_id_dec)

  const activeCount = entries.filter((e) => e.status_in_10s === 'ACTIVE').length
  const inactiveCount = entries.filter((e) => e.status_in_10s === 'INACTIVE').length

  return {
    bus_name: bus,
    channel,
    bitrate_kbps: bitrateKbps,
    total_catalog_can_ids: catalog.length,
    active_can_ids_count: activeCount,
    inactive_can_ids_count: inactiveCount,
    total_frames_observed: busFrames.length,
    can_ids: entries,
  }
}

// ── In-Memory Rolling Buffer (Continuously keeps last 15s) ──
const ROLLING_BUFFER_CAPACITY_MS = 15000
const rollingSteeringSamples: SteeringSample[] = []
const rollingModeSamples: ModeSample[] = []
const rollingHeartbeatSamples: HeartbeatSample[] = []

// Periodic rolling buffer sampler (every 250ms)
if (typeof window !== 'undefined') {
  window.setInterval(() => {
    const store = useAppStore.getState()
    const msgs = store.messages
    if (!msgs || msgs.length === 0) return

    const now = Date.now()
    const cutoff = now - ROLLING_BUFFER_CAPACITY_MS

    // Prune expired samples
    while (rollingSteeringSamples.length > 0 && rollingSteeringSamples[0].timestamp_ms < cutoff) {
      rollingSteeringSamples.shift()
    }
    while (rollingModeSamples.length > 0 && rollingModeSamples[0].timestamp_ms < cutoff) {
      rollingModeSamples.shift()
    }
    while (rollingHeartbeatSamples.length > 0 && rollingHeartbeatSamples[0].timestamp_ms < cutoff) {
      rollingHeartbeatSamples.shift()
    }

    // Sample current state
    const steer = getSteeringPipeline(msgs)
    const auth = getAuthorityPipeline(msgs)
    const ses = store.status?.session

    const isManual =
      ses?.confirmed_mode === 'manual' ||
      ses?.confirmed_mode === 'MANUAL' ||
      auth.sysCommandedMode === 'MANUAL' ||
      auth.sysCommandedMode === '0' ||
      steer.sesMode === 'MANUAL' ||
      !steer.rtTargetAngleDeg

    rollingSteeringSamples.push({
      timestamp_ms: now,
      rel_time_ms: 0,
      ses_actual_deg: steer.sesAngleDeg,
      rt_target_deg: steer.rtTargetAngleDeg,
      host_steer_deg: steer.hostSteerDeg,
      host_yaw_rate: steer.hostYawRate,
      ses_torque_nm: steer.sesTorqueNm,
      ses_mode: steer.sesMode,
      rt_control_enable: steer.rtSesReq ? signalIsOn(steer.rtSesReq, 'control_enable') : null,
      driver_override_active: (steer.sesTorqueNm != null && Math.abs(steer.sesTorqueNm) > 3.0),
      is_manual_mode: isManual,
    })

    rollingModeSamples.push({
      timestamp_ms: now,
      rel_time_ms: 0,
      confirmed_mode: ses?.confirmed_mode ?? 'MANUAL',
      requested_mode: ses?.requested_mode ?? 'MANUAL',
      sys_cmd_mode: auth.sysCommandedMode,
      rt_reported_mode: auth.rtReportedMode,
      ses_control_mode: steer.sesMode,
      is_manual_mode: isManual,
      estop_active: !!observeEstop(msgs, ses).any,
    })

    rollingHeartbeatSamples.push({
      timestamp_ms: now,
      rel_time_ms: 0,
      controllers: (store.topology || []).map((n) => ({
        node: n.node,
        can_id_hex: `0x${n.can_id.toString(16).toUpperCase().padStart(3, '0')}`,
        can_id_dec: n.can_id,
        bus: n.bus,
        liveness: n.liveness,
        freshness: n.freshness,
      })),
      worker_alive: store.status?.adapter?.worker_alive ?? null,
    })
  }, 250)
}

// ── Active 10-Second Recording Session Controller ──
export class TelemetryRecordingSession {
  private startTime: number = 0
  private targetDurationMs: number = 10000
  private timerHandle: number | null = null
  private sampleIntervalHandle: number | null = null
  private isRunning: boolean = false

  private steeringSamples: SteeringSample[] = []
  private modeSamples: ModeSample[] = []
  private heartbeatSamples: HeartbeatSample[] = []
  private stateSnapshots: DiagnosticExportBundle['raw_telemetry']['state_snapshots'] = []

  private onProgressCb?: (progress: number, secondsRemaining: number) => void
  private onCompleteCb?: (bundle: DiagnosticExportBundle) => void

  public start(
    durationMs: number = 10000,
    onProgress?: (progress: number, secondsRemaining: number) => void,
    onComplete?: (bundle: DiagnosticExportBundle) => void,
  ) {
    if (this.isRunning) {
      this.cancel()
    }

    this.isRunning = true
    this.targetDurationMs = durationMs
    this.startTime = Date.now()
    this.onProgressCb = onProgress
    this.onCompleteCb = onComplete

    this.steeringSamples = []
    this.modeSamples = []
    this.heartbeatSamples = []
    this.stateSnapshots = []

    // Immediately record baseline sample (t = 0)
    this.recordSample()

    // Sample at 100ms intervals
    this.sampleIntervalHandle = window.setInterval(() => {
      if (!this.isRunning) return
      const elapsed = Date.now() - this.startTime
      const progress = Math.min(1, elapsed / this.targetDurationMs)
      const remaining = Math.max(0, (this.targetDurationMs - elapsed) / 1000)

      this.recordSample()
      this.onProgressCb?.(progress, remaining)

      if (elapsed >= this.targetDurationMs) {
        void this.finishAndExport()
      }
    }, 100)
  }

  public cancel() {
    this.isRunning = false
    if (this.timerHandle) {
      window.clearTimeout(this.timerHandle)
      this.timerHandle = null
    }
    if (this.sampleIntervalHandle) {
      window.clearInterval(this.sampleIntervalHandle)
      this.sampleIntervalHandle = null
    }
  }

  public async finishAndExport(): Promise<DiagnosticExportBundle> {
    this.cancel()
    const captureDuration = Date.now() - this.startTime

    const bundle = await compileExportBundle({
      captureType: 'live_10s_record',
      durationMs: captureDuration,
      steeringSamples: this.steeringSamples,
      modeSamples: this.modeSamples,
      heartbeatSamples: this.heartbeatSamples,
      stateSnapshots: this.stateSnapshots,
    })

    this.onCompleteCb?.(bundle)
    downloadExportJson(bundle)
    return bundle
  }

  public getActiveState() {
    return {
      isRunning: this.isRunning,
      elapsedMs: this.isRunning ? Date.now() - this.startTime : 0,
      targetDurationMs: this.targetDurationMs,
      sampleCount: this.steeringSamples.length,
    }
  }

  private recordSample() {
    const store = useAppStore.getState()
    const msgs = store.messages
    const status = store.status
    const topo = store.topology
    const now = Date.now()
    const relMs = now - this.startTime

    const steer = getSteeringPipeline(msgs)
    const speed = getSpeedPipeline(msgs)
    const brake = getBrakePipeline(msgs)
    const auth = getAuthorityPipeline(msgs)
    const ses = status?.session

    const isManual =
      ses?.confirmed_mode === 'manual' ||
      ses?.confirmed_mode === 'MANUAL' ||
      auth.sysCommandedMode === 'MANUAL' ||
      auth.sysCommandedMode === '0' ||
      steer.sesMode === 'MANUAL' ||
      !steer.rtTargetAngleDeg

    const driverOverride =
      steer.sesTorqueNm != null && Math.abs(steer.sesTorqueNm) > 3.0

    // 1. Steering Sample
    this.steeringSamples.push({
      timestamp_ms: now,
      rel_time_ms: relMs,
      ses_actual_deg: steer.sesAngleDeg,
      rt_target_deg: steer.rtTargetAngleDeg,
      host_steer_deg: steer.hostSteerDeg,
      host_yaw_rate: steer.hostYawRate,
      ses_torque_nm: steer.sesTorqueNm,
      ses_mode: steer.sesMode,
      rt_control_enable: steer.rtSesReq ? signalIsOn(steer.rtSesReq, 'control_enable') : null,
      driver_override_active: driverOverride,
      is_manual_mode: isManual,
    })

    // 2. Mode Sample
    this.modeSamples.push({
      timestamp_ms: now,
      rel_time_ms: relMs,
      confirmed_mode: ses?.confirmed_mode ?? (isManual ? 'MANUAL' : 'AUTO'),
      requested_mode: ses?.requested_mode ?? (isManual ? 'MANUAL' : 'AUTO'),
      sys_cmd_mode: auth.sysCommandedMode,
      rt_reported_mode: auth.rtReportedMode,
      ses_control_mode: steer.sesMode,
      is_manual_mode: isManual,
      estop_active: !!observeEstop(msgs, ses).any,
    })

    // 3. Heartbeat Sample
    this.heartbeatSamples.push({
      timestamp_ms: now,
      rel_time_ms: relMs,
      controllers: (topo || []).map((n) => ({
        node: n.node,
        can_id_hex: `0x${n.can_id.toString(16).toUpperCase().padStart(3, '0')}`,
        can_id_dec: n.can_id,
        bus: n.bus,
        liveness: n.liveness,
        freshness: n.freshness,
      })),
      worker_alive: status?.adapter?.worker_alive ?? null,
    })

    // 4. Compact state snapshot (every 250ms)
    if (this.stateSnapshots.length === 0 || relMs - this.stateSnapshots[this.stateSnapshots.length - 1].rel_time_ms >= 250) {
      this.stateSnapshots.push({
        rel_time_ms: relMs,
        message_count: msgs.length,
        speed: {
          host_speed_mmps: speed.hostSpeed ?? null,
          rt_speed_mmps: speed.rtSpeed ?? null,
          mtr_fbk_speed_mmps: speed.mtrFbkSpeed ?? speed.physicalSpeed ?? null,
          wheel_speed_mmps: speed.physicalSpeed ?? null,
        },
        steering: {
          ses_angle_deg: steer.sesAngleDeg ?? null,
          rt_target_deg: steer.rtTargetAngleDeg ?? null,
          host_steer_deg: steer.hostSteerDeg ?? null,
          yaw_rate: steer.hostYawRate ?? null,
          torque_nm: steer.sesTorqueNm ?? null,
        },
        brake: {
          host_pressure_kpa: brake.hostPressureKpa ?? null,
          rt_pressure_kpa: brake.rtPressureKpa ?? null,
          actual_pressure_kpa: brake.actualPressureKpa ?? null,
          actual_stroke_mm: brake.actualStrokeMm ?? null,
        },
      })
    }
  }
}

// Global active recorder instance
export const activeRecordingSession = new TelemetryRecordingSession()

// ── Instant Export of Rolling Buffer (Previous 10 Seconds) ──
export async function exportInstantRollingBuffer(durationMs: number = 10000): Promise<DiagnosticExportBundle> {
  const now = Date.now()
  const cutoff = now - durationMs

  const steeringSamples = rollingSteeringSamples.filter((s) => s.timestamp_ms >= cutoff).map((s) => ({
    ...s,
    rel_time_ms: s.timestamp_ms - cutoff,
  }))

  const modeSamples = rollingModeSamples.filter((s) => s.timestamp_ms >= cutoff).map((s) => ({
    ...s,
    rel_time_ms: s.timestamp_ms - cutoff,
  }))

  const heartbeatSamples = rollingHeartbeatSamples.filter((s) => s.timestamp_ms >= cutoff).map((s) => ({
    ...s,
    rel_time_ms: s.timestamp_ms - cutoff,
  }))

  const bundle = await compileExportBundle({
    captureType: 'instant_10s_buffer',
    durationMs,
    steeringSamples,
    modeSamples,
    heartbeatSamples,
    stateSnapshots: [],
  })

  downloadExportJson(bundle)
  return bundle
}

// ── Compile Export Bundle with In-Depth LLM Diagnostic Logic ──
async function compileExportBundle(params: {
  captureType: 'live_10s_record' | 'instant_10s_buffer'
  durationMs: number
  steeringSamples: SteeringSample[]
  modeSamples: ModeSample[]
  heartbeatSamples: HeartbeatSample[]
  stateSnapshots: DiagnosticExportBundle['raw_telemetry']['state_snapshots']
}): Promise<DiagnosticExportBundle> {
  const store = useAppStore.getState()
  const status = store.status
  const msgs = store.messages
  const topo = store.topology

  // Fetch recent CAN frames and logs in parallel
  let frames: DiagnosticExportBundle['raw_telemetry']['can_frames'] = []
  let auditLogs: Array<Record<string, unknown>> = []

  try {
    const [histRes, logsRes] = await Promise.all([
      api.history(4096).catch(() => null),
      fetch('/api/v1/logs?limit=500').then((r) => r.json()).catch(() => null),
    ])

    if (histRes?.frames) {
      frames = histRes.frames.map((f) => ({
        global_sequence: f.global_sequence,
        bus: f.bus,
        can_id: f.can_id,
        can_id_hex: `0x${f.can_id.toString(16).toUpperCase().padStart(3, '0')}`,
        dlc: f.dlc,
        data_hex: f.data_hex,
        direction: f.direction,
        backend_arrival_ns: f.backend_arrival_ns,
      }))
    }

    if (logsRes?.logs) {
      auditLogs = logsRes.logs
    }
  } catch {
    /* gracefully continue */
  }

  // 1. Vehicle Mode & Authority Analysis
  const wasInManualMode =
    params.modeSamples.some((m) => m.is_manual_mode) ||
    status?.session?.confirmed_mode === 'manual' ||
    status?.session?.confirmed_mode === 'MANUAL'

  const manualModeEvidence: string[] = []
  if (status?.session?.confirmed_mode === 'manual' || status?.session?.confirmed_mode === 'MANUAL') {
    manualModeEvidence.push('Vehicle session confirmed_mode is set to MANUAL')
  }
  const sysModeMsg = findMsg(msgs, 'SYS_MODE_CMD')
  if (sysModeMsg) {
    const sysMode = signalText(sysModeMsg, 'mode')
    manualModeEvidence.push(`SYS_MODE_CMD (0x110) mode signal: "${sysMode}"`)
  }
  const rtSesReq = findMsg(msgs, 'VCU_SES_REQ')
  if (rtSesReq) {
    const ctrlEn = signalIsOn(rtSesReq, 'control_enable')
    if (!ctrlEn) {
      manualModeEvidence.push('VCU_SES_REQ (0x169) control_enable is 0 (Closed-loop steering disabled; manual driver authority)')
    } else {
      manualModeEvidence.push('VCU_SES_REQ (0x169) control_enable is 1 (Autonomous steering active)')
    }
  }
  const sesStatus = findMsg(msgs, 'SES_STATUS')
  if (sesStatus) {
    const modeCode = signalNum(sesStatus, 'control_mode')
    manualModeEvidence.push(`SES_STATUS (0x201) control_mode: ${modeCode === 1 ? '1 (AUTO)' : modeCode === 0 ? '0 (MANUAL)' : 'Unknown'}`)
  }

  const estopObs = observeEstop(msgs, status?.session)

  // 2. Steering Analysis & Changes
  const sesAngles = params.steeringSamples
    .map((s) => s.ses_actual_deg)
    .filter((v): v is number => typeof v === 'number' && Number.isFinite(v))

  const initialSesAngle = sesAngles.length > 0 ? sesAngles[0] : null
  const finalSesAngle = sesAngles.length > 0 ? sesAngles[sesAngles.length - 1] : null
  const minSesAngle = sesAngles.length > 0 ? Math.min(...sesAngles) : null
  const maxSesAngle = sesAngles.length > 0 ? Math.max(...sesAngles) : null
  const totalAngleChange =
    initialSesAngle != null && finalSesAngle != null ? finalSesAngle - initialSesAngle : null

  // Calculate following errors and anomalies
  let maxFollowingError: number | null = null
  const anomalies: string[] = []

  params.steeringSamples.forEach((sample) => {
    if (
      typeof sample.ses_actual_deg === 'number' &&
      typeof sample.rt_target_deg === 'number' &&
      sample.rt_control_enable
    ) {
      const err = Math.abs(sample.ses_actual_deg - sample.rt_target_deg)
      if (maxFollowingError === null || err > maxFollowingError) {
        maxFollowingError = err
      }
    }
  })

  if (typeof maxFollowingError === 'number' && maxFollowingError > 10.0) {
    anomalies.push(`High following error: Max discrepancy of ${(maxFollowingError as number).toFixed(1)}° between RT target and SES actual angle`)
  }

  const driverOverridesCount = params.steeringSamples.filter((s) => s.driver_override_active).length
  if (driverOverridesCount > 0) {
    anomalies.push(`Driver torque override detected in ${driverOverridesCount} samples during the 10s window`)
  }

  if (wasInManualMode && totalAngleChange != null && Math.abs(totalAngleChange) > 2.0) {
    anomalies.push(`Driver manual steering rotation observed: Angle shifted from ${initialSesAngle?.toFixed(1)}° to ${finalSesAngle?.toFixed(1)}° (Δ ${totalAngleChange > 0 ? '+' : ''}${totalAngleChange.toFixed(1)}°)`)
  }

  // 3. Heartbeat & Liveness Analysis
  const currentTopo = topo || []
  const criticalEcus = ['SYS', 'RT', 'SES', 'SEB', 'MTR', 'HOST']
  const nodesHealth = currentTopo.map((n) => ({
    node: n.node,
    can_id_hex: `0x${n.can_id.toString(16).toUpperCase().padStart(3, '0')}`,
    can_id_dec: n.can_id,
    bus: n.bus,
    liveness: n.liveness,
    freshness: n.freshness,
  }))

  const onlineCount = nodesHealth.filter((n) => n.liveness === 'live').length
  const allControllersHealthy = onlineCount >= 5 && !nodesHealth.some((n) => n.liveness === 'lost')

  // 4. Activation Gates Analysis
  const gateEval = evaluateActivationGates(msgs, false)
  const inhibitors: string[] = []
  gateEval.gates.forEach((g) => {
    if (g.status !== 'ready') {
      inhibitors.push(`[${g.controller}] ${g.statusLabel}: ${g.blocker ?? 'Inhibited'}`)
    }
  })

  // 5. CAN IDs Inventory (Strict High Bus CH0 & Low Bus CH1 Separation)
  const highFrames = frames.filter((f) => f.bus === 'high' || f.bus === 'ch0' || f.bus === 'CH0')
  const lowFrames = frames.filter(
    (f) => f.bus === 'low' || f.bus === 'ch1' || f.bus === 'CH1' || (!['high', 'ch0', 'CH0'].includes(f.bus)),
  )

  const highBusSummary = buildCanBusSummary(
    CAN_CATALOG_HIGH,
    'high',
    'CH0',
    500,
    highFrames,
    msgs,
    params.durationMs,
  )
  const lowBusSummary = buildCanBusSummary(
    CAN_CATALOG_LOW,
    'low',
    'CH1',
    500,
    lowFrames,
    msgs,
    params.durationMs,
  )

  const canBusInventory: CanBusInventory = {
    high_bus: highBusSummary,
    low_bus: lowBusSummary,
  }

  const combinedCanInventory: CanIdInventoryEntry[] = [
    ...highBusSummary.can_ids,
    ...lowBusSummary.can_ids,
  ]

  // 6. Protocol Diagnostics & Blocker Decoding
  const activeDiagEvents: DiagnosticExportBundle['llm_diagnostic_context']['protocol_diagnostics']['active_diagnostic_events'] = []
  for (const m of msgs) {
    if (m.can_id === 0x601 || m.can_id === 0x621 || m.can_id === 0x631 || m.name?.includes('DIAG_EVENT_RPT')) {
      const parsed = parseDiagEventReport(m)
      if (parsed) {
        activeDiagEvents.push({
          can_id_hex: `0x${m.can_id.toString(16).toUpperCase().padStart(3, '0')}`,
          diag_id_hex: parsed.diagIdHex,
          diag_key: parsed.def?.key ?? 'UNKNOWN_DIAG',
          reporter: parsed.def?.reporter ?? 'CAN',
          subsystem: parsed.def?.subsystem ?? 'SAFETY',
          severity: parsed.def?.severity ?? 'ERROR',
          reaction: parsed.def?.reaction ?? 'NONE',
          state: parsed.stateName,
          is_root_estop_cause: parsed.isRootEstop,
          decoded_snapshot: parsed.decodedSnapshot,
          description: parsed.def?.description ?? '',
        })
      }
    }
  }

  // Blocker mask from 0x115 SYS_ESTOP_RESET_RSP
  let sysEstopBlockers: string[] = []
  const resetMsg = findMsg(msgs, 'SYS_ESTOP_RESET_RSP') || findMsg(msgs, 'ESTOP_RESET_REPORT') || msgs.find((m) => m.can_id === 0x115)
  if (resetMsg) {
    const parsedReset = parseEstopResetRsp(resetMsg)
    if (parsedReset) {
      sysEstopBlockers = parsedReset.blockers.map((b) => `${b.name}: ${b.description}`)
    }
  }

  // Node states from 0x500, 0x501, 0x502
  const ecuNodeStates: Record<string, { state: string; block_mask: string; blockers: string[] }> = {}
  for (const m of msgs) {
    if (m.can_id === 0x500 || m.can_id === 0x501 || m.can_id === 0x502 || m.name?.includes('NODE_STATUS')) {
      const parsed = parseNodeStatus(m)
      if (parsed) {
        ecuNodeStates[parsed.reporter] = {
          state: parsed.nodeStateName,
          block_mask: `0x${parsed.blockMask.toString(16).padStart(4, '0')}`,
          blockers: parsed.blockers.map((b) => b.name),
        }
      }
    }
  }

  // CAN controller health
  const sysDiag = findMsg(msgs, 'SYS_DIAG_RPT')
  const rtDiag = findMsg(msgs, 'RT_DIAG_RPT')
  const canBusHealth = {
    low_can_twai: {
      tec: sysDiag ? signalNum(sysDiag, 'tec') ?? undefined : undefined,
      rec: sysDiag ? signalNum(sysDiag, 'rec') ?? undefined : undefined,
    },
    high_can_mcp: {
      tec: rtDiag ? signalNum(rtDiag, 'mcp_tec') ?? undefined : undefined,
      rec: rtDiag ? signalNum(rtDiag, 'mcp_rec') ?? undefined : undefined,
      bus_off: rtDiag ? signalNum(rtDiag, 'mcp_bus_off') === 1 : undefined,
    },
  }

  const protocolDiagnostics: DiagnosticExportBundle['llm_diagnostic_context']['protocol_diagnostics'] = {
    active_diagnostic_events: activeDiagEvents,
    sys_estop_blockers: sysEstopBlockers,
    ecu_node_states: ecuNodeStates,
    can_bus_health: canBusHealth,
  }

  // 7. Pre-Engineered LLM Diagnostic Prompt
  const llmPrompt = generateLlmPromptMarkdown({
    wasInManualMode,
    manualModeEvidence,
    initialSesAngle,
    finalSesAngle,
    minSesAngle,
    maxSesAngle,
    totalAngleChange,
    maxFollowingError,
    anomalies,
    onlineCount,
    totalControllers: criticalEcus.length,
    allControllersHealthy,
    inhibitors,
    canBusInventory,
    estopObs,
    protocolDiagnostics,
    steeringSamplesCount: params.steeringSamples.length,
    durationMs: params.durationMs,
  })

  const executiveSummary = [
    `Vehicle Mode: ${wasInManualMode ? 'MANUAL' : 'AUTO'}.`,
    `Steering: SES Actual Angle went from ${initialSesAngle != null ? `${initialSesAngle.toFixed(1)}°` : '—'} to ${finalSesAngle != null ? `${finalSesAngle.toFixed(1)}°` : '—'} (Δ ${totalAngleChange != null ? `${totalAngleChange > 0 ? '+' : ''}${totalAngleChange.toFixed(1)}°` : '0°'}).`,
    `Heartbeats: ${onlineCount}/${criticalEcus.length} Controllers live.`,
    `CAN Activity: High Bus (CH0) ${highBusSummary.active_can_ids_count}/${highBusSummary.total_catalog_can_ids} active IDs (${highFrames.length} frames) · Low Bus (CH1) ${lowBusSummary.active_can_ids_count}/${lowBusSummary.total_catalog_can_ids} active IDs (${lowFrames.length} frames).`,
    sysEstopBlockers.length > 0 ? `SYS Reset Blockers: ${sysEstopBlockers.join('; ')}.` : '',
    activeDiagEvents.length > 0 ? `Diagnostic Events: ${activeDiagEvents.map((e) => e.diag_key).join(', ')}.` : '',
    inhibitors.length > 0 ? `Activation Inhibitors: ${inhibitors.join('; ')}.` : 'Activation Gates: All 6 Gates Ready.',
  ].filter(Boolean).join(' ')

  return {
    export_metadata: {
      format: 'e-trike-telemetry-10s-bundle',
      version: '1.0',
      exported_at_iso: new Date().toISOString(),
      exported_at_unix_ms: Date.now(),
      capture_type: params.captureType,
      capture_duration_ms: params.durationMs,
      vehicle_profile: status?.session?.profile ?? 'full_vehicle',
      transport_mode: 'real',
      destination: status?.session?.destination ?? 'vcan0',
      wire_hash: status?.wire_hash ?? 'unknown',
      semantic_hash: status?.semantic_hash,
    },
    llm_diagnostic_context: {
      executive_summary: executiveSummary,
      vehicle_mode_analysis: {
        overall_mode: wasInManualMode ? 'MANUAL' : 'AUTO',
        was_in_manual_mode: wasInManualMode,
        manual_mode_evidence: manualModeEvidence,
        mode_timeline: params.modeSamples,
        estop_active: !!estopObs.any,
        estop_cause_summary: estopObs.any ? estopObs.label : undefined,
      },
      steering_analysis: {
        subsystem_summary: 'Host Steer (0x303) → RT Setpoint (0x169) → SES Closed-Loop Angle Feedback (0x201)',
        is_manual_steering: wasInManualMode,
        initial_ses_angle_deg: initialSesAngle,
        final_ses_angle_deg: finalSesAngle,
        min_ses_angle_deg: minSesAngle,
        max_ses_angle_deg: maxSesAngle,
        total_angle_change_deg: totalAngleChange,
        max_following_error_deg: maxFollowingError,
        sample_count: params.steeringSamples.length,
        anomalies_detected: anomalies,
        steering_samples: params.steeringSamples,
      },
      heartbeat_analysis: {
        summary: allControllersHealthy ? 'All 6 critical ECUs reporting healthy periodic heartbeats.' : 'One or more controller heartbeats degraded or timed out.',
        all_controllers_healthy: allControllersHealthy,
        controllers_online_count: onlineCount,
        total_controllers_count: criticalEcus.length,
        nodes: nodesHealth,
        heartbeat_timeline: params.heartbeatSamples,
      },
      activation_gates_analysis: {
        all_gates_cleared: gateEval.allCleared,
        cleared_count: gateEval.clearedGates,
        total_count: gateEval.totalGates,
        inhibitors,
        gates: gateEval.gates.map((g) => ({
          id: g.id,
          controller: g.controller,
          cleared: g.status === 'ready',
          reason: g.statusLabel,
          blockers: g.blocker ? [g.blocker] : [],
          required_can_ids: g.primaryCanIds,
        })),
      },
      can_bus_inventory: canBusInventory,
      can_ids_inventory: combinedCanInventory,
      protocol_diagnostics: protocolDiagnostics,
      llm_prompt_markdown: llmPrompt,
    },
    raw_telemetry: {
      state_snapshots: params.stateSnapshots,
      can_frames_by_bus: {
        high: highFrames,
        low: lowFrames,
      },
      can_frames: frames,
      audit_logs: auditLogs,
      latest_messages: msgs,
    },
  }
}

function formatBusMarkdownTable(summary: CanBusSummary): string {
  const lines: string[] = [
    `### ${summary.channel} (${summary.bus_name.toUpperCase()} Bus · ${summary.bitrate_kbps} kbps)`,
    `- **Activity**: ${summary.active_can_ids_count}/${summary.total_catalog_can_ids} Active Catalog IDs · ${summary.total_frames_observed} Frames Captured · ${summary.inactive_can_ids_count} Inactive`,
    '',
    '| CAN ID | Message Name | Sender | Receivers | Cycle | Status (10s) | Rate (Hz) | Frames | Last Seen | Payload / Signals Snapshot |',
    '| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |',
  ]

  for (const item of summary.can_ids) {
    const cycleStr = item.expected_cycle_ms > 0 ? `${item.expected_cycle_ms}ms` : 'Event'
    const statusBadge = item.status_in_10s === 'ACTIVE' ? '**ACTIVE**' : 'INACTIVE'
    const rateStr = item.observed_hz != null ? `${item.observed_hz} Hz` : '—'
    const framesStr = item.frames_in_10s > 0 ? `${item.frames_in_10s}` : '0'
    const lastSeenStr =
      item.last_seen_age_ms != null
        ? `${item.last_seen_age_ms}ms ago`
        : item.status_in_10s === 'ACTIVE'
          ? '<10s'
          : 'Never'

    let snapshotStr = '—'
    const sigEntries = Object.entries(item.signals)
    if (sigEntries.length > 0) {
      snapshotStr = sigEntries
        .slice(0, 3)
        .map(([k, s]) => `${k}=${s.value}${s.unit ? ` ${s.unit}` : ''}${s.enum_label ? ` (${s.enum_label})` : ''}`)
        .join(', ')
      if (sigEntries.length > 3) snapshotStr += ` (+${sigEntries.length - 3} more)`
    } else if (item.last_payload_hex) {
      snapshotStr = `Hex: [${item.last_payload_hex}]`
    }

    const safeName = item.message_name.replace(/\|/g, '/')
    const safeSender = item.sender.replace(/\|/g, '/')
    const safeReceivers = (item.receivers.join(', ') || '—').replace(/\|/g, '/')
    const safeSnapshot = snapshotStr.replace(/\|/g, '/')

    lines.push(
      `| \`${item.can_id_hex}\` (${item.can_id_dec}) | \`${safeName}\` | ${safeSender} | ${safeReceivers} | ${cycleStr} | ${statusBadge} | ${rateStr} | ${framesStr} | ${lastSeenStr} | ${safeSnapshot} |`,
    )
  }

  return lines.join('\n')
}

// ── Markdown Diagnostic Prompt Generator for LLMs ──
function generateLlmPromptMarkdown(ctx: {
  wasInManualMode: boolean
  manualModeEvidence: string[]
  initialSesAngle: number | null
  finalSesAngle: number | null
  minSesAngle: number | null
  maxSesAngle: number | null
  totalAngleChange: number | null
  maxFollowingError: number | null
  anomalies: string[]
  onlineCount: number
  totalControllers: number
  allControllersHealthy: boolean
  inhibitors: string[]
  canBusInventory: CanBusInventory
  estopObs: { any?: boolean; label?: string }
  protocolDiagnostics: DiagnosticExportBundle['llm_diagnostic_context']['protocol_diagnostics']
  steeringSamplesCount: number
  durationMs: number
}): string {
  const diagEvents = ctx.protocolDiagnostics.active_diagnostic_events
  const blockers = ctx.protocolDiagnostics.sys_estop_blockers
  const nodes = ctx.protocolDiagnostics.ecu_node_states
  const bus = ctx.protocolDiagnostics.can_bus_health

  return `# e-Trike Telemetry Diagnostic Snapshot (10-Second Window)
You are an expert embedded systems & automotive CAN protocol diagnostic engineer analyzing an autonomous/drive-by-wire electric trike.

## Vehicle Operational State
- **Mode**: ${ctx.wasInManualMode ? '**MANUAL MODE** (Driver manual authority)' : '**AUTONOMOUS MODE** (Drive-by-wire)'}
- **ESTOP Active**: ${ctx.estopObs.any ? `YES — Cause: ${ctx.estopObs.label}` : 'NO (Clear)'}
- **Recording Duration**: ${(ctx.durationMs / 1000).toFixed(1)} seconds (${ctx.steeringSamplesCount} samples recorded)
- **Manual Mode Indicators**:
${ctx.manualModeEvidence.map((e) => `  - ${e}`).join('\n')}

## Real CAN Protocol Diagnostics & Active Blockers (contracts/*.yaml & diagnostics.yaml)
- **SYS ESTOP Reset Blockers (0x115)**: ${blockers.length > 0 ? blockers.join('; ') : 'None (No reset blockers asserted)'}
- **ECU Operating Node States (0x500 / 0x501 / 0x502)**:
  - SYS: ${nodes.SYS ? `${nodes.SYS.state} (block_mask: ${nodes.SYS.block_mask}${nodes.SYS.blockers.length > 0 ? ` - ${nodes.SYS.blockers.join(', ')}` : ''})` : 'STANDBY'}
  - RT: ${nodes.RT ? `${nodes.RT.state} (block_mask: ${nodes.RT.block_mask}${nodes.RT.blockers.length > 0 ? ` - ${nodes.RT.blockers.join(', ')}` : ''})` : 'STANDBY'}
  - MTR: ${nodes.MTR ? `${nodes.MTR.state} (block_mask: ${nodes.MTR.block_mask}${nodes.MTR.blockers.length > 0 ? ` - ${nodes.MTR.blockers.join(', ')}` : ''})` : 'STANDBY'}
- **Diagnostic Plane Events (0x601 / 0x621 / 0x631)**:
${diagEvents.length > 0
  ? diagEvents.map((e) => `  - [${e.reporter}] \`${e.diag_id_hex}\` **${e.diag_key}** (${e.severity}, Reaction: ${e.reaction}) — State: ${e.state}, ${e.decoded_snapshot}${e.is_root_estop_cause ? ' ★ ROOT ESTOP TRIGGER' : ''}`).join('\n')
  : '  - None (Diagnostic plane clear; 0 active faults reported)'}
- **CAN Controller Telemetry**:
  - Low CAN TWAI: TEC=${bus.low_can_twai.tec ?? 'N/A'}, REC=${bus.low_can_twai.rec ?? 'N/A'}
  - High CAN MCP2515: TEC=${bus.high_can_mcp.tec ?? 'N/A'}, REC=${bus.high_can_mcp.rec ?? 'N/A'}, Bus-Off=${bus.high_can_mcp.bus_off ? 'YES' : 'NO'}

## Steering Subsystem Tracking
- **Initial SES Angle (0x201)**: ${ctx.initialSesAngle != null ? `${ctx.initialSesAngle.toFixed(1)}°` : '—'}
- **Final SES Angle (0x201)**: ${ctx.finalSesAngle != null ? `${ctx.finalSesAngle.toFixed(1)}°` : '—'}
- **Angle Range**: [${ctx.minSesAngle != null ? `${ctx.minSesAngle.toFixed(1)}°` : '—'}, ${ctx.maxSesAngle != null ? `${ctx.maxSesAngle.toFixed(1)}°` : '—'}]
- **Total Net Change**: ${ctx.totalAngleChange != null ? `${ctx.totalAngleChange > 0 ? '+' : ''}${ctx.totalAngleChange.toFixed(1)}°` : '—'}
- **Max Closed-Loop Following Error**: ${ctx.maxFollowingError != null ? `${ctx.maxFollowingError.toFixed(1)}°` : 'N/A (Manual)'}
${ctx.anomalies.length > 0 ? `\n### Observed Steering Anomalies / Notes:\n${ctx.anomalies.map((a) => `- ⚠ ${a}`).join('\n')}` : '\n- No steering errors or anomalies detected.'}

## Controller Heartbeat & Health Matrix
- **Liveness**: ${ctx.onlineCount}/${ctx.totalControllers} critical ECUs online (${ctx.allControllersHealthy ? 'HEALTHY' : 'DEGRADED'})
- **Controllers Observed**: SYS (0x011), RT (0x012), SES (0x201), SEB (0x721), MTR (0x7FE), HOST (0x300)

## Activation Gates & Inhibitors
${ctx.inhibitors.length > 0 ? ctx.inhibitors.map((i) => `- ❌ ${i}`).join('\n') : '- ✓ All 6 Activation Gates are satisfied for autonomous operation.'}

## CAN Bus Traffic & Inventory Matrix (Separated by Bus)

${formatBusMarkdownTable(ctx.canBusInventory.high_bus)}

${formatBusMarkdownTable(ctx.canBusInventory.low_bus)}

---
## Questions for Bug Diagnosis:
1. Did the steering actuator behave consistently with the vehicle authority mode (Manual vs Auto)?
2. If autonomous mode was requested, what specific controller gate or CAN signal inhibited engagement?
3. Were there any heartbeat timeouts or packet drop anomalies across High TWAI / Low MCP buses?
4. Are there any active diagnostic codes (0x601 / 0x621 / 0x631) or blocker masks (0x115) preventing operation?
`
}

// ── Trigger Browser JSON Download & Persist to tem/control-toolkit-logs ──
export function downloadExportJson(bundle: DiagnosticExportBundle) {
  const filename = `etrike-telemetry-10s-${Date.now()}.json`
  const jsonStr = JSON.stringify(bundle, null, 2)

  // Persist directly to gitignored tem/control-toolkit-logs/ via backend API
  api.saveExport(bundle, filename).catch(() => undefined)

  // Trigger browser download
  const blob = new Blob([jsonStr], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  a.click()
  URL.revokeObjectURL(url)
}
