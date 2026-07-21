import { useCallback, useEffect, useState } from 'react'
import { api, type RuntimeIndicator, type SettingsSnapshot } from '../api'
import { activateTransportProfile } from '../lib/session'
import { PROFILE_LABELS, shortHash, transportModeOf } from '../lib/signals'
import { useAppStore } from '../store'
import { Button } from './ui/button'
import { WorkspaceShell } from './WorkspaceShell'

type RealSubProfile = 'bench_test' | 'full_vehicle'

export function Settings() {
  const setStatus = useAppStore((s) => s.setStatus)
  const status = useAppStore((s) => s.status)
  const [snap, setSnap] = useState<SettingsSnapshot | null>(null)
  const [log, setLog] = useState('')
  const [busy, setBusy] = useState(false)
  const [realSub, setRealSub] = useState<RealSubProfile>('bench_test')

  const activeProfile =
    (snap?.transport?.active?.profile as string | undefined) ||
    status?.session?.profile ||
    status?.profile ||
    'pure_software'
  const activeMode =
    (snap?.transport?.active?.mode as 'computer' | 'real' | undefined) ||
    transportModeOf(activeProfile)

  const refreshSettings = useCallback(async () => {
    try {
      const s = await api.settings()
      setSnap(s)
    } catch {
      // Fallback to profiles if aggregate endpoint unavailable.
      const r = await api.profiles()
      setSnap((prev) => ({
        service: prev?.service ?? {
          title: 'Control Toolkit',
          version: '—',
          ready: false,
          api_prefix: '/api/v1',
          host: '—',
          port: 0,
          workers: 1,
        },
        transport: {
          modes: r.transport_modes || [],
          profiles: r.profiles || [],
          physical_adapter: r.physical_adapter || {
            kind: 'canalystii',
            available: false,
          },
          channel_map: prev?.transport?.channel_map ?? {},
          active: prev?.transport?.active ?? {
            profile: status?.session?.profile ?? status?.profile,
            destination: status?.session?.destination,
            mode: transportModeOf(status?.session?.profile ?? status?.profile),
          },
        },
        session: prev?.session ?? {},
        adapter: prev?.adapter ?? {},
        protocol: prev?.protocol ?? {
          wire_hash: '',
          semantic_hash: '',
          network_hash: '',
          catalog: { messages: 0, instances: 0 },
        },
        runtime: prev?.runtime ?? {
          default_profile: 'pure_software',
          stream_heartbeat_ms: 0,
          latest_state_batch_hz: 0,
          browser_degraded_ms: 0,
          browser_lost_ms: 0,
          rx_queue_maxsize: 0,
          history_capacity: 0,
        },
        history: prev?.history ?? {},
        control: prev?.control ?? {},
        synthetic_peers: prev?.synthetic_peers ?? [],
        diagnostics: prev?.diagnostics ?? { episode_count: 0, episodes: [] },
        recording: prev?.recording ?? { active: null },
        simulation: prev?.simulation ?? {
          mode: 'computer',
          profile: 'pure_software',
          backend: { state: 'unknown' },
          virtual_can: { state: 'unknown' },
          router: { state: 'unknown' },
          rt_sil: { state: 'unknown' },
          sys_sil: { state: 'unavailable', available: false },
          protocol: { state: 'unknown' },
        },
      }))
    }
  }, [status?.session?.destination, status?.session?.profile, status?.profile])

  useEffect(() => {
    void refreshSettings().catch(() => undefined)
    const id = window.setInterval(() => void refreshSettings().catch(() => undefined), 8000)
    return () => window.clearInterval(id)
  }, [refreshSettings])

  useEffect(() => {
    if (activeProfile === 'full_vehicle') setRealSub('full_vehicle')
    else if (activeProfile === 'bench_test') setRealSub('bench_test')
  }, [activeProfile])

  async function ensureThenSetProfile(profile: string) {
    setBusy(true)
    try {
      const nextStatus = await activateTransportProfile(profile)
      setStatus(nextStatus)
      await refreshSettings()
      const label = PROFILE_LABELS[profile] ?? profile
      setLog(
        `Active: ${label} · session ${nextStatus.session.session_id} · phase ${nextStatus.session.phase}` +
          (nextStatus.session.destination ? ` · dest ${nextStatus.session.destination}` : ''),
      )
    } catch (e) {
      setLog(String(e))
      try {
        setStatus(await api.status())
      } catch {
        /* keep last known status */
      }
      await refreshSettings().catch(() => undefined)
    } finally {
      setBusy(false)
    }
  }

  async function activateComputer() {
    await ensureThenSetProfile('pure_software')
  }

  async function activateReal() {
    setBusy(true)
    try {
      const nextStatus = await activateTransportProfile(realSub)
      setStatus(nextStatus)
      await refreshSettings()
      const label = PROFILE_LABELS[realSub] ?? realSub
      const noLink =
        nextStatus.adapter?.health === 'absent' || nextStatus.link?.connected === false
      setLog(
        noLink
          ? `Active: ${label} · Link: No connection (plug CANalyst when ready; TX stays off)`
          : `Active: ${label} · Link: Connected · session ${nextStatus.session.session_id}`,
      )
    } catch (e) {
      setLog(String(e))
      try {
        setStatus(await api.status())
      } catch {
        /* keep last */
      }
      await refreshSettings().catch(() => undefined)
    } finally {
      setBusy(false)
    }
  }

  async function restartSession(profile: string) {
    setBusy(true)
    try {
      const st = await api.status()
      if (st.session?.session_id) {
        await api.closeSession(st.session.session_id, st.session.revision)
      }
      const created = await api.createSession(profile)
      const next = await api.status()
      setStatus(next)
      await refreshSettings()
      const linkNote =
        profile !== 'pure_software' &&
        (next.adapter?.health === 'absent' || next.link?.connected === false)
          ? ' · Link: No connection (plug CANalyst when ready)'
          : ''
      setLog(
        `Restarted ${PROFILE_LABELS[profile] ?? profile} · session ${created.session.session_id} · phase ${created.session.phase}${linkNote}`,
      )
    } catch (e) {
      setLog(String(e))
      await refreshSettings().catch(() => undefined)
    } finally {
      setBusy(false)
    }
  }

  async function closeCurrentSession() {
    const sid = String(snap?.session?.session_id || status?.session?.session_id || '')
    const rev = Number(snap?.session?.revision ?? status?.session?.revision ?? 0)
    if (!sid) return
    setBusy(true)
    try {
      await api.closeSession(sid, rev)
      setStatus(await api.status())
      await refreshSettings()
      setLog(`Ended session ${sid}; Bench TX and control jobs are stopped.`)
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function releaseLease(leaseId: string) {
    const sid = String(snap?.session?.session_id || status?.session?.session_id || '')
    if (!sid) return
    setBusy(true)
    try {
      await api.releaseLease(sid, leaseId)
      setStatus(await api.status())
      await refreshSettings()
      setLog(`Released ownership lease ${leaseId}`)
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function toggleBenchTx(enabled: boolean) {
    const sid = String(snap?.session?.session_id || status?.session?.session_id || '')
    const rev = Number(snap?.session?.revision ?? status?.session?.revision ?? 0)
    if (!sid) {
      setLog('No active session — start Computer or Real first.')
      return
    }
    setBusy(true)
    try {
      await api.setBenchTx(sid, enabled, rev)
      setStatus(await api.status())
      await refreshSettings()
      setLog(`Bench TX ${enabled ? 'enabled' : 'disabled'}`)
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function doStopAll() {
    const sid = String(snap?.session?.session_id || status?.session?.session_id || '')
    const rev = Number(snap?.session?.revision ?? status?.session?.revision ?? 0)
    if (!sid) {
      setLog('No active session.')
      return
    }
    setBusy(true)
    try {
      await api.stopAll(sid, rev)
      setStatus(await api.status())
      await refreshSettings()
      setLog('Stop-all applied')
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function setSimulationRunning(running: boolean) {
    setBusy(true)
    try {
      const result = running ? await api.startSimulation() : await api.stopSimulation()
      await refreshSettings()
      setLog(`RT SIL ${result.simulation.rt_sil.state}. Virtual CAN remains ${result.simulation.virtual_can.state}.`)
    } catch (e) {
      setLog(String(e).replace(/^Error:\s*/i, ''))
      await refreshSettings().catch(() => undefined)
    } finally {
      setBusy(false)
    }
  }

  const modes = snap?.transport?.modes ?? []
  const profiles = snap?.transport?.profiles ?? []
  const physAdapter = snap?.transport?.physical_adapter
  const computerMode = modes.find((m) => m.id === 'computer')
  const realMode = modes.find((m) => m.id === 'real')
  const realOk = physAdapter?.available ?? realMode?.available ?? false
  const realReason =
    physAdapter?.reason ||
    realMode?.reason ||
    profiles.find((p) => p.id === realSub)?.reason

  const session = snap?.session ?? {}
  const adapterLive = snap?.adapter ?? status?.adapter ?? {}
  const channelMap = snap?.transport?.channel_map ?? {}
  const protocol = snap?.protocol
  const runtime = snap?.runtime
  const history = snap?.history ?? {}
  const control = snap?.control ?? {}
  const service = snap?.service
  const simulation = snap?.simulation
  const benchTx = String(session.bench_tx ?? 'disabled')
  const caps = Array.isArray(session.capabilities) ? (session.capabilities as string[]) : []
  const leases = Array.isArray(session.leases) ? (session.leases as string[]) : []
  const adapterChannels =
    adapterLive.channels && typeof adapterLive.channels === 'object'
      ? (adapterLive.channels as Record<string, unknown>)
      : {}

  return (
    <WorkspaceShell
      testId="workspace-settings"
      title="Settings"
      description={<>Session, transport, adapter, and protocol · live from <span className="mono">GET /api/v1/settings</span></>}
    >

      <section className="panel" data-testid="transport-mode-panel">
        <h2>Transport mode</h2>
        <p className="muted small" style={{ marginTop: 0 }}>
          Two runtimes of the <strong>same software</strong>: virtual buses on this PC, or physical
          High/Low via CANalyst-II (no silent fallback).
        </p>

        <div className="transport-toggle" data-testid="transport-toggle" role="tablist">
          <button
            type="button"
            role="tab"
            className={activeMode === 'computer' ? 'transport-btn active' : 'transport-btn'}
            data-testid="mode-computer"
            aria-selected={activeMode === 'computer'}
            disabled={busy}
            onClick={() => void activateComputer()}
          >
            <span className="transport-btn-title">
              {computerMode?.label?.split('(')[0]?.trim() || 'Computer'}
            </span>
            <span className="transport-btn-sub">Virtual dual CAN · no USB</span>
          </button>
          <button
            type="button"
            role="tab"
            className={activeMode === 'real' ? 'transport-btn active real' : 'transport-btn'}
            data-testid="mode-real"
            aria-selected={activeMode === 'real'}
            disabled={busy}
            title={
              realOk
                ? 'Connect via CANalyst-II'
                : `Enter Real without adapter · ${String(realReason || 'no CANalyst yet')}`
            }
            onClick={() => void activateReal()}
          >
            <span className="transport-btn-title">
              {realMode?.label?.split('(')[0]?.trim() || 'Real'}
            </span>
            <span className="transport-btn-sub">
              {realOk ? 'CANalyst-II CH0/CH1' : 'No adapter · enter Real'}
            </span>
          </button>
        </div>

        <div className="transport-cards" data-testid="profile-list">
          <div
            className={`transport-card${activeMode === 'computer' ? ' active' : ''}`}
            data-testid="card-mode-computer"
          >
            <div className="transport-card-head">
              <h3>{computerMode?.label || 'Computer (virtual)'}</h3>
              <span className={`chip tiny ${activeMode === 'computer' ? 'ok' : ''}`}>
                {activeMode === 'computer' ? 'active' : 'idle'}
              </span>
            </div>
            <p className="muted small">
              {computerMode?.description ||
                'Dual virtual High/Low buses on this PC. No CANalyst required.'}
            </p>
            <ul className="transport-bullets muted small">
              <li>
                Destination: <strong>{computerMode?.destination || 'virtual'}</strong>
              </li>
              <li>
                Profile: <span className="mono">{computerMode?.profile || 'pure_software'}</span>
              </li>
              <li>
                High → {channelMap.high?.physical || 'virtual:high'} · Low →{' '}
                {channelMap.low?.physical || 'virtual:low'}
              </li>
            </ul>
            {activeMode === 'computer' && (
              <div className="software-runtime" data-testid="software-runtime-panel">
                <div className="software-runtime-head">
                  <strong>Software runtime</strong>
                  <span className="muted small">explicit process health</span>
                </div>
                <div className="runtime-indicators">
                  {[
                    ['Backend', simulation?.backend],
                    ['Virtual CAN', simulation?.virtual_can],
                    ['Router', simulation?.router],
                    ['RT SIL', simulation?.rt_sil],
                    ['SYS SIL', simulation?.sys_sil],
                    ['Protocol', simulation?.protocol],
                  ].map(([label, indicator]) => {
                    const item = indicator as RuntimeIndicator | undefined
                    const state = item?.state || 'unknown'
                    const tone = state === 'running' || state === 'loaded' ? 'ok' : state === 'error' ? 'danger' : 'muted'
                    return (
                      <div className="runtime-indicator" key={String(label)}>
                        <span className={`status-dot ${tone === 'ok' ? 'live' : tone === 'danger' ? 'danger' : 'muted'}`} />
                        <span>{String(label)}</span>
                        <strong className="mono">{state}</strong>
                      </div>
                    )
                  })}
                </div>
                <p className="muted small runtime-scope" data-testid="simulation-scope">
                  RT: {simulation?.rt_sil?.scope || 'RT SIL status unavailable.'} · SYS:{' '}
                  {simulation?.sys_sil?.scope ||
                    simulation?.sys_sil?.reason ||
                    simulation?.sys_sil?.state ||
                    'unknown'}
                  .
                </p>
                {!simulation?.rt_sil?.available && (
                  <p className="muted small" data-testid="simulation-unavailable-hint">
                    Start simulation is locked: RT SIL executable not configured.
                    Build <span className="mono">native-test</span> (sim_engine_native) or set{' '}
                    <span className="mono">CTK_NATIVE_SIL_EXE</span>, then restart the API.
                  </p>
                )}
                <div className="actions tight">
                  <button
                    type="button"
                    data-testid="btn-simulation-start"
                    disabled={
                      busy ||
                      simulation?.rt_sil?.state === 'running' ||
                      !simulation?.rt_sil?.available
                    }
                    title={
                      !simulation?.rt_sil?.available
                        ? 'RT SIL executable missing — set CTK_NATIVE_SIL_EXE or build native-test SIL'
                        : simulation?.rt_sil?.state === 'running'
                          ? 'Already running'
                          : 'Start RT SIL peer on virtual CAN'
                    }
                    onClick={() => void setSimulationRunning(true)}
                  >
                    Start simulation
                  </button>
                  <button
                    type="button"
                    className="secondary"
                    data-testid="btn-simulation-stop"
                    disabled={busy || simulation?.rt_sil?.state !== 'running'}
                    onClick={() => void setSimulationRunning(false)}
                  >
                    Stop simulation
                  </button>
                </div>
              </div>
            )}
            <div className="actions tight">
              <button
                type="button"
                disabled={busy}
                data-testid="btn-start-pure"
                onClick={() => void restartSession('pure_software')}
              >
                {activeMode === 'computer' ? 'Restart Computer session' : 'Switch to Computer'}
              </button>
            </div>
          </div>

          <div
            className={`transport-card${activeMode === 'real' ? ' active' : ''}`}
            data-testid="card-mode-real"
          >
            <div className="transport-card-head">
              <h3>{realMode?.label || 'Real (CANalyst-II)'}</h3>
              <span className={`chip tiny ${realOk ? 'ok' : 'danger'}`}>
                {realOk ? 'adapter OK' : 'no adapter'}
              </span>
            </div>
            <p className="muted small">
              {realMode?.description ||
                'Physical High/Low via CANalyst-II. CH0 = High, CH1 = Low @ 500 kbit/s.'}
            </p>
            <ul className="transport-bullets muted small">
              <li>
                Adapter:{' '}
                <strong className="mono">{physAdapter?.kind || realMode?.adapter || 'canalystii'}</strong>
                {physAdapter?.channels
                  ? ` · High ${physAdapter.channels.high || 'CH0'} · Low ${physAdapter.channels.low || 'CH1'}`
                  : ' · CH0 High · CH1 Low'}
                {physAdapter?.bitrate ? ` · ${physAdapter.bitrate / 1000} kbit/s` : ''}
              </li>
              <li>
                Status:{' '}
                {realOk ? (
                  <span className="ok-text">detected</span>
                ) : (
                  <span className="danger-text">{String(realReason || 'not detected')}</span>
                )}
              </li>
              <li>
                Driver:{' '}
                <strong className="mono">
                  {physAdapter?.backend || 'python-can/canalystii'}
                </strong>
                {physAdapter?.python_can_version
                  ? ` · python-can ${physAdapter.python_can_version}`
                  : ''}
                {physAdapter?.device_index != null ? ` · device ${physAdapter.device_index}` : ''}
              </li>
              <li>
                USB:{' '}
                <span className="mono">
                  {physAdapter?.usb_vid != null && physAdapter?.usb_pid != null
                    ? `${physAdapter.usb_vid.toString(16).padStart(4, '0').toUpperCase()}:${physAdapter.usb_pid.toString(16).padStart(4, '0').toUpperCase()}`
                    : '04D8:0053'}
                </span>
                {physAdapter?.usb_visible === true
                  ? ' · visible'
                  : physAdapter?.usb_visible === false
                    ? ' · not visible'
                    : ' · visibility unknown'}
              </li>
            </ul>

            <div className="field" style={{ marginTop: 10 }}>
              <span className="field-label">Real sub-profile</span>
              <div className="seg" data-testid="real-sub-profile">
                <button
                  type="button"
                  className={realSub === 'bench_test' ? 'seg-btn active' : 'seg-btn'}
                  data-testid="btn-profile-bench_test"
                  disabled={busy}
                  onClick={() => setRealSub('bench_test')}
                >
                  Bench Test
                </button>
                <button
                  type="button"
                  className={realSub === 'full_vehicle' ? 'seg-btn active' : 'seg-btn'}
                  data-testid="btn-profile-full_vehicle"
                  disabled={busy}
                  onClick={() => setRealSub('full_vehicle')}
                >
                  Full Vehicle
                </button>
              </div>
              <span className="field-hint">
                Bench = ECU under test on CANalyst; Full Vehicle = production-like physical path.
              </span>
            </div>

            <div className="actions tight">
              <button
                type="button"
                disabled={busy}
                className={activeMode === 'real' ? 'secondary' : undefined}
                data-testid="btn-connect-real"
                title={
                  realOk
                    ? 'Open Real session on CANalyst-II'
                    : 'Enter Real mode without adapter (link shows No connection until USB is present)'
                }
                onClick={() => void restartSession(realSub)}
              >
                {activeMode === 'real' && activeProfile === realSub
                  ? 'Restart Real session'
                  : realOk
                    ? 'Connect Real (CANalyst-II)'
                    : 'Enter Real (no adapter yet)'}
              </button>
            </div>
          </div>
        </div>

        <dl className="kv" style={{ marginTop: 16 }}>
          <dt>Mode</dt>
          <dd data-testid="settings-active-mode">
            {activeMode === 'real' ? 'Real · CANalyst-II' : 'Computer · Virtual'}
          </dd>
          <dt>Profile</dt>
          <dd data-testid="settings-active-profile">
            {PROFILE_LABELS[activeProfile] ?? activeProfile}
          </dd>
          <dt>Destination</dt>
          <dd className="mono">
            {String(session.destination ?? status?.session?.destination ?? '—')}
          </dd>
          <dt>Revision</dt>
          <dd className="mono">{String(session.revision ?? status?.session?.revision ?? 0)}</dd>
          <dt>Session ID</dt>
          <dd className="mono">
            {String(session.session_id ?? status?.session?.session_id ?? 'none')}
          </dd>
          <dt>Adapter health</dt>
          <dd className="mono">
            {String(adapterLive.health ?? status?.adapter?.health ?? '—')}
          </dd>
          <dt>Worker / reconnect</dt>
          <dd className="mono">
            {adapterLive.worker_alive == null
              ? '—'
              : adapterLive.worker_alive
                ? 'running'
                : 'stopped'}
            {adapterLive.retry_count != null
              ? ` · retries ${String(adapterLive.retry_count)}`
              : ''}
          </dd>
          {adapterLive.last_error ? (
            <>
              <dt>Adapter error</dt>
              <dd className="danger-text">{String(adapterLive.last_error)}</dd>
            </>
          ) : null}
        </dl>
        <pre className="log" data-testid="settings-log">
          {log ||
            'Pick Computer (virtual buses) or Real (CANalyst-II). Other panels below reflect live backend state.'}
        </pre>
      </section>

      <section className="panel" data-testid="settings-session-panel">
        <h2>Session</h2>
        <p className="muted small" style={{ marginTop: 0 }}>
          Active session controls from the backend session manager (bench TX, stop-all, leases).
        </p>
        <dl className="kv compact">
          <dt>Phase</dt>
          <dd className="mono" data-testid="settings-session-phase">
            {String(session.phase ?? status?.session?.phase ?? '—')}
          </dd>
          <dt>Bench TX</dt>
          <dd className="mono" data-testid="settings-bench-tx">
            {benchTx}
          </dd>
          <dt>Capabilities</dt>
          <dd className="mono">{caps.length ? caps.join(', ') : '—'}</dd>
          <dt>Leases</dt>
          <dd className="mono">{leases.length ? leases.join(', ') : 'none'}</dd>
          <dt>Requested mode</dt>
          <dd className="mono">{String(session.requested_mode ?? '—')}</dd>
          <dt>E-STOP (view)</dt>
          <dd className="mono">
            {session.estop_active == null ? '—' : session.estop_active ? 'active' : 'clear'}
          </dd>
        </dl>
        <div className="actions tight" style={{ marginTop: 10 }}>
          <Button
            disabled={busy || !session.session_id}
            data-testid="btn-bench-tx-on"
            onClick={() => void toggleBenchTx(true)}
          >
            Enable Bench TX
          </Button>
          <Button
            variant="secondary"
            disabled={busy || !session.session_id}
            data-testid="btn-bench-tx-off"
            onClick={() => void toggleBenchTx(false)}
          >
            Disable Bench TX
          </Button>
          <Button
            variant="danger"
            disabled={busy || !session.session_id}
            data-testid="btn-settings-stop-all"
            onClick={() => void doStopAll()}
          >
            Stop all
          </Button>
          <Button
            variant="secondary"
            disabled={busy || !session.session_id}
            data-testid="btn-close-session"
            onClick={() => void closeCurrentSession()}
          >
            End session
          </Button>
        </div>
        {leases.length > 0 && (
          <div className="actions tight" style={{ marginTop: 10 }} data-testid="lease-controls">
            {leases.map((leaseId) => (
              <button
                key={leaseId}
                type="button"
                className="secondary"
                disabled={busy}
                data-testid={`btn-release-lease-${leaseId}`}
                onClick={() => void releaseLease(leaseId)}
              >
                Release lease {leaseId.slice(0, 10)}
              </button>
            ))}
          </div>
        )}
      </section>

      <section className="panel" data-testid="settings-adapter-panel">
        <h2>Adapter &amp; channels</h2>
        <p className="muted small" style={{ marginTop: 0 }}>
          Live transport status plus logical High/Low channel map for this destination.
        </p>
        <dl className="kv compact">
          <dt>Identity</dt>
          <dd className="mono">{String(adapterLive.identity ?? '—')}</dd>
          <dt>Health</dt>
          <dd className="mono">{String(adapterLive.health ?? '—')}</dd>
          <dt>Epoch</dt>
          <dd className="mono">{String(adapterLive.adapter_epoch ?? '—')}</dd>
        </dl>
        <div className="settings-channel-grid" data-testid="settings-channel-map">
          {(['high', 'low'] as const).map((bus) => {
            const cm = channelMap[bus]
            const chState = adapterChannels[bus] ?? adapterChannels[cm?.physical || '']
            return (
              <div key={bus} className="settings-channel-card">
                <div className="transport-card-head">
                  <h3>{bus.toUpperCase()}</h3>
                  <span className="chip tiny">{cm?.physical || bus}</span>
                </div>
                <dl className="kv compact">
                  <dt>Map</dt>
                  <dd className="mono">{cm?.physical || '—'}</dd>
                  <dt>Bitrate</dt>
                  <dd className="mono">
                    {cm?.bitrate ? `${cm.bitrate / 1000} kbit/s` : '—'}
                  </dd>
                  <dt>Role</dt>
                  <dd className="muted small">{cm?.role || '—'}</dd>
                  {chState != null && typeof chState === 'object' ? (
                    <>
                      <dt>State</dt>
                      <dd className="mono">
                        {JSON.stringify(chState).slice(0, 120)}
                        {JSON.stringify(chState).length > 120 ? '…' : ''}
                      </dd>
                    </>
                  ) : null}
                </dl>
              </div>
            )
          })}
        </div>
      </section>

      <section className="panel" data-testid="settings-protocol-panel">
        <h2>Protocol</h2>
        <p className="muted small" style={{ marginTop: 0 }}>
          Wire / semantic / network hashes and dictionary catalog sizes from the loaded protocol
          package.
        </p>
        <dl className="kv compact">
          <dt>Wire hash</dt>
          <dd className="mono" title={protocol?.wire_hash}>
            {shortHash(protocol?.wire_hash, 16)}
          </dd>
          <dt>Semantic hash</dt>
          <dd className="mono" title={protocol?.semantic_hash}>
            {shortHash(protocol?.semantic_hash, 16)}
          </dd>
          <dt>Network hash</dt>
          <dd className="mono" title={protocol?.network_hash}>
            {shortHash(protocol?.network_hash, 16)}
          </dd>
          <dt>Messages</dt>
          <dd className="mono" data-testid="settings-msg-count">
            {protocol?.catalog?.messages ?? '—'}
          </dd>
          <dt>Instances</dt>
          <dd className="mono">{protocol?.catalog?.instances ?? '—'}</dd>
        </dl>
      </section>

      <section className="panel" data-testid="settings-runtime-panel">
        <h2>Runtime (CTK config)</h2>
        <p className="muted small" style={{ marginTop: 0 }}>
          {runtime?.notes ||
            'Process ToolkitConfig values (overridable via CTK_* environment variables).'}
        </p>
        <dl className="kv compact" data-testid="settings-runtime-kv">
          <dt>Default profile</dt>
          <dd className="mono">{runtime?.default_profile ?? '—'}</dd>
          <dt>Stream heartbeat</dt>
          <dd className="mono">
            {runtime?.stream_heartbeat_ms != null ? `${runtime.stream_heartbeat_ms} ms` : '—'}
          </dd>
          <dt>Latest-state batch</dt>
          <dd className="mono">
            {runtime?.latest_state_batch_hz != null ? `${runtime.latest_state_batch_hz} Hz` : '—'}
          </dd>
          <dt>Browser degraded / lost</dt>
          <dd className="mono">
            {runtime
              ? `${runtime.browser_degraded_ms} / ${runtime.browser_lost_ms} ms`
              : '—'}
          </dd>
          <dt>RX queue max</dt>
          <dd className="mono">{runtime?.rx_queue_maxsize ?? '—'}</dd>
          <dt>History capacity</dt>
          <dd className="mono">{runtime?.history_capacity ?? '—'}</dd>
          <dt>Bind</dt>
          <dd className="mono">
            {service
              ? `${service.host}:${service.port}${service.api_prefix}`
              : runtime?.host
                ? `${runtime.host}:${runtime.port}`
                : '—'}
          </dd>
          <dt>Workers</dt>
          <dd className="mono">{service?.workers ?? 1}</dd>
        </dl>
      </section>

      <div className="settings-grid-2">
        <section className="panel" data-testid="settings-history-panel">
          <h2>Frame history</h2>
          <dl className="kv compact">
            <dt>Size</dt>
            <dd className="mono">
              {String(history.size ?? '—')} / {String(history.capacity ?? '—')}
            </dd>
            <dt>Appended</dt>
            <dd className="mono">{String(history.total_appended ?? '—')}</dd>
            <dt>Dropped</dt>
            <dd className="mono">{String(history.dropped ?? '—')}</dd>
          </dl>
        </section>

        <section className="panel" data-testid="settings-control-panel">
          <h2>Control intent</h2>
          <dl className="kv compact">
            <dt>Active</dt>
            <dd className="mono">{control.active ? 'yes' : 'no'}</dd>
            <dt>Method</dt>
            <dd className="mono">{String(control.method ?? control.mode ?? '—')}</dd>
            <dt>Source</dt>
            <dd className="mono">{String(control.source ?? '—')}</dd>
            <dt>E-STOP</dt>
            <dd className="mono">{control.estop ? 'active' : 'clear'}</dd>
            <dt>Label</dt>
            <dd className="muted small">{String(control.method_label ?? '—')}</dd>
          </dl>
        </section>
      </div>

      <section className="panel" data-testid="settings-misc-panel">
        <h2>Recording · diagnostics · synthetic</h2>
        <dl className="kv compact">
          <dt>Recording</dt>
          <dd className="mono">
            {snap?.recording?.active
              ? JSON.stringify(snap.recording.active).slice(0, 100)
              : 'none active'}
          </dd>
          <dt>Diag episodes</dt>
          <dd className="mono">{snap?.diagnostics?.episode_count ?? 0}</dd>
          <dt>Synthetic peers</dt>
          <dd className="mono">
            {Array.isArray(snap?.synthetic_peers) && snap.synthetic_peers.length
              ? snap.synthetic_peers
                  .map((p) => (typeof p === 'string' ? p : JSON.stringify(p)))
                  .join(', ')
              : 'none'}
          </dd>
        </dl>
        <div className="actions tight" style={{ marginTop: 8 }}>
          <Button
            variant="secondary"
            disabled={busy}
            data-testid="btn-refresh-settings"
            onClick={() => void refreshSettings().then(() => setLog('Settings refreshed'))}
          >
            Refresh settings
          </Button>
        </div>
      </section>
    </WorkspaceShell>
  )
}
