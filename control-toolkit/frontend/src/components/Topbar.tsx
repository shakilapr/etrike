import { useMemo, useState } from 'react'
import { api } from '../api'
import { linkLabelFromStatus } from '../lib/session'
import { buildEcuPresence } from '../lib/ecuPresence'
import {
  busActivityTone,
  getAuthorityPipeline,
  getControllerModes,
  getVehicleGear,
  getVehiclePower,
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
  IconPower,
  IconRadio,
  IconRotateCcw,
  IconSliders,
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

  async function resetEstop() {
    setModeErr(null)
    try {
      const ses = status?.session
      const benchOn = String(ses?.bench_tx ?? '').toLowerCase() === 'enabled'
      if (benchOn) {
        try {
          const res = await api.rearmEstop()
          setStatus(await api.status())
          const estop = res.estop as { active?: boolean; summary?: string } | undefined
          if (estop?.active) {
            setModeErr(`Bench REARM emitted · remaining: ${estop.summary}`)
          } else {
            setModeErr('ESTOP reset & rearmed successfully (SYS clear frames + power cycle emitted)')
          }
          return
        } catch {
          // Fallback to clearEstop if rearm is unsupported or failed
        }
      }
      const clearRes = await api.clearEstop()
      setStatus(await api.status())
      const remaining = clearRes.estop as { active?: boolean; summary?: string } | undefined
      if (remaining?.active) {
        setModeErr(`Host latch cleared · active on bus/ECUs: ${remaining.summary}`)
      } else {
        setModeErr('Host ESTOP latch cleared')
      }
    } catch (e) {
      setModeErr(String(e).replace(/^Error:\s*/i, '').slice(0, 180))
    }
  }

  const auth = useMemo(() => getAuthorityPipeline(messages), [messages])
  const vehicleGear = useMemo(() => getVehicleGear(messages), [messages])
  const ctrlModes = useMemo(() => getControllerModes(messages), [messages])
  const pwrInfo = useMemo(() => getVehiclePower(messages, ses), [messages, ses])

  const isAuto =
    auth.hmiReqMode === 'AUTO' ||
    ctrlModes.sys === 'AUTO' ||
    ctrlModes.rt === 'AUTO' ||
    ses?.requested_mode === 'AUTO' ||
    ses?.confirmed_mode === 'AUTO'
  const isPowerOn =
    pwrInfo.state === 'ON' ||
    auth.hmiReqStart === 'ON' ||
    ses?.requested_power === 'ON' ||
    ses?.confirmed_power === 'ON'

  async function handleBenchTx() {
    setModeErr(null)
    try {
      const st = await api.status()
      if (!st.session?.session_id) {
        setModeErr('No active session — start one in Settings to enable CAN transmission')
        return
      }
      const nextTx = !benchOn
      await api.setBenchTx(st.session.session_id, nextTx, st.session.revision)
      const fresh = await api.status()
      setStatus(fresh)
      setModeErr(nextTx ? 'Bench TX Armed: host CAN frame transmission active' : 'Bench TX Disarmed: listen-only safe mode')
    } catch (e) {
      setModeErr(String(e).replace(/^Error:\s*/i, '').slice(0, 180))
    }
  }

  async function handleHmiMode() {
    setModeErr(null)
    try {
      const nextMode = isAuto ? 0 : 1
      await api.hmiMode(nextMode, true)
      setStatus(await api.status())
      setModeErr(`High CAN HMI_MODE_REQ (0x111): ${nextMode === 1 ? 'AUTO' : 'MANUAL'} commanded (1 Hz)`)
    } catch (e) {
      setModeErr(String(e).replace(/^Error:\s*/i, '').slice(0, 180))
    }
  }

  async function handleHmiPower() {
    setModeErr(null)
    try {
      const nextPwr = isPowerOn ? 0 : 1
      await api.hmiPower(nextPwr, true)
      setStatus(await api.status())
      setModeErr(`High CAN HMI_PWR_REQ (0x112): ${nextPwr === 1 ? 'ON' : 'OFF'} commanded (1 Hz)`)
    } catch (e) {
      setModeErr(String(e).replace(/^Error:\s*/i, '').slice(0, 180))
    }
  }

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
    ? (/active/i.test(estopLabel) ? estopLabel : `Active · ${estopLabel}`)
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
      <div className="topbar-row topbar-row-primary">
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

        {/* Vehicle Dynamic Telemetry: Gear, Drive Mode of each controller, Power */}
        <div
          className="topbar-vehicle-cluster flex items-center gap-2"
          data-testid="topbar-vehicle-telemetry"
          aria-label="Vehicle Dynamic Telemetry"
        >
          {/* Gear */}
          <div
            className="vehicle-cluster-item vehicle-gear"
            data-testid="chip-gear"
            title={`Vehicle Gear: ${vehicleGear} (physical feedback MTR 0x206 / RT 0x204 / Host 0x300)`}
          >
            <span className="v-label">Gear</span>
            <span className={`v-gear-pill gear-${vehicleGear.toLowerCase()}`}>
              {vehicleGear}
            </span>
          </div>

          <div className="vehicle-cluster-divider" aria-hidden />

          {/* Drive Mode of each controller */}
          <div
            className="vehicle-cluster-item vehicle-modes"
            data-testid="chip-controller-modes"
            title={`Drive Mode of each controller · SYS: ${ctrlModes.sys} · RT: ${ctrlModes.rt} · MTR: ${ctrlModes.mtr}`}
          >
            <span className="v-label">Mode</span>
            <div className="v-controller-tags" aria-label="Controller Modes">
              <span
                className={`v-ctrl-tag ctrl-sys mode-${ctrlModes.sys.toLowerCase()}`}
                title={`SYS Controller Mode: ${ctrlModes.sys} (SYS_MODE_CMD 0x110 / SYS_DIAG_RPT 0x600)`}
              >
                <span className="c-name">SYS</span>
                <span className="c-val">{ctrlModes.sys}</span>
              </span>
              <span
                className={`v-ctrl-tag ctrl-rt mode-${ctrlModes.rt.toLowerCase()}`}
                title={`RT Controller Mode: ${ctrlModes.rt} (RT_STATE_RPT 0x210)`}
              >
                <span className="c-name">RT</span>
                <span className="c-val">{ctrlModes.rt}</span>
              </span>
              <span
                className={`v-ctrl-tag ctrl-mtr mode-${ctrlModes.mtr.toLowerCase()}`}
                title={`MTR Motor Mode: ${ctrlModes.mtr} (MTR_MOTOR_FBK 0x206 / MTR_NODE_STATUS 0x502)`}
              >
                <span className="c-name">MTR</span>
                <span className="c-val">{ctrlModes.mtr}</span>
              </span>
            </div>
          </div>

          <div className="vehicle-cluster-divider" aria-hidden />

          {/* Power */}
          <div
            className="vehicle-cluster-item vehicle-power"
            data-testid="chip-power"
            title={`Drive Power: ${pwrInfo.state} (${pwrInfo.detail})`}
          >
            <span className="v-label">Power</span>
            <span className={`v-power-pill pwr-${pwrInfo.state.toLowerCase()}`}>
              <span className={`status-dot ${pwrInfo.state === 'ON' ? 'live' : 'muted'}`} />
              <span>{pwrInfo.state}</span>
            </span>
          </div>
        </div>

        {/* Action Buttons (Icon-only with standard tooltips & states) */}
        <div className="topbar-actions flex items-center gap-1.5" aria-label="System command actions">
          {/* Bench TX Toggle Button */}
          <button
            type="button"
            className={`btn-topbar-action btn-bench-tx ${benchOn ? 'tx-armed' : 'tx-off'}`}
            data-testid="btn-header-bench-tx"
            aria-label={`Bench TX: ${benchOn ? 'Armed' : 'Off'}`}
            title={`Bench TX: ${benchOn ? 'Armed (Caution: Active CAN Transmission)' : 'Off (Listen-only Safe)'} · Click to ${benchOn ? 'Disarm' : 'Arm'}`}
            onClick={() => void handleBenchTx()}
          >
            <IconRadio />
            <span className="sr-only">Bench TX: {benchOn ? 'Armed' : 'Off'}</span>
          </button>

          {/* High Bus HMI Mode Toggle (AUTO / MANUAL) */}
          <button
            type="button"
            className={`btn-topbar-action btn-hmi-mode ${isAuto ? 'mode-auto' : 'mode-manual'}`}
            data-testid="btn-header-hmi-mode"
            aria-label={`HMI Mode: ${isAuto ? 'AUTO' : 'MANUAL'}`}
            title={`Toggle High-bus HMI_MODE_REQ (0x111) · Current: ${isAuto ? 'AUTO' : 'MANUAL'} · Click to switch to ${isAuto ? 'MANUAL' : 'AUTO'}`}
            onClick={() => void handleHmiMode()}
          >
            <IconSliders />
            <span className="sr-only">{isAuto ? 'Mode: AUTO' : 'Mode: MANUAL'}</span>
          </button>

          {/* High Bus HMI Power Toggle (ON / OFF) */}
          <button
            type="button"
            className={`btn-topbar-action btn-hmi-power ${isPowerOn ? 'pwr-on' : 'pwr-off'}`}
            data-testid="btn-header-hmi-power"
            aria-label={`HMI Power: ${isPowerOn ? 'ON' : 'OFF'}`}
            title={`Send High-bus HMI_PWR_REQ (0x112) · Current: ${isPowerOn ? 'ON' : 'OFF'} · Click to turn ${isPowerOn ? 'OFF' : 'ON'}`}
            onClick={() => void handleHmiPower()}
          >
            <IconPower />
            <span className="sr-only">{isPowerOn ? 'Power: ON' : 'Power OFF'}</span>
          </button>

          <div className="topbar-actions-divider" aria-hidden />

          {/* ESTOP Inject Button */}
          <button
            type="button"
            className="btn-topbar-action btn-estop-icon"
            data-testid="btn-header-estop"
            aria-label="Inject ESTOP"
            title="Inject SAFETY_ESTOP (DLC=0) on High and Low · Latches host ESTOP · Requires TX armed"
            onClick={() => void injectEstop()}
          >
            <IconOctagonAlert />
            <span className="sr-only">Inject ESTOP</span>
          </button>

          {/* ESTOP Reset Button */}
          <button
            type="button"
            className={`btn-topbar-action btn-estop-reset-icon ${estopOn || estopObs.hostLatch ? 'is-active-reset' : ''}`}
            data-testid="btn-header-estop-reset"
            aria-label="Reset ESTOP"
            title="Reset ESTOP latch and rearm vehicle safety path (sends 0x011 clear & power cycle when Bench TX armed)"
            onClick={() => void resetEstop()}
          >
            <IconRotateCcw />
            <span className="sr-only">Reset ESTOP</span>
          </button>
        </div>
      </div>

      {/* Secondary context — session meta left, inline action notice, ECU presence right */}
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

        <div className="meta-group" data-testid="chip-record" title="CAN telemetry recording state">
          <span className="meta-k">Rec</span>
          <span className={`meta-v ${ses?.recording ? 'ok-text font-bold' : ''}`}>
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

        {/* Action notice / error rendered cleanly inline in second topbar */}
        {modeErr && (
          <div
            className={`meta-action-notice ${
              modeErr.toLowerCase().includes('error') ||
              modeErr.toLowerCase().includes('failed') ||
              modeErr.toLowerCase().includes('lost')
                ? 'is-error'
                : 'is-info'
            }`}
            role="status"
            data-testid="topbar-action-error"
          >
            <span className="notice-text">{modeErr}</span>
            <button
              type="button"
              className="notice-close"
              onClick={() => setModeErr(null)}
              aria-label="Dismiss message"
              title="Dismiss message"
            >
              ×
            </button>
          </div>
        )}

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
