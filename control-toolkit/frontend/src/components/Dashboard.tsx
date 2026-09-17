import { useEffect, useMemo, useState } from 'react'
import { useAppStore } from '../store'
import { api } from '../api'
import {
  getBrakePipeline,
  getSpeedPipeline,
  getSteeringPipeline,
} from '../lib/signals'
import { runFullProtocolAudit } from '../lib/protocolAudit'
import { evaluateActivationGates } from '../lib/activationGates'
import {
  activeRecordingSession,
  exportInstantRollingBuffer,
  downloadExportJson,
  type DiagnosticExportBundle,
} from '../lib/telemetryRecorder'
import { MultiMeter } from './MultiMeter'
import { SteeringMeter } from './SteeringMeter'
import { BrakeMeter } from './BrakeMeter'
import { CanAuditLogger } from './CanAuditLogger'
import { ControllerActivationMatrix } from './ControllerActivationMatrix'
import { WorkspaceShell } from './WorkspaceShell'
import { StatusPill } from './primitives'

export function Dashboard() {
  const messages = useAppStore((s) => s.messages)
  const status = useAppStore((s) => s.status)

  const [unitMode, setUnitMode] = useState<'kmh' | 'mmps'>('kmh')
  const [demoMode, setDemoMode] = useState<boolean>(false)
  const [activeView, setActiveView] = useState<'cluster' | 'tables'>('cluster')
  const [activeTab, setActiveTab] = useState<'protocol' | 'pipeline' | 'gates'>('protocol')
  const [chassisView, setChassisView] = useState<'both' | 'steer' | 'brake'>('both')
  const [dictMessages, setDictMessages] = useState<Array<Record<string, unknown>> | null>(null)

  // 10-Second Telemetry Recording & LLM Export State
  const [isRecording, setIsRecording] = useState(false)
  const [recordingRemaining, setRecordingRemaining] = useState(10.0)
  const [exportDropdownOpen, setExportDropdownOpen] = useState(false)
  const [exportBundle, setExportBundle] = useState<DiagnosticExportBundle | null>(null)
  const [copiedPrompt, setCopiedPrompt] = useState(false)

  const handleStartRecording = () => {
    setExportDropdownOpen(false)
    setIsRecording(true)
    setRecordingRemaining(10.0)
    activeRecordingSession.start(
      10000,
      (_progress, remaining) => {
        setRecordingRemaining(remaining)
      },
      (bundle) => {
        setIsRecording(false)
        setExportBundle(bundle)
      },
    )
  }

  const handleStopRecordingEarly = () => {
    setIsRecording(false)
    void activeRecordingSession.finishAndExport().then((bundle) => {
      setExportBundle(bundle)
    })
  }

  const handleExportInstantBuffer = async () => {
    setExportDropdownOpen(false)
    const bundle = await exportInstantRollingBuffer(10000)
    setExportBundle(bundle)
  }

  const handleCopyPrompt = () => {
    if (!exportBundle?.llm_diagnostic_context?.llm_prompt_markdown) return
    void navigator.clipboard.writeText(exportBundle.llm_diagnostic_context.llm_prompt_markdown).then(() => {
      setCopiedPrompt(true)
      setTimeout(() => setCopiedPrompt(false), 2500)
    })
  }

  useEffect(() => {
    let cancel = false
    void api
      .protocolDictionary()
      .then((res) => {
        if (!cancel && res?.messages) {
          setDictMessages(res.messages)
        }
      })
      .catch(() => undefined)
    return () => {
      cancel = true
    }
  }, [])

  // 3-Tier Subsystem Pipelines
  const speed = getSpeedPipeline(messages)
  const steer = getSteeringPipeline(messages)
  const brake = getBrakePipeline(messages)

  // Conversion helpers
  const toDisplaySpeed = (mmps: number | null | undefined): number | null => {
    if (mmps == null || !Number.isFinite(mmps)) return null
    if (unitMode === 'kmh') {
      return mmps * 0.0036 // 1000 mm/s = 3.6 km/h
    }
    return mmps
  }

  // Live or Demo fallback values
  const hasFrames = messages.length > 0
  const isDemo = demoMode // Grounded in explicit demoMode state

  // 1. Speed values (raw in mm/s)
  const rawMtrSpeed = isDemo ? 2450 : (speed.mtrFbkSpeed ?? speed.physicalSpeed ?? null)
  const rawHostSpeed = isDemo ? 2778 : (speed.hostSpeed ?? null)
  const rawRtSpeed = isDemo ? 2500 : (speed.rtSpeed ?? null)
  const rawSysOutputSpeed = isDemo ? 2350 : (speed.manualSpeed ?? speed.mtrFbkSpeed ?? null)

  const mtrSpeedVal = toDisplaySpeed(rawMtrSpeed)
  const hostSpeedVal = toDisplaySpeed(rawHostSpeed)
  const rtSpeedVal = toDisplaySpeed(rawRtSpeed)
  const sysOutputVal = toDisplaySpeed(rawSysOutputSpeed)
  const targetSpeedVal = rtSpeedVal ?? hostSpeedVal

  // 2. Steering values (deg & dynamics)
  const sesAngleVal = isDemo ? 12.4 : (steer.sesAngleDeg ?? null)
  const hostSteerVal = isDemo
    ? 14.0
    : (steer.hostSteerDeg ?? (steer.hostYawRate != null ? steer.hostYawRate * 0.05 : null))
  const rtSteerVal = isDemo ? 12.0 : (steer.rtTargetAngleDeg ?? null)
  const rtSlewVal = isDemo ? 85 : (steer.rtTargetSlewRate ?? null)
  const hostYawVal = isDemo ? 120 : (steer.hostYawRate ?? null)
  const sesTorqueVal = isDemo ? 4.8 : (steer.sesTorqueNm ?? null)

  // 3. Brake values (kPa & stroke mm)
  const actualBrakeVal = isDemo ? 1850 : (brake.actualPressureKpa ?? brake.compositeBrakeKpa ?? null)
  const hostBrakeVal = isDemo ? 2000 : (brake.hostPressureKpa ?? null)
  const rtBrakeVal = isDemo ? 1900 : (brake.rtPressureKpa ?? null)
  const actualStrokeVal = isDemo ? 18.5 : (brake.actualStrokeMm ?? null)
  const reqStrokeVal = isDemo ? 20.0 : (brake.sysStrokeMm ?? null)
  const diagStrokeVal = isDemo ? 18.0 : (brake.actualStrokeMm != null ? brake.actualStrokeMm * 0.98 : null)

  // Secondary speed metrics
  const wheelSpeedVal = toDisplaySpeed(isDemo ? 2450 : (speed.physicalSpeed ?? null))
  const manualThrottleVal = sysOutputVal

  // Programmatic Protocol Audit execution
  const rawSignalValues = useMemo<Record<string, number | null>>(() => ({
    'speed-mtr-fbk': rawMtrSpeed,
    'speed-host-cmd': rawHostSpeed,
    'speed-rt-cmd': rawRtSpeed,
    'steer-ses-actual': sesAngleVal,
    'steer-host-cmd': hostSteerVal,
    'steer-rt-req': rtSteerVal,
    'brake-seb-actual': actualBrakeVal,
    'brake-host-req': hostBrakeVal,
    'brake-rt-cmd': rtBrakeVal,
    'demand-sys-throttle': manualThrottleVal != null ? (unitMode === 'kmh' ? manualThrottleVal / 0.0036 : manualThrottleVal) : null,
    'demand-wheel-speed': wheelSpeedVal != null ? (unitMode === 'kmh' ? wheelSpeedVal / 0.0036 : wheelSpeedVal) : null,
    'stroke-seb-actual': actualStrokeVal,
    'stroke-sys-req': reqStrokeVal,
    'dynamics-host-yaw': hostYawVal,
    'dynamics-ses-torque': sesTorqueVal,
  }), [rawMtrSpeed, rawHostSpeed, rawRtSpeed, sesAngleVal, hostSteerVal, rtSteerVal, actualBrakeVal, hostBrakeVal, rtBrakeVal, manualThrottleVal, wheelSpeedVal, actualStrokeVal, reqStrokeVal, hostYawVal, sesTorqueVal, unitMode])

  const auditReport = useMemo(() => {
    return runFullProtocolAudit(messages, rawSignalValues, dictMessages)
  }, [messages, rawSignalValues, dictMessages])

  const activationReport = useMemo(() => {
    return evaluateActivationGates(messages, isDemo)
  }, [messages, isDemo])

  const speedAudit = auditReport.subsystems.find((s) => s.subsystem === 'Speed')
  const steerAudit = auditReport.subsystems.find((s) => s.subsystem === 'Steering')
  const brakeAudit = auditReport.subsystems.find((s) => s.subsystem === 'Braking')

  const speedMax = unitMode === 'kmh' ? 20 : 5556
  const speedTicks =
    unitMode === 'kmh' ? [0, 5, 10, 15, 20] : [0, 1000, 2000, 3000, 4000, 5000]
  const speedUnit = unitMode === 'kmh' ? 'km/h' : 'mm/s'

  return (
    <WorkspaceShell
      testId="workspace-dashboard"
      title="Dashboard"
      fitScreen={true}
      hideHeader={true}
    >
      <div className="dashboard-container">
        {/* ── HUD Overview Strip ── */}
        <header className="dashboard-hud-strip" data-testid="dashboard-hud">
          <div className="dashboard-hud-group">
            <span className="dashboard-brand-pill">DASHBOARD</span>

            {/* Protocol Audit Health Pill */}
            <div className="dashboard-hud-item">
              <span className="dashboard-hud-k">Protocol Status</span>
              <div className="dashboard-hud-v">
                <button
                  type="button"
                  className={`dashboard-audit-badge tone-${hasFrames ? auditReport.overallStatus : (demoMode ? 'info' : 'muted')} cursor-pointer hover:opacity-80`}
                  data-testid="dashboard-protocol-summary"
                  title="Click to view detailed Programmatic Protocol Audit table"
                  onClick={() => {
                    setActiveView('tables')
                    setActiveTab('protocol')
                  }}
                >
                  {hasFrames
                    ? auditReport.overallStatus === 'conforming'
                      ? '✓ Protocol Verified'
                      : `${auditReport.conformingCount}/${auditReport.totalSignals} Verified`
                    : demoMode
                      ? 'Simulated Telemetry'
                      : 'No CAN Telemetry'}
                </button>
              </div>
            </div>
          </div>

          {/* Unit, Demo & View Switch Controls */}
          <div className="dashboard-hud-group">
            {/* Activation Gates Quick Pill */}
            <button
              type="button"
              className={`dashboard-gates-pill tone-${activationReport.allCleared ? 'ok' : 'danger'}`}
              onClick={() => {
                setActiveView('tables')
                setActiveTab('gates')
              }}
              title="Click to view controller activation gate matrix"
              data-testid="hud-activation-gates-pill"
            >
              {activationReport.allCleared
                ? `✓ ${activationReport.clearedGates}/${activationReport.totalGates} Gates Ready`
                : `⚠ ${activationReport.totalGates - activationReport.clearedGates} Gate Blocked`}
            </button>

            {/* Speed Unit Toggle */}
            <div className="flex items-center gap-1 bg-surface-2 p-0.5 rounded border border-border">
              <button
                type="button"
                className={`px-2 py-0.5 text-xs font-semibold rounded ${
                  unitMode === 'kmh' ? 'bg-primary text-white' : 'text-text-secondary'
                }`}
                onClick={() => setUnitMode('kmh')}
                data-testid="toggle-unit-kmh"
              >
                km/h
              </button>
              <button
                type="button"
                className={`px-2 py-0.5 text-xs font-semibold rounded ${
                  unitMode === 'mmps' ? 'bg-primary text-white' : 'text-text-secondary'
                }`}
                onClick={() => setUnitMode('mmps')}
                data-testid="toggle-unit-mmps"
              >
                mm/s
              </button>
            </div>

            {/* Demo / Live Toggle */}
            {!hasFrames && (
              <button
                type="button"
                className={`px-2.5 py-0.5 text-xs font-semibold rounded border ${
                  demoMode ? 'bg-info-soft text-primary border-primary' : 'bg-surface text-text border-border'
                }`}
                onClick={() => setDemoMode((v) => !v)}
                data-testid="toggle-demo-mode"
                title="Toggle simulated preview telemetry when no hardware CAN frames are incoming"
              >
                {demoMode ? 'Sim: ON' : 'Preview'}
              </button>
            )}

            {/* View Switcher: Cluster vs Audit Tables */}
            <div className="flex items-center gap-0.5 bg-surface-2 p-0.5 rounded border border-border">
              <button
                type="button"
                className={`px-2.5 py-0.5 text-xs font-semibold rounded ${
                  activeView === 'cluster' ? 'bg-primary text-white' : 'text-text-secondary'
                }`}
                onClick={() => setActiveView('cluster')}
                data-testid="toggle-view-cluster"
              >
                Cluster
              </button>
              <button
                type="button"
                className={`px-2.5 py-0.5 text-xs font-semibold rounded ${
                  activeView === 'tables' ? 'bg-primary text-white' : 'text-text-secondary'
                }`}
                onClick={() => setActiveView('tables')}
                data-testid="toggle-view-tables"
              >
                Tables
              </button>
            </div>

            {/* Chassis Actuator View Switcher */}
            {activeView === 'cluster' && (
              <div className="flex items-center gap-0.5 bg-surface-2 p-0.5 rounded border border-border">
                <button
                  type="button"
                  className={`px-2 py-0.5 text-xs font-semibold rounded ${
                    chassisView === 'both' ? 'bg-primary text-white' : 'text-text-secondary'
                  }`}
                  onClick={() => setChassisView('both')}
                  title="Display Steering and Braking side-by-side in dual full-height panels"
                  data-testid="toggle-chassis-both"
                >
                  Dual
                </button>
                <button
                  type="button"
                  className={`px-2 py-0.5 text-xs font-semibold rounded ${
                    chassisView === 'steer' ? 'bg-primary text-white' : 'text-text-secondary'
                  }`}
                  onClick={() => setChassisView('steer')}
                  title="Focus Steering full-width"
                  data-testid="toggle-chassis-steer"
                >
                  Steer
                </button>
                <button
                  type="button"
                  className={`px-2 py-0.5 text-xs font-semibold rounded ${
                    chassisView === 'brake' ? 'bg-primary text-white' : 'text-text-secondary'
                  }`}
                  onClick={() => setChassisView('brake')}
                  title="Focus Braking full-width"
                  data-testid="toggle-chassis-brake"
                >
                  Brake
                </button>
              </div>
            )}

            {/* 10-Second Diagnostic Export Action */}
            <div className="dashboard-export-group">
              {isRecording ? (
                <button
                  type="button"
                  className="dashboard-export-btn is-recording"
                  onClick={handleStopRecordingEarly}
                  title="Click to stop recording and download 10s diagnostic bundle immediately"
                  data-testid="btn-recording-active"
                >
                  <span className="dashboard-record-pulse-dot" />
                  <span>Recording: {recordingRemaining.toFixed(1)}s</span>
                  <span className="text-[10px] underline ml-1">Save Now</span>
                </button>
              ) : (
                <div className="flex items-center">
                  <button
                    type="button"
                    className="dashboard-export-btn"
                    onClick={handleStartRecording}
                    title="Export 10-second diagnostic window (state, steering changes, manual mode, CAN IDs, heartbeats)"
                    data-testid="btn-export-10s"
                  >
                    <span className="text-danger font-bold">⏺</span>
                    <span>Export 10s</span>
                  </button>
                  <button
                    type="button"
                    className="dashboard-export-btn dashboard-export-chevron"
                    onClick={() => setExportDropdownOpen((v) => !v)}
                    title="More export options"
                    data-testid="btn-export-chevron"
                  >
                    ▼
                  </button>
                </div>
              )}

              {exportDropdownOpen && !isRecording && (
                <div className="dashboard-export-dropdown" data-testid="export-dropdown-menu">
                  <button
                    type="button"
                    className="dashboard-export-item"
                    onClick={handleStartRecording}
                  >
                    <span className="dashboard-export-item-title">⏺ Capture Next 10s Window</span>
                    <span className="dashboard-export-item-desc">
                      Records next 10 seconds of live state, steering changes, and CAN frames.
                    </span>
                  </button>
                  <button
                    type="button"
                    className="dashboard-export-item"
                    onClick={() => void handleExportInstantBuffer()}
                  >
                    <span className="dashboard-export-item-title">⚡ Export Past 10s Snapshot</span>
                    <span className="dashboard-export-item-desc">
                      Instantly exports recent 10s rolling buffer without waiting.
                    </span>
                  </button>
                </div>
              )}
            </div>
          </div>
        </header>

        {activeView === 'cluster' ? (
          /* ── 3-Column Vehicle Cluster: Velocity, Stacked (Steering + Brake), and CAN Audit & Refusal Log ── */
          <section
            className="dashboard-cluster-grid"
            data-testid="dashboard-meters"
            aria-label="Vehicle instrument cluster and audit logger"
          >
          {/* Column 1: Velocity (Host -> RT -> Throttle -> FBK Pending) */}
          <div className="dashboard-col-velocity">
            <MultiMeter
              title="Velocity"
              badge={demoMode ? 'Simulated' : undefined}
              badgeTone="ok"
              protocolAudit={
                speedAudit
                  ? {
                      status: speedAudit.status,
                      note: `${speedAudit.summary} · ${speedAudit.consistencyDetail}`,
                    }
                  : undefined
              }
              primary={{
                label: 'TARGET SPEED',
                value: targetSpeedVal,
                unit: speedUnit,
                digits: 0,
                canId: '0x204',
                varName: 'RT_DRIVE_CMD: motor_speed_mmps (Low) · Active setpoint',
              }}
              targetValue={targetSpeedVal}
              subColumns={[
                {
                  label: 'FROM HOST',
                  value: hostSpeedVal,
                  unit: speedUnit,
                  digits: 0,
                  canId: '0x300',
                  varName: 'HOST_DRIVE_CMD: speed_mmps (Autonomy Target)',
                },
                {
                  label: 'RT TO SYS',
                  value: rtSpeedVal,
                  unit: speedUnit,
                  digits: 0,
                  canId: '0x204',
                  varName: 'RT_DRIVE_CMD: motor_speed_mmps (Supervisor Setpoint)',
                },
                {
                  label: 'SYS OUTPUT',
                  value: sysOutputVal,
                  unit: speedUnit,
                  digits: 0,
                  canId: '0x120',
                  varName: 'SYS_THROTTLE_STS: speed_mmps (Actuator Command Output)',
                },
              ]}
              secondaryMetrics={[
                {
                  label: 'Wheel Speed',
                  value: `${wheelSpeedVal != null ? wheelSpeedVal.toFixed(0) : '—'} ${speedUnit}`,
                  canId: '0x122',
                  varName: 'RT_WHEEL_SPEED_STS: measured_speed_mmps (Low)',
                },
                {
                  label: 'Plausibility',
                  value: speedAudit?.consistencyPass ? '✓ Matched' : '⚠ Divergent',
                  varName: speedAudit?.consistencyDetail,
                },
              ]}
              min={0}
              max={speedMax}
              ticks={speedTicks}
              testId="meter-speed"
            />
          </div>

          {/* Column 2: Chassis Actuation — Steering & Braking organized side-by-side in full height */}
          <div
            className={`dashboard-col-chassis view-${chassisView}`}
            data-testid="dashboard-col-chassis"
          >
            {(chassisView === 'both' || chassisView === 'steer') && (
              <SteeringMeter
                title="Steering"
                badge={steer.sesMode && steer.sesMode !== '—' ? steer.sesMode : 'Active'}
                badgeTone="info"
                actualAngle={sesAngleVal}
                hostAngle={hostSteerVal}
                rtAngle={rtSteerVal}
                yawRate={hostYawVal}
                slewRate={rtSlewVal}
                torqueNm={sesTorqueVal}
                maxAngle={90}
                compact={false}
                testId="meter-steer"
                protocolAudit={
                  steerAudit
                    ? {
                        status: steerAudit.status,
                        note: `${steerAudit.summary} · ${steerAudit.consistencyDetail}`,
                      }
                    : undefined
                }
              />
            )}

            {(chassisView === 'both' || chassisView === 'brake') && (
              <BrakeMeter
                title="Braking"
                badge={actualBrakeVal != null && actualBrakeVal > 0 ? 'Active' : 'Standby'}
                badgeTone={actualBrakeVal != null && actualBrakeVal > 3500 ? 'danger' : 'ok'}
                actualPressure={actualBrakeVal}
                hostPressure={hostBrakeVal}
                rtPressure={rtBrakeVal}
                actualStroke={actualStrokeVal}
                reqStroke={reqStrokeVal}
                diagStroke={diagStrokeVal}
                maxPressure={5000}
                maxStroke={50}
                compact={false}
                testId="meter-brake"
                protocolAudit={
                  brakeAudit
                    ? {
                        status: brakeAudit.status,
                        note: `${brakeAudit.summary} · ${brakeAudit.consistencyDetail}`,
                      }
                    : undefined
                }
              />
            )}
          </div>

          {/* Column 3: The Third Column — CAN Audit & Refusal Logger */}
          <div className="dashboard-col-logger">
            <CanAuditLogger
              messages={messages}
              status={status}
              isDemo={isDemo}
              testId="dashboard-can-logger"
            />
          </div>
        </section>
      ) : (
        /* ── Tabbed Inspection Section: Programmatic Protocol Audit vs Pipeline Verification ── */
        <section className="dashboard-details-card" data-testid="dashboard-summary-table">
          <div className="dashboard-tab-bar">
            <div className="flex items-center gap-2">
              <button
                type="button"
                className={`dashboard-tab-btn ${activeTab === 'protocol' ? 'active' : ''}`}
                onClick={() => setActiveTab('protocol')}
                data-testid="tab-protocol-audit"
              >
                Programmatic Protocol Audit
              </button>
              <button
                type="button"
                className={`dashboard-tab-btn ${activeTab === 'pipeline' ? 'active' : ''}`}
                onClick={() => setActiveTab('pipeline')}
                data-testid="tab-pipeline-verification"
              >
                Command vs Feedback Pipeline
              </button>
              <button
                type="button"
                className={`dashboard-tab-btn ${activeTab === 'gates' ? 'active' : ''}`}
                onClick={() => setActiveTab('gates')}
                data-testid="tab-activation-gates"
              >
                Activation Gates ({activationReport.clearedGates}/{activationReport.totalGates})
              </button>
            </div>

            <div className="flex items-center gap-2 text-xs text-muted">
              <span>YAML Protocol Contracts:</span>
              <span className="mono font-semibold text-text">
                {auditReport.conformingCount}/{auditReport.totalSignals} Signals Verified
              </span>
            </div>
          </div>

          {activeTab === 'protocol' ? (
            /* Programmatic Protocol Verification Table */
            <div className="overflow-x-auto" data-testid="protocol-audit-table">
              <table className="dashboard-table">
                <thead>
                  <tr>
                    <th>Subsystem</th>
                    <th>Tier / Node</th>
                    <th>CAN ID & Bus</th>
                    <th>Signal (Hover for Key)</th>
                    <th>Contract Bounds</th>
                    <th>Observed Value</th>
                    <th>Schema</th>
                    <th>Limits</th>
                    <th>Protocol Status</th>
                  </tr>
                </thead>
                <tbody>
                  {auditReport.subsystems.flatMap((sub) =>
                    sub.signals.map((sig) => (
                      <tr key={sig.contract.id}>
                        <td className="font-semibold text-xs">{sig.contract.subsystem}</td>
                        <td className="text-xs text-muted">{sig.contract.tier}</td>
                        <td className="mono text-xs font-bold text-text">
                          <span
                            className="can-id-tag"
                            title={`${sig.contract.bus.toUpperCase()} CAN ID ${sig.contract.canIdHex} (${sig.contract.cycleMs}ms)`}
                          >
                            {sig.contract.canIdHex}
                          </span>
                          <span className="text-[10px] text-muted ml-1 uppercase">
                            {sig.contract.bus}
                          </span>
                        </td>
                        <td>
                          <span
                            className="cursor-help font-medium text-xs text-text border-b border-dashed border-border"
                            title={`${sig.contract.msgName}.${sig.contract.signalKey} — ${sig.contract.description}`}
                          >
                            {sig.contract.msgName}
                          </span>
                        </td>
                        <td className="mono text-xs text-muted">
                          [{sig.contract.contractMin}, {sig.contract.contractMax}] {sig.contract.expectedUnit}
                        </td>
                        <td className="mono font-bold text-xs">
                          {sig.liveValue != null
                            ? `${sig.liveValue.toFixed(1)} ${sig.liveUnit}`
                            : '—'}
                        </td>
                        <td>
                          <span
                            className={`text-xs font-semibold ${
                              sig.schemaValid ? 'text-success' : 'text-danger font-bold'
                            }`}
                          >
                            {sig.schemaValid ? '✓ Valid' : '✗ Discrepancy'}
                          </span>
                        </td>
                        <td>
                          <span
                            className={`text-xs font-semibold ${
                              sig.rangeValid ? 'text-success' : 'text-warning font-bold'
                            }`}
                          >
                            {sig.rangeValid ? '✓ In Range' : '⚠ Out of Range'}
                          </span>
                        </td>
                        <td>
                          <StatusPill
                            label={
                              sig.status === 'conforming'
                                ? 'Conforms'
                                : sig.status === 'warning'
                                  ? 'Limit Warn'
                                  : sig.status === 'error'
                                    ? 'Fault'
                                    : 'Unseen'
                            }
                            tone={
                              sig.status === 'conforming'
                                ? 'ok'
                                : sig.status === 'warning'
                                  ? 'warn'
                                  : sig.status === 'error'
                                    ? 'danger'
                                    : 'muted'
                            }
                          />
                        </td>
                      </tr>
                    )),
                  )}
                </tbody>
              </table>
            </div>
          ) : activeTab === 'pipeline' ? (
            /* 3-Tier Pipeline Comparison Breakdown */
            <div className="overflow-x-auto" data-testid="pipeline-table">
              <table className="dashboard-table">
                <thead>
                  <tr>
                    <th>Subsystem</th>
                    <th>Tier 1 (Host / Nav)</th>
                    <th>Tier 2 (RT Core)</th>
                    <th>Tier 3 (Actuator / MTR)</th>
                    <th>Tracking Delta</th>
                    <th>Consistency Check</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td className="font-semibold">Drive Speed</td>
                    <td className="mono">
                      {hostSpeedVal != null ? `${hostSpeedVal.toFixed(0)} ${speedUnit}` : '—'}
                      <div className="can-id-tag mt-0.5" title="HOST_DRIVE_CMD: speed_mmps">
                        0x300
                      </div>
                    </td>
                    <td className="mono">
                      {rtSpeedVal != null ? `${rtSpeedVal.toFixed(0)} ${speedUnit}` : '—'}
                      <div className="can-id-tag mt-0.5" title="RT_DRIVE_CMD: motor_speed_mmps">
                        0x204
                      </div>
                    </td>
                    <td className="mono font-bold">
                      {mtrSpeedVal != null ? `${mtrSpeedVal.toFixed(0)} ${speedUnit}` : '—'}
                      <div className="can-id-tag mt-0.5" title="MTR_MOTOR_FBK: motor_command_speed_mmps">
                        0x206
                      </div>
                    </td>
                    <td className="mono">
                      {hostSpeedVal != null && mtrSpeedVal != null
                        ? `${(mtrSpeedVal - hostSpeedVal).toFixed(1)} ${speedUnit}`
                        : '—'}
                    </td>
                    <td>
                      <StatusPill
                        label={speedAudit?.consistencyDetail ?? 'EGAS L2 Consistent'}
                        tone={speedAudit?.consistencyPass ? 'ok' : 'danger'}
                      />
                    </td>
                  </tr>
                  <tr>
                    <td className="font-semibold">Steering Angle</td>
                    <td className="mono">
                      {hostSteerVal != null ? `${hostSteerVal.toFixed(1)}°` : '—'}
                      <div className="can-id-tag mt-0.5" title="HOST_STEER_CMD: steer_angle_0_1deg">
                        0x303
                      </div>
                    </td>
                    <td className="mono">
                      {rtSteerVal != null ? `${rtSteerVal.toFixed(1)}°` : '—'}
                      <div className="can-id-tag mt-0.5" title="VCU_SES_REQ: target_angle_raw">
                        0x169
                      </div>
                    </td>
                    <td className="mono font-bold">
                      {sesAngleVal != null ? `${sesAngleVal.toFixed(1)}°` : '—'}
                      <div className="can-id-tag mt-0.5" title="SES_STATUS: angle_deg">
                        0x201
                      </div>
                    </td>
                    <td className="mono">
                      {rtSteerVal != null && sesAngleVal != null
                        ? `${(sesAngleVal - rtSteerVal).toFixed(1)}°`
                        : '—'}
                    </td>
                    <td>
                      <StatusPill
                        label={steerAudit?.consistencyDetail ?? 'Tracking Nominal'}
                        tone={steerAudit?.consistencyPass ? 'ok' : 'warn'}
                      />
                    </td>
                  </tr>
                  <tr>
                    <td className="font-semibold">Brake Pressure</td>
                    <td className="mono">
                      {hostBrakeVal != null ? `${hostBrakeVal.toFixed(0)} kPa` : '—'}
                      <div className="can-id-tag mt-0.5" title="HOST_BRAKE_REQ: brake_pressure_kpa">
                        0x301
                      </div>
                    </td>
                    <td className="mono">
                      {rtBrakeVal != null ? `${rtBrakeVal.toFixed(0)} kPa` : '—'}
                      <div className="can-id-tag mt-0.5" title="RT_BRAKE_CMD: brake_pressure_kpa">
                        0x205
                      </div>
                    </td>
                    <td className="mono font-bold">
                      {actualBrakeVal != null ? `${actualBrakeVal.toFixed(0)} kPa` : '—'}
                      <div className="can-id-tag mt-0.5" title="SEB_STATUS: pressure_kpa">
                        0x721
                      </div>
                    </td>
                    <td className="mono">
                      {rtBrakeVal != null && actualBrakeVal != null
                        ? `${(actualBrakeVal - rtBrakeVal).toFixed(0)} kPa`
                        : '—'}
                    </td>
                    <td>
                      <StatusPill
                        label={brakeAudit?.consistencyDetail ?? 'Hydraulic Nominal'}
                        tone={brakeAudit?.consistencyPass ? 'ok' : 'warn'}
                      />
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          ) : (
            <ControllerActivationMatrix report={activationReport} compact={false} />
          )}
        </section>
      )}

      {/* ── LLM Diagnostic Export Modal ── */}
      {exportBundle && (
        <div
          className="dashboard-modal-backdrop"
          onClick={() => setExportBundle(null)}
          data-testid="export-modal-backdrop"
        >
          <div
            className="dashboard-modal-card"
            onClick={(e) => e.stopPropagation()}
            data-testid="export-diagnostic-modal"
          >
            <div className="dashboard-modal-header">
              <div className="dashboard-modal-title">
                <span>✓ 10-Second Telemetry Diagnostic Exported</span>
              </div>
              <button
                type="button"
                className="dashboard-modal-close"
                onClick={() => setExportBundle(null)}
                title="Close modal"
              >
                ✕
              </button>
            </div>

            <div className="dashboard-modal-body">
              <div className="dashboard-summary-chips-grid">
                <div className="dashboard-summary-chip">
                  <div className="dashboard-summary-chip-k">Vehicle Authority</div>
                  <div className="dashboard-summary-chip-v text-primary">
                    {exportBundle.llm_diagnostic_context.vehicle_mode_analysis.overall_mode}
                    {exportBundle.llm_diagnostic_context.vehicle_mode_analysis.was_in_manual_mode && ' (Manual Mode)'}
                  </div>
                </div>
                <div className="dashboard-summary-chip">
                  <div className="dashboard-summary-chip-k">Steering Net Delta</div>
                  <div className="dashboard-summary-chip-v">
                    {exportBundle.llm_diagnostic_context.steering_analysis.initial_ses_angle_deg != null
                      ? `${exportBundle.llm_diagnostic_context.steering_analysis.initial_ses_angle_deg.toFixed(1)}° → ${exportBundle.llm_diagnostic_context.steering_analysis.final_ses_angle_deg?.toFixed(1)}°`
                      : '—'}{' '}
                    <span className="text-muted text-[11px]">
                      ({exportBundle.llm_diagnostic_context.steering_analysis.sample_count} samples)
                    </span>
                  </div>
                </div>
                <div className="dashboard-summary-chip">
                  <div className="dashboard-summary-chip-k">Heartbeat Continuity</div>
                  <div className="dashboard-summary-chip-v">
                    {exportBundle.llm_diagnostic_context.heartbeat_analysis.controllers_online_count}/
                    {exportBundle.llm_diagnostic_context.heartbeat_analysis.total_controllers_count} Controllers Healthy
                  </div>
                </div>
                <div className="dashboard-summary-chip">
                  <div className="dashboard-summary-chip-k">Gates & Safety</div>
                  <div className="dashboard-summary-chip-v">
                    {exportBundle.llm_diagnostic_context.activation_gates_analysis.all_gates_cleared
                      ? 'All Gates Ready'
                      : `${exportBundle.llm_diagnostic_context.activation_gates_analysis.inhibitors.length} Inhibited`}{' '}
                    ·{' '}
                    <span className={exportBundle.llm_diagnostic_context.vehicle_mode_analysis.estop_active ? 'text-red-500 font-bold' : 'text-green-600'}>
                      {exportBundle.llm_diagnostic_context.vehicle_mode_analysis.estop_active ? 'ESTOP' : 'ESTOP Clear'}
                    </span>
                  </div>
                </div>
                <div className="dashboard-summary-chip">
                  <div className="dashboard-summary-chip-k">High CAN Bus (CH0)</div>
                  <div className="dashboard-summary-chip-v">
                    {exportBundle.llm_diagnostic_context.can_bus_inventory?.high_bus?.active_can_ids_count ?? 0}/
                    {exportBundle.llm_diagnostic_context.can_bus_inventory?.high_bus?.total_catalog_can_ids ?? 29} Active IDs ·{' '}
                    {exportBundle.raw_telemetry.can_frames_by_bus?.high?.length ?? 0} Frames
                  </div>
                </div>
                <div className="dashboard-summary-chip">
                  <div className="dashboard-summary-chip-k">Low CAN Bus (CH1)</div>
                  <div className="dashboard-summary-chip-v">
                    {exportBundle.llm_diagnostic_context.can_bus_inventory?.low_bus?.active_can_ids_count ?? 0}/
                    {exportBundle.llm_diagnostic_context.can_bus_inventory?.low_bus?.total_catalog_can_ids ?? 33} Active IDs ·{' '}
                    {exportBundle.raw_telemetry.can_frames_by_bus?.low?.length ?? 0} Frames
                  </div>
                </div>
              </div>

              <div>
                <div className="flex items-center justify-between mb-1">
                  <span className="font-semibold text-xs text-text">LLM Diagnostic Summary Prompt</span>
                  <span className="text-[11px] text-muted">Ready to paste into ChatGPT, Claude, or Gemini</span>
                </div>
                <div className="dashboard-llm-preview-box">
                  {exportBundle.llm_diagnostic_context.llm_prompt_markdown}
                </div>
              </div>
            </div>

            <div className="dashboard-modal-footer">
              <span className="text-[11px] text-muted mr-auto hidden sm:inline-flex items-center gap-1">
                📁 Saved to <code className="text-primary font-mono text-[10.5px]">tem/control-toolkit-logs/</code> (gitignored)
              </span>
              <button
                type="button"
                className="px-3 py-1 text-xs font-semibold rounded border border-border bg-surface text-text hover:bg-surface-2"
                onClick={() => downloadExportJson(exportBundle)}
                title="Download telemetry JSON file again"
                data-testid="btn-redownload-json"
              >
                ⤓ Download JSON
              </button>
              <button
                type="button"
                className="px-3 py-1 text-xs font-semibold rounded bg-primary text-white hover:opacity-90 flex items-center gap-1.5"
                onClick={handleCopyPrompt}
                title="Copy prompt context to clipboard for LLM bug diagnosis"
                data-testid="btn-copy-llm-prompt"
              >
                <span>{copiedPrompt ? '✓ Copied to Clipboard!' : '📋 Copy LLM Prompt'}</span>
              </button>
              <button
                type="button"
                className="px-3 py-1 text-xs font-semibold rounded border border-border bg-surface-2 text-text hover:bg-surface"
                onClick={() => setExportBundle(null)}
              >
                Done
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  </WorkspaceShell>
  )
}
