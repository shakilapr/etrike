import { useMemo, useState } from 'react'
import { api } from '../api'
import { linkLabelFromStatus } from '../lib/session'
import { buildEcuPresence } from '../lib/ecuPresence'
import {
  busActivityTone,
  observeEstop,
  PROFILE_LABELS,
  transportModeOf,
  type OverallHealth,
} from '../lib/signals'
import { useAppStore } from '../store'
import {
  IconCable,
  IconCpu,
  IconNetwork,
  IconOctagonAlert,
  IconRadio,
} from './icons'

/** green=clean live · yellow=late or live+errors · red=dead · muted=unknown */
function ecuDotTone(liveness: string): 'live' | 'warning' | 'danger' | 'muted' {
  const k = liveness.toLowerCase()
  if (k === 'live') return 'live'
  // late / degraded (live with faults) / recovering → yellow
  if (k === 'late' || k === 'degraded' || k === 'recovering' || k === 'warn') return 'warning'
  if (k === 'fault' || k === 'offline' || k === 'missing') return 'danger'
  return 'muted'
}

function ecuConnectedLabel(liveness: string): string {
  const k = liveness.toLowerCase()
  if (k === 'live') return 'connected'
  if (k === 'late') return 'late'
  if (k === 'degraded') return 'live · error'
  if (k === 'fault') return 'fault'
  if (k === 'missing') return 'missing'
  if (k === 'offline') return 'offline'
  return liveness || 'unknown'
}

