import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import { api } from '../api'
import { cleanupControlStreams, isStaleSequenceError } from '../lib/cleanup'
import { hexId } from '../lib/format'
import { findMsg, PROFILE_LABELS } from '../lib/signals'
import { useAppStore } from '../store'
import { ActivityBar } from './ActivityBar'
import { IconGauge, NAV_SECTIONS } from './icons'

export function Sidebar() {
  const workspace = useAppStore((s) => s.workspace)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  const activity = useAppStore((s) => s.activity)
  const setStatus = useAppStore((s) => s.setStatus)
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const messages = useAppStore((s) => s.messages)

  const [busy, setBusy] = useState(false)
  const [fakeBusy, setFakeBusy] = useState(false)
  const [fakeRunning, setFakeRunning] = useState<string[]>([])
  const [controlNote, setControlNote] = useState('')
  const [kbEnabled, setKbEnabled] = useState(false)
  const [kbSnap, setKbSnap] = useState<Record<string, unknown> | null>(null)
  const [busFilter, setBusFilter] = useState<'both' | 'high' | 'low'>('both')
  const [monitorExpanded, setMonitorExpanded] = useState<string | null>(null)

  const seqRef = useRef(0)
  const keysRef = useRef<Record<string, boolean>>({})
  const kbEnabledRef = useRef(false)
  kbEnabledRef.current = kbEnabled

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

  const hostFresh = hostCmd?.freshness ? String(hostCmd.freshness).toLowerCase() : ''
  const rtFresh = rtDriveCmd?.freshness ? String(rtDriveCmd.freshness).toLowerCase() : ''
  const hostTxLive = hostFresh === 'live' || hostFresh === 'late'
  const rtTxLive = rtFresh === 'live' || rtFresh === 'late'
  const benchOn = String(ses?.bench_tx ?? '').toLowerCase() === 'enabled'
  const fullVehicle = profileId === 'full_vehicle'
  const fakeOn = fakeRunning.includes('host_drive_analysis')

  /** live | late → live; everything else → dead */
  const isMsgLive = (freshness?: string) => {
    const f = String(freshness || '').toLowerCase()
    return f === 'live' || f === 'late'
  }

  const liveCompact = useMemo(() => {
    return [...messages]
      .filter((m) => (busFilter === 'both' ? true : m.bus === busFilter))
      .sort((a, b) => {
        const la = isMsgLive(a.freshness) ? 0 : 1
        const lb = isMsgLive(b.freshness) ? 0 : 1
        if (la !== lb) return la - lb
        return a.bus.localeCompare(b.bus) || a.can_id - b.can_id
      })
      .slice(0, 64)
  }, [messages, busFilter])

  const monitorLiveCount = useMemo(
    () => liveCompact.filter((m) => isMsgLive(m.freshness)).length,
    [liveCompact],
  )
  const monitorDeadCount = liveCompact.length - monitorLiveCount

  const refreshFakeSignals = useCallback(async () => {
    try {
      const peers = await api.syntheticPeers()
      setFakeRunning((peers.running || []).map((p) => String(p.name)))
    } catch {
      /* ignore poll errors */
    }
  }, [])

  useEffect(() => {
    if (activity !== 'control') return
    void refreshFakeSignals()
    const id = window.setInterval(() => void refreshFakeSignals(), 2000)
    return () => window.clearInterval(id)
  }, [activity, refreshFakeSignals])

  // Leave control activity → drop keyboard ownership
  useEffect(() => {
    if (activity === 'control') return
    if (!kbEnabledRef.current) return
    setKbEnabled(false)
    setKbSnap(null)
    void api.controlRelease('left_control_activity').catch(() => undefined)
  }, [activity])

  // Keyboard teleop while Control activity is open
  useEffect(() => {
    if (!kbEnabled || activity !== 'control') return

    const onDown = (e: KeyboardEvent) => {
      keysRef.current[e.code] = true
      if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space'].includes(e.code)) {
        e.preventDefault()
      }
    }
    const onUp = (e: KeyboardEvent) => {
      keysRef.current[e.code] = false
    }
    const onBlur = () => {
      keysRef.current = {}
      void api.controlRelease('blur').catch(() => undefined)
      setKbEnabled(false)
      setControlNote('Keyboard released (window blur)')
    }
    const onVis = () => {
      if (document.hidden) {
        void api.controlRelease('tab_hidden').catch(() => undefined)
        setKbEnabled(false)
        setControlNote('Keyboard released (tab hidden)')
      }
    }

    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    window.addEventListener('blur', onBlur)
    document.addEventListener('visibilitychange', onVis)

    const tick = window.setInterval(() => {
      if (!kbEnabledRef.current) return
      const k = keysRef.current
      let throttle = 0
      let steer = 0
      if (k.KeyW || k.ArrowUp) throttle += 1
      if (k.KeyS || k.ArrowDown) throttle -= 1
      if (k.KeyA || k.ArrowLeft) steer -= 1
      if (k.KeyD || k.ArrowRight) steer += 1
      const hard_brake = !!k.ShiftLeft || !!k.ShiftRight
      const estop = !!k.Space
      seqRef.current += 1
      void api
        .controlIntent({
          sequence: seqRef.current,
          source: 'sidebar_keyboard',
          mode: 'kinematics',
          throttle,
          steer,
          gear: throttle < 0 ? 3 : 1,
          hard_brake,
          estop,
        })
        .then((r) => setKbSnap(r.control))
        .catch((e) => {
          const msg = String(e)
          if (isStaleSequenceError(msg)) return
          setKbEnabled(false)
          setControlNote(`Control lost: ${msg}`)
          void api.status().then(setStatus).catch(() => undefined)
        })
    }, 50)

    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
      window.removeEventListener('blur', onBlur)
      document.removeEventListener('visibilitychange', onVis)
      window.clearInterval(tick)
    }
  }, [kbEnabled, activity, setStatus])

  async function toggleBenchTx() {
    setBusy(true)
    try {
      const fresh = await api.status()
      const session = fresh.session
      if (!session?.session_id) {
        setControlNote('No session — start one in Settings (explorer)')
        return
      }
      await api.setBenchTx(session.session_id, !benchOn, session.revision)
      setStatus(await api.status())
      setControlNote(!benchOn ? 'Bench TX armed' : 'Bench TX off')
    } catch (e) {
      setControlNote(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function toggleKeyboard() {
    if (kbEnabled) {
      setBusy(true)
      try {
        setKbEnabled(false)
        setKbSnap(null)
        await api.controlRelease('sidebar_kb_disable')
        setControlNote('Keyboard off')
      } catch (e) {
        setControlNote(String(e))
      } finally {
        setBusy(false)
      }
      return
    }

    setBusy(true)
    try {
      const st = await api.status()
      if (!st.session?.session_id) {
        setControlNote('No session — start one in Settings (explorer)')
        return
      }
      if (String(st.session.bench_tx).toLowerCase() !== 'enabled') {
        setControlNote('Arm Bench TX before keyboard')
        return
      }
      await cleanupControlStreams('sidebar_kb_enable')
      seqRef.current = 0
      keysRef.current = {}
      setKbEnabled(true)
      setControlNote('Keyboard on — WASD / arrows')
    } catch (e) {
      setControlNote(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stopAllMotion() {
    setBusy(true)
    try {
      setKbEnabled(false)
      setKbSnap(null)
      const st = await api.status()
      if (st.session?.session_id) {
        await api.stopAll(st.session.session_id, st.session.revision)
      }
      await cleanupControlStreams('sidebar_stop_all')
      setStatus(await api.status())
      await refreshFakeSignals()
      setControlNote('Stop all — motion TX cleared')
    } catch (e) {
      setControlNote(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function toggleFakeSignals() {
    setFakeBusy(true)
    try {
      if (fakeOn) {
        await api.stopSyntheticPeers()
        setControlNote('Fake signals stopped')
      } else {
        const st = await api.status()
        if (!st.session?.session_id) {
          setControlNote('No session — start one in Settings (explorer)')
          return
        }
        if (String(st.session.bench_tx).toLowerCase() !== 'enabled') {
          setControlNote('Arm Bench TX before fake signals')
          return
        }
        await cleanupControlStreams('sidebar_fake_start', { direct: false })
        await api.startSyntheticPeers(['host_drive_analysis'])
        setControlNote('Fake signals · host_drive_analysis')
      }
      await refreshFakeSignals()
      setStatus(await api.status())
    } catch (e) {
      setControlNote(String(e))
    } finally {
      setFakeBusy(false)
    }
  }

  let body: ReactNode
  let bodyTestId = 'sidebar'
  let bodyLabel = 'Workspace explorer'
  let bodyClass = 'sidebar-body explorer-body'

  if (activity === 'control') {
    bodyTestId = 'sidebar-control'
    bodyLabel = 'Control sidebar'
    bodyClass = 'sidebar-body control-toolbox'
    body = (
      <>
        <div className="context-sidebar-head">
          <span className="nav-label">Operate</span>
          <strong>Control</strong>
          <small>TX · keyboard · fake signals</small>
        </div>

        <div className="context-arm-card" data-testid="control-tx-card">
          <span className={`status-dot ${benchOn ? 'success' : 'danger'}`} />
          <div>
            <strong>{benchOn ? 'TX armed' : 'TX off'}</strong>
            <small>Bench TX gate</small>
          </div>
          <button
            data-testid="sidebar-bench-toggle"
            type="button"
            disabled={busy || !ses?.session_id}
            onClick={() => void toggleBenchTx()}
          >
            {benchOn ? 'Disarm' : 'Arm'}
          </button>
        </div>

        <div className="control-toolbox-block" data-testid="control-keyboard">
          <p className="nav-label">Keyboard</p>
          <button
            type="button"
            data-testid="sidebar-kb-toggle"
            className={kbEnabled ? '' : 'secondary'}
            disabled={busy || fullVehicle || !ses?.session_id}
            onClick={() => void toggleKeyboard()}
          >
            {kbEnabled ? 'Stop keyboard' : 'Start keyboard'}
          </button>
          <ul className="controls-legend muted small control-kb-legend">
            <li>
              <kbd>W</kbd>/<kbd>↑</kbd> throttle · <kbd>S</kbd>/<kbd>↓</kbd> reverse
            </li>
            <li>
              <kbd>A</kbd>/<kbd>D</kbd> yaw · <kbd>Shift</kbd> brake · <kbd>Space</kbd> ESTOP
            </li>
          </ul>
          {kbEnabled && (
            <p className="ok-text small" data-testid="sidebar-kb-active">
              Armed — Host intent on High bus
            </p>
          )}
          {kbSnap && (
            <dl className="kv compact" data-testid="sidebar-kb-shaped">
              <dt>Speed</dt>
              <dd className="mono">{String(kbSnap.shaped_speed_mmps ?? '—')} mm/s</dd>
              <dt>Yaw</dt>
              <dd className="mono">{String(kbSnap.shaped_yaw_mrad_s ?? '—')} mrad/s</dd>
              <dt>Gear</dt>
              <dd className="mono">
                {String(kbSnap.gear_label ?? kbSnap.gear ?? '—')}
              </dd>
            </dl>
          )}
        </div>

        <div className="control-toolbox-block" data-testid="control-fake-signals">
          <p className="nav-label">Fake signals</p>
          <div className="context-arm-card compact">
            <span className={`status-dot ${fakeOn ? 'success' : 'muted'}`} />
            <div>
              <strong>{fakeOn ? 'Stimulus on' : 'Stimulus off'}</strong>
              <small>Zero-speed Host 0x300</small>
            </div>
            <button
              data-testid="sidebar-fake-toggle"
              type="button"
              disabled={fakeBusy || busy || fullVehicle || (!fakeOn && !ses?.session_id)}
              onClick={() => void toggleFakeSignals()}
            >
              {fakeOn ? 'Stop' : 'Start'}
            </button>
          </div>
        </div>

        <div className="control-toolbox-actions">
          <button
            type="button"
            className="danger"
            data-testid="sidebar-stop-all"
            disabled={busy}
            onClick={() => void stopAllMotion()}
          >
            Stop all motion TX
          </button>
        </div>

        {controlNote ? (
          <p className="control-toolbox-note" data-testid="sidebar-control-note">
            {controlNote}
          </p>
        ) : (
          <p className="context-warning">
            Blur or hide the tab releases keyboard. Workspace tabs only in explorer.
          </p>
        )}
      </>
    )
  } else if (activity === 'monitor') {
    bodyTestId = 'sidebar-monitor'
    bodyLabel = 'CAN monitor sidebar'
    bodyClass = 'sidebar-body monitor-sidebar'
    body = (
      <>
        <div className="context-sidebar-head">
          <span className="nav-label">Inspect</span>
          <strong>CAN monitor</strong>
          <small>
            {monitorLiveCount} live · {monitorDeadCount} dead
          </small>
        </div>

        <div className="monitor-bus-filter" role="group" aria-label="Bus filter">
          {(['both', 'high', 'low'] as const).map((b) => (
            <button
              key={b}
              type="button"
              data-testid={`monitor-bus-${b}`}
              className={busFilter === b ? 'route-chip active' : 'route-chip'}
              onClick={() => setBusFilter(b)}
            >
              {b === 'both' ? 'Both' : b === 'high' ? 'High' : 'Low'}
            </button>
          ))}
        </div>

        <div className="monitor-msg-list" data-testid="monitor-live-simplified">
          {liveCompact.length === 0 && (
            <p className="muted small monitor-msg-empty">No frames yet</p>
          )}
          {liveCompact.map((m) => {
            const key = `${m.bus}-${m.can_id}`
            const live = isMsgLive(m.freshness)
            const open = monitorExpanded === key
            const label = m.name?.trim() || hexId(m.can_id)
            return (
              <div
                key={key}
                className={`monitor-msg ${live ? 'is-live' : 'is-dead'}${open ? ' is-open' : ''}`}
                data-testid={`monitor-msg-${m.bus}-${m.can_id}`}
                data-status={live ? 'live' : 'dead'}
              >
                <button
                  type="button"
                  className="monitor-msg-row"
                  aria-expanded={open}
                  onClick={() => setMonitorExpanded((cur) => (cur === key ? null : key))}
                >
                  <span className={`status-dot ${live ? 'live' : 'danger'}`} aria-hidden />
                  <span className="monitor-msg-label" title={label}>
                    {label}
                  </span>
                  <span className={`monitor-msg-status ${live ? 'ok-text' : 'danger-text'}`}>
                    {live ? 'live' : 'dead'}
                  </span>
                  <span className="monitor-msg-chevron muted" aria-hidden>
                    {open ? '▾' : '▸'}
                  </span>
                </button>
                {open && (
                  <div className="monitor-msg-detail" data-testid={`monitor-msg-detail-${m.bus}-${m.can_id}`}>
                    <span className={`status-dot ${live ? 'live' : 'danger'}`} aria-hidden />
                    <span className={`monitor-msg-status ${live ? 'ok-text' : 'danger-text'}`}>
                      {live ? 'live' : 'dead'}
                    </span>
                    <span className="mono">{m.bus === 'high' ? 'H' : 'L'}</span>
                    <span className="mono">{hexId(m.can_id)}</span>
                    {m.name ? <span className="monitor-msg-detail-name">{m.name}</span> : null}
                    {m.observed_rate_hz != null && Number.isFinite(m.observed_rate_hz) ? (
                      <span className="mono muted">{m.observed_rate_hz.toFixed(1)} Hz</span>
                    ) : null}
                  </div>
                )}
              </div>
            )
          })}
        </div>
      </>
    )
  } else {
    body = (
      <>
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
      </>
    )
  }

  return (
    <aside className="sidebar" data-testid="sidebar" aria-label="Application sidebar">
      <ActivityBar />
      <div className={bodyClass} data-testid={bodyTestId} aria-label={bodyLabel}>
        {body}
      </div>
    </aside>
  )
}
