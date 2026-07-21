import { useState } from 'react'
import { api } from '../api'
import { activateTransportProfile } from '../lib/session'
import { busActivityTone, dash, PROFILE_LABELS, transportModeOf, type OverallHealth } from '../lib/signals'
import { useAppStore } from '../store'
import { IconCable, IconMonitor } from './icons'

export function Topbar() {
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const quality = useAppStore((s) => s.streamQuality)
  const mismatch = useAppStore((s) => s.protocolMismatch)
  const reconnect = useAppStore((s) => s.reconnectAttempts)
  const [modeBusy, setModeBusy] = useState(false)
  const [modeErr, setModeErr] = useState<string | null>(null)
  const ses = status?.session
  const high = status?.adapter?.channels?.high
  const low = status?.adapter?.channels?.low
  const profileId = ses?.profile ?? status?.profile ?? '—'
  const profileLabel = PROFILE_LABELS[profileId] ?? profileId
  const mode = transportModeOf(profileId)
  const dest = ses?.destination ?? (mode === 'real' ? 'physical' : 'virtual')
  const adapterHealth = (status?.adapter?.health || '—').toLowerCase()
  const benchOn = (ses?.bench_tx || '').toLowerCase() === 'enabled'
  const estopOn = !!ses?.estop_active

  async function injectEstop() {
    setModeErr(null)
    try {
      let st = await api.status()
      if (transportModeOf(st.session?.profile ?? st.profile) === 'computer') {
        if (!st.session?.session_id) {
          await api.createSession('pure_software')
          st = await api.status()
        }
        if (st.session.bench_tx !== 'enabled') {
          await api.setBenchTx(st.session.session_id!, true, st.session.revision)
        }
      } else if (st.session?.bench_tx !== 'enabled') {
        throw new Error('Physical TX is disabled. Enable Bench TX before injecting ESTOP on Real buses.')
      }
      await api.injectEstop()
      setStatus(await api.status())
    } catch (e) {
      setModeErr(String(e).replace(/^Error:\s*/i, '').slice(0, 180))
    }
  }

  /** Same session restart path as Settings Computer / Real. */
  async function switchTransportMode(next: 'computer' | 'real') {
    if (modeBusy) return
    if (next === mode) return
    setModeBusy(true)
    setModeErr(null)
    try {
      const profile = next === 'computer' ? 'pure_software' : 'bench_test'
      setStatus(await activateTransportProfile(profile))
    } catch (e) {
      setModeErr(String(e).replace(/^Error:\s*/i, '').slice(0, 120))
      try {
        setStatus(await api.status())
      } catch {
        /* keep last */
      }
    } finally {
      setModeBusy(false)
    }
  }

  const streamText =
    quality === 'live'
      ? 'Live'
      : quality === 'delayed'
        ? 'Delayed'
        : quality === 'dropping'
          ? 'Dropping'
          : quality === 'lost'
            ? 'Lost'
            : 'Connecting'

  // Fault = safety/protocol problem. Offline = no backend/API.
  // If HTTP status is ready but WS is reconnecting, prefer Degraded not Offline.
  const overall: OverallHealth = (() => {
    if (estopOn || mismatch) return 'fault'
    if (adapterHealth === 'failed' || adapterHealth === 'error') return 'fault'
    const apiUp = !!status?.ready
    if (!apiUp && (quality === 'lost' || quality === 'connecting' || !status)) {
      return 'offline'
    }
    if (quality === 'lost' && !apiUp) return 'offline'
    if (quality === 'lost' && apiUp) return 'degraded' // HTTP ok, stream reconnecting
    if (
      quality === 'delayed' ||
      quality === 'dropping' ||
      quality === 'connecting' ||
      adapterHealth === 'absent' ||
      adapterHealth === 'degraded'
    ) {
      return 'degraded'
    }
    if (
      quality === 'live' &&
      (adapterHealth === 'open' ||
        adapterHealth === 'ok' ||
        adapterHealth === 'healthy' ||
        adapterHealth === 'active' ||
        apiUp)
    ) {
      return 'healthy'
    }
    return apiUp ? 'degraded' : 'offline'
  })()

  const overallLabel =
    overall === 'healthy'
      ? 'Healthy'
      : overall === 'fault'
        ? 'Fault'
        : overall === 'offline'
          ? 'Offline'
          : 'Degraded'

  const highTone = busActivityTone(high?.activity)
  const lowTone = busActivityTone(low?.activity)

  return (
    <header className="topbar z-30 shrink-0 border-b border-border bg-surface" data-testid="topbar">
      {/* Primary health strip */}
      <div className="topbar-row topbar-row-primary flex flex-wrap items-center gap-x-2.5 gap-y-2 px-3.5 py-2">
        <div className="topbar-cluster topbar-brand-cluster">
          <div className="brand">Control Toolkit</div>
          <div
            className="topbar-mode-toggle"
            data-testid="topbar-mode-toggle"
            role="group"
            aria-label="Transport mode"
            title={
              modeErr
                ? modeErr
                : mode === 'real'
                  ? 'Real · physical CANalyst-II (CH0 High / CH1 Low)'
                  : 'Computer · dual virtual CAN on this PC'
            }
          >
            <button
              type="button"
              className={`topbar-mode-btn mode-computer${mode === 'computer' ? ' active' : ''}`}
              data-testid="topbar-mode-computer"
              aria-pressed={mode === 'computer'}
              disabled={modeBusy}
              onClick={() => void switchTransportMode('computer')}
            >
              <IconMonitor />
              <span>Computer</span>
            </button>
            <button
              type="button"
              className={`topbar-mode-btn mode-real${mode === 'real' ? ' active' : ''}`}
              data-testid="topbar-mode-real"
              aria-pressed={mode === 'real'}
              disabled={modeBusy}
              onClick={() => void switchTransportMode('real')}
            >
              <IconCable />
              <span>Real</span>
            </button>
          </div>
        </div>

        <div className="health-strip" data-testid="health-strip" aria-label="System health">
          <div
            className={`health-overall tone-${overall}`}
            data-testid="chip-health-overall"
            title="Combined stream, adapter, ESTOP, and protocol health"
          >
            <span className={`status-dot ${overall === 'healthy' ? 'live' : overall === 'fault' ? 'danger' : overall === 'offline' ? 'muted' : 'warning'}`} />
            <span className="health-overall-label">{overallLabel}</span>
          </div>

          <div className="health-divider" aria-hidden />

          <div className={`chip quality-${quality} health-chip`} data-testid="chip-stream">
            <span className="chip-k">Stream</span>
            <span className="chip-v">
              {streamText}
              {reconnect > 0 ? ` · r${reconnect}` : ''}
            </span>
          </div>

          <div
            className={`chip ${estopOn ? 'danger' : 'ok'} health-chip`}
            data-testid="chip-estop"
          >
            <span className="chip-k">ESTOP</span>
            <span className="chip-v">{estopOn ? 'Active' : 'Clear'}</span>
          </div>

          <div
            className={`chip health-chip ${benchOn ? 'ok' : ''}`}
            data-testid="chip-bench-tx"
            title="Bench TX must be enabled before inject / control"
          >
            <span className="chip-k">TX</span>
            <span className="chip-v">{benchOn ? 'Armed' : 'Off'}</span>
          </div>

          {mismatch && (
            <div className="chip danger health-chip" data-testid="chip-mismatch">
              <span className="chip-k">Protocol</span>
              <span className="chip-v">Mismatch</span>
            </div>
          )}

          <div className="health-divider" aria-hidden />

          {/* Discrete bus state — not a continuous meter (activity is not pressure). */}
          <div
            className={`chip bus-chip tone-${highTone}`}
            data-testid="chip-high"
            title={`High bus · activity ${high?.activity ?? '—'} · rx ${high?.rx_count ?? 0}`}
          >
            <span className={`status-dot ${highTone === 'ok' ? 'live' : highTone === 'warn' ? 'warning' : highTone === 'danger' ? 'danger' : 'muted'}`} />
            <span className="chip-k">High</span>
            <span className="chip-v mono">
              {high?.activity ?? '—'}
              <span className="bus-rx"> · {high?.rx_count ?? 0}</span>
            </span>
          </div>

          <div
            className={`chip bus-chip tone-${lowTone}`}
            data-testid="chip-low"
            title={`Low bus · activity ${low?.activity ?? '—'} · rx ${low?.rx_count ?? 0}`}
          >
            <span className={`status-dot ${lowTone === 'ok' ? 'live' : lowTone === 'warn' ? 'warning' : lowTone === 'danger' ? 'danger' : 'muted'}`} />
            <span className="chip-k">Low</span>
            <span className="chip-v mono">
              {low?.activity ?? '—'}
              <span className="bus-rx"> · {low?.rx_count ?? 0}</span>
            </span>
          </div>
        </div>

        <button
          type="button"
          className="btn-estop"
          data-testid="btn-header-estop"
          title="Inject SAFETY_ESTOP (DLC=0) on high and low — requires Bench TX"
          onClick={() => void injectEstop()}
        >
          Inject ESTOP
        </button>
      </div>

      {modeErr && (
        <div className="topbar-action-error" role="status" data-testid="topbar-action-error">
          {modeErr}
        </div>
      )}

      {/* Secondary context — denser, lower priority */}
      <div className="topbar-row topbar-row-meta" data-testid="topbar-row-session">
        <div className="meta-group" data-testid="chip-profile" title="Operating profile / destination">
          <span className="meta-k">Profile</span>
          <span className="meta-v">{profileLabel}</span>
          <span className="meta-sep">·</span>
          <span className="meta-v muted" data-testid="chip-destination">
            {dest}
          </span>
        </div>

        <div className="meta-group" data-testid="chip-phase" title="Session phase and id">
          <span className="meta-k">Session</span>
          <span className="meta-v">
            {ses?.phase ?? 'stopped'}
            {ses?.session_id ? (
              <span className="mono muted"> · {ses.session_id.slice(0, 10)}</span>
            ) : null}
          </span>
        </div>

        <div
          className="meta-group"
          data-testid="chip-adapter"
          title={status?.adapter?.identity || 'Adapter health'}
        >
          <span className="meta-k">Adapter</span>
          <span
            className={`meta-v ${
              adapterHealth === 'open' || adapterHealth === 'ok' || adapterHealth === 'healthy'
                ? 'ok-text'
                : adapterHealth === 'absent' || adapterHealth === 'failed'
                  ? 'danger-text'
                  : ''
            }`}
          >
            {status?.adapter?.health ?? '—'}
          </span>
        </div>

        <div className="meta-group" data-testid="chip-mode" title="Requested vs confirmed vehicle mode">
          <span className="meta-k">Mode</span>
          <span className="meta-v mono">
            {dash(ses?.requested_mode)}→{dash(ses?.confirmed_mode)}
          </span>
        </div>

        <div className="meta-group" data-testid="chip-power" title="Requested vs confirmed power">
          <span className="meta-k">Power</span>
          <span className="meta-v mono">
            {dash(ses?.requested_power)}→{dash(ses?.confirmed_power)}
          </span>
        </div>

        <div className="meta-group" data-testid="chip-record">
          <span className="meta-k">Rec</span>
          <span className={`meta-v ${ses?.recording ? 'ok-text' : ''}`}>
            {ses?.recording ? 'On' : 'Off'}
          </span>
        </div>

        <div
          className="meta-group mono muted meta-hash"
          data-testid="chip-hash"
          title={status?.wire_hash ?? ''}
        >
          <span className="meta-k">Wire</span>
          <span className="meta-v">{(status?.wire_hash ?? '').slice(0, 10) || '—'}…</span>
        </div>
      </div>
    </header>
  )
}