export function Topbar() {
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  const quality = useAppStore((s) => s.streamQuality)
  const mismatch = useAppStore((s) => s.protocolMismatch)
  const reconnect = useAppStore((s) => s.reconnectAttempts)
  const topology = useAppStore((s) => s.topology)
  const messages = useAppStore((s) => s.messages)
  const [modeErr, setModeErr] = useState<string | null>(null)
  const [estopHover, setEstopHover] = useState(false)
  const ses = status?.session
  const high = status?.adapter?.channels?.high
  const low = status?.adapter?.channels?.low
  const profileId = ses?.profile ?? status?.profile ?? '—'
  const profileLabel = PROFILE_LABELS[profileId] ?? profileId
  const mode = transportModeOf(profileId)
  const dest = ses?.destination ?? (mode === 'real' ? 'physical' : 'virtual')
  const adapterHealth = (status?.adapter?.health || '—').toLowerCase()
  const benchOn = (ses?.bench_tx || '').toLowerCase() === 'enabled'
  const estopObs = useMemo(() => observeEstop(messages, ses), [messages, ses])
  const backendEstop = status?.estop
  // Prefer fresh backend structured estop report when present and active
  const estopOn = backendEstop?.active != null ? backendEstop.active : estopObs.any
  const estopLabel = (backendEstop?.active && backendEstop.primary_cause)
    ? backendEstop.primary_cause
    : (backendEstop?.active && backendEstop.summary)
      ? backendEstop.summary
      : estopObs.label
  const estopDetail = backendEstop?.summary || estopObs.detail
  const link = linkLabelFromStatus(status)

  // Always show full unit set (incl. SBW/BBW). Prefer live CAN; topology as fallback.
  const ecuNodes = useMemo(
    () => buildEcuPresence(topology, messages),
    [topology, messages],
  )

  async function injectEstop() {
    setModeErr(null)
    try {
      let st = await api.status()
      if (!st.session?.session_id) {
        throw new Error('No active session. Connect Real in Settings first.')
      }
      if (st.session.bench_tx !== 'enabled') {
        throw new Error(
          'Physical TX is off. Enable Bench TX after the adapter is Connected before injecting ESTOP.',
        )
      }
      const result = await api.injectEstop()
      setStatus(await api.status())
      // Surface dual-bus inject result in the topbar strip (not console-only).
      const estop = (result as { estop?: Array<{ bus?: string; disposition?: string }> }).estop
      if (Array.isArray(estop) && estop.length) {
        const bits = estop.map((e) => `${e.bus ?? '?'}:${e.disposition ?? 'ok'}`).join(' · ')
        setModeErr(`ESTOP injected · ${bits} · host latch ON`)
      }
    } catch (e) {
      setModeErr(String(e).replace(/^Error:\s*/i, '').slice(0, 180))
    }
  }

  async function clearEstop() {
    setModeErr(null)
    try {
      // Only clears host inject latch — not ECU-latched ESTOP on the bus.
      await api.clearEstop()
      setStatus(await api.status())
      setModeErr('Host ESTOP latch cleared (bus/SYS/RT may still report ESTOP)')
    } catch (e) {
      setModeErr(String(e).replace(/^Error:\s*/i, '').slice(0, 180))
    }
  }


  // Fault = safety/protocol problem. Offline = no backend/API.
  // Real + no adapter is Degraded (mode is intentional), not Offline.
  // ESTOP uses multi-source observeEstop (latch + bus 0x001 + SYS/RT), not latch alone.
  const overall: OverallHealth = (() => {
    if (estopOn || mismatch) return 'fault'
    if (adapterHealth === 'failed' || adapterHealth === 'error') return 'fault'
    const apiUp = !!status?.ready
    if (!apiUp && (quality === 'lost' || quality === 'connecting' || !status)) {
      return 'offline'
    }
    if (quality === 'lost' && !apiUp) return 'offline'
    if (quality === 'lost' && apiUp) return 'degraded'
    if (mode === 'real' && link.tone === 'danger') return 'degraded'
    if (
      quality === 'delayed' ||
      quality === 'dropping' ||
      quality === 'connecting' ||
      adapterHealth === 'absent' ||
      adapterHealth === 'degraded'
    ) {
      return 'degraded'
    }
    // Bus chips use channel activity (active/quiet/unseen). Overall health must
    // not claim "Healthy" solely because Computer mode + API are up — quiet
    // virtual buses after SIL stop are degraded-or-healthy only if adapter open.
    if (quality === 'live' && apiUp) {
      if (
        adapterHealth === 'active' ||
        adapterHealth === 'ok' ||
        adapterHealth === 'healthy'
      ) {
        return 'healthy'
      }
      // open/quiet virtual bus with live stream: still usable, not "all green"
      if (adapterHealth === 'open' || adapterHealth === 'quiet') {
        const highAct = (high?.activity || '').toLowerCase()
        const lowAct = (low?.activity || '').toLowerCase()
        const anyBusActive =
          highAct === 'active' ||
          highAct === 'rx' ||
          highAct === 'tx' ||
          highAct === 'live' ||
          lowAct === 'active' ||
          lowAct === 'rx' ||
          lowAct === 'tx' ||
          lowAct === 'live'
        return anyBusActive ? 'healthy' : 'degraded'
      }
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

  const isLinkConnected = Boolean(status?.link?.connected)
  const hasLiveTraffic = messages.length > 0 && isLinkConnected
  const estopConfirmed = hasLiveTraffic && !estopOn
  const estopTone: 'danger' | 'ok' | 'muted' = estopOn
    ? 'danger'
    : estopConfirmed
      ? 'ok'
      : 'muted'
  const estopDisplayLabel = estopOn
    ? estopLabel
    : estopConfirmed
      ? 'Clear'
      : 'No signal'

  const streamText =
    quality === 'live'
      ? 'Active · Live'
      : quality === 'delayed'
        ? 'Delayed'
        : quality === 'dropping'
          ? 'Dropping'
          : quality === 'lost'
            ? 'Lost'
            : 'Connecting'

  const highTone = busActivityTone(high?.activity)
  const lowTone = busActivityTone(low?.activity)
  const highDisplayTone = isLinkConnected ? highTone : 'muted'
  const lowDisplayTone = isLinkConnected ? lowTone : 'muted'

  const streamDotTone: 'live' | 'warning' | 'danger' =
    quality === 'live'
      ? 'live'
      : quality === 'delayed' || quality === 'dropping' || quality === 'connecting'
        ? 'warning'
        : 'danger'

  const linkDotTone: 'live' | 'warning' | 'danger' | 'muted' =
    link.tone === 'ok' ? 'live' : link.tone === 'warn' ? 'warning' : 'muted'

  const estopDotTone: 'danger' | 'live' | 'muted' =
    estopTone === 'danger' ? 'danger' : estopTone === 'ok' ? 'live' : 'muted'

  const benchDotTone: 'warning' | 'muted' = benchOn ? 'warning' : 'muted'

  const highDotTone: 'live' | 'warning' | 'danger' | 'muted' =
    highDisplayTone === 'ok' ? 'live' : highDisplayTone === 'warn' ? 'warning' : highDisplayTone === 'danger' ? 'danger' : 'muted'

  const lowDotTone: 'live' | 'warning' | 'danger' | 'muted' =
    lowDisplayTone === 'ok' ? 'live' : lowDisplayTone === 'warn' ? 'warning' : lowDisplayTone === 'danger' ? 'danger' : 'muted'

  return (
    <header className="topbar z-30 shrink-0 border-b border-border bg-surface" data-testid="topbar">
      {/* Primary health strip */}
      <div className="topbar-row topbar-row-primary flex flex-wrap items-center gap-x-2.5 gap-y-2 px-3.5 py-2">
        <div className="topbar-cluster topbar-brand-cluster">
          <div className="brand">Control Toolkit</div>
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

          {/* Backend */}
          <div
            className={`chip quality-${quality} health-chip`}
            data-testid="chip-stream"
            title={`Backend: ${streamText}${reconnect > 0 ? ` · r${reconnect}` : ''} (Python backend API & event stream)`}
          >
            <span className="chip-symbol" title="Host Backend"><IconCpu /></span>
            <span className="chip-k">Backend</span>
            <span className={`status-dot ${streamDotTone}`} />
            <span className="sr-only">{streamText}</span>
          </div>

          {/* CANalyst Hardware */}
          <div
            className={`chip health-chip ${
              link.tone === 'ok' ? 'ok' : link.tone === 'warn' ? 'warning' : 'muted'
            }`}
            data-testid="chip-link"
            title={`CANalyst-II: ${link.label} (${link.detail})`}
          >
            <span className="chip-symbol" title="CANalyst-II Interface"><IconCable /></span>
            <span className="chip-k">CANalyst</span>
            <span className={`status-dot ${linkDotTone}`} />
            <span className="sr-only">{link.label}</span>
          </div>

          {/* ESTOP Telltale */}
          <div
            className={`chip ${estopTone === 'danger' ? 'danger' : estopTone === 'ok' ? 'ok' : 'muted'} health-chip chip-estop`}
            data-testid="chip-estop"
            role="button"
            tabIndex={0}
            onClick={() => setWorkspace('diagnostics')}
            onKeyDown={(e) => {
              if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault()
                setWorkspace('diagnostics')
              }
            }}
            onMouseEnter={() => setEstopHover(true)}
            onMouseLeave={() => setEstopHover(false)}
            onFocus={() => setEstopHover(true)}
            onBlur={() => setEstopHover(false)}
            aria-label={`ESTOP: ${estopDisplayLabel}. Click to open diagnostics.`}
            title={`ESTOP: ${estopDisplayLabel} (${estopDetail})`}
          >
            <span className="chip-symbol" title="Emergency Stop Telltale"><IconOctagonAlert /></span>
            <span className="chip-k">ESTOP</span>
            <span className={`status-dot ${estopDotTone}`} />
            <span className="sr-only" data-testid="chip-estop-label">
              {estopDisplayLabel}
            </span>
            {/* Bus presence of 0x001 — separate from host latch label */}
            <span className="estop-bus-lamps" aria-label="SAFETY_ESTOP bus presence">
              <span
                className={`estop-bus-lamp ${estopObs.busHigh ? 'on' : 'off'}`}
                title={estopObs.busHigh ? '0x001 recent on High' : 'No recent 0x001 on High'}
              >
                H
              </span>
              <span
                className={`estop-bus-lamp ${estopObs.busLow ? 'on' : 'off'}`}
                title={estopObs.busLow ? '0x001 recent on Low' : 'No recent 0x001 on Low'}
              >
                L
              </span>
            </span>

            {/* Non-obstructive downward popover */}
            {estopHover && (
              <div
                className="topbar-estop-popover"
                data-testid="topbar-estop-popover"
                onClick={(e) => {
                  e.stopPropagation()
                  setWorkspace('diagnostics')
                }}
              >
                <div className={`topbar-estop-popover-title ${estopTone === 'danger' ? 'danger-text' : estopTone === 'ok' ? 'ok-text' : 'muted-text'}`}>
                  {estopOn ? 'Safety Stop Active' : estopConfirmed ? 'Safety Systems Clear' : 'Safety State Unconfirmed'}
                </div>
                <div className="topbar-estop-popover-body">
                  {estopDetail || (estopOn ? 'Safety stop active across monitored buses.' : estopConfirmed ? 'All monitored safety lines clear.' : 'No bus telemetry — safety state unconfirmed.')}
                </div>
                <div className="topbar-estop-popover-hint">
                  <span>Click to inspect root cause in Diagnostics →</span>
                </div>
              </div>
            )}
          </div>

          {/* Bench TX */}
          <div
            className={`chip health-chip ${benchOn ? 'warning' : 'muted'}`}
            data-testid="chip-bench-tx"
            title={`Bench TX: ${benchOn ? 'Armed (Caution: Active CAN Transmission)' : 'Off (Listen-only Safe)'}`}
          >
            <span className="chip-symbol" title="Bench TX Broadcast Gate"><IconRadio /></span>
            <span className="chip-k">TX</span>
            <span className={`status-dot ${benchDotTone}`} />
            <span className="sr-only">{benchOn ? 'Armed' : 'Off'}</span>
          </div>

          {mismatch && (
            <div className="chip danger health-chip" data-testid="chip-mismatch">
              <span className="chip-k">Protocol</span>
              <span className="chip-v">Mismatch</span>
            </div>
          )}

          <div className="health-divider" aria-hidden />

          {/* High Bus */}
          <div
            className={`chip bus-chip tone-${highDisplayTone}`}
            data-testid="chip-high"
            title={`High bus (CH0) · ${isLinkConnected ? `activity ${high?.activity ?? '—'} · rx ${high?.rx_count ?? 0}` : 'Offline — CANalyst not connected'}`}
          >
            <span className="chip-symbol" title="High CAN Bus (CH0)"><IconNetwork /></span>
            <span className="chip-k">High</span>
            <span className={`status-dot ${highDotTone}`} />
            {isLinkConnected && (
              <span className="bus-rx"> · {high?.rx_count ?? 0}</span>
            )}
            <span className="sr-only">{isLinkConnected ? high?.activity : 'Offline'}</span>
          </div>

          {/* Low Bus */}
          <div
            className={`chip bus-chip tone-${lowDisplayTone}`}
            data-testid="chip-low"
            title={`Low bus (CH1) · ${isLinkConnected ? `activity ${low?.activity ?? '—'} · rx ${low?.rx_count ?? 0}` : 'Offline — CANalyst not connected'}`}
          >
            <span className="chip-symbol" title="Low CAN Bus (CH1)"><IconNetwork /></span>
            <span className="chip-k">Low</span>
            <span className={`status-dot ${lowDotTone}`} />
            {isLinkConnected && (
              <span className="bus-rx"> · {low?.rx_count ?? 0}</span>
            )}
            <span className="sr-only">{isLinkConnected ? low?.activity : 'Offline'}</span>
          </div>
        </div>

        <div className="topbar-estop-actions flex items-center gap-1.5">
          <button
            type="button"
            className="btn-estop"
            data-testid="btn-header-estop"
            title="Inject SAFETY_ESTOP (DLC=0) on High and Low · latches host ESTOP · requires TX armed"
            onClick={() => void injectEstop()}
          >
            Inject ESTOP
          </button>
          {/* Clear only applies to host inject latch — not bus/SYS/RT vehicle ESTOP. */}
          {estopObs.hostLatch ? (
            <button
              type="button"
              className="btn secondary"
              data-testid="btn-header-estop-clear"
              title="Clear host inject latch only. Does not clear ECU-latched ESTOP on the bus."
              onClick={() => void clearEstop()}
            >
              Clear latch
            </button>
          ) : null}
        </div>
      </div>

      {modeErr && (
        <div className="topbar-action-error" role="status" data-testid="topbar-action-error">
          {modeErr}
        </div>
      )}

      {/* Secondary context — session meta left, ECU presence right */}
      <div className="topbar-row topbar-row-meta" data-testid="topbar-row-session">
        <div className="meta-group" data-testid="chip-profile" title="Operating profile / destination">
          <span className="meta-k">Profile</span>
          <span className="meta-v">{profileLabel}</span>
          <span className="meta-sep">·</span>
          <span className="meta-v muted" data-testid="chip-destination">
            {dest}
          </span>
        </div>

        <div
          className="topbar-mode-toggle"
          data-testid="topbar-mode-toggle"
          role="group"
          aria-label="Transport mode"
          title="Real · physical CANalyst-II (CH0 High / CH1 Low) USB device"
        >
          <button
            type="button"
            className="topbar-mode-btn mode-real active"
            data-testid="topbar-mode-real"
            title="Real · physical CANalyst-II (CH0 High / CH1 Low)"
          >
            <IconCable />
            <span>Real</span>
          </button>
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
            {ses?.confirmed_mode
              ? (ses.requested_mode && ses.requested_mode !== ses.confirmed_mode
                  ? `${ses.requested_mode} → ${ses.confirmed_mode}`
                  : ses.confirmed_mode)
              : (ses?.requested_mode || 'Standby')}
          </span>
        </div>

        <div className="meta-group" data-testid="chip-power" title="Requested vs confirmed power">
          <span className="meta-k">Power</span>
          <span className="meta-v mono">
            {ses?.confirmed_power
              ? (ses.requested_power && ses.requested_power !== ses.confirmed_power
                  ? `${ses.requested_power} → ${ses.confirmed_power}`
                  : ses.confirmed_power)
              : (ses?.requested_power || 'Off')}
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

        <div
          className="ecu-rail"
          data-testid="ecu-strip"
          aria-label="ECU connection from CAN"
        >
          {ecuNodes.map((n) => {
            const tone = ecuDotTone(n.liveness)
            const state = ecuConnectedLabel(n.liveness)
            const issueHint =
              n.issues?.length ? ` · ${n.issues.slice(0, 3).join('; ')}` : ''
            return (
              <div
                key={`${n.bus}-${n.node}`}
                className={`ecu-cell tone-${tone}`}
                data-testid={`ecu-lamp-${n.node}`}
                data-liveness={n.liveness}
                title={n.title || `${n.node} · ${state}${issueHint}`}
              >
                <span className="ecu-cell-name">{n.short}</span>
                <span className={`ecu-led ${tone}`} aria-hidden />
              </div>
            )
          })}
        </div>
      </div>
    </header>
  )
}
