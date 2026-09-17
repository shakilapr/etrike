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
    can_ids_inventory: Array<{
      can_id_hex: string
      can_id_dec: number
      bus: string
      message_name: string
      freshness: string
      observed_hz: number | null
      age_ms: number | null
      signals: Record<string, unknown>
    }>
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
    can_frames: Array<{
      global_sequence: number
      bus: string
      can_id: number
      can_id_hex: string
      dlc: number
      data_hex: string
      direction: string
      backend_arrival_ns: number
    }>
    audit_logs: Array<Record<string, unknown>>
    latest_messages: MessageState[]
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
      if (maxFollowingError == null || err > maxFollowingError) {
        maxFollowingError = err
      }
    }
  })

  if (maxFollowingError != null && maxFollowingError > 10.0) {
    anomalies.push(`High following error: Max discrepancy of ${maxFollowingError.toFixed(1)}° between RT target and SES actual angle`)
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
    if (!g.cleared) {
      inhibitors.push(`[${g.controller}] ${g.reason}: ${g.blockers.join(', ')}`)
    }
  })

  // 5. CAN IDs Inventory
  const canInventory = msgs.map((m) => ({
    can_id_hex: `0x${m.can_id.toString(16).toUpperCase().padStart(3, '0')}`,
    can_id_dec: m.can_id,
    bus: m.bus,
    message_name: m.name ?? 'UNKNOWN',
    freshness: m.freshness,
    observed_hz: m.observed_rate_hz ?? null,
    age_ms: m.age_ms ?? null,
    signals: Object.entries(m.signals || {}).reduce(
      (acc, [k, s]) => {
        acc[k] = {
          value: s.engineering_value,
          unit: s.unit ?? null,
          enum: s.enum_label ?? null,
          valid: s.valid,
        }
        return acc
      },
      {} as Record<string, unknown>,
    ),
  }))

  // 6. Pre-Engineered LLM Diagnostic Prompt
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
    canInventory,
    estopObs,
    steeringSamplesCount: params.steeringSamples.length,
    durationMs: params.durationMs,
  })

  const executiveSummary = [
    `Vehicle Mode: ${wasInManualMode ? 'MANUAL' : 'AUTO'}.`,
    `Steering: SES Actual Angle went from ${initialSesAngle != null ? `${initialSesAngle.toFixed(1)}°` : '—'} to ${finalSesAngle != null ? `${finalSesAngle.toFixed(1)}°` : '—'} (Δ ${totalAngleChange != null ? `${totalAngleChange > 0 ? '+' : ''}${totalAngleChange.toFixed(1)}°` : '0°'}).`,
    `Heartbeats: ${onlineCount}/${criticalEcus.length} Controllers live.`,
    `CAN Activity: ${canInventory.length} CAN IDs observed, ${frames.length} frames logged.`,
    inhibitors.length > 0 ? `Activation Inhibitors: ${inhibitors.join('; ')}.` : 'Activation Gates: All 6 Gates Ready.',
  ].join(' ')

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
          cleared: g.cleared,
          reason: g.reason,
          blockers: g.blockers,
          required_can_ids: g.requiredCanIds,
        })),
      },
      can_ids_inventory: canInventory,
      llm_prompt_markdown: llmPrompt,
    },
    raw_telemetry: {
      state_snapshots: params.stateSnapshots,
      can_frames: frames,
      audit_logs: auditLogs,
      latest_messages: msgs,
    },
  }
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
  canInventory: Array<{ can_id_hex: string; bus: string; message_name: string }>
  estopObs: { any?: boolean; label?: string }
  steeringSamplesCount: number
  durationMs: number
}): string {
  return `# e-Trike Telemetry Diagnostic Snapshot (10-Second Window)
You are an expert embedded systems & automotive CAN protocol diagnostic engineer analyzing an autonomous/drive-by-wire electric trike.

## Vehicle Operational State
- **Mode**: ${ctx.wasInManualMode ? '**MANUAL MODE** (Driver manual authority)' : '**AUTONOMOUS MODE** (Drive-by-wire)'}
- **ESTOP Active**: ${ctx.estopObs.any ? `YES — Cause: ${ctx.estopObs.label}` : 'NO (Clear)'}
- **Recording Duration**: ${(ctx.durationMs / 1000).toFixed(1)} seconds (${ctx.steeringSamplesCount} samples recorded)
- **Manual Mode Indicators**:
${ctx.manualModeEvidence.map((e) => `  - ${e}`).join('\n')}

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

## Observed CAN IDs (${ctx.canInventory.length} Active):
${ctx.canInventory.map((c) => `- \`${c.can_id_hex}\` (${c.bus} CAN) — \`${c.message_name}\``).join('\n')}

---
## Questions for Bug Diagnosis:
1. Did the steering actuator behave consistently with the vehicle authority mode (Manual vs Auto)?
2. If autonomous mode was requested, what specific controller gate or CAN signal inhibited engagement?
3. Were there any heartbeat timeouts or packet drop anomalies across High TWAI / Low MCP buses?
`
}

// ── Trigger Browser JSON Download ──
export function downloadExportJson(bundle: DiagnosticExportBundle) {
  const jsonStr = JSON.stringify(bundle, null, 2)
  const blob = new Blob([jsonStr], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `etrike-telemetry-10s-${Date.now()}.json`
  a.click()
  URL.revokeObjectURL(url)
}
