import {
  formatReqConf,
  observeEstop,
  getSpeedPipeline,
  getSteeringPipeline,
  getBrakePipeline,
  getAuthorityPipeline,
  getDiagnosticIndicators,
} from '../lib/signals'
import { useAppStore } from '../store'
import { FreshnessBadge } from './FreshnessBadge'
import { MeterBar, MetricCard, StatusPill } from './primitives'
import { QuickCheckStrip } from './QuickCheckStrip'
import { WorkspaceShell } from './WorkspaceShell'


export function Overview() {
  const messages = useAppStore((s) => s.messages)
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const ses = status?.session
  const estopObs = observeEstop(messages, ses)

  // 3-Tier Subsystem Pipelines
  const speed = getSpeedPipeline(messages)
  const steer = getSteeringPipeline(messages)
  const brake = getBrakePipeline(messages)
  const auth = getAuthorityPipeline(messages)
  const diag = getDiagnosticIndicators(messages)


  // Stream / CAN Health (grounded in actual adapter, link, and traffic)
  const adapterHealth = status?.adapter?.health
  const linkConnected = Boolean(status?.link?.connected)
  const hasTraffic = messages.length > 0

  let streamHealthLabel = 'Healthy'
  let canHealthTone: 'ok' | 'warn' | 'danger' | 'muted' = 'ok'
  let canHealthClass = 'healthy'

  if (quality === 'lost') {
    streamHealthLabel = 'Lost'
    canHealthTone = 'danger'
    canHealthClass = 'lost'
  } else if (quality === 'connecting') {
    streamHealthLabel = 'Connecting'
    canHealthTone = 'warn'
    canHealthClass = 'degraded'
  } else if (!linkConnected) {
    streamHealthLabel = 'Disconnected'
    canHealthTone = 'muted'
    canHealthClass = 'offline'
  } else if (adapterHealth === 'error') {
    streamHealthLabel = 'Adapter Error'
    canHealthTone = 'danger'
    canHealthClass = 'lost'
  } else if (adapterHealth === 'absent') {
    streamHealthLabel = 'No Adapter'
    canHealthTone = 'muted'
    canHealthClass = 'offline'
  } else if (adapterHealth === 'recovering') {
    streamHealthLabel = 'Recovering'
    canHealthTone = 'warn'
    canHealthClass = 'degraded'
  } else if (!hasTraffic) {
    streamHealthLabel = 'No Traffic'
    canHealthTone = 'warn'
    canHealthClass = 'unknown'
  } else if (quality === 'delayed' || quality === 'dropping') {
    streamHealthLabel = quality === 'delayed' ? 'Degraded' : 'Dropping'
    canHealthTone = 'warn'
    canHealthClass = 'degraded'
  } else {
    streamHealthLabel = 'Healthy'
    canHealthTone = 'ok'
    canHealthClass = 'healthy'
  }

  // Top strip primary values
  const brakeKpa = brake.compositeBrakeKpa
  const benchOn = ses?.bench_tx === 'enabled'

  // Differences for Command / Feedback table
  const driveDelta =
    speed.rtSpeed != null && speed.mtrFbkSpeed != null
      ? speed.mtrFbkSpeed - speed.rtSpeed
      : speed.hostSpeed != null && speed.mtrFbkSpeed != null
        ? speed.mtrFbkSpeed - speed.hostSpeed
        : null

  const steerDelta =
    steer.rtTargetAngleDeg != null && steer.sesAngleDeg != null
      ? steer.sesAngleDeg - steer.rtTargetAngleDeg
      : steer.hostSteerDeg != null && steer.sesAngleDeg != null
        ? steer.sesAngleDeg - steer.hostSteerDeg
        : null

  const brakeDelta =
    brake.rtPressureKpa != null && brake.actualPressureKpa != null
      ? brake.actualPressureKpa - brake.rtPressureKpa
      : brake.hostPressureKpa != null && brake.actualPressureKpa != null
        ? brake.actualPressureKpa - brake.hostPressureKpa
        : null

  return (
    <WorkspaceShell
      testId="workspace-overview"
      title="Overview"
      description={`Vehicle 3-tier command and feedback architecture · session ${ses?.session_id ?? 'none'} · ${messages.length} live messages`}
      sectionLabel="Observe"
    >
      {/* ── Top Safety & Mode Strip ── */}
      <section className="safety-strip" data-testid="safety-strip" aria-label="Safety and mode">
        <div
          className={`strip-item ${estopObs.any ? 'hazard' : hasTraffic && linkConnected ? 'ok' : 'muted'}`}
          title={estopObs.detail}
        >
          <span className="strip-k">ESTOP</span>
          <StatusPill
            label={estopObs.label}
            tone={estopObs.any ? 'danger' : hasTraffic && linkConnected ? 'ok' : 'muted'}
            testId="meter-estop"
          />
        </div>
        <div className="strip-item">
          <span className="strip-k">Power</span>
          <span className="strip-v strip-v-detail" data-testid="status-power">
            {formatReqConf(ses?.requested_power, ses?.confirmed_power)}
          </span>
        </div>
        <div className="strip-item">
          <span className="strip-k">Mode</span>
          <span className="strip-v strip-v-detail" data-testid="status-mode">
            {formatReqConf(ses?.requested_mode, ses?.confirmed_mode)}
          </span>
        </div>
        <div className="strip-item">
          <span className="strip-k">Bench TX</span>
          <StatusPill
            label={benchOn ? 'Enabled' : 'Disabled'}
            tone={benchOn ? 'warn' : 'muted'}
            testId="meter-bench-tx"
          />
        </div>
        <div className={`strip-item health-${canHealthClass}`}>
          <span className="strip-k">CAN health</span>
          <StatusPill
            label={streamHealthLabel}
            tone={canHealthTone}
            testId="meter-can-health"
          />
        </div>
        <div
          className={`strip-item ${
            brakeKpa != null && brakeKpa >= 0.7 * 5000 ? 'hazard' : ''
          }`}
          data-testid="strip-brake"
        >
          <span className="strip-k">Brake pressure</span>
          <span className="strip-v mono">
            {brakeKpa != null ? `${brakeKpa.toFixed(0)} kPa` : 'No signal'}
          </span>
          <MeterBar
            value={brakeKpa}
            max={5000}
            tone="high-bad"
            label="Brake pressure"
            testId="meter-brake"
          />
        </div>
      </section>

      {/* ── Diagnostic Quick-Check Strip ── */}
      <QuickCheckStrip messages={messages} />

      {/* ── 3-Tier Command Architecture Pipeline ── */}
      <div className="overview-pipeline" data-testid="overview-meters">
        {/* Tier 1: High Level Commands (Host / Jetson / High CAN) */}
        <section className="tier-block" data-testid="tier-1-high">
          <div className="tier-header">
            <div className="tier-tag">
              <span className="tier-badge tier-1">Tier 1</span>
              <span>High-Level Commands · Host / Jetson / Autonomous Navigation (High CAN)</span>
            </div>
            <span className="mono muted text-xs">Bus: High · 500 kbit/s</span>
          </div>

          <div className="cards metric-cards-tier">
            {/* Speed Request (Host) */}
            <MetricCard
              title="High Speed Command"
              valueText={speed.hostSpeed != null ? speed.hostSpeed.toFixed(0) : '—'}
              unit="mm/s"
              sub="HOST_DRIVE_CMD 0x300"
              freshness={speed.hostDrive?.freshness}
              value={speed.hostSpeed}
              max={3000}
              tone="auto"
              testId="card-speed"
              meterTestId="meter-speed-cmd"
            />
            {/* Yaw Rate Request (Host) */}
            <MetricCard
              title="High Yaw Rate"
              valueText={steer.hostYawRate != null ? steer.hostYawRate.toFixed(0) : '—'}
              unit="mrad/s"
              sub="HOST_DRIVE_CMD 0x300"
              freshness={steer.hostDrive?.freshness}
              value={steer.hostYawRate}
              min={-3000}
              max={3000}
              tone="auto"
              bipolar={true}
              testId="card-yaw"
              meterTestId="meter-yaw"
            />
            {/* Steering Angle Request (Host 0x303) */}
            <MetricCard
              title="High Steer Request"
              valueText={
                steer.hostSteerDeg != null
                  ? `${steer.hostSteerDeg > 0 ? '+' : ''}${steer.hostSteerDeg.toFixed(1)}`
                  : '—'
              }
              unit="°"
              sub="HOST_STEER_CMD 0x303"
              freshness={steer.hostSteer?.freshness}
              value={steer.hostSteerDeg}
              min={-45}
              max={45}
              tone="auto"
              bipolar={true}
              testId="card-host-steer"
              meterTestId="meter-host-steer"
              badges={
                diag.inCard.hostAngleValid != null ? (
                  <span
                    className={`micro-chip ${diag.inCard.hostAngleValid ? 'chip-ok' : 'chip-warn'}`}
                  >
                    {diag.inCard.hostAngleValid ? 'Angle Valid' : 'Invalid'}
                  </span>
                ) : null
              }
            />
            {/* Brake Request (Host 0x301) */}
            <MetricCard
              title="High Brake Request"
              valueText={brake.hostPressureKpa != null ? brake.hostPressureKpa.toFixed(0) : '—'}
              unit="kPa"
              sub="HOST_BRAKE_REQ 0x301"
              freshness={brake.hostBrake?.freshness}
              value={brake.hostPressureKpa}
              max={5000}
              tone="high-bad"
              testId="card-host-brake"
              meterTestId="meter-host-brake"
            />
            {/* Gear (Host 0x300) */}
            <div className="card metric-card" data-testid="card-gear">
              <div className="card-head">
                <div className="card-title">High Gear</div>
                {speed.hostDrive ? <FreshnessBadge value={speed.hostDrive.freshness} /> : null}
              </div>
              <div className="metric metric-discrete" data-testid="metric-gear">
                <StatusPill
                  label={speed.hostGear}
                  tone={speed.hostGear === 'N' || speed.hostGear === '—' ? 'muted' : 'accent'}
                  testId="status-gear"
                />
              </div>
              <div className="card-sub muted">HOST_DRIVE_CMD 0x300</div>
            </div>
            {/* HMI Request Mode & Power (Host/HMI) */}
            <div className="card metric-card" data-testid="card-hmi-req">
              <div className="card-head">
                <div className="card-title">HMI Request</div>
                {auth.hmiMode ? <FreshnessBadge value={auth.hmiMode.freshness} /> : null}
              </div>
              <div className="metric metric-discrete flex gap-1.5 flex-wrap">
                <StatusPill
                  label={`Mode ${auth.hmiReqMode}`}
                  tone={auth.hmiReqMode === 'AUTO' ? 'accent' : 'muted'}
                  testId="status-hmi-mode"
                />
                <StatusPill
                  label={`Pwr ${auth.hmiReqStart}`}
                  tone={auth.hmiReqStart === 'ON' ? 'ok' : 'muted'}
                  testId="status-hmi-power"
                />
              </div>
              <div className="card-sub muted">HMI_MODE_REQ 0x111 / 0x112</div>
            </div>
          </div>
        </section>

        {/* Tier 2: RT to SYS & Low Bus Commands (Gateway & Kinematics) */}
        <section className="tier-block" data-testid="tier-2-rt">
          <div className="tier-header">
            <div className="tier-tag">
              <span className="tier-badge tier-2">Tier 2</span>
              <span>RT to SYS Commands · Real-Time Kinematics & Gateway (Low CAN)</span>
            </div>
            <span className="mono muted text-xs">Bus: Low · 500 kbit/s</span>
          </div>

          <div className="cards metric-cards-tier">
            {/* RT Speed Command (to SYS & MTR) */}
            <MetricCard
              title="RT Speed Command"
              valueText={speed.rtSpeed != null ? speed.rtSpeed.toFixed(0) : '—'}
              unit="mm/s"
              sub="RT_DRIVE_CMD 0x204 (to SYS/MTR)"
              freshness={speed.rtDrive?.freshness}
              value={speed.rtSpeed}
              max={3000}
              tone="auto"
              testId="card-rt-speed"
              meterTestId="meter-rt-speed"
              badges={
                diag.inCard.egasConsistent != null ? (
                  <span
                    className={`micro-chip ${diag.inCard.egasConsistent ? 'chip-ok' : 'chip-danger'}`}
                  >
                    {diag.inCard.egasConsistent ? 'EGAS L2 Match' : 'EGAS Divergent'}
                  </span>
                ) : null
              }
            />
            {/* RT Steer Target (to SES) */}
            <MetricCard
              title="RT Steer Target"
              valueText={
                steer.rtTargetAngleDeg != null
                  ? `${steer.rtTargetAngleDeg > 0 ? '+' : ''}${steer.rtTargetAngleDeg.toFixed(1)}`
                  : '—'
              }
              unit="°"
              sub="VCU_SES_REQ 0x169 (to SES)"
              freshness={steer.rtSesReq?.freshness}
              value={steer.rtTargetAngleDeg}
              min={-45}
              max={45}
              tone="auto"
              bipolar={true}
              testId="card-rt-steer"
              meterTestId="meter-rt-steer"
              badges={
                <>
                  {diag.inCard.rtCtrlEn != null ? (
                    <span
                      className={`micro-chip ${diag.inCard.rtCtrlEn ? 'chip-accent' : 'chip-muted'}`}
                    >
                      {diag.inCard.rtCtrlEn ? 'Ctrl En' : 'Ctrl Dis'}
                    </span>
                  ) : null}
                  {diag.inCard.rtAlignEn != null ? (
                    <span
                      className={`micro-chip ${diag.inCard.rtAlignEn ? 'chip-ok' : 'chip-muted'}`}
                    >
                      {diag.inCard.rtAlignEn ? 'Align En' : 'No Align'}
                    </span>
                  ) : null}
                </>
              }
            />
            {/* RT Steer Slew Rate Target */}
            <MetricCard
              title="RT Steer Slew Rate"
              valueText={steer.rtTargetSlewRate != null ? steer.rtTargetSlewRate.toFixed(0) : '—'}
              unit="°/s"
              sub="VCU_SES_REQ 0x169 Slew Limit"
              freshness={steer.rtSesReq?.freshness}
              value={steer.rtTargetSlewRate}
              min={125}
              max={525}
              tone="auto"
              testId="card-rt-slew"
              meterTestId="meter-rt-slew"
            />
            {/* RT Brake Command (to SYS) */}
            <MetricCard
              title="RT Brake Command"
              valueText={brake.rtPressureKpa != null ? brake.rtPressureKpa.toFixed(0) : '—'}
              unit="kPa"
              sub="RT_BRAKE_CMD 0x205 (to SYS)"
              freshness={brake.rtBrake?.freshness}
              value={brake.rtPressureKpa}
              max={5000}
              tone="high-bad"
              testId="card-rt-brake"
              meterTestId="meter-rt-brake"
              badges={
                diag.inCard.leverEngaged ? (
                  <span className="micro-chip chip-warn">Lever Override</span>
                ) : null
              }
            />
            {/* RT Commanded Gear */}
            <div className="card metric-card" data-testid="card-rt-gear">
              <div className="card-head">
                <div className="card-title">RT Gear</div>
                {speed.rtDrive ? <FreshnessBadge value={speed.rtDrive.freshness} /> : null}
              </div>
              <div className="metric metric-discrete">
                <StatusPill
                  label={speed.rtGear}
                  tone={speed.rtGear === 'N' || speed.rtGear === '—' ? 'muted' : 'accent'}
                  testId="status-rt-gear"
                />
              </div>
              <div className="card-sub muted">RT_DRIVE_CMD 0x204</div>
            </div>
            {/* RT Operational State & Steer State */}
            <div className="card metric-card" data-testid="card-rt-state">
              <div className="card-head">
                <div className="card-title">RT State</div>
                {steer.rtState ? <FreshnessBadge value={steer.rtState.freshness} /> : null}
              </div>
              <div className="metric metric-discrete flex gap-1.5 flex-wrap">
                <StatusPill
                  label={auth.rtReportedMode}
                  tone={auth.rtReportedMode === 'AUTO' ? 'accent' : 'muted'}
                  testId="status-rt-mode"
                />
                <StatusPill
                  label={steer.rtSteerState}
                  tone={
                    steer.rtSteerState === 'ACTIVE'
                      ? 'ok'
                      : steer.rtSteerState === 'FAULT'
                        ? 'danger'
                        : steer.rtSteerState === '—'
                          ? 'muted'
                          : 'warn'
                  }
                  testId="status-rt-steer-state"
                />
              </div>
              <div className="card-sub muted">RT_STATE_RPT 0x210</div>
            </div>
          </div>
        </section>

        {/* Tier 3: SYS to Relevant Units & Actuator Execution */}
        <section className="tier-block" data-testid="tier-3-sys">
          <div className="tier-header">
            <div className="tier-tag">
              <span className="tier-badge tier-3">Tier 3</span>
              <span>SYS to Unit Commands · Actuator Execution & Safety Authority</span>
            </div>
            <span className="mono muted text-xs">Bus: Low · MTR / SES / SEB Units</span>
          </div>

          <div className="cards metric-cards-tier">
            {/* SYS to MTR Speed (Motor Feedback Echo) */}
            <MetricCard
              title="MTR Applied Speed"
              valueText={speed.mtrFbkSpeed != null ? speed.mtrFbkSpeed.toFixed(0) : '—'}
              unit="mm/s"
              sub="MTR_MOTOR_FBK 0x206"
              freshness={speed.motorFbk?.freshness}
              value={speed.mtrFbkSpeed}
              max={3000}
              tone="auto"
              testId="card-motor"
              meterTestId="meter-speed-fbk"
              badges={
                diag.inCard.mtrContactorClosed != null ? (
                  <span
                    className={`micro-chip ${diag.inCard.mtrContactorClosed ? 'chip-ok' : 'chip-warn'}`}
                  >
                    {diag.inCard.mtrContactorClosed ? 'Contactor ON' : 'Contactor Safe'}
                  </span>
                ) : null
              }
            />
            {/* Physical Measured Wheel Speed */}
            <MetricCard
              title="Physical Wheel Speed"
              valueText={speed.physicalSpeed != null ? speed.physicalSpeed.toFixed(0) : '—'}
              unit="mm/s"
              sub="RT_WHEEL_SPEED_STS 0x122"
              freshness={speed.wheelSts?.freshness}
              value={speed.physicalSpeed}
              max={3000}
              tone="auto"
              testId="card-measured"
              meterTestId="meter-speed-physical"
              badges={
                diag.inCard.wheelSensorState ? (
                  <span className="micro-chip chip-accent">
                    {diag.inCard.wheelSensorState}
                  </span>
                ) : null
              }
            />
            {/* SES Steering Unit Feedback */}
            <MetricCard
              title="SES Steering Angle"
              valueText={
                steer.sesAngleDeg != null
                  ? `${steer.sesAngleDeg > 0 ? '+' : ''}${steer.sesAngleDeg.toFixed(1)}`
                  : '—'
              }
              unit="°"
              sub={steer.sesTorqueNm != null ? `SES 0x201 · ${steer.sesTorqueNm.toFixed(1)} Nm` : 'SES_STATUS 0x201'}
              freshness={steer.sesStatus?.freshness}
              value={steer.sesAngleDeg}
              min={-45}
              max={45}
              tone="auto"
              bipolar={true}
              testId="card-steer"
              meterTestId="meter-steer"
              badges={
                <>
                  {diag.inCard.sesAligned != null ? (
                    <span
                      className={`micro-chip ${diag.inCard.sesAligned ? 'chip-ok' : 'chip-warn'}`}
                    >
                      {diag.inCard.sesAligned ? 'Aligned' : 'Syncing'}
                    </span>
                  ) : null}
                  {diag.inCard.sesErrLevel != null && diag.inCard.sesErrLevel > 0 ? (
                    <span className="micro-chip chip-danger">
                      SES L{diag.inCard.sesErrLevel}
                    </span>
                  ) : null}
                </>
              }
            />
            {/* SYS to SEB Brake Actuation & Feedback */}
            <MetricCard
              title="SEB Brake Actuation"
              valueText={brake.compositeBrakeKpa != null ? brake.compositeBrakeKpa.toFixed(0) : '—'}
              unit="kPa"
              sub={
                brake.sysStrokeMm != null
                  ? `SYS 0x7B9 (${brake.sysStrokeMm.toFixed(1)} mm) · SEB 0x721`
                  : 'VCU_SEB_REQ 0x7B9 / 0x721'
              }
              freshness={brake.sysSebReq?.freshness ?? brake.sebStatus?.freshness}
              value={brake.compositeBrakeKpa}
              max={5000}
              tone="high-bad"
              testId="card-brake"
              meterTestId="meter-brake-card"
              badges={
                <>
                  {diag.inCard.sebAligned != null ? (
                    <span
                      className={`micro-chip ${diag.inCard.sebAligned ? 'chip-ok' : 'chip-warn'}`}
                    >
                      {diag.inCard.sebAligned ? 'Aligned' : 'Syncing'}
                    </span>
                  ) : null}
                  {diag.inCard.sebAutoBrake ? (
                    <span className="micro-chip chip-accent">Auto-Brake</span>
                  ) : null}
                  {diag.inCard.sebErrLevel != null && diag.inCard.sebErrLevel > 0 ? (
                    <span className="micro-chip chip-danger">
                      SEB L{diag.inCard.sebErrLevel}
                    </span>
                  ) : null}
                </>
              }
            />
            {/* SYS Vehicle Authority (Mode & Power Commands) */}
            <div className="card metric-card" data-testid="card-sys-authority">
              <div className="card-head">
                <div className="card-title">SYS Authority</div>
                {auth.sysMode ? <FreshnessBadge value={auth.sysMode.freshness} /> : null}
              </div>
              <div className="metric metric-discrete flex gap-1.5 flex-wrap">
                <StatusPill
                  label={`Mode ${auth.sysCommandedMode}`}
                  tone={auth.sysCommandedMode === 'AUTO' ? 'accent' : 'muted'}
                  testId="status-sys-mode"
                />
                <StatusPill
                  label={`Power ${auth.sysCommandedPower}`}
                  tone={auth.sysCommandedPower === 'ON' ? 'ok' : 'muted'}
                  testId="status-sys-power"
                />
              </div>
              <div className="card-sub muted">SYS_MODE_CMD 0x110 / 0x113</div>
            </div>

            {/* Backend & Adapter Status */}
            <div className="card metric-card" data-testid="card-ready">
              <div className="card-head">
                <div className="card-title">Backend</div>
              </div>
              <div className="metric metric-discrete">
                <StatusPill
                  label={status?.ready ? 'Ready' : 'Not ready'}
                  tone={status?.ready ? 'ok' : 'danger'}
                  testId="status-backend-ready"
                />
              </div>
              <div className="card-sub mono muted">
                {status?.adapter?.identity ?? '—'} · {status?.adapter?.health ?? 'unknown'}
              </div>
            </div>
          </div>
        </section>
      </div>

      {/* ── End-to-End Command / Feedback Pipeline Table ── */}
      <section className="panel" data-testid="cmd-feedback">
        <h2>3-Tier Command / Feedback Architecture</h2>
        <table className="data-table">
          <thead>
            <tr>
              <th>Subsystem</th>
              <th>Tier 1: High Cmd</th>
              <th>Tier 2: RT to SYS</th>
              <th>Tier 3: SYS to Unit</th>
              <th>Unit Feedback</th>
              <th>Tracking Error</th>
              <th>Level Meter</th>
              <th>Freshness</th>
            </tr>
          </thead>
          <tbody>
            {/* Speed / Drive */}
            <tr>
              <td>
                <span className="font-semibold">Drive / Speed</span>
              </td>
              <td className="mono">
                {speed.hostSpeed != null ? `${speed.hostSpeed.toFixed(0)} mm/s [${speed.hostGear}]` : '—'}
              </td>
              <td className="mono">
                {speed.rtSpeed != null ? `${speed.rtSpeed.toFixed(0)} mm/s [${speed.rtGear}]` : '—'}
              </td>
              <td className="mono">
                {speed.manualSpeed != null
                  ? `Manual: ${speed.manualSpeed.toFixed(0)} mm/s`
                  : speed.rtSpeed != null
                    ? `Auto: ${speed.rtSpeed.toFixed(0)} mm/s`
                    : '—'}
              </td>
              <td className="mono">
                {speed.mtrFbkSpeed != null
                  ? `${speed.mtrFbkSpeed.toFixed(0)} mm/s [${speed.mtrGear}]`
                  : speed.physicalSpeed != null
                    ? `${speed.physicalSpeed.toFixed(0)} mm/s (phys)`
                    : '—'}
              </td>
              <td className="mono">
                {driveDelta != null ? `${driveDelta.toFixed(0)} mm/s` : '—'}
              </td>
              <td className="meter-cell">
                <MeterBar
                  value={
                    speed.mtrFbkSpeed != null
                      ? Math.abs(speed.mtrFbkSpeed)
                      : speed.rtSpeed != null
                        ? Math.abs(speed.rtSpeed)
                        : speed.hostSpeed != null
                          ? Math.abs(speed.hostSpeed)
                          : null
                  }
                  max={3000}
                  tone="auto"
                />
              </td>
              <td>
                {speed.hostDrive || speed.rtDrive || speed.motorFbk ? (
                  <FreshnessBadge
                    value={
                      speed.motorFbk?.freshness ??
                      speed.rtDrive?.freshness ??
                      speed.hostDrive?.freshness ??
                      'unseen'
                    }
                  />
                ) : (
                  '—'
                )}
              </td>
            </tr>

            {/* Steering */}
            <tr>
              <td>
                <span className="font-semibold">Steering</span>
              </td>
              <td className="mono">
                {steer.hostYawRate != null
                  ? `${steer.hostYawRate.toFixed(0)} mrad/s`
                  : steer.hostSteerDeg != null
                    ? `${steer.hostSteerDeg.toFixed(1)}°`
                    : '—'}
              </td>
              <td className="mono">
                {steer.rtTargetAngleDeg != null
                  ? `${steer.rtTargetAngleDeg.toFixed(1)}°`
                  : '—'}
              </td>
              <td className="mono">
                {steer.rtSesReq != null
                  ? `slew=${steer.rtTargetSlewRate ?? 0}°/s`
                  : '—'}
              </td>
              <td className="mono">
                {steer.sesAngleDeg != null
                  ? `${steer.sesAngleDeg.toFixed(1)}° [${steer.sesMode}]`
                  : '—'}
              </td>
              <td className="mono">
                {steerDelta != null ? `${steerDelta.toFixed(1)}°` : '—'}
              </td>
              <td className="meter-cell">
                <MeterBar
                  value={steer.sesAngleDeg ?? steer.rtTargetAngleDeg}
                  max={45}
                  min={-45}
                  bipolar={true}
                  tone="auto"
                />
              </td>
              <td>
                {steer.sesStatus || steer.rtSesReq || steer.hostDrive ? (
                  <FreshnessBadge
                    value={
                      steer.sesStatus?.freshness ??
                      steer.rtSesReq?.freshness ??
                      steer.hostDrive?.freshness ??
                      'unseen'
                    }
                  />
                ) : (
                  '—'
                )}
              </td>
            </tr>

            {/* Brake */}
            <tr>
              <td>
                <span className="font-semibold">Brake</span>
              </td>
              <td className="mono">
                {brake.hostPressureKpa != null ? `${brake.hostPressureKpa.toFixed(0)} kPa` : '—'}
              </td>
              <td className="mono">
                {brake.rtPressureKpa != null ? `${brake.rtPressureKpa.toFixed(0)} kPa` : '—'}
              </td>
              <td className="mono">
                {brake.sysPressureKpa != null
                  ? `${brake.sysPressureKpa.toFixed(0)} kPa`
                  : brake.sysStrokeMm != null
                    ? `${brake.sysStrokeMm.toFixed(1)} mm stroke`
                    : '—'}
              </td>
              <td className="mono">
                {brake.actualPressureKpa != null
                  ? `${brake.actualPressureKpa.toFixed(0)} kPa`
                  : brake.actualStrokeMm != null
                    ? `${brake.actualStrokeMm.toFixed(1)} mm`
                    : '—'}
              </td>
              <td className="mono">
                {brakeDelta != null ? `${brakeDelta.toFixed(0)} kPa` : '—'}
              </td>
              <td className="meter-cell">
                <MeterBar
                  value={brake.compositeBrakeKpa}
                  max={5000}
                  tone="high-bad"
                  testId="meter-brake-row"
                />
              </td>
              <td>
                {brake.hostBrake || brake.rtBrake || brake.sysSebReq || brake.sebStatus ? (
                  <FreshnessBadge
                    value={
                      brake.sebStatus?.freshness ??
                      brake.sysSebReq?.freshness ??
                      brake.rtBrake?.freshness ??
                      brake.hostBrake?.freshness ??
                      'unseen'
                    }
                  />
                ) : (
                  '—'
                )}
              </td>
            </tr>

            {/* Mode Authority */}
            <tr>
              <td>
                <span className="font-semibold">Mode Authority</span>
              </td>
              <td className="mono">{auth.hmiReqMode !== '—' ? `HMI: ${auth.hmiReqMode}` : '—'}</td>
              <td className="mono">{auth.rtReportedMode !== '—' ? `RT: ${auth.rtReportedMode}` : '—'}</td>
              <td className="mono font-semibold" colSpan={2}>
                {auth.sysCommandedMode !== '—' ? `SYS Authority: ${auth.sysCommandedMode}` : '—'}
              </td>
              <td className="mono muted">
                {auth.sysCommandedMode === '—' && auth.rtReportedMode === '—'
                  ? '—'
                  : auth.sysCommandedMode === auth.rtReportedMode
                    ? 'Aligned'
                    : 'Arbitrating'}
              </td>
              <td className="meter-cell">
                <StatusPill
                  label={auth.sysCommandedMode !== '—' ? auth.sysCommandedMode : 'No signal'}
                  tone={auth.sysCommandedMode === 'AUTO' ? 'accent' : 'muted'}
                  testId="status-table-mode"
                />
              </td>
              <td>
                {auth.sysMode || auth.rtState ? (
                  <FreshnessBadge value={auth.sysMode?.freshness ?? auth.rtState?.freshness ?? 'unseen'} />
                ) : (
                  '—'
                )}
              </td>
            </tr>

            {/* Power Authority */}
            <tr>
              <td>
                <span className="font-semibold">Power Authority</span>
              </td>
              <td className="mono">{auth.hmiReqStart !== '—' ? `Req: ${auth.hmiReqStart}` : '—'}</td>
              <td className="mono">
                {speed.rtDrive || steer.rtState || steer.rtSesReq ? 'RT Active' : '—'}
              </td>
              <td className="mono font-semibold" colSpan={2}>
                {auth.sysCommandedPower !== '—' ? `SYS to MTR: ${auth.sysCommandedPower}` : '—'}
              </td>
              <td className="mono muted">
                {auth.sysCommandedPower === '—'
                  ? '—'
                  : auth.sysCommandedPower === 'ON'
                    ? 'Active'
                    : 'Standby'}
              </td>
              <td className="meter-cell">
                <StatusPill
                  label={auth.sysCommandedPower !== '—' ? auth.sysCommandedPower : 'No signal'}
                  tone={auth.sysCommandedPower === 'ON' ? 'ok' : 'muted'}
                  testId="status-table-power"
                />
              </td>
              <td>
                {auth.sysPwr ? <FreshnessBadge value={auth.sysPwr.freshness} /> : '—'}
              </td>
            </tr>

            {/* Safety & ESTOP */}
            <tr>
              <td>
                <span className="font-semibold">Safety & ESTOP</span>
              </td>
              <td className="mono">
                {estopObs.hostLatch ? 'Host Latched' : 'Host Clear'}
              </td>
              <td className="mono">
                {!estopObs.rtPresent
                  ? '—'
                  : estopObs.rtReasonCode !== 0
                    ? `RT #${estopObs.rtReasonCode}: ${estopObs.rtReasonLabel}`
                    : 'RT Clear'}
              </td>
              <td className="mono font-semibold" colSpan={2}>
                {auth.sysSafety
                  ? `estop=${auth.safetyEstop ? 'ACTIVE' : 'CLEAR'} lights(brk=${auth.lightBrake ? 1 : 0} head=${auth.lightHead ? 1 : 0})`
                  : '—'}
              </td>
              <td className="mono muted">
                {estopObs.any ? estopObs.label : (auth.sysSafety || estopObs.rtPresent) ? 'Clear' : '—'}
              </td>
              <td className="meter-cell">
                <StatusPill
                  label={estopObs.any ? estopObs.label : auth.sysSafety ? 'ESTOP clear' : 'No signal'}
                  tone={estopObs.any ? 'danger' : auth.sysSafety ? 'ok' : 'muted'}
                  testId="status-safety-estop"
                />
              </td>
              <td>
                {auth.sysSafety ? <FreshnessBadge value={auth.sysSafety.freshness} /> : '—'}
              </td>
            </tr>
          </tbody>
        </table>
      </section>
    </WorkspaceShell>
  )
}

