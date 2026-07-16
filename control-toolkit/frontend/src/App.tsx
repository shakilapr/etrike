import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from 'react'
import { useAppStore, type MessageState, type TopologyNode, type Workspace } from './store'
import { useBackendStream } from './useStream'
import { api, type ProfileInfo } from './api'
import { VehiclePreview } from './VehiclePreview'
import { CanDictionary } from './CanDictionary'
import './App.css'

/**
 * Nav rail icons — standard Lucide/Feather stroke set only (no emoji, no custom glyphs).
 * Paths match common open-source icon packs used in engineering UIs.
 */
type NavItem = {
  id: Workspace
  label: string
  icon: ReactNode
}

type NavSection = { label: string; items: NavItem[] }

function NavIcon({ children }: { children: ReactNode }) {
  return (
    <svg
      viewBox="0 0 24 24"
      width="16"
      height="16"
      stroke="currentColor"
      strokeWidth="2"
      fill="none"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
    >
      {children}
    </svg>
  )
}

/** Lucide layout-grid */
function IconLayoutGrid() {
  return (
    <NavIcon>
      <rect x="3" y="3" width="7" height="7" rx="1" />
      <rect x="14" y="3" width="7" height="7" rx="1" />
      <rect x="3" y="14" width="7" height="7" rx="1" />
      <rect x="14" y="14" width="7" height="7" rx="1" />
    </NavIcon>
  )
}

/** Lucide share-2 */
function IconShare2() {
  return (
    <NavIcon>
      <circle cx="18" cy="5" r="3" />
      <circle cx="6" cy="12" r="3" />
      <circle cx="18" cy="19" r="3" />
      <path d="M8.59 13.51 15.42 17.49" />
      <path d="M15.41 6.51 8.59 10.49" />
    </NavIcon>
  )
}

/** Lucide activity */
function IconActivity() {
  return (
    <NavIcon>
      <path d="M22 12h-4l-3 9L9 3l-3 9H2" />
    </NavIcon>
  )
}

/** Lucide sliders-horizontal */
function IconSliders() {
  return (
    <NavIcon>
      <path d="M10 5H3" />
      <path d="M21 5h-7" />
      <path d="M14 19H3" />
      <path d="M21 19h-3" />
      <path d="M12 12H3" />
      <path d="M21 12h-5" />
      <circle cx="12" cy="5" r="2" />
      <circle cx="18" cy="12" r="2" />
      <circle cx="16" cy="19" r="2" />
    </NavIcon>
  )
}

/** Lucide gauge */
function IconGauge() {
  return (
    <NavIcon>
      <path d="m12 14 4-4" />
      <path d="M3.34 19a10 10 0 1 1 17.32 0" />
    </NavIcon>
  )
}

/** Lucide terminal */
function IconTerminal() {
  return (
    <NavIcon>
      <path d="M4 17 10 11 4 5" />
      <path d="M12 19h8" />
    </NavIcon>
  )
}

/** Lucide book */
function IconBook() {
  return (
    <NavIcon>
      <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20" />
      <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z" />
    </NavIcon>
  )
}

/** Lucide search */
function IconSearch() {
  return (
    <NavIcon>
      <circle cx="11" cy="11" r="8" />
      <path d="m21 21-4.3-4.3" />
    </NavIcon>
  )
}

/** Lucide scroll-text (logs) */
function IconLogs() {
  return (
    <NavIcon>
      <path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01" />
    </NavIcon>
  )
}

/** Lucide settings (gear) */
function IconSettings() {
  return (
    <NavIcon>
      <path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z" />
      <circle cx="12" cy="12" r="3" />
    </NavIcon>
  )
}

/** Lucide external-link */
function IconExternalLink() {
  return (
    <NavIcon>
      <path d="M15 3h6v6" />
      <path d="M10 14 21 3" />
      <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6" />
    </NavIcon>
  )
}

const NAV_SECTIONS: NavSection[] = [
  {
    label: 'Observe',
    items: [
      { id: 'overview', label: 'Overview', icon: <IconLayoutGrid /> },
      { id: 'network', label: 'Network', icon: <IconShare2 /> },
      { id: 'live', label: 'Live CAN', icon: <IconActivity /> },
    ],
  },
  {
    label: 'Operate',
    items: [
      { id: 'control', label: 'Control', icon: <IconSliders /> },
      { id: 'preview', label: 'Drive', icon: <IconGauge /> },
    ],
  },
  {
    label: 'Analysis',
    items: [
      { id: 'bench', label: 'Bench', icon: <IconTerminal /> },
      { id: 'dictionary', label: 'Dictionary', icon: <IconBook /> },
      { id: 'diagnostics', label: 'Diagnostics', icon: <IconSearch /> },
      { id: 'logs', label: 'Logging', icon: <IconLogs /> },
    ],
  },
  {
    label: 'System',
    items: [{ id: 'settings', label: 'Settings', icon: <IconSettings /> }],
  },
]

const PROFILE_LABELS: Record<string, string> = {
  pure_software: 'Pure Software',
  bench_test: 'Bench Test',
  full_vehicle: 'Full Vehicle',
}

function FreshnessBadge({ value }: { value: string }) {
  return (
    <span className={`fresh fresh-${value.toLowerCase()}`} data-testid="freshness">
      {value}
    </span>
  )
}

function LivenessBadge({ value }: { value: string }) {
  return (
    <span className={`live-badge live-${value.toLowerCase()}`}>{value}</span>
  )
}

function signalText(m: MessageState | undefined, key: string): string {
  if (!m?.signals?.[key]) return '—'
  const s = m.signals[key]
  return String(s.enum_label ?? s.engineering_value ?? '—')
}

function signalNum(m: MessageState | undefined, key: string): number | null {
  const v = m?.signals?.[key]?.engineering_value
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.trim() !== '' && !Number.isNaN(Number(v))) return Number(v)
  return null
}

/** Compact horizontal meter for health / continuous signals. */
function MeterBar({
  value,
  max,
  min = 0,
  tone,
  label,
  testId,
}: {
  value: number | null
  max: number
  min?: number
  /** auto: green→amber→red by fill; high-bad: red when high (brake); low-bad: red when low */
  tone?: 'auto' | 'high-bad' | 'low-bad' | 'accent' | 'ok' | 'warn' | 'danger'
  label?: string
  testId?: string
}) {
  const span = Math.max(1e-6, max - min)
  const raw = value == null ? 0 : Math.abs(value - min) / span
  const pct = Math.max(0, Math.min(100, raw * 100))
  let t = tone ?? 'auto'
  if (t === 'auto' || t === 'high-bad' || t === 'low-bad') {
    if (t === 'high-bad') {
      t = pct >= 70 ? 'danger' : pct >= 40 ? 'warn' : 'ok'
    } else if (t === 'low-bad') {
      t = pct <= 15 ? 'danger' : pct <= 35 ? 'warn' : 'ok'
    } else {
      t = pct >= 90 ? 'warn' : 'accent'
    }
  }
  return (
    <div
      className={`meter-bar tone-${t}`}
      data-testid={testId}
      title={label}
      role="meter"
      aria-valuemin={min}
      aria-valuemax={max}
      aria-valuenow={value ?? undefined}
      aria-label={label}
    >
      <div className="meter-bar-track">
        <div className="meter-bar-fill" style={{ width: `${pct}%` }} />
      </div>
    </div>
  )
}

function MetricCard({
  title,
  valueText,
  unit,
  sub,
  freshness,
  value,
  max,
  min,
  tone,
  testId,
  meterTestId,
}: {
  title: string
  valueText: string
  unit?: string
  sub?: string
  freshness?: string
  value: number | null
  max: number
  min?: number
  tone?: 'auto' | 'high-bad' | 'low-bad' | 'accent' | 'ok' | 'warn' | 'danger'
  testId?: string
  meterTestId?: string
}) {
  return (
    <div className="card metric-card" data-testid={testId}>
      <div className="card-head">
        <div className="card-title">{title}</div>
        {freshness ? <FreshnessBadge value={freshness} /> : null}
      </div>
      <div
        className="metric"
        data-testid={
          testId === 'card-speed'
            ? 'metric-speed'
            : testId === 'card-yaw'
              ? 'metric-yaw'
              : testId === 'card-gear'
                ? 'metric-gear'
                : testId
                  ? `${testId}-value`
                  : undefined
        }
      >
        {valueText}
        {unit ? <span className="unit"> {unit}</span> : null}
      </div>
      <MeterBar
        value={value}
        max={max}
        min={min}
        tone={tone}
        label={title}
        testId={meterTestId}
      />
      {sub ? <div className="card-sub muted">{sub}</div> : null}
    </div>
  )
}

function findMsg(messages: MessageState[], name: string, bus?: string) {
  return messages.find((m) => m.name === name && (bus == null || m.bus === bus))
}

function ageMs(lastSeenNs?: number | null): string {
  if (lastSeenNs == null) return '—'
  const ms = Math.max(0, Date.now() - lastSeenNs / 1e6)
  if (ms < 1000) return `${Math.round(ms)} ms`
  return `${(ms / 1000).toFixed(1)} s`
}

function hexId(id: number) {
  return `0x${id.toString(16).toUpperCase()}`
}

/* ── Shell ─────────────────────────────────────────────────────────── */

