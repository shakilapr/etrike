import { useEffect, useMemo, useState } from 'react'
import { useAppStore } from '../store'
import { api } from '../api'
import {
  getSpeedPipeline,
  getSteeringPipeline,
  getBrakePipeline,
  getAuthorityPipeline,
  observeEstop,
} from '../lib/signals'
import { runFullProtocolAudit } from '../lib/protocolAudit'
import { evaluateActivationGates } from '../lib/activationGates'
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
  const quality = useAppStore((s) => s.streamQuality)

  const [unitMode, setUnitMode] = useState<'kmh' | 'mmps'>('kmh')
  const [demoMode, setDemoMode] = useState<boolean>(false)
  const [activeView, setActiveView] = useState<'cluster' | 'tables'>('cluster')
  const [activeTab, setActiveTab] = useState<'protocol' | 'pipeline' | 'gates'>('protocol')
  const [dictMessages, setDictMessages] = useState<Array<Record<string, unknown>> | null>(null)

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

  const ses = status?.session
  const estopObs = observeEstop(messages, ses)

  // 3-Tier Subsystem Pipelines
  const speed = getSpeedPipeline(messages)
  const steer = getSteeringPipeline(messages)
  const brake = getBrakePipeline(messages)
  const auth = getAuthorityPipeline(messages)

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
  const isDemo = demoMode || !hasFrames

  // 1. Speed values (raw in mm/s)
  const rawMtrSpeed = isDemo ? 27222 : (speed.mtrFbkSpeed ?? speed.physicalSpeed ?? null)
  const rawHostSpeed = isDemo ? 27778 : (speed.hostSpeed ?? null)
  const rawRtSpeed = isDemo ? 26944 : (speed.rtSpeed ?? null)

  const mtrSpeedVal = toDisplaySpeed(rawMtrSpeed)
  const hostSpeedVal = toDisplaySpeed(rawHostSpeed)
  const rtSpeedVal = toDisplaySpeed(rawRtSpeed)

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
  const wheelSpeedVal = toDisplaySpeed(isDemo ? 26800 : (speed.physicalSpeed ?? null))
  const manualThrottleVal = toDisplaySpeed(isDemo ? 25000 : (speed.manualSpeed ?? null))

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

  // Active Gear
  const activeGear =
    speed.mtrGear || speed.rtGear || speed.hostGear || (isDemo ? 'D' : 'N')

  // Speed max & ticks based on unit
  const speedMax = unitMode === 'kmh' ? 200 : 25000
  const speedTicks =
    unitMode === 'kmh' ? [0, 50, 100, 150, 200] : [0, 5000, 10000, 15000, 20000, 25000]
  const speedUnit = unitMode === 'kmh' ? 'km/h' : 'mm/s'

  // Stream health
  const isHealthy = quality === 'live' || (isDemo && hasFrames)

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

            {/* ESTOP Status */}
            <div className="dashboard-hud-item">
              <span className="dashboard-hud-k">ESTOP</span>
              <div className="dashboard-hud-v">
                <StatusPill
                  label={estopObs.any ? estopObs.label : 'Clear'}
                  tone={estopObs.any ? 'danger' : 'ok'}
                  testId="dashboard-estop-pill"
                />
              </div>
            </div>

            {/* Gear Selector */}
            <div className="dashboard-hud-item">
              <span className="dashboard-hud-k">Gear</span>
              <div className="dashboard-hud-v">
                <span className="dashboard-gear-pill" data-testid="dashboard-gear">
                  {activeGear}
                </span>
                <span className="text-[11px] text-muted font-medium">
                  {activeGear === 'D'
                    ? 'Drive'
                    : activeGear === 'R'
                      ? 'Reverse'
                      : activeGear === 'S'
                        ? 'Sport'
                        : 'Neutral'}
                </span>
              </div>
            </div>

            {/* Drive Mode */}
            <div className="dashboard-hud-item">
              <span className="dashboard-hud-k">Drive Mode</span>
              <div className="dashboard-hud-v" data-testid="dashboard-mode">
                {ses?.confirmed_mode ?? auth.rtReportedMode ?? (isDemo ? 'AUTO' : 'MANUAL')}
              </div>
            </div>

            {/* Power / Contactor */}
            <div className="dashboard-hud-item">
              <span className="dashboard-hud-k">Power</span>
              <div className="dashboard-hud-v" data-testid="dashboard-power">
                <StatusPill
                  label={ses?.confirmed_power ?? (isDemo ? 'ON' : 'OFF')}
                  tone={ses?.confirmed_power === 'ON' || isDemo ? 'ok' : 'muted'}
                />
              </div>
            </div>

            {/* CAN Link Health */}
            <div className="dashboard-hud-item">
              <span className="dashboard-hud-k">CAN Bus</span>
              <div className="dashboard-hud-v">
                <StatusPill
                  label={isHealthy ? 'Live' : quality}
                  tone={isHealthy ? 'ok' : 'warn'}
                  testId="dashboard-can-pill"
                />
                <span className="text-xs text-muted">
                  {messages.length} frames
                </span>
              </div>
            </div>

            {/* Protocol Audit Health Pill */}
            <div className="dashboard-hud-item">
              <span className="dashboard-hud-k">Protocol Status</span>
              <div className="dashboard-hud-v">
                <button
                  type="button"
                  className={`dashboard-audit-badge tone-${auditReport.overallStatus} cursor-pointer hover:opacity-80`}
                  data-testid="dashboard-protocol-summary"
                  title="Click to view detailed Programmatic Protocol Audit table"
                  onClick={() => {
                    setActiveView('tables')
                    setActiveTab('protocol')
                  }}
                >
                  {auditReport.overallStatus === 'conforming'
                    ? '✓ Protocol Verified'
                    : `${auditReport.conformingCount}/${auditReport.totalSignals} Conforming`}
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
              badge={hasFrames ? (activeGear === 'D' ? 'Forward' : activeGear === 'R' ? 'Reverse' : 'Neutral') : 'Sim Preview'}
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
                label: 'RT SPEED CMD',
                value: rtSpeedVal ?? hostSpeedVal,
                unit: speedUnit,
                digits: 0,
                canId: '0x204',
                varName: 'RT_DRIVE_CMD: motor_speed_mmps (Low) · Active setpoint (MTR FBK Pending HW)',
              }}
              subLeft={{
                label: 'HOST SPEED',
                value: hostSpeedVal,
                unit: speedUnit,
                digits: 0,
                canId: '0x300',
                varName: 'HOST_DRIVE_CMD: speed_mmps (High Planner Target)',
              }}
              subRight={{
                label: 'SYS THROTTLE',
                value: manualThrottleVal,
                unit: speedUnit,
                digits: 0,
                canId: '0x120',
                varName: 'SYS_THROTTLE_STS: speed_mmps (Low Driver Demand)',
              }}
              secondaryMetrics={[
                {
                  label: 'Wheel Speed',
                  value: `${wheelSpeedVal != null ? wheelSpeedVal.toFixed(0) : '—'} ${speedUnit}`,
                  canId: '0x122',
                  varName: 'RT_WHEEL_SPEED_STS: measured_speed_mmps (Low)',
                },
                {
                  label: 'MTR FBK',
                  value: 'Pending HW',
                  canId: '0x206',
                  varName: 'MTR_MOTOR_FBK: Motor feedback not yet implemented on hardware bench',
                },
                {
                  label: 'EGAS L2',
                  value: speedAudit?.consistencyPass ? '✓ Match' : '⚠ Delta',
                  varName: speedAudit?.consistencyDetail,
                },
              ]}
              min={0}
              max={speedMax}
              ticks={speedTicks}
              testId="meter-speed"
            />
          </div>

          {/* Column 2: Stacked Steering and Brake (Shrunk in height to fit with speed) */}
          <div className="dashboard-col-stacked">
            {/* 2. STEERING METER — Compact, Host -> RT -> SES */}
            <SteeringMeter
              title="Steering"
              badge={steer.sesMode ?? 'EPS Active'}
              badgeTone="info"
              actualAngle={sesAngleVal}
              hostAngle={hostSteerVal}
              rtAngle={rtSteerVal}
              yawRate={hostYawVal}
              slewRate={rtSlewVal}
              torqueNm={sesTorqueVal}
              maxAngle={90}
              compact={true}
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

            {/* 3. BRAKE METER — Compact, Host -> RT -> SYS -> SEB */}
            <BrakeMeter
              title="Braking"
              badge={actualBrakeVal != null && actualBrakeVal > 0 ? 'SEB Active' : 'SEB Standby'}
              badgeTone={actualBrakeVal != null && actualBrakeVal > 3500 ? 'danger' : 'ok'}
              actualPressure={actualBrakeVal}
              hostPressure={hostBrakeVal}
              rtPressure={rtBrakeVal}
              actualStroke={actualStrokeVal}
              reqStroke={reqStrokeVal}
              diagStroke={diagStrokeVal}
              maxPressure={5000}
              maxStroke={50}
              compact={true}
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
            /* Controller Activation Gates Detailed Matrix */
            <ControllerActivationMatrix report={activationReport} compact={false} />
          )}
        </section>
      )}
    </div>
  </WorkspaceShell>
  )
}
