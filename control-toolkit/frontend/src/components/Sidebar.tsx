import { useState } from 'react'
import { api } from '../api'
import { cleanupControlStreams } from '../lib/cleanup'
import { findMsg, PROFILE_LABELS } from '../lib/signals'
import { useAppStore } from '../store'
import { IconExternalLink, IconGauge, NAV_SECTIONS } from './icons'

export function Sidebar() {
  const workspace = useAppStore((s) => s.workspace)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  const activity = useAppStore((s) => s.activity)
  const controlMethod = useAppStore((s) => s.controlMethod)
  const setControlMethod = useAppStore((s) => s.setControlMethod)
  const setStatus = useAppStore((s) => s.setStatus)
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const messages = useAppStore((s) => s.messages)
  const [busy, setBusy] = useState(false)

  const ses = status?.session
  const profileId = ses?.profile ?? status?.profile ?? '—'
  const profileLabel = PROFILE_LABELS[profileId] ?? profileId
  const adapterHealth = status?.adapter?.health ?? '—'
  const streamOk = quality === 'live' || quality === 'delayed'
  const streamLabel =
    quality === 'live'
      ? 'Stream live'
      : quality === 'delayed'
        ? 'Stream delayed'
        : quality === 'dropping'
          ? 'Stream dropping'
          : quality === 'lost'
            ? 'Stream lost'
            : 'Connecting…'

  const motor = findMsg(messages, 'MTR_MOTOR_FBK')
  const sesStatus = findMsg(messages, 'SES_STATUS')
  const hostCmd = findMsg(messages, 'HOST_DRIVE_CMD')
  const rtDriveCmd = findMsg(messages, 'RT_DRIVE_CMD')
  const speedRaw = motor?.signals?.actual_speed_mmps?.engineering_value
  const steerRaw = sesStatus?.signals?.angle_deg?.engineering_value
  const speedText =
    typeof speedRaw === 'number' && Number.isFinite(speedRaw)
      ? `${speedRaw.toFixed(0)} mm/s`
      : '—'
  const steerText =
    typeof steerRaw === 'number' && Number.isFinite(steerRaw)
      ? `${steerRaw.toFixed(1)}°`
      : '—'

  // Outbound CAN commands (TX) — high Host intent and/or low RT drive
  const hostSpeed = hostCmd?.signals?.speed_mmps?.engineering_value
  const hostYaw = hostCmd?.signals?.yaw_rate_mrad_s?.engineering_value
  const hostGear =
    hostCmd?.signals?.gear?.enum_label ?? hostCmd?.signals?.gear?.engineering_value
  const rtSpeed = rtDriveCmd?.signals?.motor_speed_mmps?.engineering_value
  const rtGear =
    rtDriveCmd?.signals?.gear?.enum_label ??
    rtDriveCmd?.signals?.gear?.engineering_value

  const fmtNum = (v: unknown, digits = 0, unit = '') => {
    if (typeof v === 'number' && Number.isFinite(v)) {
      return `${v.toFixed(digits)}${unit ? ` ${unit}` : ''}`
    }
    if (v != null && v !== '') return String(v)
    return '—'
  }

  const hostCmdSpeedText = fmtNum(hostSpeed, 0, 'mm/s')
  const hostCmdYawText = fmtNum(hostYaw, 0, 'mrad/s')
  const hostCmdGearText = hostGear != null && hostGear !== '' ? String(hostGear) : '—'
  const rtCmdSpeedText = fmtNum(rtSpeed, 0, 'mm/s')
  const rtCmdGearText = rtGear != null && rtGear !== '' ? String(rtGear) : '—'

  const hostFresh = hostCmd?.freshness
    ? String(hostCmd.freshness).toLowerCase()
    : ''
  const rtFresh = rtDriveCmd?.freshness
    ? String(rtDriveCmd.freshness).toLowerCase()
    : ''
  const hostTxLive = hostFresh === 'live' || hostFresh === 'late'
  const rtTxLive = rtFresh === 'live' || rtFresh === 'late'
  const benchOn = String(ses?.bench_tx ?? '').toLowerCase() === 'enabled'
  const fullVehicle = profileId === 'full_vehicle'

  async function selectControlRoute(method: 'high' | 'low' | 'mtr' | 'hmi') {
    if (method === controlMethod) {
      setWorkspace('control')
      return
    }
    setBusy(true)
    try {
      await cleanupControlStreams('activity_route_switch')
      setControlMethod(method)
      setWorkspace('control')
    } finally {
      setBusy(false)
    }
  }

  async function toggleBenchTx() {
    setBusy(true)
    try {
      const fresh = await api.status()
      const session = fresh.session
      if (!session?.session_id) return
      await api.setBenchTx(session.session_id, !benchOn, session.revision)
      setStatus(await api.status())
    } finally {
      setBusy(false)
    }
  }

  if (activity === 'control') {
    return (
      <aside className="sidebar contextual-sidebar" data-testid="sidebar-control" aria-label="All-node control sidebar">
        <div className="context-sidebar-head">
          <span className="nav-label">Operate</span>
          <strong>All-node control</strong>
          <small>One motion route at a time</small>
        </div>
        <div className="context-arm-card">
          <span className={`status-dot ${benchOn ? 'success' : 'danger'}`} />
          <div>
            <strong>{benchOn ? 'Bench TX armed' : 'Bench TX off'}</strong>
            <small>Shared gate for every route</small>
          </div>
          <button data-testid="sidebar-bench-toggle" type="button" disabled={busy || !ses?.session_id} onClick={() => void toggleBenchTx()}>
            {benchOn ? 'Disarm' : 'Arm'}
          </button>
        </div>
        <nav className="context-nav" aria-label="Control routes">
          <button data-testid="control-route-high" className={controlMethod === 'high' ? 'nav active' : 'nav'} disabled={busy} onClick={() => void selectControlRoute('high')}>
            <span><strong>High bus</strong><small>Host 0x300 → RT kinematics</small></span>
          </button>
          <button data-testid="control-route-low" className={controlMethod === 'low' ? 'nav active' : 'nav'} disabled={busy || fullVehicle} onClick={() => void selectControlRoute('low')}>
            <span><strong>Low bus</strong><small>Motor + steering + brake</small></span>
          </button>
          <button data-testid="control-route-mtr" className={controlMethod === 'mtr' ? 'nav active' : 'nav'} disabled={busy || fullVehicle} onClick={() => void selectControlRoute('mtr')}>
            <span><strong>MTR direct</strong><small>Motor-only 0x204 on Low</small></span>
          </button>
          <button data-testid="control-route-hmi" className={controlMethod === 'hmi' ? 'nav active' : 'nav'} disabled={busy || fullVehicle} onClick={() => void selectControlRoute('hmi')}>
            <span><strong>HMI</strong><small>Mode and power requests</small></span>
          </button>
        </nav>
        <p className="context-warning">Changing routes stops existing motion streams before selecting the next route.</p>
      </aside>
    )
  }

  if (activity === 'monitor') {
    const monitorItems = NAV_SECTIONS.flatMap((section) => section.items).filter((item) =>
      ['live', 'network', 'diagnostics', 'logs', 'dictionary'].includes(item.id),
    )
    return (
      <aside className="sidebar contextual-sidebar" data-testid="sidebar-monitor" aria-label="CAN monitor sidebar">
        <div className="context-sidebar-head">
          <span className="nav-label">Inspect</span>
          <strong>CAN monitor</strong>
          <small>Live traffic and evidence</small>
        </div>
        <nav className="context-nav" aria-label="Monitor workspaces">
          {monitorItems.map((item) => (
            <button key={item.id} type="button" className={workspace === item.id ? 'nav active' : 'nav'} onClick={() => setWorkspace(item.id)}>
              {item.icon}<span>{item.label}</span>
            </button>
          ))}
        </nav>
      </aside>
    )
  }

  return (
    <aside
      className="sidebar flex w-[var(--sidebar-w)] shrink-0 flex-col overflow-y-auto border-r border-border bg-surface-2"
      data-testid="sidebar"
      aria-label="Primary navigation"
    >
      <nav className="sidebar-nav flex flex-col gap-3 p-3" aria-label="Primary workspaces">
        {NAV_SECTIONS.map((section) => (
          <div key={section.label} className="nav-section">
            <p className="nav-label">{section.label}</p>
            {section.items.map((item) => (
              <button
                key={item.id}
                type="button"
                data-testid={`nav-${item.id}`}
                className={workspace === item.id ? 'nav active' : 'nav'}
                onClick={() => setWorkspace(item.id)}
              >
                {item.icon}
                <span>{item.label}</span>
              </button>
            ))}
          </div>
        ))}
      </nav>

      <section className="vehicle-card" aria-labelledby="side-vehicle-title">
        <div className="vehicle-card-header">
          <div className="vehicle-card-title">
            <IconGauge />
            <div>
              <strong id="side-vehicle-title">eTrike</strong>
              <small>Cmd TX · feedback</small>
            </div>
          </div>
          {streamOk ? (
            <span className="vehicle-live">Live</span>
          ) : (
            <span className="vehicle-offline">Offline</span>
          )}
        </div>
        <div className="vehicle-readouts">
          <div className="vehicle-readout">
            <span>Speed fbk</span>
            <strong data-testid="sidebar-speed">{speedText}</strong>
            <small className="vehicle-cmd" data-testid="sidebar-speed-cmd">
              {hostTxLive
                ? `cmd ${hostCmdSpeedText}`
                : rtTxLive
                  ? `cmd ${rtCmdSpeedText}`
                  : hostCmd
                    ? `cmd ${hostCmdSpeedText}`
                    : rtDriveCmd
                      ? `cmd ${rtCmdSpeedText}`
                      : 'cmd —'}
            </small>
          </div>
          <div className="vehicle-readout">
            <span>Steer fbk</span>
            <strong data-testid="sidebar-steer">{steerText}</strong>
            <small className="vehicle-cmd" data-testid="sidebar-steer-cmd">
              {hostCmd ? `cmd yaw ${hostCmdYawText}` : 'cmd yaw —'}
            </small>
          </div>
        </div>
        <div className="vehicle-cmd-strip" data-testid="sidebar-cmd-strip">
          <div className="vehicle-cmd-line" data-testid="sidebar-cmd-high">
            <span className="vehicle-cmd-tag">TX High</span>
            <span className="mono">
              0x300 {hostCmdSpeedText} · yaw {hostCmdYawText} · gear {hostCmdGearText}
            </span>
            {hostFresh ? (
              <span className={`vehicle-cmd-fresh fresh-${hostFresh}`}>{hostFresh}</span>
            ) : (
              <span className="vehicle-cmd-fresh muted">—</span>
            )}
          </div>
          <div className="vehicle-cmd-line" data-testid="sidebar-cmd-low">
            <span className="vehicle-cmd-tag">TX Low</span>
            <span className="mono">
              0x204 {rtCmdSpeedText} · gear {rtCmdGearText}
            </span>
            {rtFresh ? (
              <span className={`vehicle-cmd-fresh fresh-${rtFresh}`}>{rtFresh}</span>
            ) : (
              <span className="vehicle-cmd-fresh muted">—</span>
            )}
          </div>
        </div>
        <button
          type="button"
          className="vehicle-open-drive"
          data-testid="sidebar-open-drive"
          onClick={() => setWorkspace('preview')}
        >
          <IconExternalLink />
          <span>Open Drive console</span>
        </button>
      </section>

      <div className="system-card" data-testid="sidebar-system-card">
        <div className="system-card-row">
          <span
            className={`status-dot ${streamOk ? 'success' : quality === 'connecting' ? 'warning' : 'danger'}`}
          />
          <div>
            <strong>{streamLabel}</strong>
            <small>
              {profileLabel} · adapter {adapterHealth}
            </small>
          </div>
        </div>
      </div>
    </aside>
  )
}