function Topbar() {
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const mismatch = useAppStore((s) => s.protocolMismatch)
  const reconnect = useAppStore((s) => s.reconnectAttempts)
  const ses = status?.session
  const high = status?.adapter?.channels?.high
  const low = status?.adapter?.channels?.low
  const profileId = ses?.profile ?? status?.profile ?? '—'
  const profileLabel = PROFILE_LABELS[profileId] ?? profileId

  async function injectEstop() {
    try {
      await api.injectEstop()
    } catch (e) {
      console.error(e)
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

  return (
    <header className="topbar" data-testid="topbar">
      {/* Row 1 — identity + stream health + ESTOP action */}
      <div className="topbar-row topbar-row-primary">
        <div className="topbar-cluster">
          <div className="brand">Control Toolkit</div>
          <span className="chip chip-pill">CAN bench</span>
          <div className={`chip quality-${quality}`} data-testid="chip-stream">
            <span className="chip-k">Stream</span>
            <span className="chip-v">
              {streamText}
              {reconnect > 0 ? ` · retry ${reconnect}` : ''}
            </span>
          </div>
          <div
            className={`chip ${ses?.estop_active ? 'danger' : 'ok'}`}
            data-testid="chip-estop"
          >
            <span className="chip-k">ESTOP</span>
            <span className="chip-v">{ses?.estop_active ? 'Active' : 'Clear'}</span>
          </div>
          {mismatch && (
            <div className="chip danger" data-testid="chip-mismatch">
              Protocol mismatch
            </div>
          )}
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

      {/* Row 2 — session + transport */}
      <div className="topbar-row" data-testid="topbar-row-session">
        <div className="chip" data-testid="chip-profile" title="Active operating profile">
          <span className="chip-k">Profile</span>
          <span className="chip-v">{profileLabel}</span>
        </div>
        <div className="chip" data-testid="chip-destination">
          <span className="chip-k">Dest</span>
          <span className="chip-v">{ses?.destination ?? '—'}</span>
        </div>
        <div className="chip" data-testid="chip-phase">
          <span className="chip-k">Session</span>
          <span className="chip-v">
            {ses?.phase ?? 'stopped'}
            {ses?.session_id ? ` · ${ses.session_id.slice(0, 10)}` : ''}
          </span>
        </div>
        <div className="chip" data-testid="chip-adapter">
          <span className="chip-k">Adapter</span>
          <span className="chip-v">{status?.adapter?.health ?? '—'}</span>
        </div>
        <div className="chip" data-testid="chip-high">
          <span className="chip-k">High</span>
          <span className="chip-v">
            {high?.activity ?? '—'} · rx {high?.rx_count ?? 0}
          </span>
        </div>
        <div className="chip" data-testid="chip-low">
          <span className="chip-k">Low</span>
          <span className="chip-v">
            {low?.activity ?? '—'} · rx {low?.rx_count ?? 0}
          </span>
        </div>
        <div className="chip" data-testid="chip-bench-tx">
          <span className="chip-k">Bench TX</span>
          <span className="chip-v">{ses?.bench_tx ?? 'disabled'}</span>
        </div>
      </div>

      {/* Row 3 — vehicle view + evidence */}
      <div className="topbar-row" data-testid="topbar-row-vehicle">
        <div className="chip" data-testid="chip-mode" title="Requested vs confirmed vehicle mode">
          <span className="chip-k">Mode</span>
          <span className="chip-v">
            Req {ses?.requested_mode ?? '—'} · Veh {ses?.confirmed_mode ?? '—'}
          </span>
        </div>
        <div className="chip" data-testid="chip-power" title="Requested vs confirmed power">
          <span className="chip-k">Power</span>
          <span className="chip-v">
            Req {ses?.requested_power ?? '—'} · Veh {ses?.confirmed_power ?? '—'}
          </span>
        </div>
        <div className="chip" data-testid="chip-record">
          <span className="chip-k">Recording</span>
          <span className="chip-v">{ses?.recording ? 'On' : 'Off'}</span>
        </div>
        <div
          className="chip mono muted"
          data-testid="chip-hash"
          title={status?.wire_hash ?? ''}
        >
          <span className="chip-k">Wire</span>
          <span className="chip-v">{(status?.wire_hash ?? '').slice(0, 12) || '—'}…</span>
        </div>
      </div>
    </header>
  )
}

function Sidebar() {
  const workspace = useAppStore((s) => s.workspace)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const messages = useAppStore((s) => s.messages)

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

  return (
    <aside className="sidebar" data-testid="sidebar" aria-label="Primary navigation">
      <nav className="sidebar-nav" aria-label="Primary workspaces">
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

/* ── Overview ──────────────────────────────────────────────────────── */

function Overview() {
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

  const canHealth =
    quality === 'live'
      ? 'healthy'
      : quality === 'delayed'
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

  const streamPct =
    quality === 'live' ? 100 : quality === 'delayed' ? 55 : quality === 'dropping' ? 30 : 8

  return (
    <div className="workspace" data-testid="workspace-overview">
      <header className="ws-header">
        <h1>Overview</h1>
        <p className="muted">
          Vehicle state and immediate health · session {ses?.session_id ?? 'none'} ·{' '}
          {messages.length} live messages
        </p>
      </header>

      <section className="safety-strip" data-testid="safety-strip" aria-label="Safety and mode">
        <div className={`strip-item ${ses?.estop_active ? 'hazard' : 'ok'}`}>
          <span className="strip-k">ESTOP</span>
          <span className="strip-v">{ses?.estop_active ? 'Active' : 'Clear'}</span>
          <MeterBar
            value={ses?.estop_active ? 100 : 0}
            max={100}
            tone={ses?.estop_active ? 'danger' : 'ok'}
            label="ESTOP"
            testId="meter-estop"
          />
        </div>
        <div className="strip-item">
          <span className="strip-k">Power</span>
          <span className="strip-v">
            Req {ses?.requested_power ?? '—'} / Conf {ses?.confirmed_power ?? '—'}
          </span>
        </div>
        <div className="strip-item">
          <span className="strip-k">Mode</span>
          <span className="strip-v">
            Req {ses?.requested_mode ?? '—'} / Conf {ses?.confirmed_mode ?? '—'}
          </span>
        </div>
        <div className="strip-item">
          <span className="strip-k">Bench TX</span>
          <span className="strip-v">{ses?.bench_tx ?? 'disabled'}</span>
          <MeterBar
            value={ses?.bench_tx === 'enabled' ? 100 : 0}
            max={100}
            tone={ses?.bench_tx === 'enabled' ? 'warn' : 'ok'}
            label="Bench TX"
            testId="meter-bench-tx"
          />
        </div>
        <div className={`strip-item health-${canHealth}`}>
          <span className="strip-k">CAN health</span>
          <span className="strip-v">{canHealth}</span>
          <MeterBar
            value={streamPct}
            max={100}
            tone={
              canHealth === 'healthy' ? 'ok' : canHealth === 'degraded' ? 'warn' : 'danger'
            }
            label="CAN stream health"
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
          sub="HOST/RT brake · high → red"
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
          <div className="metric" data-testid="metric-gear">
            {signalText(drive, 'gear') || signalText(motor, 'gear_state') || '—'}
          </div>
          <div className="card-sub muted">N/D/S/R · host + motor</div>
        </div>
        <div className="card metric-card" data-testid="card-ready">
          <div className="card-head">
            <div className="card-title">Backend</div>
          </div>
          <div className="metric">{status?.ready ? 'ready' : 'not ready'}</div>
          <MeterBar
            value={status?.ready ? 100 : 0}
            max={100}
            tone={status?.ready ? 'ok' : 'danger'}
            label="Backend ready"
          />
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
              <th>Meter</th>
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
                <MeterBar
                  value={
                    String(signalText(safety, 'estop_active')).toLowerCase().includes('1') ||
                    String(signalText(safety, 'estop_active')).toLowerCase() === 'true'
                      ? 100
                      : 0
                  }
                  max={100}
                  tone="high-bad"
                />
              </td>
              <td>
                {safety ? <FreshnessBadge value={safety.freshness} /> : '—'}
              </td>
            </tr>
          </tbody>
        </table>
      </section>
    </div>
  )
}

/* ── Network ───────────────────────────────────────────────────────── */

function busNodes(nodes: TopologyNode[], bus: string) {
  return nodes.filter((n) => n.bus === bus)
}

function Network() {
  const topology = useAppStore((s) => s.topology)
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const high = status?.adapter?.channels?.high
  const low = status?.adapter?.channels?.low
  const highNodes = busNodes(topology, 'high')
  const lowNodes = busNodes(topology, 'low')

  return (
    <div className="workspace" data-testid="workspace-network">
      <header className="ws-header">
        <h1>Network</h1>
        <p className="muted">
          ECU topology and bus health · High and Low never collapsed into one lamp
        </p>
      </header>

      <section className="bus-health" data-testid="bus-health">
        <div className="health-card">
          <h3>High bus</h3>
          <dl className="kv">
            <dt>Activity</dt>
            <dd>{high?.activity ?? '—'}</dd>
            <dt>RX frames</dt>
            <dd className="mono">{high?.rx_count ?? 0}</dd>
            <dt>TX frames</dt>
            <dd className="mono">{high?.tx_count ?? 0}</dd>
            <dt>Overflow</dt>
            <dd className="mono">{high?.rx_overflow ?? 0}</dd>
          </dl>
        </div>
        <div className="health-card">
          <h3>Low bus</h3>
          <dl className="kv">
            <dt>Activity</dt>
            <dd>{low?.activity ?? '—'}</dd>
            <dt>RX frames</dt>
            <dd className="mono">{low?.rx_count ?? 0}</dd>
            <dt>TX frames</dt>
            <dd className="mono">{low?.tx_count ?? 0}</dd>
            <dt>Overflow</dt>
            <dd className="mono">{low?.rx_overflow ?? 0}</dd>
          </dl>
        </div>
        <div className="health-card">
          <h3>Connection layers</h3>
          <dl className="kv">
            <dt>USB adapter</dt>
            <dd>{status?.adapter?.health ?? '—'}</dd>
            <dt>Backend stream</dt>
            <dd>{quality}</dd>
            <dt>Destination</dt>
            <dd>{status?.session?.destination ?? '—'}</dd>
            <dt>Adapter epoch</dt>
            <dd className="mono">{status?.adapter?.adapter_epoch ?? '—'}</dd>
          </dl>
        </div>
      </section>

      <section className="topology" data-testid="topology-map">
        <h2>Topology map</h2>
        <div className="bus-row">
          <div className="bus-label">High</div>
          <div className="bus-line high">
            {(highNodes.length ? highNodes : [
              { node: 'Host', bus: 'high', can_id: 0x7fc, liveness: 'offline', freshness: 'unseen' },
              { node: 'RT_high', bus: 'high', can_id: 0x7fd, liveness: 'offline', freshness: 'unseen' },
            ]).map((n) => (
              <div
                key={`${n.bus}-${n.node}`}
                className={`topo-node liveness-${n.liveness}`}
                data-testid={`node-${n.node}`}
                title={`${n.node} ${hexId(n.can_id)} · ${n.liveness}`}
              >
                <div className="topo-name">{n.node}</div>
                <div className="mono muted">{hexId(n.can_id)}</div>
                <LivenessBadge value={n.liveness} />
              </div>
            ))}
          </div>
        </div>
        <div className="bus-bridge muted">RT gateway bridges High ↔ Low domains</div>
        <div className="bus-row">
          <div className="bus-label">Low</div>
          <div className="bus-line low">
            {(lowNodes.length ? lowNodes : [
              { node: 'RT_low', bus: 'low', can_id: 0x7fd, liveness: 'offline', freshness: 'unseen' },
              { node: 'SYS', bus: 'low', can_id: 0x7fe, liveness: 'offline', freshness: 'unseen' },
              { node: 'MTR', bus: 'low', can_id: 0x206, liveness: 'offline', freshness: 'unseen' },
            ]).map((n) => (
              <div
                key={`${n.bus}-${n.node}`}
                className={`topo-node liveness-${n.liveness}`}
                data-testid={`node-${n.node}`}
                title={`${n.node} ${hexId(n.can_id)} · ${n.liveness}`}
              >
                <div className="topo-name">{n.node}</div>
                <div className="mono muted">{hexId(n.can_id)}</div>
                <LivenessBadge value={n.liveness} />
              </div>
            ))}
          </div>
        </div>
      </section>
    </div>
  )
}

/* ── Live CAN ──────────────────────────────────────────────────────── */

type HistoryFrame = {
  global_sequence: number
  bus: string
  can_id: number
  dlc: number
  data_hex: string
  direction: string
  source: string
}

function LiveCan() {
  const messages = useAppStore((s) => s.messages)
  const liveFilter = useAppStore((s) => s.liveFilter)
  const setLiveFilter = useAppStore((s) => s.setLiveFilter)
  const selected = useAppStore((s) => s.selectedMessageKey)
  const setSelected = useAppStore((s) => s.setSelectedMessageKey)
  const [busFilter, setBusFilter] = useState<'both' | 'high' | 'low'>('both')
  const [viewMode, setViewMode] = useState<'latest' | 'chrono'>('latest')
  const [paused, setPaused] = useState(false)
  const [chrono, setChrono] = useState<HistoryFrame[]>([])
  const [chronoFrozen, setChronoFrozen] = useState<HistoryFrame[]>([])

  useEffect(() => {
    if (viewMode !== 'chrono' || paused) return
    let cancelled = false
    async function poll() {
      try {
        const r = await api.history(300)
        if (!cancelled) setChrono(r.frames || [])
      } catch {
        /* ignore */
      }
    }
    void poll()
    const id = window.setInterval(() => void poll(), 500)
    return () => {
      cancelled = true
      window.clearInterval(id)
    }
  }, [viewMode, paused])

  const filtered = useMemo(() => {
    const q = liveFilter.trim().toLowerCase()
    return [...messages]
      .filter((m) => (busFilter === 'both' ? true : m.bus === busFilter))
      .filter((m) => {
        if (!q) return true
        const id = hexId(m.can_id).toLowerCase()
        const name = (m.name || '').toLowerCase()
        const sigs = Object.keys(m.signals || {}).join(' ').toLowerCase()
        return id.includes(q) || name.includes(q) || sigs.includes(q) || m.bus.includes(q)
      })
      .sort((a, b) => a.bus.localeCompare(b.bus) || a.can_id - b.can_id)
  }, [messages, liveFilter, busFilter])

  const chronoView = paused ? chronoFrozen : chrono
  const chronoFiltered = useMemo(() => {
    const q = liveFilter.trim().toLowerCase()
    return chronoView
      .filter((f) => (busFilter === 'both' ? true : f.bus === busFilter))
      .filter((f) => {
        if (!q) return true
        return (
          hexId(f.can_id).toLowerCase().includes(q) ||
          f.bus.includes(q) ||
          f.data_hex.includes(q) ||
          f.source.toLowerCase().includes(q)
        )
      })
      .slice()
      .reverse()
  }, [chronoView, liveFilter, busFilter])

  const detail = filtered.find(
    (m) => `${m.bus}-${m.can_id}` === selected || m.key === selected,
  )

  return (
    <div className="workspace live-layout" data-testid="workspace-live">
      <header className="ws-header">
        <h1>Live CAN</h1>
        <p className="muted">
          {viewMode === 'latest'
            ? `Latest-by-message · updates in place · ${filtered.length} rows`
            : `Chronological stream · ${chronoFiltered.length} frames (pause freezes rendering, not capture)`}
        </p>
      </header>

      <div className="toolbar">
        <input
          data-testid="live-filter"
          className="search"
          placeholder="Filter ID, name, signal…"
          value={liveFilter}
          onChange={(e) => setLiveFilter(e.target.value)}
        />
        <div className="seg">
          {(['both', 'high', 'low'] as const).map((b) => (
            <button
              key={b}
              type="button"
              className={busFilter === b ? 'seg-btn active' : 'seg-btn'}
              data-testid={`filter-bus-${b}`}
              onClick={() => setBusFilter(b)}
            >
              {b === 'both' ? 'Both buses' : b}
            </button>
          ))}
        </div>
        <div className="seg" data-testid="live-view-mode">
          <button
            type="button"
            className={viewMode === 'latest' ? 'seg-btn active' : 'seg-btn'}
            data-testid="live-mode-latest"
            onClick={() => setViewMode('latest')}
          >
            Latest
          </button>
          <button
            type="button"
            className={viewMode === 'chrono' ? 'seg-btn active' : 'seg-btn'}
            data-testid="live-mode-chrono"
            onClick={() => setViewMode('chrono')}
          >
            Stream
          </button>
        </div>
        {viewMode === 'chrono' && (
          <button
            type="button"
            className="secondary dense"
            data-testid="live-pause"
            onClick={() => {
              if (!paused) setChronoFrozen(chrono)
              setPaused((p) => !p)
            }}
          >
            {paused ? 'Resume' : 'Pause'}
          </button>
        )}
      </div>

      <div className="live-split">
        <div className="table-wrap">
          {viewMode === 'latest' ? (
          <table className="can-table" data-testid="live-can-table">
            <thead>
              <tr>
                <th>Fresh</th>
                <th>Bus</th>
                <th>ID</th>
                <th>Name</th>
                <th>Rate, Hz</th>
                <th>Valid</th>
                <th>Age</th>
                <th>Signals</th>
              </tr>
            </thead>
            <tbody>
              {filtered.map((m) => {
                // Prefer bus+can_id — canonical keys like safety:safety_estop collide on high+low
                const key = `${m.bus}-${m.can_id}`
                return (
                  <tr
                    key={key}
                    data-testid={`row-${m.bus}-${m.can_id}`}
                    className={selected === key || selected === m.key ? 'selected' : undefined}
                    onClick={() => setSelected(key)}
                  >
                    <td>
                      <FreshnessBadge value={m.freshness} />
                    </td>
                    <td>{m.bus}</td>
                    <td className="mono">{hexId(m.can_id)}</td>
                    <td>{m.name}</td>
                    <td className="num mono">
                      {m.observed_rate_hz != null
                        ? m.observed_rate_hz.toFixed(1)
                        : '—'}
                      {m.expected_rate_hz != null
                        ? ` / ${m.expected_rate_hz}`
                        : ''}
                    </td>
                    <td>{m.validation_status}</td>
                    <td className="mono muted">{ageMs(m.last_seen_ns)}</td>
                    <td className="signals-cell">
                      {Object.entries(m.signals || {})
                        .map(([k, v]) => `${k}=${v.enum_label ?? v.engineering_value}`)
                        .join(' · ')}
                    </td>
                  </tr>
                )
              })}
              {filtered.length === 0 && (
                <tr>
                  <td colSpan={8} className="muted">
                    No frames yet — open Control, enable Bench TX, inject host drive.
                  </td>
                </tr>
              )}
            </tbody>
          </table>
          ) : (
          <table className="can-table" data-testid="live-chrono-table">
            <thead>
              <tr>
                <th>Seq</th>
                <th>Bus</th>
                <th>ID</th>
                <th>Dir</th>
                <th>Src</th>
                <th>DLC</th>
                <th>Data</th>
              </tr>
            </thead>
            <tbody>
              {chronoFiltered.map((f) => (
                <tr key={`${f.global_sequence}-${f.bus}-${f.can_id}`}>
                  <td className="mono num">{f.global_sequence}</td>
                  <td>{f.bus}</td>
                  <td className="mono">{hexId(f.can_id)}</td>
                  <td>{f.direction}</td>
                  <td>{f.source}</td>
                  <td className="num">{f.dlc}</td>
                  <td className="mono signals-cell">{f.data_hex}</td>
                </tr>
              ))}
              {chronoFiltered.length === 0 && (
                <tr>
                  <td colSpan={7} className="muted">
                    No history frames yet.
                  </td>
                </tr>
              )}
            </tbody>
          </table>
          )}
        </div>

        <aside className="detail-drawer" data-testid="live-detail">
          <h2>Message detail</h2>
          {!detail && <p className="muted">Select a row to inspect identity, health, and signals.</p>}
          {detail && (
            <>
              <dl className="kv">
                <dt>Identity</dt>
                <dd className="mono">
                  {detail.bus} {hexId(detail.can_id)} · {detail.name}
                </dd>
                <dt>Freshness</dt>
                <dd>
                  <FreshnessBadge value={detail.freshness} />
                </dd>
                <dt>Validation</dt>
                <dd>{detail.validation_status}</dd>
                <dt>Observed rate</dt>
                <dd className="mono">
                  {detail.observed_rate_hz?.toFixed(2) ?? '—'} Hz
                  {detail.expected_rate_hz != null
                    ? ` (expected ${detail.expected_rate_hz})`
                    : ''}
                </dd>
                <dt>Last seen</dt>
                <dd className="mono">{ageMs(detail.last_seen_ns)} ago</dd>
              </dl>
              <h3>Signals</h3>
              <table className="data-table compact">
                <thead>
                  <tr>
                    <th>Signal</th>
                    <th>Value</th>
                    <th>Raw</th>
                    <th>Unit</th>
                  </tr>
                </thead>
                <tbody>
                  {Object.entries(detail.signals || {}).map(([k, v]) => (
                    <tr key={k}>
                      <td>{k}</td>
                      <td className="mono">
                        {String(v.enum_label ?? v.engineering_value ?? '—')}
                      </td>
                      <td className="mono muted">{v.raw_value ?? '—'}</td>
                      <td className="muted">{v.unit ?? ''}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}
        </aside>
      </div>
    </div>
  )
}

/* ── Control ───────────────────────────────────────────────────────── */

function DirectActuatorCards({
  busy,
  setBusy,
  setLog,
  ensureSessionReady,
  refresh,
  disabled,
}: {
  busy: boolean
  setBusy: (b: boolean) => void
  setLog: (s: string) => void
  ensureSessionReady: () => Promise<import('./store').Status>
  refresh: () => Promise<import('./store').Status>
  disabled?: boolean
}) {
  const messages = useAppStore((s) => s.messages)
  const [motorSpeed, setMotorSpeed] = useState(300)
  const [motorGear, setMotorGear] = useState(1)
  const [steerAngle, setSteerAngle] = useState(0)
  const [brakePressure, setBrakePressure] = useState(20)
  const [active, setActive] = useState<Record<string, boolean>>({})

  const mtr = findMsg(messages, 'MTR_MOTOR_FBK')
  const ses = findMsg(messages, 'SES_STATUS')
  const seb = findMsg(messages, 'SEB_STATUS')

  async function start(channel: 'motor' | 'steering' | 'brake') {
    setBusy(true)
    try {
      await ensureSessionReady()
      // Starting any low-bus channel clears high-bus kinematics (backend exclusive).
      const values =
        channel === 'motor'
          ? { motor_speed_mmps: motorSpeed, gear: motorGear }
          : channel === 'steering'
            ? {
                target_angle_raw: steerAngle,
                control_enable: true,
                alignment_enable: true,
              }
            : {
                pressure_request_raw: brakePressure,
                control_enable: true,
                alignment_enable: true,
                control_mode: 1,
              }
      const r = await api.controlDirect({ channel, enabled: true, values })
      setActive((a) => ({ ...a, [channel]: true }))
      setLog(
        `Low-bus direct ${channel} · method=${String(r.control.method)} · channels=${JSON.stringify(r.control.direct_channels)}`,
      )
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stop(channel: 'motor' | 'steering' | 'brake') {
    setBusy(true)
    try {
      await api.controlDirect({ channel, enabled: false })
      setActive((a) => ({ ...a, [channel]: false }))
      setLog(`Stopped low-bus direct ${channel}`)
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  const locked = busy || !!disabled

  return (
    <div className="direct-grid">
      <div className="direct-card" data-testid="direct-motor">
        <h3>Motor · Low · RT_DRIVE_CMD 0x204</h3>
        <p className="muted small">Commands motor path on Low bus — not Host 0x300.</p>
        <label className="field">
          <span className="field-label">Speed, mm/s</span>
          <input
            type="number"
            data-testid="direct-motor-speed"
            value={motorSpeed}
            min={-500}
            max={3000}
            disabled={locked}
            onChange={(e) => setMotorSpeed(Number(e.target.value))}
          />
          <span className="field-hint">Allowed −500…3000</span>
        </label>
        <label className="field">
          <span className="field-label">Gear 0–3 (N/D/S/R)</span>
          <input
            type="number"
            data-testid="direct-motor-gear"
            value={motorGear}
            min={0}
            max={3}
            disabled={locked}
            onChange={(e) => setMotorGear(Number(e.target.value))}
          />
        </label>
        <div className="fbk-line muted small mono">
          FBK MTR 0x206 · speed {signalText(mtr, 'actual_speed_mmps')} · gear{' '}
          {signalText(mtr, 'gear')}
        </div>
        <div className="actions tight">
          <button
            type="button"
            data-testid="btn-direct-motor-start"
            disabled={locked}
            onClick={() => void start('motor')}
          >
            {active.motor ? 'Update stream' : 'Start stream'}
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-direct-motor-stop"
            disabled={locked || !active.motor}
            onClick={() => void stop('motor')}
          >
            Stop
          </button>
        </div>
      </div>

      <div className="direct-card" data-testid="direct-steering">
        <h3>Steering · Low · VCU_SES_REQ 0x169</h3>
        <p className="muted small">Vendor SES request — counter/checksum automatic.</p>
        <label className="field">
          <span className="field-label">Target angle raw (0.1°)</span>
          <input
            type="number"
            data-testid="direct-steer-angle"
            value={steerAngle}
            min={-450}
            max={450}
            disabled={locked}
            onChange={(e) => setSteerAngle(Number(e.target.value))}
          />
          <span className="field-hint">±450 · enable bits locked on</span>
        </label>
        <div className="fbk-line muted small mono">
          FBK SES 0x201 · angle {signalText(ses, 'angle_deg')}
        </div>
        <div className="actions tight">
          <button
            type="button"
            data-testid="btn-direct-steer-start"
            disabled={locked}
            onClick={() => void start('steering')}
          >
            {active.steering ? 'Update stream' : 'Start stream'}
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-direct-steer-stop"
            disabled={locked || !active.steering}
            onClick={() => void stop('steering')}
          >
            Stop
          </button>
        </div>
      </div>

      <div className="direct-card" data-testid="direct-brake">
        <h3>Brake · Low · VCU_SEB_REQ 0x7B9</h3>
        <p className="muted small">Vendor SEB request — independent of Host kinematics.</p>
        <label className="field">
          <span className="field-label">Pressure request raw 0–100</span>
          <input
            type="number"
            data-testid="direct-brake-pressure"
            value={brakePressure}
            min={0}
            max={100}
            disabled={locked}
            onChange={(e) => setBrakePressure(Number(e.target.value))}
          />
          <span className="field-hint">Vendor scale · enable bits locked on</span>
        </label>
        <div className="fbk-line muted small mono">
          FBK SEB 0x721 · {signalText(seb, 'pressure_kpa') || signalText(seb, 'status') || '—'}
        </div>
        <div className="actions tight">
          <button
            type="button"
            data-testid="btn-direct-brake-start"
            disabled={locked}
            onClick={() => void start('brake')}
          >
            {active.brake ? 'Update stream' : 'Start stream'}
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-direct-brake-stop"
            disabled={locked || !active.brake}
            onClick={() => void stop('brake')}
          >
            Stop
          </button>
        </div>
      </div>
    </div>
  )
}

/** Exclusive motion-control methods — high Host vs low direct (never mixed). */
type ControlMethod = 'high' | 'low' | 'hmi'

function Control() {
  const setStatus = useAppStore((s) => s.setStatus)
  const status = useAppStore((s) => s.status)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  const [method, setMethod] = useState<ControlMethod>('high')
  const [log, setLog] = useState('')
  const [busy, setBusy] = useState(false)
  const [speed, setSpeed] = useState(500)
  const [yaw, setYaw] = useState(250)
  const [gear, setGear] = useState(1) // firmware: 0=N 1=D 2=S 3=R
  const [periodMs, setPeriodMs] = useState(100)
  const [periodic, setPeriodic] = useState(true)
  const [leaseId, setLeaseId] = useState<string | null>(null)
  const leaseRef = useRef<string | null>(null)
  leaseRef.current = leaseId
  const [kbEnabled, setKbEnabled] = useState(false)
  const [kbSnap, setKbSnap] = useState<Record<string, unknown> | null>(null)
  const [ctrlStatus, setCtrlStatus] = useState<Record<string, unknown> | null>(null)
  const seqRef = useRef(0)
  const keysRef = useRef<Record<string, boolean>>({})
  const kbEnabledRef = useRef(false)
  kbEnabledRef.current = kbEnabled

  // Clear control intent / lease on unmount (architecture safety invariant).
  useEffect(() => {
    return () => {
      const st = useAppStore.getState().status
      const lid = leaseRef.current
      if (lid && st?.session?.session_id) {
        void api.releaseLease(st.session.session_id, lid).catch(() => undefined)
      }
      void api.stopAnalysis().catch(() => undefined)
      void api.controlRelease('unmount').catch(() => undefined)
    }
  }, [])

  // Poll control status so method badge stays accurate.
  useEffect(() => {
    const id = window.setInterval(() => {
      void api
        .controlStatus()
        .then((r) => setCtrlStatus(r.control))
        .catch(() => undefined)
    }, 1000)
    return () => window.clearInterval(id)
  }, [])

  // Keyboard teleop → backend /control/intent (high-bus Host kinematics only)
  useEffect(() => {
    if (!kbEnabled || method !== 'high') return
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
    }
    const onVis = () => {
      if (document.hidden) {
        void api.controlRelease('tab_hidden').catch(() => undefined)
        setKbEnabled(false)
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
          source: 'keyboard',
          mode: 'kinematics',
          throttle,
          steer,
          hard_brake,
          estop,
        })
        .then((r) => {
          setKbSnap(r.control)
          setCtrlStatus(r.control)
        })
        .catch((e) => setLog(String(e)))
    }, 50)

    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
      window.removeEventListener('blur', onBlur)
      document.removeEventListener('visibilitychange', onVis)
      window.clearInterval(tick)
      void api.controlRelease('disable').catch(() => undefined)
    }
  }, [kbEnabled, method])

  const refresh = useCallback(async () => {
    const st = await api.status()
    setStatus(st)
    try {
      const c = await api.controlStatus()
      setCtrlStatus(c.control)
    } catch {
      /* ignore */
    }
    return st
  }, [setStatus])

  async function ensureSessionReady() {
    let st = await refresh()
    if (!st.session?.session_id) {
      try {
        await api.createSession('pure_software')
      } catch (e) {
        // Concurrent create or leftover active session — reuse if present.
        st = await refresh()
        if (!st.session?.session_id) throw e
      }
      st = await refresh()
    }
    const tx = String(st.session?.bench_tx ?? '').toLowerCase()
    if (tx !== 'enabled' && st.session?.session_id) {
      try {
        await api.setBenchTx(st.session.session_id, true, st.session.revision)
      } catch {
        // Revision race: re-read and retry once.
        st = await refresh()
        if (String(st.session?.bench_tx ?? '').toLowerCase() !== 'enabled' && st.session?.session_id) {
          await api.setBenchTx(st.session.session_id, true, st.session.revision)
        }
      }
      st = await refresh()
    }
    return st
  }

  /** Switch exclusive control method — release the other path first. */
  async function selectMethod(next: ControlMethod) {
    if (next === method) return
    setBusy(true)
    try {
      if (method === 'high' || next === 'low' || next === 'hmi') {
        setKbEnabled(false)
        await api.controlRelease('method_switch').catch(() => undefined)
        await api.stopAnalysis().catch(() => undefined)
        setLeaseId(null)
      }
      if (method === 'low' || next === 'high') {
        // Stop all low-bus direct streams when leaving low method.
        for (const ch of ['motor', 'steering', 'brake'] as const) {
          await api.controlDirect({ channel: ch, enabled: false }).catch(() => undefined)
        }
        await api.controlRelease('method_switch').catch(() => undefined)
      }
      setMethod(next)
      setLog(
        next === 'high'
          ? 'Method: High bus · Host kinematics (HOST_DRIVE_CMD 0x300)'
          : next === 'low'
            ? 'Method: Low bus · Direct actuators (motor / steer / brake)'
            : 'Method: HMI (mode/power requests only — not motion)',
      )
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function enableTx() {
    setBusy(true)
    try {
      await ensureSessionReady()
      setLog('Bench TX enabled')
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function injectHostDrive() {
    setBusy(true)
    try {
      await ensureSessionReady()
      // High-bus only. Free competing owners before analysis inject.
      setKbEnabled(false)
      await api.controlRelease('pre_inject').catch(() => undefined)
      for (const ch of ['motor', 'steering', 'brake'] as const) {
        await api.controlDirect({ channel: ch, enabled: false }).catch(() => undefined)
      }
      await api.stopAnalysis().catch(() => undefined)
      const res = await api.hostDrive({
        speed_mmps: speed,
        yaw_rate_mrad_s: yaw,
        gear,
        period_ms: periodic ? periodMs : null,
      })
      const lid = (res as { lease_id?: string }).lease_id
      if (typeof lid === 'string') setLeaseId(lid)
      setLog(`High-bus inject HOST_DRIVE_CMD: ${JSON.stringify(res)}`)
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stopAll() {
    setBusy(true)
    try {
      const st = await refresh()
      if (st.session?.session_id) {
        await api.stopAll(st.session.session_id, st.session.revision)
      }
      await api.stopAnalysis().catch(() => undefined)
      setKbEnabled(false)
      setLeaseId(null)
      setLog('Stop All — high and low motion streams cleared')
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function setMode(mode: string) {
    setBusy(true)
    try {
      const st = await ensureSessionReady()
      await api.vehicleView(st.session.session_id!, { requested_mode: mode })
      // Protocol HMI_MODE_REQ is MANUAL=0 / AUTO=1 only; PURE_SIM is UI request label.
      if (mode === 'MANUAL' || mode === 'AUTO') {
        const res = await api.hmiMode(mode === 'AUTO' ? 1 : 0, true)
        setLog(`HMI mode TX: ${JSON.stringify(res)} (confirmed remains independent)`)
      } else {
        setLog(`Requested mode: ${mode} (no wire HMI_MODE_REQ for PURE_SIM yet)`)
      }
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function setPower(power: string) {
    setBusy(true)
    try {
      const st = await ensureSessionReady()
      await api.vehicleView(st.session.session_id!, { requested_power: power })
      const res = await api.hmiPower(power === 'ON' ? 1 : 0, true)
      setLog(`HMI power TX: ${JSON.stringify(res)}`)
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  const activeMethod = String(ctrlStatus?.method ?? 'none')
  const activeLabel = String(ctrlStatus?.method_label ?? 'No active motion method')

  return (
    <div className="workspace" data-testid="workspace-control">
      <header className="ws-header">
        <h1>Control</h1>
        <p className="muted">
          Two exclusive motion methods: <strong>High bus</strong> Host kinematics vs{' '}
          <strong>Low bus</strong> direct actuators. They use different messages and must not
          run together. HMI mode/power is separate (not motion).
        </p>
      </header>

      <section className="panel control-method-panel" data-testid="control-method-picker">
        <h2>Control method</h2>
        <div className="seg" role="tablist" aria-label="Control method">
          <button
            type="button"
            role="tab"
            data-testid="control-method-high"
            className={method === 'high' ? 'seg-btn active' : 'seg-btn'}
            aria-selected={method === 'high'}
            disabled={busy}
            onClick={() => void selectMethod('high')}
          >
            High bus · Host
          </button>
          <button
            type="button"
            role="tab"
            data-testid="control-method-low"
            className={method === 'low' ? 'seg-btn active' : 'seg-btn'}
            aria-selected={method === 'low'}
            disabled={busy}
            onClick={() => void selectMethod('low')}
          >
            Low bus · Direct
          </button>
          <button
            type="button"
            role="tab"
            data-testid="control-method-hmi"
            className={method === 'hmi' ? 'seg-btn active' : 'seg-btn'}
            aria-selected={method === 'hmi'}
            disabled={busy}
            onClick={() => void selectMethod('hmi')}
          >
            HMI (mode/power)
          </button>
        </div>
        <div className="method-compare" data-testid="method-compare">
          <div className={method === 'high' ? 'method-card active' : 'method-card'}>
            <strong>High-level</strong>
            <span className="chip tiny">High bus</span>
            <ul className="muted small">
              <li>Message: HOST_DRIVE_CMD 0x300 @ 10 ms</li>
              <li>You send Host intent (speed / yaw / gear)</li>
              <li>RT runs kinematics and safety</li>
              <li>Drive console + keyboard use this path</li>
            </ul>
          </div>
          <div className={method === 'low' ? 'method-card active' : 'method-card'}>
            <strong>Low-level</strong>
            <span className="chip tiny">Low bus</span>
            <ul className="muted small">
              <li>Motor 0x204 · Steer 0x169 · Brake 0x7B9</li>
              <li>Bypasses Host / RT kinematics stack</li>
              <li>Isolated unit test of each actuator</li>
              <li>Exclusive with high-bus motion</li>
            </ul>
          </div>
        </div>
        <dl className="kv compact">
          <dt>Backend active method</dt>
          <dd data-testid="control-active-method">
            <span className="mono">{activeMethod}</span>
            <span className="muted small"> · {activeLabel}</span>
          </dd>
          <dt>Bench TX</dt>
          <dd>{status?.session?.bench_tx ?? '—'}</dd>
        </dl>
        <div className="actions tight">
          <button
            type="button"
            className="secondary"
            data-testid="btn-enable-tx"
            disabled={busy}
            onClick={() => void enableTx()}
          >
            Enable Bench TX
          </button>
          <button
            type="button"
            className="danger"
            data-testid="btn-stop-all"
            disabled={busy}
            onClick={() => void stopAll()}
          >
            Stop All
          </button>
        </div>
      </section>

      {method === 'high' && (
        <>
          <section className="panel" data-testid="keyboard-control">
            <h2>High bus · Keyboard Host intent</h2>
            <p className="muted small">
              Shapes to HOST_DRIVE_CMD on <strong>High</strong> only. Backend 10 ms TX · gear
              N/D/S/R · 500 ms stale stop · blur releases. Does not write Low-bus SES/SEB/motor
              commands.
            </p>
            <div className="actions">
              <button
                type="button"
                data-testid="btn-kb-enable"
                disabled={busy}
                className={kbEnabled ? '' : 'secondary'}
                onClick={() => {
                  if (kbEnabled) {
                    setKbEnabled(false)
                    void api.controlRelease('disable').catch(() => undefined)
                  } else {
                    void ensureSessionReady()
                      .then(() => setKbEnabled(true))
                      .catch((e) => setLog(String(e)))
                  }
                }}
              >
                {kbEnabled ? 'Disable keyboard' : 'Enable keyboard control'}
              </button>
              <button
                type="button"
                className="secondary"
                data-testid="btn-open-drive-from-control"
                onClick={() => setWorkspace('preview')}
              >
                Open Drive console
              </button>
            </div>
            <ul className="controls-legend muted small">
              <li>
                <kbd>W</kbd>/<kbd>↑</kbd> throttle · <kbd>S</kbd>/<kbd>↓</kbd> reverse
              </li>
              <li>
                <kbd>A</kbd>/<kbd>D</kbd> yaw · <kbd>Shift</kbd> hard brake · <kbd>Space</kbd>{' '}
                ESTOP
              </li>
            </ul>
            {kbSnap && (
              <dl className="kv">
                <dt>Method</dt>
                <dd className="mono">{String(kbSnap.method ?? 'high_kinematics')}</dd>
                <dt>Active</dt>
                <dd>{kbSnap.active ? 'yes' : 'no'}</dd>
                <dt>Speed, mm/s</dt>
                <dd className="mono">{String(kbSnap.shaped_speed_mmps)}</dd>
                <dt>Yaw, mrad/s</dt>
                <dd className="mono">{String(kbSnap.shaped_yaw_mrad_s)}</dd>
                <dt>Gear</dt>
                <dd className="mono">
                  {String(kbSnap.gear_label)} ({String(kbSnap.gear)})
                </dd>
                <dt>Loss</dt>
                <dd>{String(kbSnap.loss_reason ?? '—')}</dd>
              </dl>
            )}
          </section>

          <section className="panel" data-testid="high-analysis-inject">
            <h2>High bus · Analysis inject (HOST_DRIVE_CMD)</h2>
            <p className="muted small">
              Numeric Host intent for yaw/speed study on High bus — not Low-bus actuators, not a
              full synthetic vehicle.
            </p>
            <div className="form-grid">
              <label>
                Speed, mm/s
                <input
                  data-testid="input-speed"
                  type="number"
                  value={speed}
                  min={-500}
                  max={3000}
                  onChange={(e) => setSpeed(Number(e.target.value))}
                />
                <span className="field-hint">Allowed range: −500 to 3000</span>
              </label>
              <label>
                Yaw rate, mrad/s
                <input
                  data-testid="input-yaw"
                  type="number"
                  value={yaw}
                  min={-3000}
                  max={3000}
                  onChange={(e) => setYaw(Number(e.target.value))}
                />
                <span className="field-hint">Allowed range: −3000 to 3000</span>
              </label>
              <label>
                Gear
                <input
                  data-testid="input-gear"
                  type="number"
                  min={0}
                  max={3}
                  value={gear}
                  onChange={(e) => setGear(Number(e.target.value))}
                />
                <span className="field-hint">0=N 1=D 2=S 3=R (host.yaml / MTR)</span>
              </label>
              <label>
                Period, ms
                <input
                  data-testid="input-period"
                  type="number"
                  value={periodMs}
                  disabled={!periodic}
                  onChange={(e) => setPeriodMs(Number(e.target.value))}
                />
              </label>
              <label className="check">
                <input
                  data-testid="check-periodic"
                  type="checkbox"
                  checked={periodic}
                  onChange={(e) => setPeriodic(e.target.checked)}
                />
                Periodic (re-encode each period)
              </label>
            </div>
            <div className="actions">
              <button
                type="button"
                data-testid="btn-inject-drive"
                disabled={busy}
                onClick={() => void injectHostDrive()}
              >
                Inject host drive (High)
              </button>
            </div>
          </section>
        </>
      )}

      {method === 'low' && (
        <section className="panel" data-testid="direct-actuators">
          <h2>Low bus · Direct actuators</h2>
          <p className="muted small">
            Completely separate from Host kinematics. Each card streams its own Low-bus
            message with automatic counter/checksum. Starting any channel cancels high-bus
            HOST_DRIVE_CMD jobs.
          </p>
          <DirectActuatorCards
            busy={busy}
            setBusy={setBusy}
            setLog={setLog}
            ensureSessionReady={ensureSessionReady}
            refresh={refresh}
          />
        </section>
      )}

      {method === 'hmi' && (
        <section className="panel" data-testid="hmi-panel">
          <h2>HMI requests (not a motion method)</h2>
          <p className="muted small">
            Mode/power requests on HMI frames. Independent of high Host intent and low
            direct actuators. Requested vs confirmed stay separate until ECU feedback.
          </p>
          <div className="actions">
            {(['MANUAL', 'AUTO', 'PURE_SIM'] as const).map((m) => (
              <button
                key={m}
                type="button"
                data-testid={`btn-mode-${m.toLowerCase()}`}
                disabled={busy}
                className="secondary"
                onClick={() => void setMode(m)}
              >
                Request {m.replace('_', ' ')}
              </button>
            ))}
            <button
              type="button"
              data-testid="btn-power-on"
              disabled={busy}
              className="secondary"
              onClick={() => void setPower('ON')}
            >
              Request power ON
            </button>
            <button
              type="button"
              data-testid="btn-power-off"
              disabled={busy}
              className="secondary"
              onClick={() => void setPower('OFF')}
            >
              Request power OFF
            </button>
          </div>
          <div className="muted small">
            Current: mode req {status?.session?.requested_mode ?? '—'} / conf{' '}
            {status?.session?.confirmed_mode ?? '—'} · power req{' '}
            {status?.session?.requested_power ?? '—'} / conf{' '}
            {status?.session?.confirmed_power ?? '—'}
          </div>
        </section>
      )}

      <pre className="log" data-testid="control-log">
        {log || 'Ready. Pick High bus (Host) or Low bus (direct).'}
      </pre>
    </div>
  )
}

/* ── Bench ─────────────────────────────────────────────────────────── */

function Bench() {
  const status = useAppStore((s) => s.status)
  return (
    <div className="workspace" data-testid="workspace-bench">
      <header className="ws-header">
        <h1>Bench</h1>
        <p className="muted">
          Physical ECU under test and synthetic peers. Physical Bench Test profile is
          hardware-track; Pure Software uses virtual buses only.
        </p>
      </header>
      <section className="panel">
        <h2>Bench setup</h2>
        <ol className="setup-list">
          <li>Physical target ECU(s) — deferred (hardware track)</li>
          <li>Connected bus/channel — virtual high/low active</li>
          <li>Peers present — none (analysis inject only)</li>
          <li>Missing peers to emulate — not full synthetic vehicle</li>
          <li>
            Control path — High Host kinematics (0x300) or Low direct actuators (exclusive)
          </li>
          <li>Review periodic TX before enable</li>
        </ol>
        <dl className="kv">
          <dt>Active profile</dt>
          <dd>{status?.session?.profile ?? status?.profile ?? '—'}</dd>
          <dt>Destination</dt>
          <dd>{status?.session?.destination ?? '—'}</dd>
          <dt>Bench TX</dt>
          <dd>{status?.session?.bench_tx ?? 'disabled'}</dd>
          <dt>Leases</dt>
          <dd className="mono">
            {(status?.session?.leases || []).join(', ') || 'none'}
          </dd>
          <dt>Jobs</dt>
          <dd className="mono">{(status?.session?.jobs || []).join(', ') || 'none'}</dd>
        </dl>
      </section>
    </div>
  )
}

/* ── Dictionary ────────────────────────────────────────────────────── */

// Structure from debug-tool (MessageCard / BitGrid / SignalTable).
// Data always from YAML-generated protocol catalog — see CanDictionary.tsx.

/* ── Diagnostics ───────────────────────────────────────────────────── */

function Diagnostics() {
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const quality = useAppStore((s) => s.streamQuality)
  const mismatch = useAppStore((s) => s.protocolMismatch)
  const [events, setEvents] = useState<Array<Record<string, unknown>>>([])
  const [episodes, setEpisodes] = useState<Array<Record<string, unknown>>>([])
  const [activeRec, setActiveRec] = useState<Record<string, unknown> | null>(null)
  const [recordings, setRecordings] = useState<Array<Record<string, unknown>>>([])
  const [recLog, setRecLog] = useState('')
  const [busy, setBusy] = useState(false)
  const [evidenceId, setEvidenceId] = useState<string | null>(null)
  const [evidenceFrames, setEvidenceFrames] = useState<Array<Record<string, unknown>>>(
    [],
  )
  const [evidenceMeta, setEvidenceMeta] = useState('')

  const refreshDiag = useCallback(async () => {
    const [ev, ep, rec, st] = await Promise.all([
      api.events(40),
      api.episodes(),
      api.recordings(),
      api.status(),
    ])
    setEvents(ev.events || [])
    setEpisodes(ep.episodes || [])
    setActiveRec(rec.active)
    setRecordings(rec.recordings || [])
    setStatus(st)
  }, [setStatus])

  useEffect(() => {
    void refreshDiag().catch(() => undefined)
    const id = window.setInterval(() => void refreshDiag().catch(() => undefined), 2000)
    return () => window.clearInterval(id)
  }, [refreshDiag])

  async function startRec() {
    setBusy(true)
    try {
      const r = await api.startRecording()
      setRecLog(`Started ${String(r.recording.recording_id)}`)
      await refreshDiag()
    } catch (e) {
      setRecLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stopRec() {
    setBusy(true)
    try {
      const id = String(activeRec?.recording_id || '')
      if (!id) {
        setRecLog('No active recording')
        return
      }
      const r = await api.stopRecording(id)
      setRecLog(
        `Stopped ${id} · frames ${String(r.recording.frame_count)} · quality ${String(r.recording.evidence_quality)}`,
      )
      await refreshDiag()
    } catch (e) {
      setRecLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function openEvidence(id: string) {
    setBusy(true)
    try {
      const body = await api.evidence(id, 80)
      setEvidenceId(id)
      setEvidenceFrames(body.frames || [])
      setEvidenceMeta(
        `${body.frame_total} frames · quality ${body.evidence_quality || '—'}`,
      )
    } catch (e) {
      setEvidenceId(id)
      setEvidenceFrames([])
      setEvidenceMeta(String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="workspace" data-testid="workspace-diagnostics">
      <header className="ws-header">
        <h1>Diagnostics</h1>
        <p className="muted">
          Event timeline, diagnostic episodes, and opt-in recording with evidence quality.
        </p>
      </header>

      <section className="panel">
        <h2>Session evidence snapshot</h2>
        <dl className="kv">
          <dt>Phase</dt>
          <dd>{status?.session?.phase ?? '—'}</dd>
          <dt>Stream</dt>
          <dd>{quality}</dd>
          <dt>Wire hash</dt>
          <dd className="mono">{status?.wire_hash ?? '—'}</dd>
          <dt>Recording</dt>
          <dd>{status?.session?.recording || activeRec ? 'on' : 'off'}</dd>
          <dt>Mismatch</dt>
          <dd>{mismatch ? 'yes' : 'no'}</dd>
        </dl>
      </section>

      <section className="panel" data-testid="recording-panel">
        <h2>Recording</h2>
        <p className="muted small">
          Opt-in capture of RX/TX frames. Evidence quality is Complete unless frames are
          dropped.
        </p>
        <div className="actions">
          <button
            type="button"
            data-testid="btn-rec-start"
            disabled={busy || !!activeRec}
            onClick={() => void startRec()}
          >
            Start recording
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-rec-stop"
            disabled={busy || !activeRec}
            onClick={() => void stopRec()}
          >
            Stop recording
          </button>
        </div>
        {activeRec && (
          <dl className="kv">
            <dt>Active ID</dt>
            <dd className="mono">{String(activeRec.recording_id)}</dd>
            <dt>Frames</dt>
            <dd className="mono">{String(activeRec.frame_count)}</dd>
            <dt>Quality</dt>
            <dd>{String(activeRec.evidence_quality)}</dd>
          </dl>
        )}
        {recordings.length > 0 && (
          <div className="mt-section">
            <h3>Recordings</h3>
            <table className="data-table compact" data-testid="recordings-table">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Frames</th>
                  <th>Quality</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {recordings.slice(0, 12).map((r) => {
                  const id = String(r.recording_id)
                  return (
                    <tr key={id}>
                      <td className="mono">{id}</td>
                      <td className="num">{String(r.frame_count)}</td>
                      <td>{String(r.evidence_quality)}</td>
                      <td>
                        <button
                          type="button"
                          className="secondary"
                          data-testid={`btn-evidence-${id}`}
                          disabled={busy}
                          onClick={() => void openEvidence(id)}
                        >
                          Open evidence
                        </button>
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
        )}
        {evidenceId && (
          <div className="mt-section" data-testid="evidence-window">
            <h3>Evidence window · {evidenceId}</h3>
            <p className="muted small">{evidenceMeta}</p>
            <div className="evidence-frames table-wrap">
              <table className="can-table">
                <thead>
                  <tr>
                    <th>Seq</th>
                    <th>Bus</th>
                    <th>ID</th>
                    <th>Dir</th>
                    <th>Data</th>
                  </tr>
                </thead>
                <tbody>
                  {evidenceFrames.map((f) => (
                    <tr key={String(f.seq)}>
                      <td className="mono">{String(f.seq)}</td>
                      <td>{String(f.bus)}</td>
                      <td className="mono">{hexId(Number(f.can_id))}</td>
                      <td>{String(f.direction)}</td>
                      <td className="mono">{String(f.data_hex)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        )}
        <pre className="log" data-testid="recording-log">
          {recLog || 'Idle.'}
        </pre>
      </section>

      <section className="panel" data-testid="episodes-panel">
        <h2>Episodes</h2>
        {episodes.length === 0 ? (
          <p className="muted small">No active diagnostic episodes.</p>
        ) : (
          <table className="data-table compact">
            <thead>
              <tr>
                <th>Code</th>
                <th>Scope</th>
                <th>Count</th>
                <th>Severity</th>
                <th>Recovered</th>
              </tr>
            </thead>
            <tbody>
              {episodes.map((e) => (
                <tr key={String(e.episode_id)}>
                  <td className="mono">{String(e.code)}</td>
                  <td>{String(e.scope)}</td>
                  <td className="num">{String(e.count)}</td>
                  <td>{String(e.severity)}</td>
                  <td>{e.recovered ? 'yes' : 'no'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </section>

      <section className="panel" data-testid="events-panel">
        <h2>Event timeline</h2>
        <table className="data-table compact" data-testid="events-table">
          <thead>
            <tr>
              <th>Severity</th>
              <th>Code</th>
              <th>Title</th>
              <th>Age, s</th>
            </tr>
          </thead>
          <tbody>
            {events.map((e) => (
              <tr key={String(e.event_id)}>
                <td>{String(e.severity)}</td>
                <td className="mono">{String(e.code)}</td>
                <td>{String(e.title)}</td>
                <td className="num mono">
                  {typeof e.age_s === 'number' ? e.age_s.toFixed(1) : '—'}
                </td>
              </tr>
            ))}
            {events.length === 0 && (
              <tr>
                <td colSpan={4} className="muted">
                  No events yet.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </section>
    </div>
  )
}

/* ── Logging (architecture §7 / §14 operational audit trail) ───────── */

const LOG_CATEGORIES = [
  'all',
  'system',
  'session',
  'transport',
  'control',
  'inject',
  'safety',
  'recording',
  'test',
  'protocol',
  'hmi',
  'api',
] as const

function Logs() {
  const [logs, setLogs] = useState<Array<Record<string, unknown>>>([])
  const [stats, setStats] = useState<Record<string, unknown> | null>(null)
  const [category, setCategory] = useState<string>('all')
  const [severity, setSeverity] = useState<string>('all')
  const [q, setQ] = useState('')
  const [err, setErr] = useState('')
  const [busy, setBusy] = useState(false)
  const [selected, setSelected] = useState<Record<string, unknown> | null>(null)
  const [auto, setAuto] = useState(true)

  const refresh = useCallback(async () => {
    try {
      const r = await api.logs({
        limit: 400,
        category: category === 'all' ? undefined : category,
        severity: severity === 'all' ? undefined : severity,
        q: q.trim() || undefined,
      })
      setLogs(r.logs || [])
      setStats(r.stats || null)
      setErr('')
    } catch (e) {
      setErr(String(e))
    }
  }, [category, severity, q])

  useEffect(() => {
    void refresh()
    if (!auto) return
    const id = window.setInterval(() => void refresh(), 1500)
    return () => window.clearInterval(id)
  }, [refresh, auto])

  async function clearAll() {
    setBusy(true)
    try {
      await api.clearLogs()
      await refresh()
    } catch (e) {
      setErr(String(e))
    } finally {
      setBusy(false)
    }
  }

  function exportJson() {
    const blob = new Blob([JSON.stringify({ stats, logs }, null, 2)], {
      type: 'application/json',
    })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `control-toolkit-logs-${Date.now()}.json`
    a.click()
    URL.revokeObjectURL(url)
  }

  return (
    <div className="workspace" data-testid="workspace-logs">
      <header className="ws-header">
        <h1>Logging</h1>
        <p className="muted">
          Operational audit trail (architecture §7 / §14). Session, transport, control,
          inject, safety, recording, and tests — separate from Live CAN frames and raw
          recording evidence.
        </p>
      </header>

      <section className="panel">
        <div className="toolbar logs-toolbar">
          <select
            data-testid="logs-category"
            value={category}
            onChange={(e) => setCategory(e.target.value)}
            aria-label="Log category"
          >
            {LOG_CATEGORIES.map((c) => (
              <option key={c} value={c}>
                {c === 'all' ? 'All categories' : c}
              </option>
            ))}
          </select>
          <select
            data-testid="logs-severity"
            value={severity}
            onChange={(e) => setSeverity(e.target.value)}
            aria-label="Log severity"
          >
            {['all', 'debug', 'info', 'warning', 'error', 'critical'].map((s) => (
              <option key={s} value={s}>
                {s === 'all' ? 'All severities' : s}
              </option>
            ))}
          </select>
          <input
            className="search"
            data-testid="logs-filter"
            placeholder="Search code, title, detail…"
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />
          <label className="check">
            <input
              type="checkbox"
              data-testid="logs-auto"
              checked={auto}
              onChange={(e) => setAuto(e.target.checked)}
            />
            Auto-refresh
          </label>
          <button
            type="button"
            className="secondary"
            data-testid="logs-refresh"
            disabled={busy}
            onClick={() => void refresh()}
          >
            Refresh
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="logs-export"
            onClick={() => exportJson()}
          >
            Export JSON
          </button>
          <button
            type="button"
            className="danger"
            data-testid="logs-clear"
            disabled={busy}
            onClick={() => void clearAll()}
          >
            Clear
          </button>
        </div>
        {stats && (
          <p className="muted small" data-testid="logs-stats">
            {String(stats.count ?? 0)} / {String(stats.capacity ?? '—')} entries · seq{' '}
            {String(stats.sequence ?? '—')}
          </p>
        )}
        {err && <p className="danger-text">{err}</p>}
      </section>

      <div className="logs-split">
        <section className="panel" data-testid="logs-table-panel">
          <div className="table-wrap logs-table-wrap">
            <table className="can-table" data-testid="logs-table">
              <thead>
                <tr>
                  <th>Age</th>
                  <th>Sev</th>
                  <th>Cat</th>
                  <th>Code</th>
                  <th>Title</th>
                  <th>Detail</th>
                </tr>
              </thead>
              <tbody>
                {logs.map((e) => (
                  <tr
                    key={String(e.log_id)}
                    className={
                      selected?.log_id === e.log_id ? 'selected' : undefined
                    }
                    data-testid={`log-row-${String(e.log_id)}`}
                    onClick={() => setSelected(e)}
                  >
                    <td className="mono num">
                      {typeof e.age_s === 'number'
                        ? `${(e.age_s as number).toFixed(1)}s`
                        : '—'}
                    </td>
                    <td>
                      <span className={`log-sev log-sev-${String(e.severity)}`}>
                        {String(e.severity)}
                      </span>
                    </td>
                    <td className="mono">{String(e.category)}</td>
                    <td className="mono">{String(e.code)}</td>
                    <td>{String(e.title)}</td>
                    <td className="muted small">{String(e.detail || '')}</td>
                  </tr>
                ))}
                {logs.length === 0 && (
                  <tr>
                    <td colSpan={6} className="muted">
                      No log entries match filters.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </section>

        <aside className="panel" data-testid="logs-detail">
          <h2>Entry detail</h2>
          {!selected && (
            <p className="muted small">Select a row to inspect full payload.</p>
          )}
          {selected && (
            <dl className="kv">
              <dt>ID</dt>
              <dd className="mono">{String(selected.log_id)}</dd>
              <dt>Code</dt>
              <dd className="mono">{String(selected.code)}</dd>
              <dt>Category</dt>
              <dd>{String(selected.category)}</dd>
              <dt>Severity</dt>
              <dd>{String(selected.severity)}</dd>
              <dt>Title</dt>
              <dd>{String(selected.title)}</dd>
              <dt>Detail</dt>
              <dd>{String(selected.detail || '—')}</dd>
              <dt>Bus / ID</dt>
              <dd className="mono">
                {String(selected.bus ?? '—')} ·{' '}
                {selected.can_id != null
                  ? `0x${Number(selected.can_id).toString(16).toUpperCase()}`
                  : '—'}
              </dd>
              <dt>Session</dt>
              <dd className="mono">{String(selected.session_id ?? '—')}</dd>
              <dt>Data</dt>
              <dd>
                <pre className="log" data-testid="logs-detail-data">
                  {JSON.stringify(selected.data ?? {}, null, 2)}
                </pre>
              </dd>
            </dl>
          )}
        </aside>
      </div>
    </div>
  )
}

/* ── Settings ──────────────────────────────────────────────────────── */

function Settings() {
  const setStatus = useAppStore((s) => s.setStatus)
  const status = useAppStore((s) => s.status)
  const [profiles, setProfiles] = useState<ProfileInfo[]>([])
  const [log, setLog] = useState('')
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    void api.profiles().then((r) => setProfiles(r.profiles || []))
  }, [])

  async function startPureSoftware() {
    setBusy(true)
    try {
      const st = await api.status()
      if (st.session?.session_id) {
        await api.closeSession(st.session.session_id, st.session.revision)
      }
      const created = await api.createSession('pure_software')
      setStatus(await api.status())
      setLog(`Session ${created.session.session_id} · phase ${created.session.phase}`)
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function tryProfile(id: string) {
    setBusy(true)
    try {
      let st = await api.status()
      if (!st.session?.session_id) {
        await api.createSession('pure_software')
        st = await api.status()
      }
      await api.changeProfile(st.session!.session_id!, id, st.session!.revision, true)
      setLog(`Switched to ${id}`)
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="workspace" data-testid="workspace-settings">
      <header className="ws-header">
        <h1>Settings</h1>
        <p className="muted">
          Operating profiles and session controls. Physical profiles refuse without an
          adapter (no silent virtual fallback).
        </p>
      </header>

      <section className="panel">
        <h2>Operating profiles</h2>
        <div className="profile-list" data-testid="profile-list">
          {profiles.map((p) => (
            <div key={p.id} className="profile-card">
              <div className="profile-title">
                {p.label}
                <span className={`chip tiny ${p.available ? 'ok' : ''}`}>
                  {p.available ? 'available' : 'blocked'}
                </span>
              </div>
              <div className="muted small">
                Destination: {p.destination}
                {p.reason ? ` · ${p.reason}` : ''}
              </div>
              <div className="actions tight">
                {p.id === 'pure_software' ? (
                  <button
                    type="button"
                    disabled={busy}
                    data-testid="btn-start-pure"
                    onClick={() => void startPureSoftware()}
                  >
                    Start Pure Software session
                  </button>
                ) : (
                  <button
                    type="button"
                    disabled={busy || !p.available}
                    className="secondary"
                    data-testid={`btn-profile-${p.id}`}
                    onClick={() => void tryProfile(p.id)}
                  >
                    Activate (will refuse if no adapter)
                  </button>
                )}
              </div>
            </div>
          ))}
        </div>
        <dl className="kv" style={{ marginTop: 16 }}>
          <dt>Active</dt>
          <dd>{status?.session?.profile ?? status?.profile ?? '—'}</dd>
          <dt>Revision</dt>
          <dd className="mono">{status?.session?.revision ?? 0}</dd>
          <dt>Session ID</dt>
          <dd className="mono">{status?.session?.session_id ?? 'none'}</dd>
        </dl>
        <pre className="log" data-testid="settings-log">
          {log || 'Select a profile action.'}
        </pre>
      </section>
    </div>
  )
}

/* ── App ───────────────────────────────────────────────────────────── */

export default function App() {
  useBackendStream()
  const workspace = useAppStore((s) => s.workspace)
  return (
    <div className="app" data-testid="app">
      <Topbar />
      <div className="body">
        <Sidebar />
        <main>
          {workspace === 'overview' && <Overview />}
          {workspace === 'network' && <Network />}
          {workspace === 'live' && <LiveCan />}
          {workspace === 'control' && <Control />}
          {workspace === 'preview' && <VehiclePreview />}
          {workspace === 'bench' && <Bench />}
          {workspace === 'dictionary' && <CanDictionary />}
          {workspace === 'diagnostics' && <Diagnostics />}
          {workspace === 'logs' && <Logs />}
          {workspace === 'settings' && <Settings />}
        </main>
      </div>
    </div>
  )
}
