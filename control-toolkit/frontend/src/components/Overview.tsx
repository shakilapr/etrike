import {
  formatReqConf,
  findMsg,
  observeEstop,
  signalNum,
  signalText,
} from '../lib/signals'
import { useAppStore } from '../store'
import { FreshnessBadge } from './FreshnessBadge'
import { MeterBar, MetricCard, StatusPill } from './primitives'
import { WorkspaceShell } from './WorkspaceShell'

export function Overview() {
  const messages = useAppStore((s) => s.messages)
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const drive = findMsg(messages, 'HOST_DRIVE_CMD')
  const motor = findMsg(messages, 'MTR_MOTOR_FBK')
  const sesStatus = findMsg(messages, 'SES_STATUS')
  const sebStatus = findMsg(messages, 'SEB_STATUS')
  const hostBrake = findMsg(messages, 'HOST_BRAKE_REQ')
  const rtBrake = findMsg(messages, 'RT_BRAKE_CMD')
  const brakeDiag = findMsg(messages, 'BRAKE_DIAG')
  const safety = findMsg(messages, 'SYS_SAFETY_STS')
  const ses = status?.session
  const estopObs = observeEstop(messages, ses)

  // Single stream/CAN label — never join two synonyms (was "lost · lost").
  const streamHealthLabel =
    quality === 'live'
      ? 'Healthy'
      : quality === 'delayed'
        ? 'Degraded'
        : quality === 'dropping'
          ? 'Dropping'
          : quality === 'lost'
            ? 'Lost'
            : quality === 'connecting'
              ? 'Connecting'
              : 'Unknown'
  const canHealthTone: 'ok' | 'warn' | 'danger' | 'muted' =
    quality === 'live'
      ? 'ok'
      : quality === 'delayed' || quality === 'dropping' || quality === 'connecting'
        ? 'warn'
        : quality === 'lost'
          ? 'danger'
          : 'muted'
  const canHealthClass =
    quality === 'live'
      ? 'healthy'
      : quality === 'delayed' || quality === 'dropping'
        ? 'degraded'
        : quality === 'lost'
          ? 'lost'
          : 'unknown'

  const cmdSpeed = signalNum(drive, 'speed_mmps')
  const cmdYaw = signalNum(drive, 'yaw_rate_mrad_s')
  const fbkSpeed =
    signalNum(motor, 'actual_speed_mmps') ?? signalNum(motor, 'speed_mmps')
  const steerDeg =
    signalNum(sesStatus, 'angle_deg') ??
    signalNum(sesStatus, 'steer_angle_deg') ??
    signalNum(sesStatus, 'angle')
  const brakeKpa =
    signalNum(hostBrake, 'brake_pressure_kpa') ??
    signalNum(rtBrake, 'brake_pressure_kpa') ??
    signalNum(brakeDiag, 'pressure_raw') ??
    signalNum(sebStatus, 'pressure_kpa')
  const speedDelta =
    cmdSpeed != null && fbkSpeed != null ? fbkSpeed - cmdSpeed : null

  const gearLabel =
    signalText(drive, 'gear') || signalText(motor, 'gear_state') || '—'
  const benchOn = ses?.bench_tx === 'enabled'

  return (
    <WorkspaceShell
      testId="workspace-overview"
      title="Overview"
      description={`Vehicle state and immediate health · session ${ses?.session_id ?? 'none'} · ${messages.length} live messages`}
    >

      <section className="safety-strip" data-testid="safety-strip" aria-label="Safety and mode">
        {/* Multi-source ESTOP (latch + 0x001 H/L + SYS/RT) — same as topbar. */}
        <div
          className={`strip-item ${estopObs.any ? 'hazard' : 'ok'}`}
          title={estopObs.detail}
        >
          <span className="strip-k">ESTOP</span>
          <StatusPill
            label={estopObs.label}
            tone={estopObs.any ? 'danger' : 'ok'}
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
            {brakeKpa != null ? `${brakeKpa.toFixed(0)} kPa` : '—'}
          </span>
          {/* Continuous quantity — progress bar only (no second text chip). */}
          <MeterBar
            value={brakeKpa}
            max={5000}
            tone="high-bad"
            label="Brake pressure"
            testId="meter-brake"
          />
        </div>
      </section>

      <div className="cards metric-cards" data-testid="overview-meters">
        <MetricCard
          title="Speed request"
          valueText={cmdSpeed != null ? cmdSpeed.toFixed(0) : '—'}
          unit="mm/s"
          sub="HOST_DRIVE_CMD 0x300"
          freshness={drive?.freshness}
          value={cmdSpeed}
          max={3000}
          tone="auto"
          testId="card-speed"
          meterTestId="meter-speed-cmd"
        />
        <MetricCard
          title="Motor feedback"
          valueText={fbkSpeed != null ? fbkSpeed.toFixed(0) : '—'}
          unit="mm/s"
          sub="MTR_MOTOR_FBK 0x206"
          freshness={motor?.freshness}
          value={fbkSpeed}
          max={3000}
          tone="auto"
          testId="card-motor"
          meterTestId="meter-speed-fbk"
        />
        <MetricCard
          title="Yaw rate"
          valueText={cmdYaw != null ? cmdYaw.toFixed(0) : '—'}
          unit="mrad/s"
          sub="HOST_DRIVE_CMD"
          freshness={drive?.freshness}
          value={cmdYaw}
          max={3000}
          tone="auto"
          testId="card-yaw"
          meterTestId="meter-yaw"
        />
        <MetricCard
          title="Steering angle"
          valueText={steerDeg != null ? steerDeg.toFixed(1) : '—'}
          unit="°"
          sub="SES_STATUS 0x201"
          freshness={sesStatus?.freshness}
          value={steerDeg}
          max={45}
          min={-45}
          tone="auto"
          testId="card-steer"
          meterTestId="meter-steer"
        />
        <MetricCard
          title="Brake pressure"
          valueText={brakeKpa != null ? brakeKpa.toFixed(0) : '—'}
          unit="kPa"
          sub="HOST/RT brake · continuous · high → red"
          freshness={
            hostBrake?.freshness ?? rtBrake?.freshness ?? brakeDiag?.freshness
          }
          value={brakeKpa}
          max={5000}
          tone="high-bad"
          testId="card-brake"
          meterTestId="meter-brake-card"
        />
        <div className="card metric-card" data-testid="card-gear">
          <div className="card-head">
            <div className="card-title">Gear</div>
            {drive ? <FreshnessBadge value={drive.freshness} /> : null}
          </div>
          {/* Single discrete value — no big text + pill with the same label. */}
          <div className="metric metric-discrete" data-testid="metric-gear">
            <StatusPill
              label={gearLabel}
              tone={gearLabel === 'N' || gearLabel === '—' ? 'muted' : 'accent'}
              testId="status-gear"
            />
          </div>
          <div className="card-sub muted">N/D/S/R enum — not a bar</div>
        </div>
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
          <div className="card-sub mono muted">{status?.adapter?.health ?? '—'}</div>
        </div>
      </div>

      <section className="panel" data-testid="cmd-feedback">
        <h2>Command / feedback</h2>
        <table className="data-table">
          <thead>
            <tr>
              <th>System</th>
              <th>Command</th>
              <th>Feedback</th>
              <th>Difference</th>
              <th>Level</th>
              <th>Health</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>Drive</td>
              <td className="mono">
                {cmdSpeed != null ? `${cmdSpeed.toFixed(0)} mm/s` : '—'}
              </td>
              <td className="mono">
                {fbkSpeed != null ? `${fbkSpeed.toFixed(0)} mm/s` : '—'}
              </td>
              <td className="mono">
                {speedDelta != null ? `${speedDelta.toFixed(0)} mm/s` : '—'}
              </td>
              <td className="meter-cell">
                <MeterBar value={Math.abs(fbkSpeed ?? 0)} max={3000} tone="auto" />
              </td>
              <td>{drive ? <FreshnessBadge value={drive.freshness} /> : '—'}</td>
            </tr>
            <tr>
              <td>Steering</td>
              <td className="mono">
                {cmdYaw != null ? `${cmdYaw.toFixed(0)} mrad/s` : '—'}
              </td>
              <td className="mono">
                {steerDeg != null ? `${steerDeg.toFixed(1)}°` : sesStatus ? 'SES_STATUS' : '—'}
              </td>
              <td className="muted">—</td>
              <td className="meter-cell">
                <MeterBar value={steerDeg} max={45} min={-45} tone="auto" />
              </td>
              <td>
                {sesStatus ? <FreshnessBadge value={sesStatus.freshness} /> : '—'}
              </td>
            </tr>
            <tr>
              <td>Brake</td>
              <td className="mono">
                {signalNum(hostBrake, 'brake_pressure_kpa') != null
                  ? `${signalNum(hostBrake, 'brake_pressure_kpa')!.toFixed(0)} kPa`
                  : signalNum(rtBrake, 'brake_pressure_kpa') != null
                    ? `${signalNum(rtBrake, 'brake_pressure_kpa')!.toFixed(0)} kPa`
                    : '—'}
              </td>
              <td className="mono">
                {brakeKpa != null
                  ? `${brakeKpa.toFixed(0)} kPa`
                  : sebStatus
                    ? 'SEB_STATUS'
                    : '—'}
              </td>
              <td className="muted">—</td>
              <td className="meter-cell">
                <MeterBar
                  value={brakeKpa}
                  max={5000}
                  tone="high-bad"
                  testId="meter-brake-row"
                />
              </td>
              <td>
                {hostBrake || rtBrake || sebStatus ? (
                  <FreshnessBadge
                    value={
                      hostBrake?.freshness ??
                      rtBrake?.freshness ??
                      sebStatus?.freshness ??
                      'unseen'
                    }
                  />
                ) : (
                  '—'
                )}
              </td>
            </tr>
            <tr>
              <td>Safety STS</td>
              <td className="muted">—</td>
              <td className="mono">
                {safety
                  ? `estop=${signalText(safety, 'estop_active')} brake_lt=${signalText(safety, 'light_brake')}`
                  : '—'}
              </td>
              <td className="muted">—</td>
              <td className="meter-cell">
                {/* Binary ESTOP — pill, not a 0/100 progress bar */}
                <StatusPill
                  label={
                    estopObs.any
                      ? estopObs.label
                      : safety
                        ? 'ESTOP clear'
                        : '—'
                  }
                  tone={estopObs.any ? 'danger' : safety ? 'ok' : 'muted'}
                  testId="status-safety-estop"
                />
              </td>
              <td>
                {safety ? <FreshnessBadge value={safety.freshness} /> : '—'}
              </td>
            </tr>
          </tbody>
        </table>
      </section>
    </WorkspaceShell>
  )
}
